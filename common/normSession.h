#ifndef _NORM_SESSION
#define _NORM_SESSION

#include "normMessage.h"
#include "normObject.h"
#include "normNode.h"
#include "normEncoder.h"

#include "protocolTimer.h"
#include "udpSocket.h"


class NormController
{
    public:
        enum Event
        {
            TX_QUEUE_EMPTY,
            RX_OBJECT_NEW,
            RX_OBJECT_INFO,
            RX_OBJECT_UPDATE,
            RX_OBJECT_COMPLETE,
        };
                  
        virtual void Notify(NormController::Event event,
                            class NormSessionMgr* sessionMgr,
                            class NormSession*    session,
                            class NormServerNode* server,
                            class NormObject*     object) = 0;
                    
};  // end class NormController

class NormSessionMgr
{
    friend class NormSession;
    public:
        NormSessionMgr();
        ~NormSessionMgr();
        void Init(ProtocolTimerInstallFunc* timerInstaller, 
                  const void*               timerInstallData,
                  UdpSocketInstallFunc*     socketInstaller,
                  void*                     socketInstallData,
                  NormController*           theController)
        {
            timer_mgr.SetInstaller(timerInstaller, timerInstallData);
            socket_installer = socketInstaller;
            socket_install_data = socketInstallData;   
            controller = theController;
        }
        void Destroy();
        
        class NormSession* NewSession(const char*   sessionAddress,
                                      UINT16        sessionPort,
                                      NormNodeId    localNodeId = 
                                                    NORM_NODE_ANY);
        void DeleteSession(class NormSession* theSession);
        
        
        UdpSocketInstallFunc* SocketInstaller() {return socket_installer;}
        const void* SocketInstallData() {return socket_install_data;}
        
        void Notify(NormController::Event event,
                    class NormSession*    session,
                    class NormServerNode* server,
                    class NormObject*     object)
        {
            if (controller)
                controller->Notify(event, this, session, server, object);   
        }
               
        void InstallTimer(ProtocolTimer* timer) {timer_mgr.InstallTimer(timer);}
    
        private:   
        ProtocolTimerMgr        timer_mgr;  
        UdpSocketInstallFunc*   socket_installer;
        const void*             socket_install_data;
        NormController*         controller;
        
        class NormSession*      top_session;  // top of NormSession list
              
};  // end class NormSessionMgr


class NormSession
{
    friend class NormSessionMgr;
    
    public:
        enum {DEFAULT_MESSAGE_POOL_DEPTH = 16};
        static const double DEFAULT_TRANSMIT_RATE;  // in bytes per second
        static const double DEFAULT_PROBE_MIN;
        static const double DEFAULT_PROBE_MAX;
        static const double DEFAULT_GRTT_ESTIMATE;
        static const double DEFAULT_GRTT_MAX;
        static const unsigned int DEFAULT_GRTT_DECREASE_DELAY;
        static const double DEFAULT_BACKOFF_FACTOR;  // times GRTT = backoff max
        static const double DEFAULT_GSIZE_ESTIMATE;
               
        // General methods
        const NormNodeId& LocalNodeId() {return local_node_id;}
        bool Open();
        void Close();
        bool IsOpen() {return (rx_socket.IsOpen() || tx_socket.IsOpen());}
        const NetworkAddress& Address() {return address;}
        void SetAddress(const NetworkAddress& addr) {address = addr;}
        
        
        // Session parameters
        double TxRate() {return (tx_rate * 8.0);}
        void SetTxRate(double txRate) {tx_rate = txRate / 8.0;}
        double BackoffFactor() {return backoff_factor;}
        void SetBackoffFactor(double value) {backoff_factor = value;}
        void SetUnicastNacks(bool state) {unicast_nacks = state;}
        bool UnicastNacks() {return unicast_nacks;}
        void SetLoopback(bool state)
        {
            rx_socket.SetLoopback(state);
            tx_socket.SetLoopback(state);
        }
        
        void Notify(NormController::Event event,
                    class NormServerNode* server,
                    class NormObject*     object)
        {
            notify_pending = true;
            session_mgr.Notify(event, this, server, object);  
            notify_pending = false;
        }
        
        NormMessage* GetMessageFromPool() {return message_pool.RemoveHead();}
        void ReturnMessageToPool(NormMessage* msg) {message_pool.Append(msg);}
        void QueueMessage(NormMessage* msg);
        
        // Server methods
        bool StartServer(unsigned long bufferSpace,
                         UINT16        segmentSize,
                         UINT16        numData,
                         UINT16        numParity);
        void StopServer();
        NormStreamObject* QueueTxStream(UINT32      bufferSize, 
                                        const char* infoPtr = NULL, 
                                        UINT16      infoLen = 0);
        NormFileObject* QueueTxFile(const char* path,
                                    const char* infoPtr = NULL,
                                    UINT16      infoLen = 0);
                
        bool IsServer() {return is_server;}
        UINT16 ServerSegmentSize() {return segment_size;}
        UINT16 ServerBlockSize() {return ndata;}
        UINT16 ServerNumParity() {return nparity;}
        UINT16 ServerAutoParity() {return auto_parity;}
        void ServerSetAutoParity(UINT16 autoParity)
            {ASSERT(autoParity <= nparity); auto_parity = autoParity;}
        
        double ServerGroupSize() {return gsize_measured;}
        void ServerSetGroupSize(double gsize)
        {
            gsize_measured = gsize;
            gsize_quantized = NormQuantizeGroupSize(gsize);   
            gsize_advertised = NormUnquantizeGroupSize(gsize_quantized);
        }
        
        void ServerEncode(const char* segment, char** parityVectorList)
            {encoder.Encode(segment, parityVectorList);}
        
        
        NormBlock* ServerGetFreeBlock(NormObjectId objectId, NormBlockId blockId);
        void ServerPutFreeBlock(NormBlock* block)
        {
            block->EmptyToPool(segment_pool);
            block_pool.Put(block);
        }
        char* ServerGetFreeSegment(NormObjectId objectId, NormBlockId blockId);
        void ServerPutFreeSegment(char* segment) {segment_pool.Put(segment);}
        void TouchServer() 
        {
            posted_tx_queue_empty = false;
            if (!notify_pending) Serve();
        }
        
        // Client methods
        bool StartClient(unsigned long bufferSpace);
        void StopClient();
        bool IsClient() {return is_client;}
        unsigned long RemoteServerBufferSize() 
            {return remote_server_buffer_size;}
        void InstallTimer(ProtocolTimer* timer) 
            {session_mgr.InstallTimer(timer);}
        
        // Debug settings
        void SetTrace(bool state) {trace = state;}
        void SetTxLoss(double percent) {tx_loss_rate = percent;}
        void SetRxLoss(double percent) {rx_loss_rate = percent;}

#ifdef SIMULATE   
        // Simulation specific methods
        NormSimObject* QueueTxSim(unsigned long objectSize);
        bool SimSocketRecvHandler(char* buffer, unsigned short buflen,
                                  const NetworkAddress& src, bool unicast);
#endif // SIMULATE
                    
    private:
        // Only NormSessionMgr can create/delete sessions
        NormSession(NormSessionMgr& sessionMgr, NormNodeId localNodeId);
        ~NormSession();
        
        void Serve();
        bool QueueTxObject(NormObject* obj, bool touchServer = true);
        void DeleteTxObject(NormObject* obj);
        
        bool OnTxTimeout();
        bool OnRepairTimeout();
        bool OnCommandTimeout();
        bool OnProbeTimeout();
        bool OnReportTimeout();
        
        bool TxSocketRecvHandler(UdpSocket* theSocket);
        bool RxSocketRecvHandler(UdpSocket* theSocket);
        
        void HandleReceiveMessage(NormMessage& msg, bool wasUnicast);
        
        void ServerHandleNackMessage(NormNackMsg& nack);
        void ServerUpdateGrttEstimate(const struct timeval& grttResponse);
        bool ServerQueueSquelch(NormObjectId objectId);
        void ServerQueueFlush();
        void ServerUpdateGroupSize();
        
        void ClientHandleObjectMessage(NormMessage& msg);
        void ClientHandleCommand(NormMessage& msg);
        void ClientHandleNackMessage(NormNackMsg& nack);
        
        NormSessionMgr&     session_mgr;
        bool                notify_pending;
        ProtocolTimer       tx_timer;
        UdpSocket           tx_socket;
        UdpSocket           rx_socket;
        NormMessageQueue    message_queue;
        NormMessageQueue    message_pool;
        ProtocolTimer       report_timer;
        UINT16              tx_sequence;
        
        // General session parameters
        NormNodeId          local_node_id;
        NetworkAddress      address;  // session destination address
        UINT8               ttl;      // session multicast ttl       
        double              tx_rate;  // bytes per second
        double              backoff_factor;
        
        // Server parameters and state
        bool                is_server;
        UINT16              segment_size;
        UINT16              ndata;
        UINT16              nparity;
        UINT16              auto_parity;
        
        NormObjectTable     tx_table;
        NormSlidingMask     tx_pending_mask;
        NormSlidingMask     tx_repair_mask;
        ProtocolTimer       repair_timer;
        NormBlockPool       block_pool;
        NormSegmentPool     segment_pool;
        NormEncoder         encoder;
        
        NormObjectId        next_tx_object_id;
        unsigned int        tx_cache_count_min;
        unsigned int        tx_cache_count_max;
        NormObjectSize      tx_cache_size_max;
        ProtocolTimer       flush_timer;
        int                 flush_count;
        bool                posted_tx_queue_empty;
        
        ProtocolTimer       probe_timer;  // GRTT/congestion control probes
        double              probe_interval;
        double              probe_interval_min;
        double              probe_interval_max;
        
        double              grtt_max;
        unsigned int        grtt_decrease_delay_count;
        bool                grtt_response;
        double              grtt_current_peak;
        double              grtt_measured;
        double              grtt_advertised;
        UINT8               grtt_quantized;
        double              gsize_measured;
        double              gsize_advertised;
        UINT8               gsize_quantized;
        unsigned int        gsize_nack_count;
        double              gsize_nack_ave;
        double              gsize_correction_factor;
        double              gsize_nack_delta;
        
        // Client parameters
        bool                is_client;
        NormNodeTree        server_tree;
        unsigned long       remote_server_buffer_size;
        bool                unicast_nacks;
        
        // Protocol test/debug parameters
        bool                trace;
        double              tx_loss_rate;  // for correlated loss
        double              rx_loss_rate;  // for uncorrelated loss

        // Linkers
        NormSession*        next;
};  // end class NormSession


#endif  // _NORM_SESSION
