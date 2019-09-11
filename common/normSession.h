#ifndef _NORM_SESSION
#define _NORM_SESSION

#include "normMessage.h"
#include "normObject.h"
#include "normNode.h"
#include "normEncoder.h"

#include "protokit.h"

class NormController
{
    public:
        virtual ~NormController() {}
        enum Event
        {
            EVENT_INVALID = 0,
            TX_QUEUE_VACANCY,
            TX_QUEUE_EMPTY,
            TX_FLUSH_COMPLETED,
            TX_OBJECT_SENT,
            TX_OBJECT_PURGED,
            LOCAL_SERVER_CLOSED,
            REMOTE_SERVER_NEW,
            REMOTE_SERVER_ACTIVE,
            REMOTE_SERVER_INACTIVE,
            REMOTE_SERVER_PURGED,
            RX_OBJECT_NEW,
            RX_OBJECT_INFO,
            RX_OBJECT_UPDATED,
            RX_OBJECT_COMPLETED,
            RX_OBJECT_ABORTED
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
        NormSessionMgr(ProtoTimerMgr&            timerMgr,
                       ProtoSocket::Notifier&    socketNotifier);
        ~NormSessionMgr();
        void SetController(NormController* theController)
            {controller = theController;}
        void Destroy();
        
        class NormSession* NewSession(const char*   sessionAddress,
                                      UINT16        sessionPort,
                                      NormNodeId    localNodeId = NORM_NODE_ANY);
        void DeleteSession(class NormSession* theSession);
        
        void Notify(NormController::Event event,
                    class NormSession*    session,
                    class NormServerNode* server,
                    class NormObject*     object)
        {
            if (controller)
                controller->Notify(event, this, session, server, object);   
        }
               
        void ActivateTimer(ProtoTimer& timer) {timer_mgr.ActivateTimer(timer);}
        ProtoTimerMgr& GetTimerMgr() const {return timer_mgr;}        
        ProtoSocket::Notifier& GetSocketNotifier() const {return socket_notifier;}
    
        NormController* GetController() const {return controller;}
    private:   
        ProtoTimerMgr&          timer_mgr;  
        ProtoSocket::Notifier&  socket_notifier;
        NormController*         controller;
        
        class NormSession*      top_session;  // top of NormSession list
              
};  // end class NormSessionMgr


class NormSession
{
    friend class NormSessionMgr;
    
    public:
        enum {DEFAULT_MESSAGE_POOL_DEPTH = 16};
        static const UINT8 DEFAULT_TTL;  
        static const double DEFAULT_TRANSMIT_RATE;  // in bytes per second
        static const double DEFAULT_GRTT_INTERVAL_MIN;
        static const double DEFAULT_GRTT_INTERVAL_MAX;
        static const double DEFAULT_GRTT_ESTIMATE;
        static const double DEFAULT_GRTT_MAX;
        static const unsigned int DEFAULT_GRTT_DECREASE_DELAY;
        static const double DEFAULT_BACKOFF_FACTOR;  // times GRTT = backoff max
        static const double DEFAULT_GSIZE_ESTIMATE;
        static const UINT16 DEFAULT_NDATA;
        static const UINT16 DEFAULT_NPARITY;
        enum ProbingMode {PROBE_NONE, PROBE_PASSIVE, PROBE_ACTIVE};
               
        // General methods
        const NormNodeId& LocalNodeId() const {return local_node_id;}
        bool Open(const char* interfaceName = NULL);
        void Close();
        bool IsOpen() {return (rx_socket.IsOpen() || tx_socket->IsOpen());}
        const ProtoAddress& Address() {return address;}
        void SetAddress(const ProtoAddress& addr) {address = addr;}
        bool SetMulticastInterface(const char* interfaceName);
        bool SetTTL(UINT8 theTTL) 
        {
            bool result = tx_socket->IsOpen() ? tx_socket->SetTTL(theTTL) : true;
            ttl = result ? theTTL : ttl;
            return result; 
        }
        bool SetLoopback(bool state) 
        {
            bool result = tx_socket->IsOpen() ? tx_socket->SetLoopback(state) : true;
            loopback = result ? state : loopback;
            return result; 
        }
        void SetTxPort(UINT16 txPort) {tx_port = txPort;}
        void SetRxPortReuse(bool state) {rx_port_reuse = state;}
        static double CalculateRate(double size, double rtt, double loss);
        
        NormSessionMgr& GetSessionMgr() {return session_mgr;}
        
        bool SetTxSocketBuffer(unsigned int bufferSize)
            {return tx_socket->SetTxBufferSize(bufferSize);}
        bool SetRxSocketBuffer(unsigned int bufferSize)
            {return rx_socket.SetRxBufferSize(bufferSize);}
        
        // Session parameters
        double TxRate() {return (tx_rate * 8.0);}
        // (TBD) watch timer scheduling and min/max bounds
        void SetTxRate(double txRate)
        {
            txRate /= 8.0;  // convert to bytes/sec
            SetTxRateInternal(txRate);   
        }
        double BackoffFactor() {return backoff_factor;}
        void SetBackoffFactor(double value) {backoff_factor = value;}
        bool CongestionControl() {return cc_enable;}
        void SetCongestionControl(bool state) 
        {
            cc_enable = state;
        }
        // GRTT measurement management
        void SetGrttProbingMode(ProbingMode  probingMode);
        void SetGrttProbingInterval(double intervalMin, double intervalMax);
        void SetGrttMax(double grttMax) {grtt_max = grttMax;}
        
        void SetTxRateBounds(double rateMin, double rateMax);
        void SetTxCacheBounds(NormObjectSize sizeMax,
                              unsigned long  countMin,
                              unsigned long  countMax);
        void Notify(NormController::Event event,
                    class NormServerNode* server,
                    class NormObject*     object)
        {
            notify_pending = true;
            session_mgr.Notify(event, this, server, object);  
            notify_pending = false;
        }
        
        NormMsg* GetMessageFromPool() {return message_pool.RemoveHead();}
        void ReturnMessageToPool(NormMsg* msg) {message_pool.Append(msg);}
        void QueueMessage(NormMsg* msg);
        bool SendMessage(NormMsg& msg);
        void ActivateTimer(ProtoTimer& timer) {session_mgr.ActivateTimer(timer);}
        
        void SetUserData(const void* userData) {user_data = userData;}
        const void* GetUserData() {return user_data;}
        
        // Server methods
        void ServerSetBaseObjectId(NormObjectId baseId)
        {
            next_tx_object_id = IsServer() ? next_tx_object_id : baseId;   
            //instance_id = IsServer() ? instance_id : (UINT16)baseId;
        }
        bool IsServer() {return is_server;}
        bool StartServer(UINT16         instanceId,
                         UINT32         bufferSpace,
                         UINT16         segmentSize,
                         UINT16         numData,
                         UINT16         numParity,
                         const char*    interfaceName = NULL);
        void StopServer();
        NormStreamObject* QueueTxStream(UINT32      bufferSize, 
                                        bool        doubleBuffer = false,
                                        const char* infoPtr = NULL, 
                                        UINT16      infoLen = 0);
        NormFileObject* QueueTxFile(const char* path,
                                    const char* infoPtr = NULL,
                                    UINT16      infoLen = 0);
        NormDataObject* QueueTxData(const char* dataPtr,
                                    UINT32      dataLen,
                                    const char* infoPtr = NULL,
                                    UINT16      infoLen = 0);
        void DeleteTxObject(NormObject* obj); 
        
        // postive ack mgmnt
        void ServerSetWatermark(NormObjectId  objectId,
                                NormBlockId   blockId,
                                NormSegmentId segmentId);
        bool ServerAddAckingNode(NormNodeId nodeId);
        void ServerRemoveAckingNode(NormNodeId nodeId);
        
        
        UINT16 ServerSegmentSize() const {return segment_size;}
        UINT16 ServerBlockSize() const {return ndata;}
        UINT16 ServerNumParity() const {return nparity;}
        UINT16 ServerAutoParity() const {return auto_parity;}
        void ServerSetAutoParity(UINT16 autoParity)
            {ASSERT(autoParity <= nparity); auto_parity = autoParity;}
        UINT16 ServerExtraParity() const {return extra_parity;}
        void ServerSetExtraParity(UINT16 extraParity)
            {extra_parity = extraParity;}
        
        bool ServerGetFirstPending(NormObjectId& objectId)
        {
            UINT32 index;
            bool result = tx_pending_mask.GetFirstSet(index);
            objectId = (UINT16)index;
            return result;   
        }
        bool ServerGetFirstRepairPending(NormObjectId& objectId)
        {
            UINT32 index;
            bool result = tx_repair_mask.GetFirstSet(index);
            objectId = (UINT16)index;
            return result;   
        }
        
        double ServerGrtt() {return grtt_advertised;}
        void ServerSetGrtt(double grttValue)
        {
            if (IsServer())
            {
                double grttMin = 2.0 * ((double)(44+segment_size))/tx_rate;
                grttValue = (grttValue < grttMin) ? grttMin : grttValue;
            }
            grtt_quantized = NormQuantizeRtt(grttValue);
            grtt_measured = grtt_advertised = NormUnquantizeRtt(grtt_quantized);      
        }
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
        
        
        void PromptServer() {QueueMessage(NULL);}
        
        void TouchServer() 
        {
            posted_tx_queue_empty = false;
            PromptServer();
            //if (!notify_pending) Serve();
        }
        
        // Client methods
        bool StartClient(unsigned long bufferSpace, 
                         const char*   interfaceName = NULL);
        void StopClient();
        bool IsClient() const {return is_client;}
        unsigned long RemoteServerBufferSize() const
            {return remote_server_buffer_size;}
        void ClientSetUnicastNacks(bool state) {unicast_nacks = state;}
        bool ClientGetUnicastNacks() const {return unicast_nacks;}
        void ClientSetSilent(bool state) {client_silent = state;}
        bool ClientIsSilent() const {return client_silent;}
        
        NormObject::NackingMode ClientGetDefaultNackingMode() const
            {return default_nacking_mode;}
        void ClientSetDefaultNackingMode(NormObject::NackingMode nackingMode)
            {default_nacking_mode = nackingMode;}
        
        NormServerNode::RepairBoundary ClientGetDefaultRepairBoundary() const
            {return default_repair_boundary;}
        void ClientSetDefaultRepairBoundary(NormServerNode::RepairBoundary repairBoundary)
            {default_repair_boundary = repairBoundary;}
        
        // Debug settings
        void SetTrace(bool state) {trace = state;}
        void SetTxLoss(double percent) {tx_loss_rate = percent;}
        void SetRxLoss(double percent) {rx_loss_rate = percent;}

#ifdef SIMULATE   
        // Simulation specific methods
        NormSimObject* QueueTxSim(unsigned long objectSize);
        bool SimSocketRecvHandler(char* buffer, unsigned short buflen,
                                  const ProtoAddress& src, bool unicast);
#endif // SIMULATE
                    
    private:
        // Only NormSessionMgr can create/delete sessions
        NormSession(NormSessionMgr& sessionMgr, NormNodeId localNodeId);
        ~NormSession();
        
        void Serve();
        bool QueueTxObject(NormObject* obj);
        
        
        bool OnTxTimeout(ProtoTimer& theTimer);
        bool OnRepairTimeout(ProtoTimer& theTimer);
        bool OnFlushTimeout(ProtoTimer& theTimer);
        bool OnProbeTimeout(ProtoTimer& theTimer);
        bool OnReportTimeout(ProtoTimer& theTimer);
        
        void TxSocketRecvHandler(ProtoSocket& theSocket, ProtoSocket::Event theEvent);
        void RxSocketRecvHandler(ProtoSocket& theSocket, ProtoSocket::Event theEve);        
        void HandleReceiveMessage(NormMsg& msg, bool wasUnicast);
        
        // Server message handling routines
        void ServerHandleNackMessage(const struct timeval& currentTime, 
                                     NormNackMsg&          nack);
        void ServerHandleAckMessage(const struct timeval& currentTime, 
                                    const NormAckMsg&     ack,
                                    bool                  wasUnicast);
        void ServerUpdateGrttEstimate(double clientRtt);
        double CalculateRtt(const struct timeval& currentTime,
                            const struct timeval& grttResponse);
        void ServerHandleCCFeedback(struct timeval currentTime,
                                    NormNodeId     nodeId,              
                                    UINT8          ccFlags,             
                                    double         ccRtt,               
                                    double         ccLoss,              
                                    double         ccRate,              
                                    UINT16         ccSequence);         
        void AdjustRate(bool onResponse);
        void SetTxRateInternal(double txRate);  // here, txRate is bytes/sec
        bool ServerQueueSquelch(NormObjectId objectId);
        void ServerQueueFlush();
        bool ServerQueueWatermarkFlush();
        bool ServerBuildRepairAdv(NormCmdRepairAdvMsg& cmd);
        void ServerUpdateGroupSize();
        
        // Client message handling routines
        void ClientHandleObjectMessage(const struct timeval& currentTime, 
                                       const NormObjectMsg&  msg);
        void ClientHandleCommand(const struct timeval& currentTime,
                                 const NormCmdMsg&     msg);
        void ClientHandleNackMessage(const NormNackMsg& nack);
        void ClientHandleAckMessage(const NormAckMsg& ack);
        
        NormSessionMgr&                 session_mgr;
        bool                            notify_pending;
        ProtoTimer                      tx_timer;
        UINT16                          tx_port;
        ProtoSocket*                    tx_socket;
        ProtoSocket                     tx_socket_actual;
        ProtoSocket                     rx_socket;
        NormMessageQueue                message_queue;
        NormMessageQueue                message_pool;
        ProtoTimer                      report_timer;
        UINT16                          tx_sequence;
        
        // General session parameters
        NormNodeId                      local_node_id;
        ProtoAddress                    address;  // session destination address & port
        UINT8                           ttl;      // session multicast ttl   
        bool                            loopback; // to receive own traffic
        bool                            rx_port_reuse; // enable port reuse
        char                            interface_name[32];    
        double                          tx_rate;  // bytes per second
        double                          tx_rate_min;
        double                          tx_rate_max;
        double                          backoff_factor;
        
        // Server parameters and state
        bool                            is_server;
        UINT16                          instance_id;
        UINT16                          segment_size;
        UINT16                          ndata;
        UINT16                          nparity;
        UINT16                          auto_parity;
        UINT16                          extra_parity;
        
        NormObjectTable                 tx_table;
        ProtoSlidingMask                tx_pending_mask;
        ProtoSlidingMask                tx_repair_mask;
        ProtoTimer                      repair_timer;
        NormBlockPool                   block_pool;
        NormSegmentPool                 segment_pool;
        NormEncoder                     encoder;
        
        NormObjectId                    next_tx_object_id;
        unsigned int                    tx_cache_count_min;
        unsigned int                    tx_cache_count_max;
        NormObjectSize                  tx_cache_size_max;
        ProtoTimer                      flush_timer;
        int                             flush_count;
        bool                            posted_tx_queue_empty;
        
        // For postive acknowledgement collection
        NormNodeTree                    acking_node_tree;
        unsigned int                    acking_node_count;
        bool                            watermark_pending;
        NormObjectId                    watermark_object_id;
        NormBlockId                     watermark_block_id;
        NormSegmentId                   watermark_segment_id;
        unsigned int                    acks_collected;
        
        // for unicast nack/cc feedback suppression
        bool                            advertise_repairs;
        bool                            suppress_nonconfirmed;
        double                          suppress_rate;
        double                          suppress_rtt;
        
        ProtoTimer                      probe_timer;  // GRTT/congestion control probes
        bool                            probe_proactive;
        bool                            probe_pending; // true while CMD(CC) enqueued
        bool                            probe_reset;   
        struct timeval                  probe_time_last;
        
        double                          grtt_interval;     // current GRTT update interval
        double                          grtt_interval_min; // minimum GRTT update interval
        double                          grtt_interval_max; // maximum GRTT update interval
        
        double                          grtt_max;
        unsigned int                    grtt_decrease_delay_count;
        bool                            grtt_response;
        double                          grtt_current_peak;
        double                          grtt_measured;
        double                          grtt_age;
        double                          grtt_advertised;
        UINT8                           grtt_quantized;
        double                          gsize_measured;
        double                          gsize_advertised;
        UINT8                           gsize_quantized;
        
        // Server congestion control parameters
        bool                            cc_enable;
        UINT8                           cc_sequence;
        NormNodeList                    cc_node_list;
        bool                            cc_slow_start;
        double                          sent_rate;         // measured sent rate
        struct timeval                  prev_update_time;  // for sent_rate measurement
        unsigned long                   sent_accumulator;  // for sent_rate measurement
        double                          nominal_packet_size;
        
        // Client parameters
        bool                            is_client;
        NormNodeTree                    server_tree;
        unsigned long                   remote_server_buffer_size;
        bool                            unicast_nacks;
        bool                            client_silent;
        NormServerNode::RepairBoundary  default_repair_boundary;
        NormObject::NackingMode         default_nacking_mode;    
        
        // Protocol test/debug parameters
        bool                            trace;
        double                          tx_loss_rate;  // for correlated loss
        double                          rx_loss_rate;  // for uncorrelated loss

        const void*                     user_data;
        
        // Linkers
        NormSession*                    next;
};  // end class NormSession


#endif  // _NORM_SESSION
