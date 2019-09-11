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
        NormSessionMgr(ProtoTimerMgr&            timerMgr,
                       ProtoSocket::Notifier&    socketNotifier);
        ~NormSessionMgr();
        void SetController(NormController* theController)
            {controller = theController;}
        void Destroy();
        
        class NormSession* NewSession(const char*   sessionAddress,
                                      UINT16        sessionPort,
                                      NormNodeId    localNodeId = 
                                                    NORM_NODE_ANY);
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
        ProtoTimerMgr& GetTimerMgr() {return timer_mgr;}        
        ProtoSocket::Notifier& GetSocketNotifier() {return socket_notifier;}
    
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
               
        // General methods
        const NormNodeId& LocalNodeId() {return local_node_id;}
        bool Open();
        void Close();
        bool IsOpen() {return (rx_socket.IsOpen() || tx_socket.IsOpen());}
        const ProtoAddress& Address() {return address;}
        void SetAddress(const ProtoAddress& addr) {address = addr;}
        static double CalculateRate(double size, double rtt, double loss);
        
        
        // Session parameters
        double TxRate() {return (tx_rate * 8.0);}
        // (TBD) watch timer scheduling and min/max bounds
        void SetTxRate(double txRate) {tx_rate = txRate / 8.0;}
        double BackoffFactor() {return backoff_factor;}
        void SetBackoffFactor(double value) {backoff_factor = value;}
        void SetLoopback(bool state)
        {
            rx_socket.SetLoopback(state);
            tx_socket.SetLoopback(state);
        }
        bool CongestionControl() {return cc_enable;}
        void SetCongestionControl(bool state) {cc_enable = state;}
        
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
        
        // Server methods
        void ServerSetBaseObjectId(NormObjectId baseId)
        {
            next_tx_object_id = IsServer() ? next_tx_object_id : baseId;   
            session_id = IsServer() ? session_id : (UINT16)baseId;
        }
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
        UINT16 ServerExtraParity() {return extra_parity;}
        void ServerSetExtraParity(UINT16 extraParity)
            {extra_parity = extraParity;}
        
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
        
        
        void PromptServer()
        {
            if (!tx_timer.IsActive())
            {
                tx_timer.SetInterval(0.0);
                ActivateTimer(tx_timer);    
            }
        }
        
        
        void TouchServer() 
        {
            posted_tx_queue_empty = false;
            PromptServer();
            //if (!notify_pending) Serve();
        }
        
        // Client methods
        bool StartClient(unsigned long bufferSpace);
        void StopClient();
        bool IsClient() {return is_client;}
        unsigned long RemoteServerBufferSize() 
            {return remote_server_buffer_size;}
        void SetUnicastNacks(bool state) {unicast_nacks = state;}
        bool UnicastNacks() {return unicast_nacks;}
        void ClientSetSilent(bool state) {client_silent = state;}
        bool ClientIsSilent() {return client_silent;}
        
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
        bool QueueTxObject(NormObject* obj, bool touchServer = true);
        void DeleteTxObject(NormObject* obj);
        
        bool OnTxTimeout(ProtoTimer& theTimer);
        bool OnRepairTimeout(ProtoTimer& theTimer);
        bool OnFlushTimeout(ProtoTimer& theTimer);
        bool OnWatermarkTimeout(ProtoTimer& theTimer);
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
        void ServerHandleCCFeedback(NormNodeId nodeId,
                                    UINT8      ccFlags,
                                    double     ccRtt,
                                    double     ccLoss,
                                    double     ccRate,
                                    UINT16      ccSequence);
        void AdjustRate(bool onResponse);
        bool ServerQueueSquelch(NormObjectId objectId);
        void ServerQueueFlush();
        bool ServerBuildRepairAdv(NormCmdRepairAdvMsg& cmd);
        void ServerUpdateGroupSize();
        
        // Client message handling routines
        void ClientHandleObjectMessage(const struct timeval& currentTime, 
                                       const NormObjectMsg&  msg, 
                                       NormServerNode*       theServer);
        void ClientHandleCommand(const struct timeval& currentTime,
                                 const NormCmdMsg&     msg, 
                                 NormServerNode*       theServer);
        void ClientHandleNackMessage(const NormNackMsg& nack);
        void ClientHandleAckMessage(const NormAckMsg& ack);
        
        NormSessionMgr&     session_mgr;
        bool                notify_pending;
        ProtoTimer          tx_timer;
        ProtoSocket         tx_socket;
        ProtoSocket         rx_socket;
        NormMessageQueue    message_queue;
        NormMessageQueue    message_pool;
        ProtoTimer          report_timer;
        UINT16              tx_sequence;
        
        // General session parameters
        NormNodeId          local_node_id;
        ProtoAddress        address;  // session destination address
        UINT8               ttl;      // session multicast ttl       
        double              tx_rate;  // bytes per second
        double              backoff_factor;
        
        // Server parameters and state
        bool                is_server;
        UINT16              session_id;
        UINT16              segment_size;
        UINT16              ndata;
        UINT16              nparity;
        UINT16              auto_parity;
        UINT16              extra_parity;
        
        NormObjectTable     tx_table;
        NormSlidingMask     tx_pending_mask;
        NormSlidingMask     tx_repair_mask;
        ProtoTimer          repair_timer;
        NormBlockPool       block_pool;
        NormSegmentPool     segment_pool;
        NormEncoder         encoder;
        
        NormObjectId        next_tx_object_id;
        unsigned int        tx_cache_count_min;
        unsigned int        tx_cache_count_max;
        NormObjectSize      tx_cache_size_max;
        ProtoTimer          flush_timer;
        int                 flush_count;
        bool                posted_tx_queue_empty;
        ProtoTimer          watermark_timer;
        int                 watermark_count;
        // (TBD) watermark_object_id, watermark_block_id, watermark_symbol_id
        
        // for unicast nack/cc feedback suppression
        bool                advertise_repairs;
        bool                suppress_nonconfirmed;
        double              suppress_rate;
        double              suppress_rtt;
        
        ProtoTimer          probe_timer;  // GRTT/congestion control probes
        bool                probe_proactive;
        bool                probe_pending; // true while CMD(CC) enqueued
        bool                probe_reset;   
        
        double              grtt_interval;     // current GRTT update interval
        double              grtt_interval_min; // minimum GRTT update interval
        double              grtt_interval_max; // maximum GRTT update interval
        
        double              grtt_max;
        unsigned int        grtt_decrease_delay_count;
        bool                grtt_response;
        double              grtt_current_peak;
        double              grtt_measured;
        double              grtt_age;
        double              grtt_advertised;
        UINT8               grtt_quantized;
        double              gsize_measured;
        double              gsize_advertised;
        UINT8               gsize_quantized;
        
        // Server congestion control parameters
        bool                cc_enable;
        UINT8               cc_sequence;
        NormNodeList        cc_node_list;
        bool                cc_slow_start;
        double              sent_rate;         // measured sent rate
        struct timeval      prev_update_time;  // for sent_rate measurement
        unsigned long       sent_accumulator;  // for sent_rate measurement
        double              nominal_packet_size;
        
        // Client parameters
        bool                is_client;
        NormNodeTree        server_tree;
        unsigned long       remote_server_buffer_size;
        bool                unicast_nacks;
        bool                client_silent;
        
        // Protocol test/debug parameters
        bool                trace;
        double              tx_loss_rate;  // for correlated loss
        double              rx_loss_rate;  // for uncorrelated loss

        // Linkers
        NormSession*        next;
};  // end class NormSession


#endif  // _NORM_SESSION
