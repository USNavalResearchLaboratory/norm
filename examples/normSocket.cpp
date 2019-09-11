
#include "normSocket.h"
#include <stdio.h>  // for stderr
#include <assert.h> // for assert()
#include <string.h>  // for strlen()
#include <arpa/inet.h>  // for inet_ntoa() (TBD - change to use Protolib routines?)

// COMPILE: (assumes "normApi.h" in "include" ...
// g++ -I../include -c normSocket.cpp 


// This "NormSocket" class is used to maintain tx/rx state for a NORM "socket" connection.
// At the moment this "socket" connection represents a single, bi-directional NORM_OBJECT_STREAM
// in either a unicast context or an asymmetric "server" multicast stream to possibly multiple "client"
// nodes with individual unicast streams in return from those "client" nodes. (I.e., the server will need to
// have a normSocket per client even for the server multicast case (maybe :-) )

const NormSocketHandle NORM_SOCKET_INVALID = (NormSocketHandle)0;

const double NORM_DEFAULT_CONNECT_TIMEOUT = 60.0;

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

class NormSocket
{
    public:
        NormSocket(NormSessionHandle normSession = NORM_SESSION_INVALID);
    
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
    
        NormSessionHandle GetSession() const
            {return norm_session;}
        NormSessionHandle GetMulticastSession() const
            {return mcast_session;}
    
        void InitRxStream(NormObjectHandle rxStream)
            {rx_stream = rxStream;}
        NormObjectHandle GetRxStream() const
            {return rx_stream;}
        
        void InitTxStream(NormObjectHandle txStream, unsigned int bufferSize, UINT16 segmentSize, UINT16 blockSize)
        {
            tx_stream = txStream;
            tx_segment_size = segmentSize;
            tx_stream_buffer_max = NormGetStreamBufferSegmentCount(bufferSize, segmentSize, blockSize);
            tx_stream_buffer_max -= blockSize;  // a little safety margin (perhaps not necessary)
            tx_stream_buffer_count = 0;
            tx_stream_bytes_remain = 0;
            tx_watermark_pending = false;
        }
        
        
        bool Listen(NormInstanceHandle instance, UINT16 serverPort, const char* groupAddr);
        NormSocket* Accept(NormNodeHandle client, NormInstanceHandle instance = NORM_INSTANCE_INVALID);
        bool Connect(NormInstanceHandle instance, const char* serverAddr, UINT16 serverPort, const char* groupAddr, NormNodeId clientId);
        
        
        // Write to tx stream (with flow control)
        unsigned int Write(const char* buffer, unsigned int numBytes);
        void Flush(bool eom = false, NormFlushMode flushMode = NORM_FLUSH_ACTIVE);
        // Read from rx_stream
        bool Read(char* buffer, unsigned int& numBytes);
        
        // "graceful" shutdown (stream is flushed and stream end, etc)
        void Shutdown();
        
        // hard, immediate closure
        void Close();
        
        void GetSocketEvent(const NormEvent& event, NormSocketEvent& socketEvent);
        
        typedef enum State
        {
            CLOSED,
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

            
    private:
        State               socket_state; 
        NormSessionHandle   norm_session;
        NormSessionHandle   mcast_session;   // equals norm_session for a multicast server
        NormSocket*         server_socket;   // only applies to server-side sockets
        unsigned int        client_count;    // only applies to mcast server sockets
        NormNodeId          client_id;       // only applies to mcast client socket
        NormNodeHandle      remote_node;     //
        // Send stream and associated flow control state variables
        NormObjectHandle    tx_stream;
        bool                tx_ready;
        UINT16              tx_segment_size;
        unsigned int        tx_stream_buffer_max;
        unsigned int        tx_stream_buffer_count;
        unsigned int        tx_stream_bytes_remain;
        bool                tx_watermark_pending;
        // Receive stream state
        NormObjectHandle    rx_stream;
      
};  // end class NormSocket


NormSocket::NormSocket(NormSessionHandle normSession)
 : socket_state(CLOSED), norm_session(normSession), 
   mcast_session(NORM_SESSION_INVALID), server_socket(NULL),
   client_count(0), client_id(NORM_NODE_NONE), remote_node(NORM_NODE_INVALID),
   tx_stream(NORM_OBJECT_INVALID), tx_ready(false), tx_segment_size(0), 
   tx_stream_buffer_max(0), tx_stream_buffer_count(0),
   tx_stream_bytes_remain(0), tx_watermark_pending(false),
   rx_stream(NORM_OBJECT_INVALID)
{
    // For now we use the NormSession "user data" option to associate
    // the session with a "socket".  In the future we may add a
    // dedicated NormSetSocket(NormSessionHandle session, NormSocketHandle normSocket) API
    // to keep the "user data" feature available for other purposes
    if (NORM_SESSION_INVALID != normSession) // this should always be true
        NormSetUserData(normSession, this);
}


bool NormSocket::Listen(NormInstanceHandle instance, UINT16 serverPort, const char* groupAddr)
{
    if (CLOSED != socket_state)
    {
        fprintf(stderr, "NormSocket::Listen() error: socket already open?!\n");
        return false;
    }
    
    if (NULL != groupAddr)
    {
        // TBD - validate that "groupAddr" is indeed a multicast address
        norm_session = NormCreateSession(instance, groupAddr, serverPort, NORM_NODE_ANY);
        NormSetTxPort(norm_session, serverPort); // can't do this and receive unicast feedback
        mcast_session = norm_session;
    }   
    else
    {
        // For unicast , the "server" has a NormNodeId of '1' and the "clients" are '2'
        // to obviate need for explicit id management and will allow NAT to work, etc
        norm_session = NormCreateSession(instance, "127.0.0.1", serverPort, 1);  
    }    
    if (NORM_SESSION_INVALID == norm_session)
    {
        fprintf(stderr, "NormSocket::Listen() error: NormCreateSession() failure\n");
        return false;
    }
    NormSetUserData(norm_session, this);
    // Note the port reuse here lets us manage our "client" rx-only unicast connections the
    // way we need, but does allow a second multicast server to be started on this group which leads
    // to undefined behavior.  TBD - see if we can prevent via binding wizardry 
    // (How is it done for TCP servers? - probably because the accept() call is in the network stack 
    //  instead of user-space) Perhaps we could have a semaphore lock to block a second "server"
    NormSetRxPortReuse(norm_session, true);  
    
    // use default sync policy so a "serversocket" doesn't NACK the senders it detects
    // NORM_SYNC_STREAM tries to get everything the sender has cached/buffered
    //NormSetDefaultSyncPolicy(norm_session, NORM_SYNC_STREAM);
    //NormSetDefaultSyncPolicy(norm_session, NORM_SYNC_ALL);
    
    if (NULL == groupAddr)
    {
        // Unicast server
        // Note we use a small buffer size here since a "listening" socket isn't 
        // going to be receiving data (TBD - implement a mechanism to handoff remote
        // sender (i.e. "client") from parent 
        if (!NormStartReceiver(norm_session, 2048))
        {
            fprintf(stderr, "NormSocket::Listen() error: NormStartReceiver() failure (perhaps port already in use)\n");  
            NormDestroySession(norm_session);
            norm_session = NORM_SESSION_INVALID;      
        }
    }
    else
    {
        //NormSetMulticastInterface(norm_session, "lo0");
        NormSetMulticastLoopback(norm_session, true);  // for testing
        if (!NormStartReceiver(norm_session, 2048))
        {
            fprintf(stderr, "NormSocket::Listen() error: NormStartReceiver() failure (perhaps port already in use)\n");  
            NormDestroySession(norm_session);
            norm_session = NORM_SESSION_INVALID;      
        }
        // TBD - We _could_ go ahead and call NormStartSender(), but for now we'll wait until we hear the application
        //       makes at least one NormAccept() call ...
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
    
    NormSessionHandle clientSession = NormCreateSession(instance, clientAddr, serverPort, 1);
    
    NormSetTxPort(clientSession, serverPort, false);

    // This next API call will cause NORM to tightly bind the remote client src addr/port to
    // our server port so the "clientSession" captures the client packets instead of the "server" session
    
    // Any new packets will come to our new connected clientSession instead
    // However, note that even though we've "connected" this sender, 
    // there is a chance that additional packets in the "serverSession" 
    // rx socket buffer may look like a new sender if deleted now, so
    // we wait for NORM_REMOTE_SENDER_INACTIVE to delete
    
    // NORM_SYNC_STREAM tries to get everything the sender has cached/buffered
    //NormSetDefaultSyncPolicy(norm_session, NORM_SYNC_STREAM);
    NormSetDefaultSyncPolicy(norm_session, NORM_SYNC_ALL);
            
    NormSetRxPortReuse(clientSession, true, 0, clientAddr, clientPort);  // "connects" to remote client addr/port
    NormSetDefaultUnicastNack(clientSession, true);
    
    NormStartReceiver(clientSession, 2*1024*1024);
    
    NormSocket* clientSocket = new NormSocket(clientSession);
    clientSocket->server_socket = this;  // this is a server-side socket
    clientSocket->remote_node = client;
    NormNodeSetUserData(client, clientSocket);
    
    NormNodeId clientId = NormNodeGetId(client);
    
    if (IsUnicastSocket())
    {
        // The clientSession is bi-directional so we need to NormStartSender(), etc
        NormAddAckingNode(clientSession, 2); //clientId);  
        NormSetFlowControl(clientSession, 0);  // disable timer-based flow control since we do explicit, ACK-based flow control
        NormStartSender(clientSession, NormGetRandomSessionId(), 2*1024*1024, 1400, 16, 4);
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
        clientSocket->client_id = client_id;
        if (LISTENING == socket_state)
        {
            NormSetFlowControl(norm_session, 0);  // disable timer-based flow control since we do explicit, ACK-based flow control
            NormStartSender(norm_session, NormGetRandomSessionId(), 2*1024*1024, 1400, 16, 4);
            socket_state = CONNECTED;   
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
    clientSocket->socket_state = ACCEPTING;  // will transision to CONNECTED when client is detected on new clientSession
    return clientSocket;
}  // end NormSocket::Accept()

// TBD - provide options for binding to a specific local address, interface, etc
bool NormSocket::Connect(NormInstanceHandle instance, const char* serverAddr, UINT16 serverPort, const char* groupAddr, NormNodeId clientId)
{
    // For unicast connections, the "client" manages a single NormSession for send and receive
    // (For multicast connections, there are two sessions: The same unicast session that will
    //  be set to txOnly upon CONNECT and a NormSession for multicast reception)
    norm_session = NormCreateSession(instance, "127.0.0.1", 0, clientId);  // TBD - use "clientId" here for mcast sockets?
    if (NORM_SESSION_INVALID == norm_session)
    {
        fprintf(stderr, "NormSocket::Connect() error: NormCreateSession() failure\n");
        return false;
    }
    // NORM_SYNC_STREAM tries to get everything the sender has cached/buffered
    //NormSetDefaultSyncPolicy(norm_session, NORM_SYNC_STREAM);
    NormSetDefaultSyncPolicy(norm_session, NORM_SYNC_ALL);
    
    NormSetUserData(norm_session, this);
    NormSetRxPortReuse(norm_session, true, NULL, serverAddr, serverPort);
    // TBD - for a multicast connection, the unicast receiver could be started with minimal buffer
    // (not that it matters since the buffers aren't activated until a sender starts sending _data_)
    if (!NormStartReceiver(norm_session, 2*1024*1024))  // to get ephemeral port assigned
    {
        fprintf(stderr, "NormSocket::Connect() error: unicast NormStartReceiver() failure\n");
        return false;
    }
    NormChangeDestination(norm_session, serverAddr, serverPort, false); // "connect" our NORM tx_socket (so we can get ICMP)
    NormSessionId sessionId = NormGetRandomSessionId();
    NormAddAckingNode(norm_session, 1);  // servers always have NormNodeId '1'
    NormSetFlowControl(norm_session, 0); // since we do explicit, ACK-based flow control
    if (!NormStartSender(norm_session, sessionId, 2*1024*1024, 1400, 16, 4))
    {
        fprintf(stderr, "NormSocket::Connect() error: NormStartSender() failure\n");
        return false;
    }
    
    if (NULL != groupAddr)
    {
        // Create the "mcast_session" for multicast reception
        mcast_session = NormCreateSession(instance, groupAddr, serverPort, clientId); 
        //NormSetTxPort(mcast_session, serverPort);  // TBD - not sure this is a good idea if multiple clients on a machine?
        NormSetUserData(mcast_session, this);
        // NORM_SYNC_STREAM tries to get everything the sender has cached/buffered
        //NormSetDefaultSyncPolicy(mcast_session, NORM_SYNC_STREAM);
        NormSetDefaultSyncPolicy(mcast_session, NORM_SYNC_ALL);
    
        NormSetDefaultUnicastNack(mcast_session, true);  // we could optionally allow multicast NACKing, too
        NormSetMulticastLoopback(norm_session, true);  // for testing
        client_id = clientId;
        // TBD - make this SSM??? ... this would allow for multiple servers using the same groupAddr/port
        NormSetRxPortReuse(mcast_session, true, groupAddr);  // Should we upgrade rx port reuse and 'connect' to server tx port upon CONNECT?
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
    
    socket_state = CONNECTING;
    
    return true;
}  // end NormSocket::Connect()


unsigned int NormSocket::Write(const char* buffer, unsigned int numBytes)
{
    // TBD - make sure the socket is CONNECTED first
    if (IsMulticastClient() && IsServerSide())
    {
        // This is multicast server rxonly client socket, so we redirect
        // the write() to the associated txonly multicast socket
        return server_socket->Write(buffer, numBytes);
    }
    // TBD - if tx_stream not yet open, open it!!!
    if (NORM_OBJECT_INVALID == tx_stream)
    {
        tx_stream = NormStreamOpen(norm_session, 2*1024*1024);
        InitTxStream(tx_stream, 2*1024*1024, 1400, 16);
    }
    
    // This method uses NormStreamWrite(), but limits writes by explicit ACK-based flow control status
    if (tx_stream_buffer_count < tx_stream_buffer_max)
    {
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
        return bytesWritten;
    }
    else
    {
        return 0;
    }
}  // end NormSocket::Write()

void NormSocket::Flush(bool eom, NormFlushMode flushMode)
{
    // TBD - make sure the socket is CONNECTED first
    if (IsMulticastClient() && IsServerSide())
    {
        // This is multicast server rxOnly client socket, so we redirect 
        // the flush() to the associated txonly multicast socket
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
     return NormStreamRead(rx_stream, buffer, &numBytes);
 }  // end NormSocket::Read()
 
 void NormSocket::Shutdown()
 {
     if ((NORM_OBJECT_INVALID == tx_stream)  ||
         (IsServerSide() && IsMulticastClient()))
     {
         Close();  // close immediately since this socket doesn't control a tx_stream
     }
     else
     {
         // It controls a tx_stream, so shutdown the tx_stream gracefully 
         NormStreamClose(tx_stream, true);  // Note our "trick" here to do a graceful close, _then_ watermark to get ack
         NormSetWatermark(norm_session, tx_stream, true);  // future NORM API will add "bool watermark" option to graceful close
         socket_state = CLOSING;
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
                 // timer so that NORM_SOCKET_CLOSED events are dispatched for them
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
        else  // client-side multicast socket, so we need to destroy mcast_session, too
        {
            NormDestroySession(mcast_session);
        }
        mcast_session = NORM_SESSION_INVALID;
     }
     if (NORM_SESSION_INVALID != norm_session)
     {
         NormDestroySession(norm_session);
         norm_session = NORM_SESSION_INVALID;
     }
     server_socket = NULL;
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
                        Close();
                        socketEvent.type = NORM_SOCKET_CLOSED;
                    }
                    break;
                }
                case CLOSING:
                {
                    // Socket that was shutdown has either been acknowledged or timed out
                    // TBD - should we issue a different event if ACK_FAILURE???
                    Close();
                    socketEvent.type = NORM_SOCKET_CLOSED;
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
                            //NormResetWatermark(event.session);
                            // For now, we'll just declare the connection broken/closed
                            Close();
                            socketEvent.type = NORM_SOCKET_CLOSED;
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
                                    // We use the session timer to dispatch a NORM_SOCKET_CLOSED per failed client
                                    // (This will also remove the client from this server's acking list)
                                    NormSetUserTimer(clientSocket->norm_session, 0.0);
                                    clientSocket->socket_state = CLOSING;
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
        case NORM_REMOTE_SENDER_RESET:
        case NORM_REMOTE_SENDER_NEW:
        {
            switch (socket_state)
            {
                case LISTENING:
                    socketEvent.type = NORM_SOCKET_ACCEPT;
                    break;
                case ACCEPTING:
                    if (IsServerSide() && IsClientSocket() && (NORM_NODE_INVALID != remote_node))
                        NormNodeDelete(remote_node);
                case CONNECTING:
                    // TBD - We should validate that it's the right remote sender
                    //       (i.e., by source address and/or nodeId)
                    NormCancelUserTimer(norm_session);
                    socketEvent.type = NORM_SOCKET_CONNECT;
                    socket_state = CONNECTED;
                    remote_node = event.sender;
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
                            // Different sender showing up in multicast group!?
                            fprintf(stderr, "NormSocket warning: multicast sender %s reset?!\n", NormNodeGetAddressString(event.sender));
                            // TBD - should Close() the socket and issue a NORM_SOCKET_CLOSED event
                            //        and leave it up to the application to reconnect?  Or should we
                            //        provides some sort of NORM_SOCKET_DISCONNECT event
                            socketEvent.type = NORM_SOCKET_CLOSED;
                            Close();
                        }
                    }
                    else  // unicast
                    {
                        // Eemote sender reset? How do we tell?
                        fprintf(stderr, "NormSocket warning: unicast sender %s reset?!\n", NormNodeGetAddressString(event.sender));
                        socketEvent.type = NORM_SOCKET_CLOSED;
                        Close();
                    }
                    break;

                default:  // CLOSING, CLOSED
                    // shouldn't happen
                    break;
            }
            break;
        }
        case NORM_SEND_ERROR:
        {
            switch (socket_state)
            {
                case CONNECTING:
                case ACCEPTING:
                case CONNECTED:
                case CLOSING:
                    if (IsMulticastServer())
                        fprintf(stderr, "SEND_ERROR on a multicast server socket?!\n");
                    socketEvent.event.sender = remote_node;
                    socketEvent.type = NORM_SOCKET_CLOSED;
                    Close();
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
                    socketEvent.event.sender = remote_node;
                    socketEvent.type = NORM_SOCKET_CLOSED;
                    Close();
                    break;
                default:
                    // shouldn't happen
                    assert(0);
                    break;
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
                        fprintf(stderr, "NormSocket::GetSocketEvent(): client stream reset?!\n");
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
                case CLOSING:  // we allow reading during graceful closure
                    // TBD - use an rx_ready indication to filter this event a little more
                    if (IsServerSocket()) break;  // we don't receive data on server socket
                    assert(event.object == rx_stream);
                    socketEvent.type = NORM_SOCKET_READ;
                    break;
                default:
                    // shouldn't happen
                    break;    
            }
            break;
        }
        case NORM_RX_OBJECT_COMPLETED:
        {
            rx_stream = NORM_OBJECT_INVALID;
            switch (socket_state)
            {
                case CONNECTED:
                    // Initiate graceful closure of our tx_stream to allow at least some time to
                    // acknowledge the remote before closing everything down
                    NormStreamClose(tx_stream, true);  // Note our "trick" here to do a graceful close, _then_ watermark to get ack
                    NormSetWatermark(norm_session, tx_stream, true);  // future NORM API will add "bool watermark" option to graceful close
                    socket_state = CLOSING;
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
        }
        default:
            break;
    }
}  // end NormSocket::GetSocketEvent()
 


 ///////////////////////////////////////////////////////////////////////////////////
 // NormSocket API implementation 
 
// TBD - provide options for binding to a specific local address, interface, etc
NormSocketHandle NormListen(NormInstanceHandle instance, UINT16 serverPort, const char* groupAddr)
{
    // TBD - check results
    NormSocket* normSocket = new NormSocket();
    normSocket->Listen(instance, serverPort, groupAddr);
    return (NormSocketHandle)normSocket;
}  // end NormListen()


NormSocketHandle NormAccept(NormSocketHandle serverSocket, NormNodeHandle client)
{
    // TBD - VALIDATE PARAMETERS AND ERROR CHECK ALL THE API CALLS MADE HERE !!!!!
    NormSocket* s = (NormSocket*)serverSocket;
    return (NormSocketHandle)(s->Accept(client));
}  // end NormAccept()
 

// TBD - provide options for binding to a specific local address, interface, etc
NormSocketHandle NormConnect(NormInstanceHandle instance, const char* serverAddr, UINT16 serverPort, const char* groupAddr, NormNodeId clientId)
{
    NormSocket* normSocket = new NormSocket();
    if (NULL == normSocket)
    {
        perror("NormConnect() new NormSocket() error");
        return NULL;
    }
    if (normSocket->Connect(instance, serverAddr, serverPort, groupAddr, clientId))
    {
        return normSocket;
    }
    else
    {
        delete normSocket;
        return NULL;
    }
}  // end NormConnect()


ssize_t NormWrite(NormSocketHandle normSocket, const void *buf, size_t nbyte)
{
    // TBD - we could make write() and read() optionally blocking or non-blocking
    //       by using GetSocketEvent() as appropriate (incl. returning error conditions, etc)
    NormSocket* s = (NormSocket*)normSocket;
    return (ssize_t)s->Write((const char*)buf, nbyte);
}  // end NormWrite()

int NormFlush(NormSocketHandle normSocket)
{
    NormSocket* s = (NormSocket*)normSocket;
    s->Flush();
    return 0;
} // end NormFlush()

ssize_t NormRead(NormSocketHandle normSocket, void *buf, size_t nbyte)
{
    // TBD - we could make write() and read() optionally blocking or non-blocking
    //       by using GetSocketEvent() as appropriate (incl. returning error conditions, etc)
    NormSocket* s = (NormSocket*)normSocket;
    // TBD - make sure s->rx_stream is valid 
    unsigned int numBytes = nbyte;
    if (s->Read((char*)buf, numBytes))
        return numBytes;
    else
        return -1; // broken stream error (TBD - enumerate socket error values)
}  // end NormWrite()


void NormShutdown(NormSocketHandle normSocket)
{
    NormSocket* s = (NormSocket*)normSocket;
    s->Shutdown();
}  // end NormShutdown()

void NormClose(NormSocketHandle normSocket)
{
    NormSocket* s = (NormSocket*)normSocket;
    s->Close();
}  // end NormClose()

// This gets and translates low level NORM API events to NormSocket events 
// given the "normSocket" state
bool NormGetSocketEvent(NormInstanceHandle instance, NormSocketEvent* socketEvent, bool waitForEvent)
{
    if (NULL == socketEvent) return false;
    NormEvent event;
    if (NormGetNextEvent(instance, &event, waitForEvent))
    {
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
        return true;
    }
    else
    {
        return false;
    }
}  // end NormGetSocketEvent()

// Other helper functions

NormSessionHandle NormGetSession(NormSocketHandle normSocket)
{
    NormSocket* s = (NormSocket*)normSocket;
    return s->GetSession();
}  // end NormGetSession()

NormSessionHandle NormGetMulticastSession(NormSocketHandle normSocket)
{
    NormSocket* s = (NormSocket*)normSocket;
    return s->GetMulticastSession();
}  // end NormGetSession()
