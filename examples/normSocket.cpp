
#include "normSocket.h"
#include <stdio.h>  // for stderr
#include <assert.h> // for assert()
#include <string.h>  // for strlen()

#include "protoTree.h"
#include "protoAddress.h"

#ifdef WIN32
#include <Winsock2.h>   // for inet_ntoa() (TBD - change to use Protolib routines?)
#include <Ws2tcpip.h>   // for inet_ntop()
#else
#include <arpa/inet.h>  // for inet_ntoa() (TBD - change to use Protolib routines?)
#endif // if/else WIN32/UNIX

// COMPILE: (assumes "normApi.h" in "include" ...
// g++ -I../include -c normSocket.cpp 

#define TRACE(...) fprintf(stderr, __VA_ARGS__)

// Extra, non-public NORM API functions used by NormSocket stuff
extern void NormSetId(NormSessionHandle sesssionHandle, NormNodeId normId);

// This "NormSocket" class is used to maintain tx/rx state for a NORM "socket" connection.
// At the moment this "socket" connection represents a single, bi-directional NORM_OBJECT_STREAM
// in either a unicast context or an asymmetric "server" multicast stream to possibly multiple "client"
// nodes with individual unicast streams in return from those "client" nodes. (I.e., the server will need to
// have a normSocket per client even for the server multicast case (maybe :-) )

const NormSocketHandle NORM_SOCKET_INVALID = (NormSocketHandle)0;

const double NORM_DEFAULT_CONNECT_TIMEOUT = 60.0;

// This is extra stuff defined for NormSocket API extension purposes.  As the NormSocket
// extension is finalized, these may be refined/relocated
enum {NORM_SOCKET_VERSION = 1};
enum NormSocketCommand
{
    NORM_SOCKET_CMD_NULL = 0,  // reserved, invalid/null command
    NORM_SOCKET_CMD_REJECT,    // sent by server-listener to reject invalid connection messages
    NORM_SOCKET_CMD_ALIVE      // TBD - for NormSocket "keep-alive" option?
};
    
// Default socket option values. Can be overrided with NormSetSocketOptions()        
const UINT16 DEFAULT_NUM_DATA = 32;
const UINT16 DEFAULT_NUM_PARITY = 4;
const UINT16 DEFAULT_NUM_AUTO = 0;
const UINT16 DEFAULT_SEGMENT_SIZE = 1400;
const unsigned int DEFAULT_BUFFER_SIZE = 2*1024*1024;

// a 'helper' function we use for debugging
const char* NormNodeGetAddressString(NormNodeHandle node)
{
    char addr[16];  // big enough for IPv6
    unsigned int addrLen = 16;
    UINT16 port;
    if (NormNodeGetAddress(node, addr, &addrLen, &port))
    {
        static char text[64];
        text[0] = text[31] = '\0';    
        int addrFamily;
        if (4 == addrLen)
            addrFamily = AF_INET;
        else
            addrFamily = AF_INET6;
        inet_ntop(addrFamily, addr, text, 31);
        sprintf(text + strlen(text), "/%hu", port);
        return text;
    }
    else
    {
        return "???";
    }
}  // end NormNodeGetAddressString()


class NormSocketInfo : public ProtoTree::Item
{
    public:
        NormSocketInfo(unsigned int remoteAddrLen, const char* remoteAddr, UINT16 remotePort)
            : norm_socket(NULL)
        {
            info_keysize = MakeKey(info_key, remoteAddrLen, remoteAddr, remotePort);
        }
        
        // copy constructor
        NormSocketInfo(const NormSocketInfo& s)
            {*this = s;}
        
        ~NormSocketInfo() {}
        
        void SetSocket(class NormSocket* theSocket)
            {norm_socket = theSocket;}
        class NormSocket* GetSocket()
            {return norm_socket;}
        
        static unsigned int MakeKey(unsigned char* key, unsigned int remoteAddrLen, const char* remoteAddr, UINT16 remotePort)
        {
            key[0] = remoteAddrLen;
            memcpy(key + 1, remoteAddr, remoteAddrLen);
            unsigned int keysize = remoteAddrLen + 1;
            memcpy(key + keysize, &remotePort, 2);
            keysize += 2;
            keysize <<= 3;  // to size in 'bits'
            return keysize;
        }
                
        void GetRemoteAddress(ProtoAddress& theAddr) const
        {
            int remoteAddrLen = info_key[0];
            const char* remoteAddrPtr = (char*)info_key + 1;
            ProtoAddress::Type addrType;
            switch (remoteAddrLen)
            {
                case 4:
                    addrType = ProtoAddress::IPv4;
                    break;
                case 16:
                    addrType = ProtoAddress::IPv6;
                    break;
                default:
                    theAddr.Invalidate();
                    ASSERT(0);
                    return;
            }
            theAddr.SetRawHostAddress(addrType, remoteAddrPtr, remoteAddrLen);
            UINT16 remotePort;
            memcpy(&remotePort, remoteAddrPtr + remoteAddrLen, 2);
            theAddr.SetPort(remotePort);
        } 
            
        const char* GetKey() const 
            {return (const char*)info_key;}     
        unsigned int GetKeysize() const
            {return info_keysize;}
        
    private:
        // remoteAddrLen + remoteAddr + remotePort
        //     1         +   16 max   +   2 
        unsigned char       info_key[19];
        unsigned int        info_keysize;
        class NormSocket*   norm_socket;  // may be NULL if it is pending acceptance
        
};  // end class NormSocketInfo

// helper function
NormSocketInfo NormGetSocketInfo(NormNodeHandle client)
{
    char remoteAddr[16]; // big enough for IPv6
    unsigned int remoteAddrLen = 16;
    UINT16 remotePort;
    NormNodeGetAddress(client, remoteAddr, &remoteAddrLen, &remotePort);
    return NormSocketInfo(remoteAddrLen, remoteAddr, remotePort);
}  // end NormGetSocketInfo()

class NormSocketTable : public ProtoTreeTemplate<NormSocketInfo>
{
    public:
        NormSocketInfo* FindSocketInfo(UINT16 remotePort, unsigned int remoteAddrLen, const char* remoteAddr)
        {
            unsigned char key[19];
            unsigned int keysize = NormSocketInfo::MakeKey(key, remoteAddrLen, remoteAddr, remotePort);
            return Find((char*)key, keysize);
        }
        
        NormSocketInfo* FindSocketInfo(NormNodeHandle client)
        {
            NormSocketInfo socketInfo = NormGetSocketInfo(client);
            return Find(socketInfo.GetKey(), socketInfo.GetKeysize());
        }
        
        void RemoveSocketInfo(NormSocketInfo& socketInfo)
        {
            // safety dance
            if (NULL == Find(socketInfo.GetKey(), socketInfo.GetKeysize())) return;  // not on the dance floor
            Remove(socketInfo);
        }
};  // end class NormSocketTable

class NormSocket
{
    public:
        NormSocket(NormSessionHandle normSession = NORM_SESSION_INVALID);
        ~NormSocket();
    
        // These methods identify the role of this socket with respect
        // to the client / server relationship (a "server socket" is
        // one for which NormListen() has been invoked).
        bool IsServerSocket() const
            {return (server_socket == this);}
        bool IsClientSocket() const
            {return (server_socket != this);}
        bool IsUnicastSocket() const
            {return (NORM_SESSION_INVALID == mcast_session);}
        bool IsMulticastSocket() const
            {return !IsUnicastSocket();}
        bool IsMulticastServer() const
            {return (IsMulticastSocket() && IsServerSocket());}
        bool IsMulticastClient() const
            {return (IsMulticastSocket() && IsClientSocket());}
        bool IsServerSide() const 
            {return (NULL != server_socket);}
        bool IsClientSide() const
            {return (NULL == server_socket);}
        
        NormSocket* GetServerSocket() 
            {return server_socket;}
    
        NormInstanceHandle GetInstance() const
            {return NormGetInstance(norm_session);}
        NormSessionHandle GetSession() const
            {return norm_session;}
        NormSessionHandle GetMulticastSession() const
            {return mcast_session;}
        
        NormObjectHandle GetTxStream() const
            {return tx_stream;}
    
        void InitRxStream(NormObjectHandle rxStream)
            {rx_stream = rxStream;}
        NormObjectHandle GetRxStream() const
            {return rx_stream;}
        
        
        void SetFlowControl(bool state)
        {
            tx_flow_control = state;
            if (NORM_OBJECT_INVALID != tx_stream)
                NormStreamSetPushEnable(tx_stream, state ? false : true);
        }
        
        void InitTxStream(NormObjectHandle txStream, unsigned int bufferSize, UINT16 segmentSize, UINT16 blockSize)
        {
            tx_stream = txStream;
            tx_segment_size = segmentSize;
            tx_stream_buffer_max = NormGetStreamBufferSegmentCount(bufferSize, segmentSize, blockSize);
            tx_stream_buffer_max -= blockSize;  // a little safety margin (perhaps not necessary)
            tx_stream_buffer_count = 0;
            tx_stream_bytes_remain = 0;
            tx_watermark_pending = false;
            tx_ready = true;
            NormStreamSetPushEnable(tx_stream, tx_flow_control ? false : true);
        }
        
        bool Open(NormInstanceHandle instance = NORM_INSTANCE_INVALID);
        
        bool Listen(UINT16 serverPort, const char* groupAddr, const char* serverAddr);
        NormSocket* Accept(NormNodeHandle client, NormInstanceHandle instance = NORM_INSTANCE_INVALID);
        bool Connect(const char* serverAddr, UINT16 serverPort, UINT16 localPort, const char* groupAddr, NormNodeId clientId);
        
        
        // Write to tx stream (with flow control)
        unsigned int Write(const char* buffer, unsigned int numBytes);
        void Flush(bool eom = false, NormFlushMode flushMode = NORM_FLUSH_ACTIVE);
        // Read from rx_stream
        bool Read(char* buffer, unsigned int& numBytes);
        
        // "graceful" shutdown (stream is flushed and stream end, etc)
        void Shutdown();
        
        // hard, immediate closure
        void Close();
    
        void GetOptions(NormSocketOptions* options)
        {
            if (NULL != options) *options = socket_option;
        }
    
        bool SetOptions(NormSocketOptions* options)
        {
            // TBD - do validity checking and perhaps reset to defaults if (NULL == options)
            if (NULL != options) socket_option = *options;
            return true;
        }
        
        void SetSocketInfo(NormSocketInfo* socketInfo)        // for server-side, client sockets only
            {socket_info = socketInfo;}
        
        NormSocketInfo* FindSocketInfo(NormNodeHandle client)
            {return client_table.FindSocketInfo(client);}
        void RemoveSocketInfo(NormSocketInfo& socketInfo)     // for server sockets only
            {client_table.RemoveSocketInfo(socketInfo);}
        
        void SetUserData(const void* userData)
            {user_data = userData;}
        const void* GetUserData() const
            {return user_data;}
        
        void SetTrace(bool state);
        
        void GetSocketEvent(const NormEvent& event, NormSocketEvent& socketEvent);
        
        typedef enum State
        {
            CLOSED,
            OPEN,
            LISTENING,
            CONNECTING,
            ACCEPTING,
            CONNECTED,
            CLOSING
        } State;
            
        bool AddAckingNode(NormNodeId nodeId)
        {
            if (NormAddAckingNode(norm_session, nodeId))
            {
                client_count++;
                return true;
            }
            else
            {
                return false;
            }
        }
        void RemoveAckingNode(NormNodeId nodeId)
            {NormRemoveAckingNode(norm_session, nodeId);}
        
        UINT16 GetLocalPort() const
            {return (NORM_SESSION_INVALID != norm_session) ? NormGetRxPort(norm_session) : 0;}
        
        //bool GetLocalAddress(char* addr, unsigned int& addrLen, UINT16& port)
        //    {return NormGetRxBindAddress(norm_session, addr, addrLen, port)}
        
        void GetPeerName(char* addr, unsigned int* addrLen, UINT16* port)
        {
            if (NULL == addrLen) return;
            switch (remote_version)
            {
                case 4:
                    if ((*addrLen >= 4) && (NULL != addr))
                        memcpy(addr, remote_addr, 4);
                    *addrLen = 4;
                    break;
                case 6:
                    if ((*addrLen >= 16) && (NULL != addr))
                        memcpy(addr, remote_addr, 16);
                    *addrLen = 16;
                    break;
                default:
                    *addrLen = 0;
                    return;
            }   
            if (NULL != port) *port = remote_port;
        }
            
    private:
        void UpdateRemoteAddress()
        {
            unsigned int addrLen = 16;
            NormNodeGetAddress(remote_node, remote_addr, &addrLen, &remote_port);
            if (4 == addrLen)
                remote_version = 4;
            else
                remote_version = 6;
        }      
        
        NormSocketOptions   socket_option;
        State               socket_state; 
        NormSessionHandle   norm_session;
        NormSessionHandle   mcast_session;   // equals norm_session for a multicast server
        NormSocket*         server_socket;   // only applies to server-side sockets
        NormSocketTable     client_table;    // only applies to server sockets
        NormSocketInfo*     socket_info;     // only applies to server-side, client sockets
        unsigned int        client_count;    // only applies to mcast server sockets
        NormNodeId          client_id;       // only applies to mcast client socket
        NormNodeHandle      remote_node;     // client socket peer info
        UINT8               remote_version;  // 4 or 6
        char                remote_addr[16]; // big enough for IPv6
        UINT16              remote_port;
        // Send stream and associated flow control state variables
        NormObjectHandle    tx_stream;
        bool                tx_ready;
        UINT16              tx_segment_size;
        unsigned int        tx_stream_buffer_max;
        unsigned int        tx_stream_buffer_count;
        unsigned int        tx_stream_bytes_remain;
        bool                tx_watermark_pending;
        bool                tx_flow_control;
        // Receive stream state
        NormObjectHandle    rx_stream;
        const void*         user_data;      // for use by user application
      
};  // end class NormSocket


NormSocket::NormSocket(NormSessionHandle normSession)
 : socket_state(CLOSED), norm_session(normSession), 
   mcast_session(NORM_SESSION_INVALID), server_socket(NULL),
   socket_info(NULL), client_count(0), client_id(NORM_NODE_NONE), 
   remote_node(NORM_NODE_INVALID), remote_version(0), remote_port(0),
   tx_stream(NORM_OBJECT_INVALID), tx_ready(false), tx_segment_size(0), 
   tx_stream_buffer_max(0), tx_stream_buffer_count(0),
   tx_stream_bytes_remain(0), tx_watermark_pending(false),
   tx_flow_control(true),
   rx_stream(NORM_OBJECT_INVALID), user_data(NULL)
{
    // Initialize socket_option with default values
    socket_option.num_data = DEFAULT_NUM_DATA;
    socket_option.num_parity = DEFAULT_NUM_PARITY;
    socket_option.num_auto = DEFAULT_NUM_AUTO;
    socket_option.segment_size = DEFAULT_SEGMENT_SIZE;
    socket_option.buffer_size = DEFAULT_BUFFER_SIZE;
    socket_option.silent_receiver = false;
    socket_option.max_delay = -1;
    
    // For now we use the NormSession "user data" option to associate
    // the session with a "socket".  In the future we may add a
    // dedicated NormSetSocket(NormSessionHandle session, NormSocketHandle normSocket) API
    // to keep the "user data" feature available for other purposes
    if (NORM_SESSION_INVALID != normSession) // this should always be true
        NormSetUserData(normSession, this);
}

NormSocket::~NormSocket()
{
    Close();
    if (NORM_SESSION_INVALID != norm_session)
    {
        NormDestroySession(norm_session);
        norm_session = NORM_SESSION_INVALID;
    }
}

bool NormSocket::Open(NormInstanceHandle instance)
{
    if (CLOSED != socket_state)
    {
        fprintf(stderr, "NormSocket::Open() error: socket already open?!\n");
        return false;
    }
    // A proper NormNodeId will be set upon NormBind(), NormConnect(), or NormListen()
    if (NORM_SESSION_INVALID == (norm_session = NormCreateSession(instance, "127.0.0.1", 0, NORM_NODE_ANY)))
    {
        perror("NormSocket::Open() error");
        return false;
    }
    NormSetUserData(norm_session, this);
    socket_state = OPEN;
    return true;
}  // end NormSocket::Open()

bool NormSocket::Listen(UINT16 serverPort, const char* groupAddr, const char* serverAddr)
{
    if (OPEN != socket_state)
    {
        /*  This wasn't a good idea (yet and maybe never)
        if ((CLOSED == socket_state) && (NORM_SESSION_INVALID != norm_session))
        {
            // closed socekt, not in use, so re-open socket ..
           NormInstanceHandle instance = NormGetInstance(norm_session);
           NormSessionHandle oldSession = norm_session;
           if (!Open(instance))
           {
               norm_session = oldSession;
               perror("NormSocket::Listen() error: unable to reopen socket");
               return false;
           }
           else
           {
               NormDestroySession(oldSession);
           }
       }
       else*/
       {
            fprintf(stderr, "NormSocket::Listen() error: socket not open!?\n");
            return false;
       }
    }
    // The code below will be cleaned/tightened up somewhat once all is working
    
    // Note that port reuse here lets us manage our "client" rx-only unicast connections the
    // way we need, but does allow a second multicast server to be started on this group which leads
    // to undefined behavior.  TBD - see if we can prevent via binding wizardry 
    // (How is it done for TCP servers? - probably because the accept() call is in the network stack 
    //  instead of user-space) Perhaps we could have a semaphore lock to block a second "server"
    if (NULL != groupAddr)
    {
        // TBD - validate that "groupAddr" is indeed a multicast address
        NormChangeDestination(norm_session, groupAddr, serverPort);
        NormSetId(norm_session, 1);  // server always uses NormNodeId '1'
        // TBD - we _could_ let the server have an independent, ephemeral tx_port
        // by _not_ calling NormSetTxPort() here to enable multiple multicast
        // servers on same group/port on same host if server instance use unique NormNodeIds?
        NormSetTxPort(norm_session, serverPort); // can't do this and distinguish unicast feedback
        NormSetMulticastInterface(norm_session, serverAddr);
        NormSetRxPortReuse(norm_session, true); 
        mcast_session = norm_session;
        
        NormSetMulticastLoopback(norm_session, true);  // for testing
        
    }   
    else
    {
        // For unicast , the "server" has a NormNodeId of '1' and the "clients" are '2'
        // to obviate need for explicit id management and will allow NAT to work, etc
        NormChangeDestination(norm_session, "127.0.0.1", serverPort);
        NormSetId(norm_session, 1);  // server always uses NormNodeId '1'
        NormSetTxPort(norm_session, serverPort);
#ifdef WIN32
        // UDP socket bind/connect does not work properly on WIN32, so no port reuse
        // (so a little different strategy is used for Win32 connections)
        NormSetRxPortReuse(norm_session, false, serverAddr);
#else
        NormSetRxPortReuse(norm_session, true, serverAddr);
#endif // if/else WIN32

    }   
    
    // TBD - the next four calls could be combined into a "NormStartListener()" function

    // Set session to track incoming clients by their addr/port
    // (instead of NormNodeId as usual)
    NormSetServerListener(norm_session, true);
    
    // Our listener is a "silent" receiver since all actual reception
    // (unicast) is handed off to a separate "client" session
    NormSetSilentReceiver(norm_session, true);
    
    // So that the listener can construct (unsent) ACKs without failure
    NormSetDefaultUnicastNack(norm_session, true);
    
    // Note we use a small buffer size here since a "listening" socket isn't 
    // going to be receiving data (TBD - implement a mechanism to handoff remote
    // sender (i.e. "client") from parent 
    if (!NormStartReceiver(norm_session, 2048))
    {
        fprintf(stderr, "NormSocket::Listen() error: NormStartReceiver() failure (perhaps port already in use)\n");  
        //NormDestroySession(norm_session);
        //norm_session = NORM_SESSION_INVALID;      
        return false;
    }
    server_socket  = this;
    socket_state = LISTENING;
    return true;
}  // end NormSocket::Listen()

NormSocket* NormSocket::Accept(NormNodeHandle client, NormInstanceHandle instance)
{   
    if (!IsServerSocket()) return NULL;
    char clientAddr[64];
    clientAddr[63] = '\0';    
    char addr[16]; // big enough for IPv6
    unsigned int addrLen = 16;
    UINT16 clientPort;
    NormNodeGetAddress(client, addr, &addrLen, &clientPort);
    int addrFamily;
    UINT8 version;
    if (4 == addrLen)
    {
        addrFamily = AF_INET;
        version = 4;
    }
    else
    {
        addrFamily = AF_INET6;
        version = 6;
    }
    inet_ntop(addrFamily, addr, clientAddr, 63);

    UINT16 serverPort = NormGetRxPort(norm_session);
    if (NORM_INSTANCE_INVALID == instance)
        instance = NormGetInstance(norm_session);
#ifdef WIN32
	NormSessionHandle clientSession = NormCreateSession(instance, clientAddr, 0, 1);
#else
    NormSessionHandle clientSession = NormCreateSession(instance, clientAddr, serverPort, 1);
    NormSetTxPort(clientSession, serverPort, true);
#endif // if/else WIN32/UNIX

	// NORM_SYNC_STREAM tries to get everything the sender has cached/buffered
    NormSetDefaultSyncPolicy(norm_session, NORM_SYNC_STREAM);
    //NormSetDefaultSyncPolicy(norm_session, NORM_SYNC_ALL);

    // This next API call will cause NORM to tightly bind the remote client src addr/port to
    // our server port so the "clientSession" captures the client packets instead of the "server" session
    
    // Any new packets will come to our new connected clientSession instead
    // However, note that even though we've "connected" this sender, 
    // there is a chance that additional packets in the "serverSession" 
    // rx socket buffer may look like a new sender if deleted now, so
    // we wait for NORM_REMOTE_SENDER_INACTIVE to delete
    
#ifndef WIN32
    // Enable rx port reuse since it's the server port, and connect
    // this socket to client addr/port for unique, tight binding
    // TBD - support option to bind to specific server address
    //fprintf(stderr, "accepting connection from %s/%d on port %d ...\n", clientAddr, clientPort, serverPort);
    NormSetRxPortReuse(clientSession, true, NULL, clientAddr, clientPort);  
#endif // WIN32
    NormSetDefaultUnicastNack(clientSession, true);
	
    NormStartReceiver(clientSession, 2*1024*1024);
	
    // This call immediately inserts the "client" remote sender state 
    // into the newly-created clientSession by injecting a NORM_CMD(CC)
    // message (and, as a result, a NORM_ACK is sent back to the client 
    // for a quick initial RTT estimate)
    NormTransferSender(clientSession, client);
    
    NormSocket* clientSocket = new NormSocket(clientSession);
    clientSocket->server_socket = this;  // this is a server-side socket
    clientSocket->remote_node = client;
    clientSocket->UpdateRemoteAddress();
    NormNodeSetUserData(client, clientSocket);
    clientSocket->socket_option = socket_option; // inherit server_socket options (TBD - allow alt options passed into NormAccept()?)
    
    NormNodeId clientId = NormNodeGetId(client);
    
    if (IsUnicastSocket())
    {
        NormChangeDestination(clientSession, clientAddr, clientPort, false); // point unicast session dest to client port
        // The clientSession is bi-directional so we need to NormStartSender(), etc
        NormAddAckingNode(clientSession, 2); //clientId);  
        NormSetFlowControl(clientSession, 0);  // disable timer-based flow control since we do explicit, ACK-based flow control
        NormStartSender(clientSession, NormGetRandomSessionId(), 
                        socket_option.buffer_size, socket_option.segment_size, 
                        socket_option.num_data, socket_option.num_parity);
        NormSetAutoParity(clientSession, socket_option.num_auto);
    }
    else // if IsMulticastSocket()
    {
        // TBD - should we make sure this not a NormNodeId we already have?
        // TBD - should we wait to add the client as acking node until CONNECT
        //       (probably for heavyweight; for lightweight we know the client
        //        has already started his multicast receiver)
        AddAckingNode(clientId);  // TBD - check result
		NormNodeHandle node = NormGetAckingNodeHandle(mcast_session, clientId);
		NormNodeSetUserData(node, clientSocket); // a way to track mcast client sockets
        clientSocket->mcast_session = mcast_session;
        clientSocket->client_id = clientId;
        if (LISTENING == socket_state)
        {
			NormSetFlowControl(norm_session, 0);  // disable timer-based flow control since we do explicit, ACK-based flow control
			NormStartSender(norm_session, NormGetRandomSessionId(), 
                            socket_option.buffer_size, socket_option.segment_size, 
                            socket_option.num_data, socket_option.num_parity);
            NormSetAutoParity(norm_session, socket_option.num_auto);
            socket_state = CONNECTED;  
            if (NORM_OBJECT_INVALID == tx_stream)
            {
                tx_stream = NormStreamOpen(norm_session,socket_option.buffer_size);
                InitTxStream(tx_stream, socket_option.buffer_size, socket_option.segment_size, socket_option.num_data);
            }
        }
        /* The code below would be invoked for "heavyweight" mcast client admission
          (for the moment we go with a "lightweight" model - this might be invokable upon
           as an optional behavior later)
          
        // Here, we start the clientSession (w/ a minimal buffer size) and create a temporary sender
        // stream that is immediately flushed/closed to inform the "client" that his connection
        // has been accepted.  The sender function is terminated upon client acknowledgement
        NormAddAckingNode(clientSession, clientId);  
        NormSetFlowControl(clientSession, 0);  // disable timer-based flow control since we do explicit, ACK-based flow control
        NormStartSender(clientSession, NormGetRandomSessionId(), 1024, 512, 1, 0);
        NormObjectHandle tempStream = NormStreamOpen(clientSession, 1024);
        NormStreamClose(tempStream, true);  // Note our "trick" here to do a graceful close, _then_ watermark to get ack
        NormSetWatermark(clientSession, tempStream, true);  // future NORM API will add "bool watermark" option to graceful close
        */
    }    
    // Note that for this lightweight connection mode, ACCEPTING is essentially CONNECTED so
    // app can treat this as a CONNECTED socket until otherwise notified
    // Should we start the time clientSocket timer and timeout if not connected in a timely fashion?
    clientSocket->socket_state = ACCEPTING;  // will transition to CONNECTED when client is detected on new clientSession
    return clientSocket;
}  // end NormSocket::Accept()

// TBD - provide options for binding to a specific local address, interface, etc
bool NormSocket::Connect(const char*    serverAddr, 
                         UINT16         serverPort, 
                         UINT16         localPort,
                         const char*    groupAddr, 
                         NormNodeId     clientId)
{
    if (OPEN != socket_state)
    {
        /* Not a good idea (yet and maybe never)
        if ((CLOSED == socket_state) && (NORM_SESSION_INVALID != norm_session))
        {
            // closed socekt, not in use, so re-open socket ..
           NormInstanceHandle instance = NormGetInstance(norm_session);
           NormSessionHandle oldSession = norm_session;
           if (!Open(instance))
           {
               norm_session = oldSession;
               perror("NormSocket::Connect() error: unable to reopen socket");
               return false;
           }
           else
           {
               NormDestroySession(oldSession);
           }
       }
       else
           */
       {
            fprintf(stderr, "NormSocket::Connect() error: socket not open!?\n");
            return false;
        }
    }
    // For unicast connections, the "client" manages a single NormSession for send and receive
    // (For multicast connections, there are two sessions: The same unicast session that will
    //  be set to txOnly upon CONNECT and a separate NormSession for multicast reception)
    // Setting the session port to zero here causes an ephemeral port to be assigned _and_
    // it is also a single socket (tx_socket == rx_socket) session for client->server unicast
    NormSetId(norm_session, clientId);
    // NORM_SYNC_STREAM tries to get everything the sender has cached/buffered
    NormSetDefaultSyncPolicy(norm_session, NORM_SYNC_STREAM);
    //NormSetDefaultSyncPolicy(norm_session, NORM_SYNC_ALL);

#ifndef WIN32
    // We don't set reuse for the ephemeral port, but do want to 'connect' to 
    // the server addr/port for this unicast client->server socket
    NormSetRxPortReuse(norm_session, false, NULL, serverAddr, serverPort);
#endif  // WIN32

    if (0 != localPort)
    {
        // Set client session up to use a user-specified (non-ephemeral) port number
        NormChangeDestination(norm_session, NULL, localPort, false); 
        NormSetTxPort(norm_session, localPort);
    }
    // TBD - for a multicast connection, the unicast receiver could be started with minimal buffer
    // (not that it matters since the buffers aren't activated until a sender starts sending _data_)
    if (!NormStartReceiver(norm_session, 2*1024*1024))  // to get ephemeral port assigned
    {
        fprintf(stderr, "NormSocket::Connect() error: unicast NormStartReceiver() failure\n");
        return false;
    }
    NormSetSynStatus(norm_session, true);
   
    // Point our unicast socket at the unicast server addr/port
    NormChangeDestination(norm_session, serverAddr, serverPort, false); 
    NormSessionId sessionId = NormGetRandomSessionId();  // TBD - use ephemeral port as session/instance id?
    //NormAddAckingNode(norm_session, 1);  // servers always have NormNodeId '1' for unicast sessions
    NormSetAutoAckingNodes(norm_session, NORM_TRACK_RECEIVERS);  // this way we get informed upon first ACK
    NormSetFlowControl(norm_session, 0); // since we do explicit, ACK-based flow control
    if (!NormStartSender(norm_session, sessionId, 2*1024*1024, 1400, socket_option.num_data, socket_option.num_parity))
    {
        fprintf(stderr, "NormSocket::Connect() error: NormStartSender() failure\n");
        return false;
    }
    NormSetAutoParity(norm_session, socket_option.num_auto);
    if (NULL != groupAddr)
    {
        // Create the "mcast_session" for multicast reception
        mcast_session = NormCreateSession(NormGetInstance(norm_session), groupAddr, serverPort, clientId); 
        //NormSetTxPort(mcast_session, serverPort);  // TBD - not sure this is a good idea if multiple clients on a machine?
        NormSetUserData(mcast_session, this);
        // NORM_SYNC_STREAM tries to get everything the sender has cached/buffered
        NormSetDefaultSyncPolicy(mcast_session, NORM_SYNC_STREAM);
        //NormSetDefaultSyncPolicy(mcast_session, NORM_SYNC_ALL);
    
        NormSetDefaultUnicastNack(mcast_session, true);  // we could optionally allow multicast NACKing, too
        NormSetMulticastLoopback(mcast_session, true);  // for testing
        client_id = clientId;
        // TBD - make this SSM??? ... this would allow for multiple servers using the same groupAddr/port
        // Note we 'connect' to server addr/port to make this associated with single, specific mcast server
        // TBD - Once we add code to set multicast interface, we can set the port reuse
        //       here to 'connect' to the specified server addr/port for tighter binding
        NormSetRxPortReuse(mcast_session, true, groupAddr);//, serverAddr, serverPort);
        // For a "lightweight" client->server connection establishment, we go ahead and 
        // stop our unicast receiver and start multicast receiver, assuming the server 
        // will admit us into the group.  
        // (TBD - provide a "heavier weight" connection acceptance confirmation/denial signal from server
        // via unicast from server -> client here (i.e. keep the unicast receiver open)
        if (!NormStartReceiver(mcast_session, 2*1024*1024))  // to get ephemeral port assigned
        {
            fprintf(stderr, "NormSocket::Connect() error: multicast NormStartReceiver() failure\n");
            return false;
        }
    }
    else
    {
        // Set timeout for connect attempt (for "heavyweight" mcast connect, this would also be done)
        NormSetUserTimer(norm_session, NORM_DEFAULT_CONNECT_TIMEOUT);   
    }
    server_socket = NULL;  // this is a client-side socket
    if (NORM_OBJECT_INVALID == tx_stream)
    {
        tx_stream = NormStreamOpen(norm_session, socket_option.buffer_size);
        InitTxStream(tx_stream, socket_option.buffer_size, socket_option.segment_size, socket_option.num_data);
    }
    socket_state = CONNECTING;
    
    return true;
}  // end NormSocket::Connect()


unsigned int NormSocket::Write(const char* buffer, unsigned int numBytes)
{
    // Make sure the socket is CONNECTED first
    // (TBD - an option for allowing NormWrite() to start sending
    //        data prior to connection confirmation is being considered
    //        to accelerate data transfer (most useful for short-lived 
    //        or 'urgent' connections such as transactions)
    if (CONNECTED != socket_state) return 0;
    
    if (IsMulticastClient() && IsServerSide())
    { 
        // This is multicast server rxonly client socket, so we redirect
        // the write() to the associated txonly multicast socket
        return server_socket->Write(buffer, numBytes);
    }
    // TBD - if tx_stream not yet open, open it!!!
    if (NORM_OBJECT_INVALID == tx_stream)
    {
        tx_stream = NormStreamOpen(norm_session, socket_option.buffer_size);
        InitTxStream(tx_stream, socket_option.buffer_size, socket_option.segment_size, socket_option.num_data);
    }
    
    if (!tx_flow_control)
    {
        unsigned int bytesWritten = NormStreamWrite(tx_stream, buffer, numBytes);
        return bytesWritten;
    }
    else if (tx_stream_buffer_count < tx_stream_buffer_max)
    {
        // This method uses NormStreamWrite(), but limits writes by explicit ACK-based flow control status
        // 1) How many buffer bytes are available?
        unsigned int bytesAvailable = tx_segment_size * (tx_stream_buffer_max - tx_stream_buffer_count);
        bytesAvailable -= tx_stream_bytes_remain;  // unflushed segment portiomn
        if (numBytes <= bytesAvailable) 
        {
            unsigned int totalBytes = numBytes + tx_stream_bytes_remain;
            unsigned int numSegments = totalBytes / tx_segment_size;
            tx_stream_bytes_remain = totalBytes % tx_segment_size;
            tx_stream_buffer_count += numSegments;
        }
        else
        {
            numBytes = bytesAvailable;
            tx_stream_buffer_count = tx_stream_buffer_max;        
        }
        // 2) Write to the stream
        unsigned int bytesWritten = NormStreamWrite(tx_stream, buffer, numBytes);
        //assert(bytesWritten == numBytes);  // this could happen if timer-based flow control is left enabled
        // 3) Check if we need to issue a watermark ACK request?
        if (!tx_watermark_pending && (tx_stream_buffer_count >= (tx_stream_buffer_max / 2)))
        {
            //fprintf(stderr, "tx_engine_t::WriteToNormStream() initiating watermark ACK request (buffer count:%lu max:%lu usage:%u)...\n",
            //            tx_stream_buffer_count, tx_stream_buffer_max, NormStreamGetBufferUsage(tx_stream));
            NormSetWatermark(norm_session, tx_stream);
            tx_watermark_pending = true;
        }
        // TBD - set "tx_ready" to false if tx_stream_buffer_count == tx_stream_buffer_max here ???
        return bytesWritten;
    }
    else
    {
        tx_ready = false;
        return 0;
    }
}  // end NormSocket::Write()

void NormSocket::Flush(bool eom, NormFlushMode flushMode)
{
    // TBD - make sure the socket is CONNECTED first
    if (IsMulticastClient() && IsServerSide())
    {
        // This is multicast server rx-only client socket, so we redirect 
        // the flush() to the associated tx-only multicast socket
        return server_socket->Flush(eom, flushMode);
    }
    
    // NormStreamFlush always will transmit pending runt segments, if applicable
    // (thus we need to manage our buffer counting accordingly if pending bytes remain)
    if (tx_watermark_pending)
    {
        NormStreamFlush(tx_stream, eom, flushMode);
    }
    else if (NORM_FLUSH_ACTIVE == flushMode)
    {
        // we flush passive, because watermark forces active ack request
        NormStreamFlush(tx_stream, eom, NORM_FLUSH_PASSIVE);
        NormSetWatermark(norm_session, tx_stream, true);
    }
    else
    {
        NormStreamFlush(tx_stream, eom, flushMode);
    }
   
    if (0 != tx_stream_bytes_remain)
    {
        // The flush forces the runt segment out, so we increment our buffer usage count
        tx_stream_buffer_count++;
        tx_stream_bytes_remain = 0;
        if (!tx_watermark_pending && (tx_stream_buffer_count >= (tx_stream_buffer_max >> 1)))
        {
            //fprintf(stderr, "tx_engine_t::stream_flush() initiating watermark ACK request (buffer count:%lu max:%lu usage:%u)...\n",
            //       tx_stream_buffer_count, tx_stream_buffer_max);
            NormSetWatermark(norm_session, tx_stream, true);
            tx_watermark_pending = true;
        }
    } 
}  // end NormSocket::Flush()

bool NormSocket::Read(char* buffer, unsigned int& numBytes)
{
    // TBD - make sure rx_stream is valid!
    // TBD - make sure this is not a tx only client socket ...
    if (NORM_OBJECT_INVALID != rx_stream)
    {
        return NormStreamRead(rx_stream, buffer, &numBytes);
    }
    else
    {
        numBytes = 0;
        return true;
    }
}  // end NormSocket::Read()

void NormSocket::Shutdown()
{
    // TBD - should we call NormStopReceiver(norm_session) here
    //       or have SHUT_RD, SHUT_WR, and SHUT_RDWR flags
    //       like the sockets "shutdown()" call???
    // For now, we do a "graceful" SHUT_RDWR behavior
    if (CONNECTED == socket_state)
    {
        NormStopReceiver(norm_session);
        rx_stream = NULL;
        if ((IsServerSide() && IsMulticastClient()) || (NORM_OBJECT_INVALID == tx_stream))
        {
            // Use a zero-timeout to immediately post NORM_SOCKET_CLOSE notification
            NormSetUserTimer(norm_session, 0.0);
        }
        else if (NORM_OBJECT_INVALID != tx_stream)
        {
            // It controls a tx_stream, so shutdown the tx_stream gracefully 
            NormStreamClose(tx_stream, true);  // Note our "trick" here to do a graceful close, _then_ watermark to get ack
            NormSetWatermark(norm_session, tx_stream, true);  // future NORM API will add "bool watermark" option to graceful close
        }
        socket_state = CLOSING;
    }
    else
    {
        // Use a zero-timeout to immediately post NORM_SOCKET_CLOSE notification
        NormSetUserTimer(norm_session, 0.0);
    }
}  // end NormSocket::Shutdown()
 
void NormSocket::Close()
{
    if (IsMulticastSocket())
    {
        if (IsServerSide())
        {
            if (IsServerSocket())
            {
                // IsMulticastSocket() guarantees the mcast_session is valid
                // Dissociate remaining clients from this session and set their
                // timers so that NORM_SOCKET_CLOSE events are dispatched for them
                NormNodeId nodeId = NORM_NODE_NONE;
                while (NormGetNextAckingNode(mcast_session, &nodeId))
                {
                    NormNodeHandle node = NormGetAckingNodeHandle(mcast_session, nodeId);
                    assert(NORM_NODE_INVALID != node);
                    NormSocket* clientSocket = (NormSocket*)NormNodeGetUserData(node);
                    NormSetUserTimer(clientSocket->norm_session, 0.0);
                }
                // for mcast server mcast_session == norm_session so it's destroyed below
            }
            else
            {
                // "IsServerSide()" guarantees the "server_socket" is non-NULL
                // server-side multicast client socket closing, so we
                // need to remove this "client" NormNodeId from the mcast
                // session's acking node list
                server_socket->RemoveAckingNode(client_id);
           }
       }
       else  // client-side multicast socket, so we need to destroy mcast_session
       {
           NormDestroySession(mcast_session);
       }
       mcast_session = NORM_SESSION_INVALID;
    }
    if (NORM_SESSION_INVALID != norm_session)
    {
        NormCancelUserTimer(norm_session);
        NormStopSender(norm_session);
        NormStopReceiver(norm_session);
    }
    if (NULL != socket_info)
    {
        if (NULL != server_socket)
            server_socket->RemoveSocketInfo(*socket_info);
        delete socket_info;
        socket_info = NULL;
    }
    
    // Iterate through remaining socket info and disassociate from any clients remaining
    NormSocketTable::Iterator iterator(client_table);
    NormSocketInfo* socketInfo;
    while (NULL != (socketInfo = iterator.GetNextItem()))
    {
        NormSocket* clientSocket = socketInfo->GetSocket();
        if (NULL != clientSocket)
            clientSocket->SetSocketInfo(NULL);
        client_table.Remove(*socketInfo);
        delete socketInfo;
    }    
    
    server_socket = NULL;
    remote_node = NORM_NODE_INVALID;
    tx_stream = NORM_OBJECT_INVALID;
    tx_segment_size = 0;
    tx_stream_buffer_max = tx_stream_buffer_count = tx_stream_bytes_remain = 0;
    tx_watermark_pending = false;
    rx_stream = NORM_OBJECT_INVALID;
    socket_state = CLOSED;
}  // end NormSocket::Close()
 
 
void NormSocket::GetSocketEvent(const NormEvent& event, NormSocketEvent& socketEvent)
{
    socketEvent.socket = (NormSocketHandle)this;
    socketEvent.type = NORM_SOCKET_NONE;  // default socket event type if no socket-specific state change occurs
    socketEvent.event = event;
    //fprintf(stderr, "NormSocket::GetSocketEvent() norm event type:%d session:%p\n", event.type, event.session);
    switch (event.type)
    {
        case NORM_TX_QUEUE_EMPTY:
        case NORM_TX_QUEUE_VACANCY:
        {
            // The socket may be tx ready, so issue a NORM_SOCKET_WRITE event
            if (CONNECTED == socket_state)
            {
                if (!tx_ready)
                {
                    tx_ready = true;
                    socketEvent.type = NORM_SOCKET_WRITE;
                }
            }
            break;
        }
        case NORM_TX_WATERMARK_COMPLETED:
        {
            switch (socket_state)
            {
                /*
                case ACCEPTING:
                {
                    // This only comes into play for the "confirmed connection"
                    // model for multicast sockets (not yet implemented)
                    assert(0);
                    assert(IsServerSide() && IsMulticastClient());
                    if (NORM_ACK_SUCCESS == NormGetAckingStatus(norm_session))
                    {
                        // Client has acknowledged our acceptance
                        socketEvent.type = NORM_SOCKET_CONNECT;
                        NormStopSender(norm_session);  // the mcast_session is our tx channel
                        break;
                    }
                    else
                    {
                        // Client didn't acknowledge, so we cull him from our server
                        socketEvent.type = NORM_SOCKET_CLOSE;
                    }
                    break;
                }
                */
                case CLOSING:
                {
                    // Socket that was shutdown has either been acknowledged or timed out
                    // TBD - should we issue a different event if ACK_FAILURE???
                    Close();
                    socketEvent.type = NORM_SOCKET_CLOSE;
                    break;
                }
                default:
                {
                    // TBD - implement option for more persistence
                    bool success = false;
                    if (NORM_ACK_SUCCESS == NormGetAckingStatus(norm_session))
                    {
                        success = true; 
                    } 
                    else
                    {
                        // At least one receiver didn't acknowledge
                        if (IsUnicastSocket() || IsMulticastClient())
                        {
                            // We could be infinitely persistent w/ NormResetWatermark()
                            // (TBD - provide a NormSocket "keep alive" option
                            NormResetWatermark(event.session);
                            // Or just declare the connection broken/closed
                            //socketEvent.type = NORM_SOCKET_CLOSE;
                        }
                        else
                        {
                            // Multicast server, so determine who failed to acknowledge
                            // and cull them from our acking node list ... and shutdown
                            // their associated unicast sockets ... ugh!!!
                            NormNodeId nodeId = NORM_NODE_NONE;
                            NormAckingStatus ackingStatus;
                            while (NormGetNextAckingNode(mcast_session, &nodeId, &ackingStatus))
                            {
                                if (NORM_ACK_SUCCESS == ackingStatus)
                                {
                                    success = true;  // there was at least one success
                                }
                                else
                                {
                                    NormNodeHandle node = NormGetAckingNodeHandle(mcast_session, nodeId);
                                    assert(NORM_NODE_INVALID != node);
                                    NormSocket* clientSocket = (NormSocket*)NormNodeGetUserData(node);
                                    assert(NULL != clientSocket);
                                    // We use the session timer to dispatch a NORM_SOCKET_CLOSE per failed client
                                    // (This will also remove the client from this server's acking list)
                                    clientSocket->socket_state = CLOSING;
                                    NormSetUserTimer(clientSocket->norm_session, 0.0);
                                }
                            }
                            // TBD - what do we if all clients failed ... issue a NORM_SOCKET_DISCONNECT event, 
                            // probably stop sending data and resume when a new client appears ???
                        }
                    }
                    if (tx_watermark_pending && success)
                    {
                        // flow control acknowledgement
                        tx_watermark_pending = false;
                        tx_stream_buffer_count -= (tx_stream_buffer_max >> 1);
                        if (!tx_ready)
                        {
                            tx_ready = true;
                            socketEvent.type = NORM_SOCKET_WRITE;
                        }
                    }
                    break;
                }
            }
            break;
        }
        case NORM_ACKING_NODE_NEW:  // This means we have received an ACK from the server
        case NORM_REMOTE_SENDER_RESET:
        case NORM_REMOTE_SENDER_NEW:
        {
            switch (socket_state)
            {
                case LISTENING:
                {
                    NormSocketInfo* socketInfo = client_table.FindSocketInfo(event.sender);
                    if (NULL == socketInfo)
                    {
                        // Add info for client socket pending acceptance
                        socketInfo = new NormSocketInfo(NormGetSocketInfo(event.sender));
                        if (NULL != socketInfo)
                        {
                            client_table.Insert(*socketInfo);
                            socketEvent.type = NORM_SOCKET_ACCEPT;
                        }
                        else
                        {
                            perror("NormSocket::GetSocketEvent() error: unable to add  pending client info to server socket:\n");
                        }
                    }
                    else //  duplicative accept event for existing socket, so ignore
                    {
                        ProtoAddress remoteAddr;
                        socketInfo->GetRemoteAddress(remoteAddr);
                        fprintf(stderr, "NormSocket::GetSocketEvent() warning:  duplicative %s from client %s/%hu...\n",
                                        (NORM_REMOTE_SENDER_NEW == event.type) ? "new" : "reset",
                                        remoteAddr.GetHostString(), remoteAddr.GetPort());
                        // TBD - should we go ahead and delete this event.sender???
                    }
                    break;
                }
                case ACCEPTING:
                    if (IsServerSide() && IsClientSocket() && (NORM_NODE_INVALID != remote_node))
                    {
                        NormNodeDelete(remote_node);
                    }
                case CONNECTING:
                    // TBD - We should validate that it's the right remote sender
                    //       (i.e., by source address and/or nodeId)
                    NormCancelUserTimer(norm_session);
                    socketEvent.type = NORM_SOCKET_CONNECT;
                    NormSetSynStatus(norm_session, false);
                    socket_state = CONNECTED;
                    // Since UDP connect/bind doesn't really work properly on 
					// Windows, the Windows NormSocket server farms out client connections
					// to new ephemeral port numbers, so we need to update
					// the destination port upon connection (Yuck!)
                    remote_node = event.sender;
					UpdateRemoteAddress();
					NormChangeDestination(norm_session, NULL, remote_port);
                    if (NORM_OBJECT_INVALID == tx_stream)
                    {
                        tx_stream = NormStreamOpen(norm_session, socket_option.buffer_size);
                        InitTxStream(tx_stream, socket_option.buffer_size, socket_option.segment_size, socket_option.num_data);
                    }
					
                    break;
                case CONNECTED:
                    if (IsMulticastSocket())
                    {
                        if (IsServerSocket())
                        {
                            // New client showing up at our multicast party
                            socketEvent.type = NORM_SOCKET_ACCEPT;
                        }
                        else
                        {
                            // TBD - validate if this same server or not (e.g. by source addr/port)
                            // Different sender showing up in multicast group!?
                            if (event.sender != remote_node)
							{
								char senderAddr[16];
								unsigned int addrLen = 16;
								UINT16 senderPort;
								NormNodeGetAddress(event.sender, senderAddr, &addrLen, &senderPort);
								unsigned int senderVersion = (4 == addrLen) ? 4 : 6;
								if ((senderVersion != remote_version) ||
									(senderPort != remote_port) ||
									(0 != memcmp(senderAddr, remote_addr, addrLen)))
								{
									//fprintf(stderr, "NormSocket warning: multicast sender %s reset?!\n", NormNodeGetAddressString(event.sender));
								}
							}
                            // TBD - should Close() the socket and issue a NORM_SOCKET_CLOSE event
                            //        and leave it up to the application to reconnect?  Or should we
                            //        provides some sort of NORM_SOCKET_DISCONNECT event
                            //socketEvent.type = NORM_SOCKET_CLOSE;
                        }
                    }
                    else  // unicast
                    {
                        // Eemote sender reset? How do we tell?
                        // TBD - validate if this same server or not (e.g. by source addr/port)
                        if (event.sender != remote_node)
                        {
                            char senderAddr[16];
                            unsigned int addrLen = 16;
                            UINT16 senderPort; 
                            NormNodeGetAddress(event.sender, senderAddr, &addrLen, &senderPort);
                            unsigned int senderVersion = (4 == addrLen) ? 4 : 6;
                            if ((senderVersion != remote_version) ||
                                (senderPort != remote_port) ||
                                (0 != memcmp(senderAddr, remote_addr, addrLen)))
                            {
                                fprintf(stderr, "NormSocket warning: unicast sender %s reset?!\n", NormNodeGetAddressString(event.sender));
                            }
                        }
                        // Close();
                        //socketEvent.type = NORM_SOCKET_CLOSE;
                    }
                    break;

                default:  // CLOSING, CLOSE
                    // shouldn't happen
                    break;
            }
            break;
        }
       
        case NORM_SEND_ERROR:
        {
            TRACE("NormSocket got SEND ERROR\n");
            switch (socket_state)
            {
                case CONNECTING:
                case ACCEPTING:
                case CONNECTED:
                case CLOSING:
                    if (IsMulticastServer())
                        fprintf(stderr, "SEND_ERROR on a multicast server socket?!\n");
                    /*else
                        fprintf(stderr, "SEND_ERROR session:%p sender:%p remote_node:%p (%s)\n", 
                                        event.session, event.sender, remote_node,
                                        NormNodeGetAddressString(remote_node));*/
                    Close();
                    socketEvent.type = NORM_SOCKET_CLOSE;
                    break;
                default:
                    // shouldn't happen
                    break;
            }
            break;
        }
        case NORM_USER_TIMEOUT:
        {
            switch (socket_state)
            {
                case CONNECTING:    // client connection attempt timed out
                case ACCEPTING:     // accepted client didn't follow through
                case CONNECTED:     // multicast client ack failure
                case CLOSING:
                    Close();
                    socketEvent.type = NORM_SOCKET_CLOSE;
                    break;
                default:
                    // shouldn't happen
                    assert(0);
                    break;
            }
            break;
        }
        case NORM_RX_CMD_NEW:
        {
            char buffer[4096];
            unsigned int buflen = 4096;
            if (NormNodeGetCommand(event.sender, buffer, &buflen))
            {
                if ((buflen < 2) || (NORM_SOCKET_VERSION == buffer[0]))
                {
                    if (NORM_SOCKET_CMD_REJECT == buffer[1])
                    {
                        Close();
                        socketEvent.type = NORM_SOCKET_CLOSE;
                    }
                    else
                    {
                        fprintf(stderr, "NormSocket warning: received unknown command\n");
                    }
                }
                else
                {
                    fprintf(stderr, "NormSocket warning: received command with invalid version\n");
                }
            }
            else
            {
                fprintf(stderr, "NormSocket warning: unable to get received command\n");
            }
            break;
        }
        case NORM_REMOTE_SENDER_INACTIVE:
        {
            switch (socket_state)
            {
                case LISTENING:
                {
                    // delete state for remote sender that has been accepted (or not)
                    // TBD - do something a little more tidy here
                    NormSocket* clientSocket = (NormSocket*)NormNodeGetUserData(event.sender);
                    if ((NULL != clientSocket) && (clientSocket->remote_node == event.sender))
                        clientSocket->remote_node = NORM_NODE_INVALID;
                    NormNodeDelete(event.sender);
                    break;
                }
                case CONNECTED:
                {
                    if (IsServerSocket())
                    {
                        NormSocket* clientSocket = (NormSocket*)NormNodeGetUserData(event.sender);
                        if ((NULL != clientSocket) && (clientSocket->remote_node == event.sender))
                            clientSocket->remote_node = NORM_NODE_INVALID;
                        NormNodeDelete(event.sender);
                    }
                    // TBD - should we do something here (perhaps issue a NORM_SOCKET_IDLE event or something
                    // that could be used as a clue that our "connection" may have broken or timed out???
                    // (Meanwhile, applications will have to figure that our for themselves)
                    break;
                }
                default:  // CONNECTING, ACCEPTING, CLOSING, CLOSED
                {
                    // shouldn't happen
                    break;
                }
            }
            break;           
        }
        case NORM_RX_OBJECT_NEW:
        {
            switch (socket_state)
            {
                case LISTENING:
                    // TBD - shouldn't happen, delete sender right away?
                    break;
                case CONNECTED:
                    // TBD - make sure the sender is who we expect it to be???
                    if (IsServerSocket()) break;
                    if (NORM_OBJECT_INVALID == rx_stream)
                    {
                        // We're expecting this, new stream ready for reading ...
                        InitRxStream(event.object); 
                        socketEvent.type = NORM_SOCKET_READ;
                    }
                    else
                    {
                        // Stream reset
                        fprintf(stderr, "NormSocket::GetSocketEvent(NORM_RX_OBJECT_NEW) warning: client stream reset?!\n");
                    }
                    break;
                default:  // CONNECTING, ACCEPTING, CLOSING, CLOSED
                    // shouldn't happen
                    break;
            }
            break;
        }
        case NORM_RX_OBJECT_UPDATED:
        {
            switch (socket_state)
            {
                case CONNECTED:
                    // TBD - use an rx_ready indication to filter this event a little more
                    if (IsServerSocket()) break;  // we don't receive data on server socket
                    if (event.object == rx_stream)
                        socketEvent.type = NORM_SOCKET_READ;
                    else
                        fprintf(stderr, "NormSocket::GetSocketEvent(NORM_RX_OBJECT_UPDATED) warning: non-matching rx object\n");
                    break;
                default:
                    // shouldn't happen
                    break;    
            }
            break;
        }
        case NORM_RX_OBJECT_ABORTED:
        case NORM_RX_OBJECT_COMPLETED:
        {
            if (event.object != rx_stream)
                break; // not our stream, so ignore
            rx_stream = NORM_OBJECT_INVALID;
            switch (socket_state)
            {
                case CONNECTED:
                    // Initiate graceful closure of our tx_stream to allow at least some time to
                    // acknowledge the remote before closing everything down
                    if (NORM_OBJECT_INVALID != tx_stream)
                    {
                        NormStreamClose(tx_stream, true);  // Note our "trick" here to do a graceful close, _then_ watermark to get ack
                        NormSetWatermark(norm_session, tx_stream, true);  // future NORM API will add "bool watermark" option to graceful close
                        socket_state = CLOSING;
                    }
                    else
                    {
                        // This still allows at least a chance of an ACK to be sent upon completion
                        TRACE("NormSetUserTimer(5) session: %p\n", norm_session);
                        NormSetUserTimer(norm_session, 0.0);
                    }
                    socketEvent.type = NORM_SOCKET_CLOSING;
                    break;
                case CLOSING:
                    // We're already closing, so just let that complete.  This helps make sure we allow
                    // at least some time to acknowledge the remote before closing everything down
                    break;
                default:
                    // shouldn't happen
                    break;
            }
            break;
        }
        default:
            break;
    }
    //fprintf(stderr, "NormSocket::GetSocketEvent() returning NormSocket event type:%d session:%p\n", socketEvent.type, event.session);
}  // end NormSocket::GetSocketEvent()
 


void NormSocket::SetTrace(bool state)
{
    if (NORM_SESSION_INVALID != norm_session)
        NormSetMessageTrace(norm_session, state);
    if (NORM_SESSION_INVALID != mcast_session)
        NormSetMessageTrace(mcast_session, state);
}  // end NormSocket::SetTrace()
 ///////////////////////////////////////////////////////////////////////////////////
 // NormSocket API implementation 


NormSocketHandle NormOpen(NormInstanceHandle instance)
{
    NormSocket* normSocket = new NormSocket();
    if (NULL == normSocket)
    {
        perror("NormOpen() new NormSocket() error");
        return NORM_SOCKET_INVALID;
    }
    else if (normSocket->Open(instance))
    {
        return (NormSocketHandle)normSocket;
    }
    else
    {
        perror("NormOpen() error");
        delete normSocket;
        return NORM_SOCKET_INVALID;
    }
}  // end NormOpen()
 
// TBD - provide options for binding to a specific local address, interface, etc
bool NormListen(NormSocketHandle normSocket, UINT16 serverPort, const char* groupAddr, const char* serverAddr)
{
    // TBD - make sure normSocket is valid
    NormSocket* s = (NormSocket*)normSocket;
    return s->Listen(serverPort, groupAddr, serverAddr);
}  // end NormListen()


NormSocketHandle NormAccept(NormSocketHandle serverSocket, NormNodeHandle client, NormInstanceHandle instance)
{
    // TBD - if another instance handle is provided use that instead
    // TBD - VALIDATE PARAMETERS AND ERROR CHECK ALL THE API CALLS MADE HERE !!!!!
    NormSocket* s = (NormSocket*)serverSocket;
    NormInstanceHandle serverInstance = s->GetInstance();
    NormSuspendInstance(serverInstance);
    NormSocketHandle clientSocket = s->Accept(client, instance);
    NormResumeInstance(serverInstance);
    if (NORM_SOCKET_INVALID != clientSocket)
    {
        // Keep track of this client socket in our serverSocket socket_table
        NormSocketInfo* socketInfo = s->FindSocketInfo(client);
        ASSERT(NULL != socketInfo);
        NormSocket* c = (NormSocket*)clientSocket;
        socketInfo->SetSocket(c);
        c->SetSocketInfo(socketInfo);
    }
    return clientSocket;
}  // end NormAccept()
 

NORM_API_LINKAGE
extern bool NormSendCommandTo(NormSessionHandle  sessionHandle,
                              const char*        cmdBuffer, 
                              unsigned int       cmdLength, 
                              const char*        addr,
                              UINT16             port);

void NormReject(NormSocketHandle    serverSocket,
                NormNodeHandle      clientNode)
{
    // Simple, single "reject" command for moment (TBD - do something more stateful so app will be bothered less)
    // Send "reject" command to source
    char buffer[2];
    buffer[0] = NORM_SOCKET_VERSION;
    buffer[1] = NORM_SOCKET_CMD_REJECT;
    NormSocket* s = (NormSocket*)serverSocket;
    NormSocketInfo socketInfo = NormGetSocketInfo(clientNode);
    ProtoAddress dest;
    socketInfo.GetRemoteAddress(dest);
    char destString[64];
    destString[63] = '\0';
    dest.GetHostString(destString, 63);
    NormSendCommandTo(s->GetSession(), buffer, 2,destString, dest.GetPort());
}  // end NormReject()

// TBD - provide options for binding to a specific local address, interface, etc
bool NormConnect(NormSocketHandle normSocket, const char* serverAddr, UINT16 serverPort, UINT16 localPort, const char* groupAddr, NormNodeId clientId)
{
    // TBD - make sure normSocket is valid
    NormSocket* s = (NormSocket*)normSocket;
    NormInstanceHandle instance = s->GetInstance();
    NormSuspendInstance(instance);
    bool result = s->Connect(serverAddr, serverPort, localPort, groupAddr, clientId);
    NormResumeInstance(instance);
    return result;
}  // end NormConnect()


ssize_t NormWrite(NormSocketHandle normSocket, const void *buf, size_t nbyte)
{
    // TBD - we could make write() and read() optionally blocking or non-blocking
    //       by using GetSocketEvent() as appropriate (incl. returning error conditions, etc)
    NormSocket* s = (NormSocket*)normSocket;
    NormInstanceHandle instance = s->GetInstance();
    NormSuspendInstance(instance);
    ssize_t result = (ssize_t)s->Write((const char*)buf, (unsigned int)nbyte);
    NormResumeInstance(instance);
    return result;
}  // end NormWrite()

int NormFlush(NormSocketHandle normSocket)
{
    NormSocket* s = (NormSocket*)normSocket;
    NormInstanceHandle instance = s->GetInstance();
    NormSuspendInstance(instance);
    s->Flush();
    NormResumeInstance(instance);
    return 0;
} // end NormFlush()

ssize_t NormRead(NormSocketHandle normSocket, void *buf, size_t nbyte)
{
    // TBD - we could make write() and read() optionally blocking or non-blocking
    //       by using GetSocketEvent() as appropriate (incl. returning error conditions, etc)
    NormSocket* s = (NormSocket*)normSocket;
    NormInstanceHandle instance = s->GetInstance();
    NormSuspendInstance(instance);
    // TBD - make sure s->rx_stream is valid 
    unsigned int numBytes = (unsigned int)nbyte;
    ssize_t result;
    if (s->Read((char*)buf, numBytes))
        result = numBytes;
    else
        result = -1; // broken stream error (TBD - enumerate socket error values)
    NormResumeInstance(instance);
    return result;
}  // end NormWrite()

void NormShutdown(NormSocketHandle normSocket)
{
    NormSocket* s = (NormSocket*)normSocket;
    NormInstanceHandle instance = s->GetInstance();
    NormSuspendInstance(instance);
    s->Shutdown();
    NormResumeInstance(instance);
}  // end NormShutdown()

void NormClose(NormSocketHandle normSocket)
{
    NormSocket* s = (NormSocket*)normSocket;
    NormInstanceHandle instance = s->GetInstance();
    NormSuspendInstance(instance);
    s->Close();
    NormResumeInstance(instance);
    delete s;
}  // end NormClose()

void NormGetSocketOptions(NormSocketHandle normSocket, NormSocketOptions* options)
{
    NormSocket* s = (NormSocket*)normSocket;
    s->GetOptions(options);
}  // end NormGetSocketOptions()

bool NormSetSocketOptions(NormSocketHandle normSocket, NormSocketOptions* options)
{
    // TBD - do some validity checking, perhaps reset to defaults if (options == NULL)
    NormSocket* s = (NormSocket*)normSocket;
    return s->SetOptions(options);
}  // end NormSetSocketOptions()

void NormSetSocketUserData(NormSocketHandle normSocket, const void* userData)
{
    if (NORM_SOCKET_INVALID != normSocket)
        ((NormSocket*)normSocket)->SetUserData(userData);
}  // end NormSetSocketUserData()

const void* NormGetSocketUserData(NormSocketHandle normSocket)
{
     NormSocket* s = (NormSocket*)normSocket;
     return s->GetUserData();
}  // end NormGetSocketUserData()


// This gets and translates low level NORM API events to NormSocket events 
// given the "normSocket" state
bool NormGetSocketEvent(NormInstanceHandle instance, NormSocketEvent* socketEvent, bool waitForEvent)
{
    if (NULL == socketEvent) return false;
    NormEvent event;
    if (NormGetNextEvent(instance, &event, waitForEvent))
    {
        NormSuspendInstance(instance);
        NormSocket* normSocket = NULL;
        if (NORM_SESSION_INVALID != event.session)
            normSocket = (NormSocket*)NormGetUserData(event.session);
        if (NULL == normSocket)
        {
            socketEvent->type = NORM_SOCKET_NONE;
            socketEvent->socket = NORM_SOCKET_INVALID;
            socketEvent->event = event;
        }
        else
        {
            normSocket->GetSocketEvent(event, *socketEvent);
        }
        NormResumeInstance(instance);
        return true;
    }
    else
    {
        return false;
    }
}  // end NormGetSocketEvent()

// Other helper functions

void NormGetPeerName(NormSocketHandle normSocket, char* addr, unsigned int* addrLen, UINT16* port)
{
     NormSocket* s = (NormSocket*)normSocket;
     s->GetPeerName(addr, addrLen, port);
}  // end NormGetPeerName()

NormSessionHandle NormGetSocketSession(NormSocketHandle normSocket)
{
    NormSocket* s = (NormSocket*)normSocket;
    return s->GetSession();
}  // end NormGetSocketSession()

NormObjectHandle NormGetSocketTxStream(NormSocketHandle normSocket)
{
    NormSocket* s = (NormSocket*)normSocket;
    return s->GetTxStream();
}  // end NormGetSocketTxStream()

NormObjectHandle NormGetSocketRxStream(NormSocketHandle normSocket)
{
    NormSocket* s = (NormSocket*)normSocket;
    return s->GetRxStream();
}  // end NormGetSocketRxStream()

NormSessionHandle NormGetSocketMulticastSession(NormSocketHandle normSocket)
{
    NormSocket* s = (NormSocket*)normSocket;
    return s->GetMulticastSession();
}  // end NormGetSocketMulticastSession()

void NormSetSocketTrace(NormSocketHandle normSocket, bool enable)
{
    NormSocket* s = (NormSocket*)normSocket;
    s->SetTrace(enable);
}  // end NormSetSocketTrace()

void NormSetSocketFlowControl(NormSocketHandle normSocket, bool enable)
{
    NormSocket* s = (NormSocket*)normSocket;
    s->SetFlowControl(enable);
}  // end NormSetSocketFlowControl()
