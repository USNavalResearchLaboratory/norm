#ifndef _NORP
#define _NORP

#include "protoSocket.h"
#include "protoTimer.h"
#include "protoTree.h"
#include "protoDispatcher.h"
#include "protoPktSOCKS.h"
#include "normApi.h"

class Norp;

// A "NorpSession" associates a SOCKS client session with either a remote
// NORP endpoint (if one was or can be established) or a "direct connect"
// (TCP, UDP) if the remote endpoint is not NORP-enabled

// SOCKS requests/replies relayed via NORM command transmissions
// are prepended with this to uniquely identify the command 
// (SOCKS request or reply and associate it with a NorpSession
// TBD - or should the endpoint socket binding addrs/ports be used instead??

//
// ProtoPktNORP is the packet format that NORP uses to relay SOCKS
// requests and replies via the NORM application-defined command
// signaling mechanism.  The format of these commands is:
//
//      0                   1                   2                   3       
//      0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1      
//     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+    
//     |    msgType     |    reserved  |         sessionId             |    
//     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+     
//     |                          normNodeId                           |                
//     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+     
//     |          normSrcPort          |          normDstPort          |                
//     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+       
//     |                         timestamp_sec                         |                
//     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+      
//     |                         timestamp_usec                        |                
//     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+    
//     |                          Content ...                          |
//


class PortPool
{
    public:
        PortPool(UINT16 basePort);
    
        UINT16 GrabPort();
        void ReleasePort(UINT16 thePort);
      
    private:
        UINT16  base_port;
        bool    port_array[50];
};  // end class PortPool

class NorpMsg : public ProtoPkt
{
    public:
        NorpMsg(UINT32*       bufferPtr = NULL, 
                unsigned int  numBytes = 0,
                bool          initFromBuffer = true, 
                bool          freeOnDestruct = false);
        ~NorpMsg();

        bool InitFromBuffer(unsigned int    msgLength,
                            UINT32*         bufferPtr = NULL,
                            unsigned int    numBytes = 0,
                            bool            freeOnDestruct = false)
            {return ProtoPkt::InitFromBuffer(msgLength, bufferPtr, numBytes, freeOnDestruct);}
        
        enum Type
        {
            MSG_INVALID = 0,
            SOCKS_REQ,      // "content" is a SOCKS5 request
            REQ_ACK,        // request acknowledgment (no content)
            SOCKS_REP,      // "content" is a SOCKS5 request
            REP_ACK,        // reply acknowledgment (no content)
            ORIG_END,       // sent by originator to end session
            CORR_END,       // sent by correspondent to end session
            ACK_END         // end session acknowledgment (by either)
        }; 
            
        Type GetType() const
            {return (Type)((UINT8*)buffer_ptr)[OFFSET_TYPE];} 
        
        UINT16 GetSessionId() const
            {return (Type)ntohs(((UINT16*)buffer_ptr)[OFFSET_SESSION_ID]);}
        
        // note "GetUINT32()" does ntohl() conversion
        NormNodeId GetNodeId() const
            {return (NormNodeId)GetUINT32(buffer_ptr + OFFSET_NODE_ID);}
        
        UINT16 GetSourcePort() const
            {return (Type)ntohs(((UINT16*)buffer_ptr)[OFFSET_SRC_PORT]);}
        
        UINT16 GetDestinationPort() const
            {return (Type)ntohs(((UINT16*)buffer_ptr)[OFFSET_DST_PORT]);}
        
        void GetTimestamp(struct timeval& timestamp) const
        {
            timestamp.tv_sec = GetUINT32(buffer_ptr + OFFSET_SEC);
            timestamp.tv_usec = GetUINT32(buffer_ptr + OFFSET_USEC);
        }
        
        void GetTimestamp(ProtoTime& theTime) const
        {
            struct timeval& timestamp = theTime.AccessTimeVal();
            timestamp.tv_sec = GetUINT32(buffer_ptr + OFFSET_SEC);
            timestamp.tv_usec = GetUINT32(buffer_ptr + OFFSET_USEC);
        }
        
        const UINT32* GetContentPtr() const 
            {return ((const UINT32*)AccessContentPtr());}
        
        UINT32* AccessContentPtr() const
            {return (buffer_ptr + OFFSET_CONTENT);}
        
        unsigned int GetContentLength() const
            {return (GetLength() - (OFFSET_CONTENT*4));}
        
        // Packet building methods
        
        void Init()
        {
            SetType(MSG_INVALID);
            SetLength(0);
        }
        
        void SetType(Type msgType)
        {
            ((UINT8*)buffer_ptr)[OFFSET_TYPE] = (UINT8)msgType;
            pkt_length = (pkt_length < 1) ? 1 : pkt_length;
        }
        
        void SetSessionId(UINT16 sessionId)
        {
            ((UINT8*)buffer_ptr)[OFFSET_RESERVED] = 0;
            ((UINT16*)buffer_ptr)[OFFSET_SESSION_ID] = htons(sessionId);
            pkt_length = (pkt_length < 4) ? 4 : pkt_length;
        }
        
        void SetNodeId(NormNodeId nodeId)
        {
            SetUINT32(buffer_ptr + OFFSET_NODE_ID, nodeId);
            pkt_length = (pkt_length<8) ? 8 : pkt_length;
        }
        
        void SetSourcePort(UINT16 srcPort)
        {
            ((UINT16*)buffer_ptr)[OFFSET_SRC_PORT] = htons(srcPort);
            pkt_length = (pkt_length < 10) ? 10 : pkt_length;
        }
        
        void SetDestinationPort(UINT16 dstPort)
        {
            ((UINT16*)buffer_ptr)[OFFSET_DST_PORT] = htons(dstPort);
            pkt_length = (pkt_length < 12) ? 12 : pkt_length;
        }
        
        void SetTimestamp(struct timeval& timestamp)
        {
            SetUINT32(buffer_ptr+OFFSET_SEC, timestamp.tv_sec);
            SetUINT32(buffer_ptr+OFFSET_USEC, timestamp.tv_usec);
            pkt_length = (pkt_length < 20) ? 20 : pkt_length;
        }
        
        bool SetContent(const char* data, unsigned int numBytes)
        {
            if ((numBytes + (4*OFFSET_CONTENT)) > GetBufferLength()) return false;
            memcpy((char*)(buffer_ptr+OFFSET_CONTENT), data, numBytes);
            SetContentLength(numBytes);
            return true;
        }
        
        void SetContentLength(unsigned int numBytes)
            {pkt_length = (4*OFFSET_CONTENT) + numBytes;}
            
    private:
        enum
        {
            OFFSET_TYPE         = 0,                           // UINT8 offset     
            OFFSET_RESERVED     = OFFSET_TYPE + 1,             // UINT8 offset     
            OFFSET_SESSION_ID   = (OFFSET_RESERVED+1)/2,       // UINT16 offset    
            OFFSET_NODE_ID      = (2*(OFFSET_SESSION_ID+1))/4, // UINT32 offset    
            OFFSET_SRC_PORT     = (4*(OFFSET_NODE_ID+1))/2,    // UINT16 offset    
            OFFSET_DST_PORT     = OFFSET_SRC_PORT + 1,         // UINT16 offset 
            OFFSET_SEC          = (2*(OFFSET_DST_PORT+1))/4,   // UINT32 offset
            OFFSET_USEC         = OFFSET_SEC + 1,              // UINT32 offset    
            OFFSET_CONTENT      = OFFSET_USEC + 1              // UINT32 offset    
        };
            
};  // end class NorpMsg



// A NorpPreset is a non-SOCKS proxied session where a
// local TCP port is monitored and connections are proxied
// proxied to a preset remote destination
class NorpPreset : public ProtoTree::Item
{
    public:
        NorpPreset(Norp& norpController, UINT16 tcpPort, const ProtoAddress& dstAddr, const ProtoAddress& norpAddr);
        ~NorpPreset();
        
        const ProtoAddress& GetDstAddr() const
            {return dst_addr;}
        const ProtoAddress& GetNorpAddr() const
            {return (norp_addr.IsValid() ? norp_addr : dst_addr);}
        
        bool Listen();
        
        ProtoSocket& AccessServerSocket() 
            {return server_socket;}
        
    private:
       const char* GetKey() const
            {return ((const char*)&tcp_port);}    
        unsigned int GetKeysize() const
            {return (sizeof(UINT16) << 3);}     
            
            
        void OnSocketEvent(ProtoSocket& theSocket, ProtoSocket::Event theEvent);    
            
        Norp&           controller;
        UINT16          tcp_port;
        ProtoAddress    dst_addr;
        ProtoAddress    norp_addr;
        ProtoSocket     server_socket;
        
};  // end class NorpPreset

class NorpPresetList : public ProtoTreeTemplate<NorpPreset>
{
    public:
        NorpPreset* FindPreset(UINT16 tcpPort) const
            {return Find((char*)&tcpPort, sizeof(UINT16) << 3);}
};  // end class NorpPresetList

class NorpSession : public ProtoTree::Item
{
    public:
        NorpSession(Norp& norpController, UINT16 sessionId, NormNodeId originatorId = NORM_NODE_NONE);
        ~NorpSession();
        
        enum SocksState
        {
            SOCKS_VOID,          // when session gives self back to "controller" at Close
            SOCKS_IDLE,          // initial, inactive state (socks_client_socket is closed)
            SOCKS_GET_AUTH_REQ,  // first active state, accumulating auth request bytes from client
            SOCKS_PUT_AUTH_REP,  // next, writing auth reply bytes to client
            SOCKS_GET_REQUEST,   // next, accumulate SOCKS request bytes from client
            SOCKS_PUT_REQUEST,   // relaying request to remote NORP peer
            SOCKS_CONNECTING,    // attempt to satisfy request via remote connect, etc (this may need to be refined)
            SOCKS_GET_REPLY,     // awaiting reply from remote NORP peer 
            SOCKS_PUT_REPLY,     // next, writing SOCK reply bytes to client       
            SOCKS_CONNECTED,     // finally, reading/writing data from/to client
            SOCKS_SHUTDOWN       // session ended, in process of shutting down
        };
        
        void SetProxyAddress(const ProtoAddress& proxyAddr)
            {proxy_addr = proxyAddr;}
        
        void SetNormRateBounds(double rateMin, double rateMax);
        
        NormNodeId GetNodeId() const;
        
        UINT16 GetSessionId() const
            {return session_id.identifier;}
        
        NormNodeId GetOriginatorId() const
            {return session_id.originator;}
        
        bool IsRemoteSession() const
            {return (NORM_SESSION_INVALID != norm_session);}
        
        bool IsRemoteOriginator() const  // assumes "IsRemoteSession()"
            {return (NORM_NODE_NONE == GetOriginatorId());}
        
        bool AcceptClientConnection(ProtoSocket& serverSocket, bool normEnable = true);
        
        bool AcceptPresetClientConnection(NorpPreset& preset, bool normEnable);
        
        SocksState GetSocksState() const
            {return socks_state;}
        
        const ProtoAddress& GetClientAddress() const
            {return socks_client_socket.GetDestination();}
        
        void Shutdown();  // hard shutdown, including signaling remote via NORP messaging
        
        bool ShutdownComplete() const;  // Tests for inband shutdown completion
        
        void Close();  // hard local shutdown and cleanup including session deletion by controller
        
        void OnNormEvent(NormEvent& theEvent);
        
        bool OnClientRequest(const ProtoPktSOCKS::Request request, const ProtoAddress& srcAddr);
        
        bool MakeDirectConnect();  // convert remote session to direct connect
        
        // handles request relayed from remote NORP server
        bool OnRemoteRequest(const NorpMsg& theMsg, const ProtoAddress& senderAddr);  
        
        bool OnRemoteRequestAcknowledgment(const NorpMsg& theMsg, const ProtoAddress& senderAddr);
        
        bool OnRemoteReply(const NorpMsg& theMsg, const ProtoAddress& senderAddr);
        
        bool OnRemoteReplyAcknowledgment(const NorpMsg& theMsg);
        
        typedef struct Moniker
        {
            NormNodeId  originator;
            UINT16      identifier;  // TBD - make this UINT32?
        } Moniker; 
        
        static bool GetNormNodeAddress(NormNodeHandle nodeHandle, ProtoAddress& theAddr);
            
        static const double NORP_RTT_MIN;
        static const double NORP_RTT_MAX;  
        static const double NORP_RTT_DEFAULT;
        
    private:            
        void ActivateTimer(ProtoTimer& theTimer);
            
        const char* GetKey() const
            {return ((const char*)&session_id);}    
        unsigned int GetKeysize() const
            {return (sizeof(Moniker) << 3);}
            
        void OnSocksClientEvent(ProtoSocket&       theSocket, 
                                ProtoSocket::Event theEvent); 
        
        // Note our "norp_tx_socket" is "connected" so we get error message when "port unreachable"
        bool SendMessage(const NorpMsg& msg)
        {
            unsigned int numBytes = msg.GetLength();
            return norp_tx_socket.Send((const char*)msg.GetBuffer(), numBytes);
        }
        
        bool OnNorpMsgTimeout(ProtoTimer& theTimer);
        
        bool OnCloseTimeout(ProtoTimer& theTimer); 
        
        void OnUdpRelayEvent(ProtoSocket&       theSocket, 
                             ProtoSocket::Event theEvent); 
        
        // These methods are invoked as needed upon different events and SOCKS state transitions
        bool GetClientAuthRequest();
        bool PutClientAuthReply();
        bool GetClientRequest();
        bool ConnectToRemote(const ProtoAddress& srcAddr);
        bool BindRemote(UINT16 bindPort);
        bool OpenUdpRelay();
        bool PutClientReply();
        bool GetClientData();
        bool PutClientData();
        
        bool PutRemoteRequest(const ProtoPktSOCKS::Request& request, const ProtoAddress& remoteAddr);
        bool OriginatorStartNorm(const ProtoAddress& senderAddr, 
                                 NormNodeId          corrNormId, 
                                 UINT16              normSrcPort, 
                                 UINT16              normDstPort);

        bool PutRemoteReply();
                
        void OnSocksRemoteEvent(ProtoSocket&       theSocket, 
                                ProtoSocket::Event theEvent);
        // These methods are invoked as needed upon different events and SOCKS state transitions
        bool GetRemoteData();
        bool PutRemoteData();   
            
        enum {SOCKS_BUFFER_SIZE = 16384};
        enum {NORP_BUFFER_SIZE = 512};
        enum {NORM_BUFFER_SIZE = (8192*1024)};
        
        unsigned int WriteToNormStream(const char* buffer, unsigned int numBytes);
        void FlushNormStream(bool eom, NormFlushMode flushMode);
        unsigned int ComputeNormStreamBufferSegmentCount(unsigned int bufferBytes, UINT16 segmentSize, UINT16 blockSize);

        
        // Member variables 
        Norp&           controller;    
        bool            is_preset;            // special "preset" (non-SOCKS) proxy session
        ProtoTimer      close_timer;
        Moniker         session_id;
        ProtoAddress    proxy_addr;           // address to advertise for remote connections
        SocksState      socks_state;
        ProtoSocket     socks_client_socket;  // TCP connection from this server to SOCKS client
        ProtoSocket     socks_remote_socket;  // TCP connection to remote when "direct connect" is used
        ProtoSocket     udp_relay_socket;     // UDP relay socket (used for UDP_ASSOC requests)
        ProtoAddress    udp_client_addr;      // address of local UDP_ASSOC client
        
        UINT32          client_buffer[SOCKS_BUFFER_SIZE/sizeof(UINT32)]; // usually data received from client
        unsigned int    client_pending;                 // total bytes pending reception or transmission
        unsigned int    client_index;                   // current index
        UINT32          remote_buffer[SOCKS_BUFFER_SIZE/sizeof(UINT32)]; // usually for data received from remote
        unsigned int    remote_pending;                 // total bytes pending reception or transmission
        unsigned int    remote_index;                   // current index
        
        // NORM-related state
        bool                norm_enable;
        ProtoSocket         norp_tx_socket;  // used to send NORP signaling messages
        ProtoAddress        norp_remote_addr;
        ProtoTimer          norp_msg_timer;
        NorpMsg             norp_msg;  // current message being transmitted
        UINT32              norp_msg_buffer[NORP_BUFFER_SIZE/sizeof(UINT32)];
        double              norp_rtt_estimate;  // from REQUEST<->ACK or REPLY<->ACK exchange
        NormSessionHandle   norm_session;
        NormObjectHandle    norm_tx_stream;
        NormObjectHandle    norm_rx_stream;
        bool                norm_rx_pending;
        bool                norm_sender_heard;
        double              persist_interval;   // in seconds, to timeout NORM delivery persistence after local TCP disconnect
        ProtoTime           persist_start_time; // marks start of persistence timeout
        double              norm_rate_min;
        double              norm_rate_max;
        
        UINT16              norm_segment_size;
        unsigned int        norm_stream_buffer_max;
        unsigned int        norm_stream_buffer_count;
        unsigned int        norm_stream_bytes_remain;
        bool                norm_watermark_pending;
        
};  // end class NorpSession

class NorpSessionList : public ProtoTreeTemplate<NorpSession>
{
    public:
        NorpSession* FindSession(UINT16 sessionId, NormNodeId originatorId = NORM_NODE_NONE) const
        {
            NorpSession::Moniker id;
            memset(&id, 0, sizeof(NorpSession::Moniker));
            id.originator = originatorId;
            id.identifier = sessionId;
            return Find((char*)&id, sizeof(NorpSession::Moniker) << 3);
        }  // end NorpSessionList::FindSession()
};  // end class NorpSessionList



// The "Norp" class is an instance of a SOCKS server that manages
// SOCKS/NORM (or direct-connect) sessions
class Norp
{
    public:
        Norp(ProtoDispatcher& theDispatcher);
        ~Norp();
        
        enum
        {
            DEFAULT_SOCKS_PORT = 7000,  // SOCKS server TCP listen port used for NORP
            DEFAULT_NORP_PORT  = 7001,  // Where NORP listens for UDP signaling, relayed commands, etc
            DEFAULT_NORM_PORT  = 7002   // Port used for NORM data transfer
        };
            
        static const double DEFAULT_TX_RATE;
        static const double DEFAULT_PERSIST_INTERVAL;
        
        void SetSocksPort(UINT16 thePort)
            {socks_port = thePort;}
        
        void SetProxyAddress(const ProtoAddress& proxyAddr)
            {proxy_addr = proxyAddr;}
        
        const ProtoAddress& GetProxyAddress() const
            {return proxy_addr;}
        
        bool StartServer(bool normEnable);
        
        void StopServer();
        
        void SetLocalNorpPort(UINT16 thePort)
            {norp_local_port = thePort;}
        
        void SetRemoteNorpPort(UINT16 thePort)
            {norp_remote_port = thePort;}
        
        UINT16 GetNorpPort() const
            {return norp_remote_port;}
        
        // In future, this will reference a "routing"
        // table using the "destAddr"
        bool GetRemoteNorpAddress(const ProtoAddress& destAddr,
                                  ProtoAddress&       norpAddr) const
        {
            norpAddr = norp_remote_addr.IsValid() ? norp_remote_addr : destAddr;
            return true;
        }
        // This sets a remote "norp" peer address that is used instead of
        // connecting directly to destination endpoints 
        // TBD - add more routing options than this
        void SetRemoteNorpAddress(const ProtoAddress& remoteAddr)
            {norp_remote_addr = remoteAddr;}
        
        void SetInitialRtt(double seconds)
            {norp_rtt_init = seconds;}
        
        double GetInitialRtt() const
            {return norp_rtt_init;}
        
        // NORM protocol parameters
        void SetNormNodeId(NormNodeId nodeId)
            {norm_node_id = nodeId;}
        NormNodeId GetNormNodeId() const
            {return norm_node_id;}
        
        NormInstanceHandle GetNormInstance() const
            {return norm_instance;}
        
        void SetNormPort(UINT16 thePort)
            {norm_port = thePort;}
        UINT16 GetNormPort() const
            {return norm_port;}
        
        /*
        UINT16 GrabPort()
            {return port_pool.GrabPort();}
        void ReleasePort(UINT16 thePort)
            {port_pool.ReleasePort(thePort);}
        */
                
        enum NormCC
        {
            NORM_CC,
            NORM_CCE,
            NORM_CCL,
            NORM_FIXED
        };
        
        void SetNormCC(NormCC ccMode)
            {norm_cc_mode = ccMode;}
        NormCC GetNormCC() const
            {return norm_cc_mode;}
        
        // For fixed-rate operation
        void SetNormTxRate(double txRate)
            {norm_tx_rate = txRate;}
        double GetNormTxRate() const
            {return norm_tx_rate;}
        
        // Sets upper bound on _cumulative_ NormSession
        // transmit rates
        void SetNormTxLimit(double txLimit)
            {norm_tx_limit = txLimit;}
        
        double GetNormTxLimit() const
            {return norm_tx_limit;}
        
        void SetNormTrace(bool enable)
            {norm_trace = enable;}
        bool GetNormTrace() const
            {return norm_trace;}

        void SetNormSegmentSize(UINT16 numBytes)
            {norm_segment_size = numBytes;}        
        UINT16 GetNormSegmentSize() const
            {return norm_segment_size;}
        
        void SetNormBlockSize(UINT32 numSegments)
            {norm_block_size = numSegments;}
        UINT16 GetNormBlockSize() const
            {return norm_block_size;}
        
        void SetNormParityCount(UINT16 numSegments)
            {norm_parity_count = numSegments;}
        UINT16 GetNormParityCount() const
            {return norm_parity_count;}
        
        void SetNormParityAuto(UINT16 numSegments)
            {norm_parity_auto = numSegments;}
        UINT16 GetNormParityAuto() const
            {return norm_parity_auto;}
        
        ProtoSocket::Notifier& GetSocketNotifier() const
            {return static_cast<ProtoSocket::Notifier&>(dispatcher);}
        
        void ActivateTimer(ProtoTimer& theTimer)
            {dispatcher.ActivateTimer(theTimer);}
        
        bool SendMessage(const NorpMsg& msg, const ProtoAddress& dstAddr);
        
        void OnNorpSocketEvent(ProtoSocket&       theSocket, 
                               ProtoSocket::Event theEvent);
        
        void OnSessionClose(NorpSession& theSession);
        
        bool AddPreset(UINT16 tcpPort, const ProtoAddress& dstAddr, const ProtoAddress& norpAddr);
        bool AcceptPresetClientConnection(NorpPreset& preset);
    
    private:
        void AddSession(NorpSession& session);
        void RemoveSession(NorpSession& session);
            
        void OnSocksServerEvent(ProtoSocket&       theSocket, 
                                ProtoSocket::Event theEvent);
    
        static void NormEventCallback(ProtoDispatcher::Descriptor descriptor, 
                                      ProtoDispatcher::Event      theEvent, 
                                      const void*                 userData);
        
        void OnNormEvent();
        void OnRemoteRequest(const NorpMsg& theMsg, const ProtoAddress& senderAddr);
        void OnRemoteReply(const NorpMsg& theMsg, const ProtoAddress& senderAddr);
    
        // Member variables
        ProtoDispatcher&    dispatcher;             // TBD - make Norp multi-threaded w/ a pool of dispatchers
            
        ProtoSocket         socks_server_socket;    // (SOCKS clients connect to this)
        UINT16              socks_port;
        ProtoAddress        proxy_addr;
        UINT16              next_session_id;    // used to dole out unique session ids
        NorpSessionList     session_list;       // list of client and remote sessions
        unsigned int        session_count;
        NorpPresetList      preset_list;        // list of preset proxies w/ listening TCP socket
        
        ProtoSocket         norp_rx_socket;     // UDP socket for receiving NORP signaling
        UINT16              norp_local_port;    // For loopback debugging, we allow "local" server port to be different than "remote"
        UINT16              norp_remote_port;
        ProtoAddress        norp_remote_addr;   // if unspecified, we proxy directly to connection destination address
        double              norp_rtt_init;      // used as initial roundtrip estimate for NORP signaling
        
        bool                norm_enable;
        NormInstanceHandle  norm_instance;
        NormNodeId          norm_node_id;
        
        // NORM parameters (including some for debugging
        UINT16              norm_port;
        NormCC              norm_cc_mode;
        double              norm_tx_rate;      // in bits/sec, only applicable for cc_mode == NORM_FIXED
        double              norm_tx_limit;     // in bits/sec (_cumulative_ rate limit option)
        char                iface_name[64];    // Network interface
        UINT16              norm_segment_size; // payload bytes per NORM_DATA message
        UINT16              norm_block_size;   // number of user data segments per FEC coding block
        UINT16              norm_parity_count; // number of _computed_ parity segments per FEC coding block
        UINT16              norm_parity_auto;  // number of proactive (automatically sent) parity segments per block
        
        bool                norm_trace;
        
        //PortPool            port_pool;
        
};  // end class Norp

#endif // _NORP
