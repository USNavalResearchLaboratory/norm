#ifndef _NORM_SESSION
#define _NORM_SESSION

#include "normMessage.h"
#include "normObject.h"
#include "normNode.h"
#include "normEncoder.h"

#include "protokit.h"

#include "protoCap.h"  // for ProtoCap for ECN_SUPPORT


// When this is defined, our experimental tweak to 
// limiting suggested cc rate to 2.0* measured recv rate is
// used during steady state similar to  "slow start"
// conditions.  What this means is that when data transmission
// is idle, the rate will be reduced.  This _may_ impact
// certain use cases.  Our theory here is that preventing rate
// overshoot will be more helpful and safer than the penalty
// imposed.  This uses the non-RFC5740 NORM_CC_FLAG_LIMIT in
// NORM-CC feedback header extensions

#define LIMIT_CC_RATE 1

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
            TX_WATERMARK_COMPLETED,
            TX_CMD_SENT,
            TX_OBJECT_SENT,
            TX_OBJECT_PURGED,
            TX_RATE_CHANGED,
            LOCAL_SENDER_CLOSED,
            REMOTE_SENDER_NEW,
            REMOTE_SENDER_RESET,
            REMOTE_SENDER_ADDRESS,
            REMOTE_SENDER_ACTIVE,
            REMOTE_SENDER_INACTIVE,
            REMOTE_SENDER_PURGED,
            RX_CMD_NEW,
            RX_OBJECT_NEW,
            RX_OBJECT_INFO,
            RX_OBJECT_UPDATED,
            RX_OBJECT_COMPLETED,
            RX_OBJECT_ABORTED,
            RX_ACK_REQUEST,         // upon receipt of app-extended watermark ack request
            GRTT_UPDATED,
            CC_ACTIVE,              // posted when cc feedback is detected
            CC_INACTIVE,            // posted when no cc feedback and min rate reached 
            ACKING_NODE_NEW,
            SEND_ERROR,
            USER_TIMEOUT,
            // The ones below here are not exposed via the NORM API
            SEND_OK
        };
                  
        virtual void Notify(NormController::Event event,
                            class NormSessionMgr* sessionMgr,
                            class NormSession*    session,
                            class NormNode*       node,
                            class NormObject*     object) = 0;
                    
};  // end class NormController

class NormSessionMgr
{
    friend class NormSession;
    public:
        NormSessionMgr(ProtoTimerMgr&            timerMgr,
                       ProtoSocket::Notifier&    socketNotifier,
                       ProtoChannel::Notifier*   channelNotifier = NULL);
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
                    class NormNode*       node,
                    class NormObject*     object)
        {
            if (controller)
                controller->Notify(event, this, session, node, object);   
        }
               
        void ActivateTimer(ProtoTimer& timer) {timer_mgr.ActivateTimer(timer);}
        ProtoTimerMgr& GetTimerMgr() const {return timer_mgr;}        
        ProtoSocket::Notifier& GetSocketNotifier() const {return socket_notifier;}
        ProtoChannel::Notifier* GetChannelNotifier() const {return channel_notifier;}
        
        void DoSystemTimeout()
            {timer_mgr.DoSystemTimeout();}
    
        NormController* GetController() const {return controller;}
        
        
        void SetDataFreeFunction(NormDataObject::DataFreeFunctionHandle freeFunc)
            {data_free_func = freeFunc;}
        NormDataObject::DataFreeFunctionHandle GetDataFreeFunction() const
            {return data_free_func;}
        
    private:   
        ProtoTimerMgr&                          timer_mgr;      
        ProtoSocket::Notifier&                  socket_notifier; 
        ProtoChannel::Notifier*                 channel_notifier; 
        NormController*                         controller;     
        NormDataObject::DataFreeFunctionHandle  data_free_func;
        
        class NormSession*       top_session;  // top of NormSession list
              
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
        static const UINT16 DEFAULT_TX_CACHE_MIN;
        static const UINT16 DEFAULT_TX_CACHE_MAX;
        static const UINT32 DEFAULT_TX_CACHE_SIZE;
        static const double DEFAULT_FLOW_CONTROL_FACTOR;
        static const UINT16 DEFAULT_RX_CACHE_MAX;
        static const int DEFAULT_ROBUST_FACTOR;
        
        enum {IFACE_NAME_MAX = 31};
        
        enum ProbingMode {PROBE_NONE, PROBE_PASSIVE, PROBE_ACTIVE};
        enum AckingStatus 
        {
            ACK_INVALID, 
            ACK_FAILURE, 
            ACK_PENDING,
            ACK_SUCCESS
        };
          
        // This is currently  used to determine whether 
        // and how to "auto populate" the acking node 
        // list based on received messages  
        enum TrackingStatus
        {
            TRACK_NONE      = 0x00,
            TRACK_RECEIVERS = 0x01,
            TRACK_SENDERS   = 0x02,
            TRACK_ALL       = 0x03
        };
       
        // Object FEC Transport Information (FTI) mode
        enum FtiMode
        {
            FTI_PRESET  = 0,  // Receivers have preset FTI, don't send
            FTI_INFO    = 1,  // Send FTI in NORM_INFO messages only
            FTI_ALWAYS  = 2   // Send FTI in NORM_DATA and NORM_INFO messages
        };
               
        // General methods
        void SetNodeId(NormNodeId nodeId)
            {local_node_id = nodeId;}
        const NormNodeId& LocalNodeId() const {return local_node_id;}
        bool Open();
        void Close();
        bool IsOpen() {return (rx_socket.IsOpen() || tx_socket->IsOpen());}
        const ProtoAddress& Address() {return address;}
        void SetAddress(const ProtoAddress& addr) {address = addr;}
        bool SetMulticastInterface(const char* interfaceName);
        bool SetSSM(const char* sourceAddress);
        bool SetTTL(UINT8 theTTL) 
        {
            bool result = tx_socket->IsOpen() ? tx_socket->SetTTL(theTTL) : true;
            ttl = result ? theTTL : ttl;
            return result; 
        }
        bool SetTOS(UINT8 theTOS) 
        {
            // (TBD) call tx_socket->SetFlowLabel() to set traffic class for IPv6 sockets
            // (or should we have ProtoSocket::SetTOS() do this for us?)
            bool result = tx_socket->IsOpen() ? tx_socket->SetTOS(theTOS) : true;
            tos = result ? theTOS : tos;
            return result; 
        }
        bool SetLoopback(bool state) 
        {
            bool result = state ? SetMulticastLoopback(true) : true;
            loopback = result ? state : loopback;
            return result; 
        }
        
        bool SetMulticastLoopback(bool state)
        {
            bool result = tx_socket->IsOpen() ? tx_socket->SetLoopback(state) : true;
            mcast_loopback = result ? state : mcast_loopback;
            return result; 
        }
        
        bool SetFragmentation(bool state) 
        {
            bool result = tx_socket->IsOpen() ? tx_socket->SetFragmentation(state) : true;
            fragmentation = result ? state : fragmentation;
            return result; 
        }
        
        // MUST be called _after_ SetAddress()
        bool SetTxPort(UINT16 txPort, bool enableReuse = false, const char* srcAddr = NULL);
        
        UINT16 GetTxPort() const;
            
        bool SetRxPortReuse(bool enableReuse, 
                            const char*       rxAddress = NULL,               // bind() to <rxAddress>/<sessionPort>
                            const char*       senderAddress = (const char*)0, // connect() to <senderAddress>/<senderPort>
                            UINT16            senderPort = 0);
        
        UINT16 GetRxPort() const;
        
        const ProtoAddress& GetRxBindAddr() const
            {return rx_bind_addr;}
        
        // "SetEcnSupport(true)" sets up raw packet capture (pcap) so that incoming packet
        // ECN status may be checked
        // NOTE: only effective _before_ sndr/rcvr startup!
        void SetEcnSupport(bool ecnEnable, bool ignoreLoss, bool tolerateLoss)  
        {
            ecn_enabled = ecnEnable;
            ecn_ignore_loss = ecnEnable ? ignoreLoss : false;
            cc_tolerate_loss = ecn_ignore_loss ? false : tolerateLoss;
        }
        bool GetEcnIgnoreLoss() const
            {return ecn_ignore_loss;}
        bool GetCCTolerateLoss() const
            {return cc_tolerate_loss;}
        static double CalculateRate(double size, double rtt, double loss);
        
        NormSessionMgr& GetSessionMgr() {return session_mgr;}
        
        bool SetTxSocketBuffer(unsigned int bufferSize)
            {return tx_socket->SetTxBufferSize(bufferSize);}
        bool SetRxSocketBuffer(unsigned int bufferSize)
            {return rx_socket.SetRxBufferSize(bufferSize);}
        
        // Session parameters
        double GetTxRate();  // returns bits/sec
        // (TBD) watch timer scheduling and min/max bounds
        void SetTxRate(double txRate)
        {
            txRate /= 8.0;  // convert to bytes/sec
            posted_tx_rate_changed = false;
            SetTxRateInternal(txRate);
        }
        void SetTxRateBounds(double rateMin, double rateMax);
        
        void ClearSendError()
            {posted_send_error = false;}
        
        double BackoffFactor() 
            {return backoff_factor;}
        void SetBackoffFactor(double value) 
            {backoff_factor = value;}
        bool CongestionControl() 
            {return cc_enable;}
        void SetCongestionControl(bool state, bool adjustRate = true) 
        {
            if (state) SetGrttProbingMode(PROBE_ACTIVE);
            cc_enable = state;
            cc_adjust = adjustRate;
            if (state) probe_proactive = true;
        }
        
        // This MUST be called before
        void SetProbeTOS(UINT8 probeTOS)
            {probe_tos = probeTOS;}
        UINT8 GetProbeTOS() const
            {return probe_tos;}
        
        // This method enables/disables flow control operation.
        void SetFlowControl(double flowControlFactor)  
            {flow_control_factor = flowControlFactor;}
        double GetFlowControl() const
            {return flow_control_factor;}
        
        // This method is used by "internal" NormSession and NormObject code
        // to activate the timer-based flow control when needed.
        void ActivateFlowControl(double delay, NormObjectId objectId, NormController::Event event);
        void DeactivateFlowControl()
            {flow_control_timer.Deactivate();}
        bool FlowControlIsActive() const
            {return flow_control_timer.IsActive();}
        NormObjectId GetFlowControlObject() const
            {return flow_control_object;}
        // The value returned here is the time interval used to determine
        // whether there has been "recent" NACKing for a given object or block.
        // A larger "flow_control_factor" stretches the time interval that
        // is considered "recent" and thus imposes stronger flow control.
        // A _strong_ "flow_control_factor" would be on the order of 
        // "tx_robust_factor", but note larger values require more
        // tx/rx caching and/or buffering to sustain high throughput
        // NOTE "flow_control_factor = 0.0" means _no_ timer-based 
        // flow control is imposed
        double GetFlowControlDelay() const
        {
            if (0.0 == flow_control_factor) return 0.0;
            double fdelay =  (flow_control_factor * (SenderGrtt() * (backoff_factor + 1)));
            return ((fdelay > 0.020) ? fdelay : 0.020);  // minimum 20 msec flow control
        }
        
        // GRTT measurement management
        void SetGrttProbingMode(ProbingMode  probingMode);
        void SetGrttProbingInterval(double intervalMin, double intervalMax);
        void SetGrttMax(double grttMax) {grtt_max = grttMax;}
        
        bool SetTxCacheBounds(NormObjectSize sizeMax,
                              unsigned long  countMin,
                              unsigned long  countMax);
        
        // For NormSocket API extension support only
        void SetServerListener(bool state)
            {is_server_listener = state;}
        bool IsServerListener() const
            {return is_server_listener;}
        
        void Notify(NormController::Event event,
                    class NormNode*       node,
                    class NormObject*     object)
        {
            notify_pending = true;
            session_mgr.Notify(event, this, node, object);  
            notify_pending = false;
        }
        
        NormMsg* GetMessageFromPool() {return message_pool.RemoveHead();}
        void ReturnMessageToPool(NormMsg* msg) {message_pool.Append(msg);}
        void QueueMessage(NormMsg* msg);
        enum MessageStatus
        {
            MSG_SEND_FAILED,
            MSG_SEND_BLOCKED,
            MSG_SEND_OK
                    
        };
        MessageStatus SendMessage(NormMsg& msg);
        void ActivateTimer(ProtoTimer& timer) {session_mgr.ActivateTimer(timer);}
        
        void SetUserData(const void* userData) 
            {user_data = userData;}
        const void* GetUserData() const
            {return user_data;}
        
        void SetUserTimer(double seconds);  // set to value less than zero to cancel
        
        // Sender methods
        void SenderSetBaseObjectId(NormObjectId baseId)
        {
            next_tx_object_id = IsSender() ? next_tx_object_id : baseId;   
            //instance_id = IsSender() ? instance_id : (UINT16)baseId;
        }
        bool IsSender() {return is_sender;}
        bool StartSender(UINT16         instanceId,
                         UINT32         bufferSpace,
                         UINT16         segmentSize,
                         UINT16         numData,
                         UINT16         numParity,
                         UINT8          fecId = 0);
        void StopSender();
        void SetTxOnly(bool txOnly, bool connectToSessionAddress = false);
        bool GetTxOnly() const
            {return tx_only;}
        
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
        
        bool RequeueTxObject(NormObject* obj);
        
        void DeleteTxObject(NormObject* obj, bool notify); 
        
        NormObject* SenderFindTxObject(NormObjectId objectId)
            {return tx_table.Find(objectId);}
        
        // postive ack mgmnt (can only fail when 'appAckReq' is set)
        bool SenderSetWatermark(NormObjectId  objectId,
                                NormBlockId   blockId,
                                NormSegmentId segmentId,
                                bool          overrideFlush = false,
                                const char*   appAckReq = NULL,
                                unsigned int  appAckReqLen = 0);
        
        void SenderResetWatermark();
        void SenderCancelWatermark();
        
        
        void SenderSetAutoAckingNodes(TrackingStatus trackingStatus)
            {acking_auto_populate = trackingStatus;}
        NormAckingNode* SenderAddAckingNode(NormNodeId nodeId, const ProtoAddress* srcAddr = NULL);
        void SenderRemoveAckingNode(NormNodeId nodeId);
        AckingStatus SenderGetAckingStatus(NormNodeId nodeId);
        // Set "prevNodeId = NORM_NODE_NONE" to init this iteration (returns "false" when done)
        bool SenderGetNextAckingNode(NormNodeId& prevNodeId, AckingStatus* ackingStatus = NULL);
        bool SenderGetAckEx(NormNodeId nodeId, char* buffer, unsigned int* buflen);
        
        NormAckingNode* SenderFindAckingNode(NormNodeId nodeId) const
        {
            return static_cast<NormAckingNode*>(acking_node_tree.FindNodeById(nodeId));
        }        
            
        // App-defined command support methods
        bool SenderSendCmd(const char* cmdBuffer, unsigned int cmdLength, bool robust);
        void SenderCancelCmd();
        
        // The following method is currently only used for NormSocket purposes
        bool SenderSendAppCmd(const char* buffer, unsigned int length, const ProtoAddress& dst);
        
        void SenderSetSynStatus(bool state)
            {syn_status = state;}
        
        // robust factor
        void SetTxRobustFactor(int value)
            {tx_robust_factor = value;}
        int GetTxRobustFactor() const
            {return tx_robust_factor;}
        void SetRxRobustFactor(int value)
            {rx_robust_factor = value;}
        int GetRxRobustFactor() const
            {return rx_robust_factor;}
        
        UINT8 GetSenderFecId() const {return fec_id;}
        UINT8 GetSenderFecFieldSize() const {return fec_m;}
        UINT16 SenderSegmentSize() const {return segment_size;}
        UINT16 SenderBlockSize() const {return ndata;}
        UINT16 SenderNumParity() const {return nparity;}
        UINT16 SenderAutoParity() const {return auto_parity;}
        void SenderSetAutoParity(UINT16 autoParity)
            {ASSERT(autoParity <= nparity); auto_parity = autoParity;}
        UINT16 SenderExtraParity() const {return extra_parity;}
        void SenderSetExtraParity(UINT16 extraParity)
            {extra_parity = extraParity;}
        
        INT32 Difference(NormBlockId a, NormBlockId b) const
            {return NormBlockId::Difference(a, b, fec_block_mask);}
        int Compare(NormBlockId a, NormBlockId b) const
            {return NormBlockId::Compare(a, b, fec_block_mask);}
        void Increment(NormBlockId& b, UINT32 i = 1) const
            {b.Increment(i, fec_block_mask);}
        
        
        // EMCON Sender (useful when there are silent receivers)
        // (NORM_INFO is redundantly sent)
        void SndrSetEmcon(bool state)
            {sndr_emcon = true;}
        bool SndrEmcon() const
            {return sndr_emcon;}
        
        bool SenderGetFirstPending(NormObjectId& objectId)
        {
            UINT32 index;
            bool result = tx_pending_mask.GetFirstSet(index);
            objectId = (UINT16)index;
            return result;   
        }
        bool SenderGetFirstRepairPending(NormObjectId& objectId)
        {
            UINT32 index;
            bool result = tx_repair_mask.GetFirstSet(index);
            objectId = (UINT16)index;
            return result;   
        }
        
        double SenderGrtt() const {return grtt_advertised;}
        void ResetGrttNotification() 
            {notify_on_grtt_update = true;}
        void SenderSetGrtt(double grttValue)
        {
            if (IsSender())
            {
                double grttMin = 2.0 * ((double)(44+segment_size))/tx_rate;
                grttValue = (grttValue < grttMin) ? grttMin : grttValue;
            }
            grtt_quantized = NormQuantizeRtt(grttValue);
            grtt_measured = grtt_advertised = NormUnquantizeRtt(grtt_quantized);  
        }
        double SenderGroupSize() {return gsize_measured;}
        void SenderSetGroupSize(double gsize)
        {
            gsize_measured = gsize;
            gsize_quantized = NormQuantizeGroupSize(gsize);   
            gsize_advertised = NormUnquantizeGroupSize(gsize_quantized);
        }
        
        FtiMode SenderFtiMode() const
            {return fti_mode;}
        void SenderSetFtiMode(FtiMode ftiMode)
            {fti_mode = ftiMode;}
        
        void SenderEncode(unsigned int segmentId, const char* segment, char** parityVectorList)
            {encoder->Encode(segmentId, segment, parityVectorList);}
        
        
        NormBlock* SenderGetFreeBlock(NormObjectId objectId, NormBlockId blockId);
        void SenderPutFreeBlock(NormBlock* block)
        {
            block->EmptyToPool(segment_pool);
            block_pool.Put(block);
        }
        char* SenderGetFreeSegment(NormObjectId objectId, NormBlockId blockId);
        void SenderPutFreeSegment(char* segment) {segment_pool.Put(segment);}
        
        
        void PromptSender() {QueueMessage(NULL);}
        
        void TouchSender() 
        {
            posted_tx_queue_empty = false;
            PromptSender();
            //if (!notify_pending) Serve();
        }
        
        bool GetPostedTxQueueEmpty() const
            {return posted_tx_queue_empty;}
        
        // Receiver methods
        bool StartReceiver(unsigned long bufferSpace);
        void StopReceiver();
        bool IsReceiver() const {return is_receiver;}
        unsigned long RemoteSenderBufferSize() const
            {return remote_sender_buffer_size;}
        
        bool InsertRemoteSender(NormSenderNode& sender);
        
        void DeleteRemoteSender(NormSenderNode& senderNode);
        
        // Call this to do remote sender memory allocations ahead of time
        bool PreallocateRemoteSender(unsigned int   bufferSize,
                                     UINT16         segmentSize, 
                                     UINT16         numData, 
                                     UINT16         numParity, 
                                     unsigned int   streamBufferSize = 0);
        
        bool SetPresetFtiData(unsigned int objectSize,
                              UINT16       segmentSize,   
                              UINT16       numData,       
                              UINT16       numParity);   
        
        bool GetPresetFtiData(NormFtiData& ftiData)
        {
            if (preset_fti.IsValid())
            {
                ftiData = preset_fti;
                return true;
            }
            return false;   
        }
        
        void ReceiverSetUnicastNacks(bool state) 
            {unicast_nacks = state;}
        bool ReceiverGetUnicastNacks() const 
            {return unicast_nacks;}
        
        void ReceiverSetSilent(bool state) 
            {receiver_silent = state;}
        bool ReceiverIsSilent() const {return receiver_silent;}
        
        void RcvrSetIgnoreInfo(bool state)
            {rcvr_ignore_info = state;}
        bool RcvrIgnoreInfo() const
            {return rcvr_ignore_info;}        
        
        // The default "rcvr_max_delay = -1" corresponds to typical 
        // operation where source data for partially received FEC blocks 
        // are only provided to the app when buffer constraints require it.
        // Otherwise, the "maxDelay" corresponds to the max number
        // of FEC blocks the receiver waits before passing partially
        // received blocks to the app.
        // Note a "maxDelay == 0" provides _no_ protection from 
        // out-of-order received packets!
        void RcvrSetMaxDelay(INT32 maxDelay) 
            {rcvr_max_delay = maxDelay;}
        bool RcvrIsLowDelay() 
            {return (ReceiverIsSilent() && (rcvr_max_delay >= 0));}
        INT32 RcvrGetMaxDelay() const
            {return rcvr_max_delay;}
        
        // When "rcvr_realtime" is set to "true", the buffer managment scheme of
        // favoring newly arriving data over attempting reliable reception of
        // buffered data is observed.  This is the same buffer management that
        // is used for silent receiver operation 
        // (TBD) allow the above "low delay" option to work with this, too?
        void RcvrSetRealtime(bool state)
            {rcvr_realtime = state;}
        bool RcvrIsRealtime() const
            {return rcvr_realtime;}
        
        NormObject::NackingMode ReceiverGetDefaultNackingMode() const
            {return default_nacking_mode;}
        void ReceiverSetDefaultNackingMode(NormObject::NackingMode nackingMode)
            {default_nacking_mode = nackingMode;}
        
        NormSenderNode::RepairBoundary ReceiverGetDefaultRepairBoundary() const
            {return default_repair_boundary;}
        void ReceiverSetDefaultRepairBoundary(NormSenderNode::RepairBoundary repairBoundary)
            {default_repair_boundary = repairBoundary;}
        
        NormSenderNode::SyncPolicy ReceiverGetDefaultSyncPolicy() const
            {return default_sync_policy;}
        void ReceiverSetDefaultSyncPolicy(NormSenderNode::SyncPolicy syncPolicy)
            {default_sync_policy = syncPolicy;}
        
        // Set default "max_pending_range" of NormObjects for reception
        void SetRxCacheMax(UINT16 maxCount)
            {rx_cache_count_max = (maxCount > 0x7fff) ? 0x7fff : maxCount;}
        UINT16 GetRxCacheMax() const
            {return rx_cache_count_max;}
        
        // Debug settings
        void SetTrace(bool state) {trace = state;}
        void SetTxLoss(double percent) {tx_loss_rate = percent;}
        void SetRxLoss(double percent) {rx_loss_rate = percent;}
        void SetReportTimerInterval(double interval) {report_timer.SetInterval(interval);}
        double GetReportTimerInterval() {return report_timer.GetInterval();} 

#ifdef SIMULATE   
        // Simulation specific methods
        NormSimObject* QueueTxSim(unsigned long objectSize);
        bool SimSocketRecvHandler(char* buffer, unsigned short buflen,
                                  const ProtoAddress& src, bool unicast);
#endif // SIMULATE
        
        void SetProbeCount(unsigned probeCount) {probe_count = probeCount;}
        bool SenderQueueSquelch(NormObjectId objectId);
                   
    private:
        // Only NormSessionMgr can create/delete sessions
        NormSession(NormSessionMgr& sessionMgr, NormNodeId localNodeId);
        ~NormSession();
        
        void Serve();
        bool QueueTxObject(NormObject* obj);
        
        
        double GetProbeInterval();
        
        bool OnTxTimeout(ProtoTimer& theTimer);
        bool OnRepairTimeout(ProtoTimer& theTimer);
        bool OnFlushTimeout(ProtoTimer& theTimer);
        bool OnProbeTimeout(ProtoTimer& theTimer);
        bool OnReportTimeout(ProtoTimer& theTimer);
        bool OnCmdTimeout(ProtoTimer& theTimer);
        bool OnFlowControlTimeout(ProtoTimer& theTimer);
        bool OnUserTimeout(ProtoTimer& theTimer);
        
        void TxSocketRecvHandler(ProtoSocket& theSocket, ProtoSocket::Event theEvent);
        void RxSocketRecvHandler(ProtoSocket& theSocket, ProtoSocket::Event theEvent);        
        void HandleReceiveMessage(NormMsg& msg, bool wasUnicast, bool ecn = false);

#ifdef ECN_SUPPORT        
        // This is used when raw packet capture is enabled
        bool OpenProtoCap();
        void CloseProtoCap();
        bool RawSendTo(const char* buffer, unsigned int& numBytes, const ProtoAddress& dstAddr, UINT8 trafficClass);
        void OnPktCapture(ProtoChannel&              theChannel,
	                      ProtoChannel::Notification notifyType);
#endif // ECN_SUPPORT
        
        // Sender message handling routines
        void SenderHandleNackMessage(const struct timeval& currentTime, 
                                     NormNackMsg&          nack);
        void SenderHandleAckMessage(const struct timeval& currentTime, 
                                    const NormAckMsg&     ack,
                                    bool                  wasUnicast);
        void SenderUpdateGrttEstimate(double rcvrRtt);
        double CalculateRtt(const struct timeval& currentTime,
                            const struct timeval& grttResponse);
        void SenderHandleCCFeedback(struct timeval currentTime,
                                    NormNodeId     nodeId,              
                                    UINT8          ccFlags,             
                                    double         ccRtt,               
                                    double         ccLoss,              
                                    double         ccRate,              
                                    UINT16         ccSequence);         
        void AdjustRate(bool onResponse);
        void SetTxRateInternal(double txRate);  // here, txRate is bytes/sec
        //bool SenderQueueSquelch(NormObjectId objectId);
        void SenderQueueFlush();
        bool SenderQueueWatermarkFlush();
        bool SenderBuildRepairAdv(NormCmdRepairAdvMsg& cmd);
        void SenderUpdateGroupSize();
        bool SenderQueueAppCmd();  
        
        // Receiver message handling routines
        void ReceiverHandleObjectMessage(const struct timeval& currentTime, 
                                         const NormObjectMsg&  msg,
                                         bool                  ecnStatus);
        void ReceiverHandleCommand(const struct timeval& currentTime,
                                   const NormCmdMsg&     msg,
                                   bool                  ecnStatus);
        void ReceiverHandleNackMessage(const NormNackMsg& nack);
        void ReceiverHandleAckMessage(const NormAckMsg& ack);
        
        NormSessionMgr&                 session_mgr;
        bool                            notify_pending;
        ProtoTimer                      tx_timer;
        UINT16                          tx_port;
        bool                            tx_port_reuse;
        ProtoAddress                    tx_address;    // bind tx_socket to tx_address when valid
        ProtoSocket                     tx_socket_actual;
        ProtoSocket*                    tx_socket;
        ProtoSocket                     rx_socket;
#ifdef ECN_SUPPORT
        ProtoCap*                       proto_cap;        // raw packet capture alternative to "rx_socket"
        ProtoAddress                    src_addr;         // used for raw packet sendto()
#endif // ECN_SUPPORT
        bool                            rx_port_reuse; // enable rx_socket port (sessionPort) reuse when true
        ProtoAddress                    rx_bind_addr;
        ProtoAddress                    rx_connect_addr;
        
        
        ProtoAddressList                dst_addr_list;  // list of local addresses
        NormMessageQueue                message_queue;
        NormMessageQueue                message_pool;
        ProtoTimer                      report_timer;
        UINT16                          tx_sequence;
        
        // General session parameters
        NormNodeId                      local_node_id;
        ProtoAddress                    address;         // session destination address/port
        ProtoAddress                    ssm_source_addr; // optional SSM source address
        UINT8                           ttl;             // session multicast ttl   
        UINT8                           tos;             // session IPv4 TOS (or IPv6 traffic class - TBD)
        bool                            loopback;        // receive own traffic it true
        bool                            mcast_loopback;  // enable socket multicast loopback if true
        bool                            fragmentation;   // enable UDP/IP fragmentation (i.e. clear DF bit) if true
        bool                            ecn_enabled;     // set true to get raw packets and check for ECN status
        
        char                            interface_name[IFACE_NAME_MAX+1];    
        double                          tx_rate;  // bytes per second
        double                          tx_rate_min;
        double                          tx_rate_max;
        unsigned int                    tx_residual;    // for NORM_CMD(CC)/NORM_DATA "packet pairing"
        
        
        // Sender parameters and state
        double                          backoff_factor;
        bool                            is_sender;
        int                             tx_robust_factor;
        UINT16                          instance_id;
        UINT16                          segment_size;
        UINT16                          ndata;
        UINT16                          nparity;
        UINT16                          auto_parity;
        UINT16                          extra_parity;
        bool                            sndr_emcon;
        bool                            tx_only;
        bool                            tx_connect;
        FtiMode                         fti_mode;  
        
        NormObjectTable                 tx_table;
        ProtoSlidingMask                tx_pending_mask;
        ProtoSlidingMask                tx_repair_mask;
        ProtoTimer                      repair_timer;
        NormBlockPool                   block_pool;
        NormSegmentPool                 segment_pool;
        NormEncoder*                    encoder;
        UINT8                           fec_id;
        UINT8                           fec_m;
        INT32                           fec_block_mask;
        
        NormObjectId                    next_tx_object_id;
        unsigned int                    tx_cache_count_min;
        unsigned int                    tx_cache_count_max;
        NormObjectSize                  tx_cache_size_max;
        ProtoTimer                      flush_timer;
        int                             flush_count;
        bool                            posted_tx_queue_empty;
        bool                            posted_tx_rate_changed;
        bool                            posted_send_error;
        
        // For postive acknowledgement collection
        NormNodeTree                    acking_node_tree;
        unsigned int                    acking_node_count;
        unsigned int                    acking_success_count;
        TrackingStatus                  acking_auto_populate;  // whether / how to "auto populate" acking node list
        bool                            watermark_pending;
        bool                            watermark_flushes;
        bool                            watermark_active;
        NormObjectId                    watermark_object_id;
        NormBlockId                     watermark_block_id;
        NormSegmentId                   watermark_segment_id;
        bool                            tx_repair_pending;
        NormObjectId                    tx_repair_object_min;
        NormBlockId                     tx_repair_block_min;
        NormSegmentId                   tx_repair_segment_min;
        
        // for unicast nack/cc feedback suppression
        bool                            advertise_repairs;
        bool                            suppress_nonconfirmed;
        double                          suppress_rate;
        double                          suppress_rtt;
        
        ProtoTimer                      probe_timer;  // GRTT/congestion control probes
        bool                            probe_proactive;
        bool                            probe_pending; // true while CMD(CC) enqueued
        bool                            probe_reset;   
        bool                            probe_data_check;  // refrain cc probe until data is send
        struct timeval                  probe_time_last;
        UINT8                           probe_tos;         // optionally use different IP TOS for GRTT probe/response
        
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
        
        // Sender congestion control parameters
        unsigned int                    probe_count;  // for experimentation (cc probes per rtt)
        bool                            cc_enable;
        bool                            cc_adjust;
        UINT16                          cc_sequence;
        NormNodeList                    cc_node_list;
        bool                            cc_slow_start;
        bool                            cc_active;
        NormNode::Accumulator           sent_accumulator;  // for sentRate measurement
        double                          nominal_packet_size;
        bool                            data_active;       // true when actively sending data
        double                          flow_control_factor;
        ProtoTimer                      flow_control_timer;
        NormObjectId                    flow_control_object;
        NormController::Event           flow_control_event;
        
        // Sender "app-defined" command state
        unsigned int                    cmd_count;
        char*                           cmd_buffer;
        unsigned int                    cmd_length;
        ProtoTimer                      cmd_timer;
        bool                            syn_status;
        
        // Sender "app-defined" ACK_REQUEST state (for NormSetWatermarkEx())
        char*                           ack_ex_buffer;
        unsigned int                    ack_ex_length;
        
        // Receiver parameters
        bool                            is_receiver;
        int                             rx_robust_factor;
        NormSenderNode*                 preset_sender;
        NormNodeTree                    sender_tree;
        unsigned long                   remote_sender_buffer_size;
        bool                            unicast_nacks;
        bool                            receiver_silent;
        bool                            rcvr_ignore_info;
        INT32                           rcvr_max_delay;
        bool                            rcvr_realtime;
        NormSenderNode::RepairBoundary  default_repair_boundary;
        NormObject::NackingMode         default_nacking_mode;
        NormSenderNode::SyncPolicy      default_sync_policy;
        UINT16                          rx_cache_count_max;
        NormFtiData                     preset_fti;
        
        // For NormSocket server-listener support
        bool                            is_server_listener;
        NormClientTree                  client_tree;
        
        // API-specific state variables
        bool                            notify_on_grtt_update;
        
        // State for some experimental congestion control
        bool                            ecn_ignore_loss;  
        bool                            cc_tolerate_loss;  
        
        // Protocol test/debug parameters
        bool                            trace;
        double                          tx_loss_rate;  // for correlated loss
        double                          rx_loss_rate;  // for uncorrelated loss
        double                          report_timer_interval;

        ProtoTimer                      user_timer;
        const void*                     user_data;
        
        // Linkers
        NormSession*                    next;
};  // end class NormSession

// This function prints out NORM message info
void NormTrace(const struct timeval&    currentTime, 
               NormNodeId               localId, 
               const NormMsg&           msg, 
               bool                     sent,
               UINT8                    fecM,
	           UINT16			instId = 0);  // this might not always be available to caller

#endif  // _NORM_SESSION
