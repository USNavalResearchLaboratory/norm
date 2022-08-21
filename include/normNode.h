#ifndef _NORM_NODE
#define _NORM_NODE

#include "normMessage.h"
#include "normObject.h"
#include "normEncoder.h"
#include "protokit.h"

class NormNode
{
    friend class NormNodeTree;
    friend class NormNodeTreeIterator;
    friend class NormNodeList;
    friend class NormNodeListIterator;
    
    public:
        enum Type
        {
            ACKER,
            SENDER,
            CC_NODE
        };        
            
        NormNode(Type theType, class NormSession& theSession, NormNodeId nodeId);
        virtual ~NormNode();
        
        Type GetType() const
            {return node_type;}
        
        NormSession& GetSession() const {return session;}
        void Retain();
        void Release();
        
        void SetUserData(const void* userData)
            {user_data = userData;}
        const void* GetUserData() const
            {return user_data;}
        
        const ProtoAddress& GetAddress() const {return addr;} 
        void SetAddress(const ProtoAddress& address) {addr = address;}
        const NormNodeId& GetId() const {return id;}
        void SetId(const NormNodeId& nodeId) {id = nodeId;}
        inline const NormNodeId& LocalNodeId() const; 
        
        class Accumulator
        {
            public:
                Accumulator();
                void Reset()
                    {msb = lsb = 0;}
                void Increment(unsigned long count)
                {
                    unsigned long lsbOld = lsb;
                    lsb += count;
                    if (lsb < lsbOld) msb++;
                }
                double GetValue() const
                    {return ((((double)0xffffffff)*msb) + lsb);}
                double GetScaledValue(double factor) const
                    {return ((((double)0xffffffff)*(factor*msb)) + (factor*lsb));}
            private:
                unsigned long   msb;
                unsigned long   lsb;
        };    
    
    protected:
        class NormSession&  session;
        
    private:
        Type                node_type;
        NormNodeId          id;
        ProtoAddress        addr;
        unsigned int        reference_count;
        const void*         user_data;
        // We keep NormNodes in a binary tree (TBD) make this a ProtoTree
        NormNode*           parent;
        NormNode*           right;
        NormNode*           left;
};  // end class NormNode

// Weighted-history loss event estimator
class NormLossEstimator
{
    public:
        NormLossEstimator();
        double LossFraction();       
        bool Update(const struct timeval&   currentTime,
                    unsigned short          seqNumber, 
                    bool                    ecn = false);
        void SetLossEventWindow(double lossWindow) 
            {event_window = lossWindow;}
        void SetInitialLoss(double lossFraction)
        {
            memset(history, 0, (DEPTH+1)*sizeof(unsigned int));
            history[1] = (unsigned int)((1.0 / lossFraction) + 0.5);
        }
        unsigned int LastLossInterval() {return history[1];}
            
    private:
        enum {DEPTH = 8};
        enum {MAX_OUTAGE = 100};
    
        void Sync(unsigned short seq) 
        {
            index_seq = seq;
            synchronized = true;
        }
        int SequenceDelta(unsigned short a, unsigned short b);
    
        static const double weight[8];
        
        bool                synchronized;
        unsigned short      index_seq;
        bool                seeking_loss_event;
        double              event_window;
        struct timeval      event_time;
        unsigned int        history[DEPTH+1];
};  // end class NormLossEstimator


class NormLossEstimator2
{
    public:
        NormLossEstimator2();
        void SetLossEventWindow(double theTime)
            {event_window = theTime;}
        bool Update(const struct timeval&   currentTime,
                    unsigned short          seqNumber, 
                    bool                    ecn = false);
        double LossFraction();
        void SetInitialLoss(double lossFraction) 
        {
            memset(history, 0, (DEPTH+1)*sizeof(unsigned int));
            history[1] = (unsigned int)((1.0 / lossFraction) + 0.5);
        }
        unsigned long CurrentLossInterval() {return history[0];}
        unsigned long LastLossInterval() {return history[1];}
        
        void SetIgnoreLoss(bool state) 
            {ignore_loss = state;}
        void SetTolerateLoss(bool state)
            {tolerate_loss = state;}
        
        unsigned short GetLagIndex() const
            {return lag_index;}
        

    private:
        enum {DEPTH = 8};
    
        enum LossEventStatus
        {
          
            CONFIRMED   = 0,
            CONFIRMING  = 1,
            SEEKING     = 2
        };
    // Members  
        bool                init;
        bool                ignore_loss;
        bool                tolerate_loss;
        unsigned long       lag_mask;
        unsigned int        lag_depth;
        unsigned long       lag_test_bit;
        unsigned short      lag_index;
        
        double              event_window;
        struct timeval      event_time;
        struct timeval      event_time_orig;  // (TBD - remove this, it's not used);
        LossEventStatus     seeking_loss_event;
        
        unsigned long       history[9];  // loss interval history
        double              discount[9];
        double              current_discount;
        static const double weight[8];
        
        void Init(unsigned short theSequence)
            {init = true; Sync(theSequence);}
        void Sync(unsigned short theSequence)
            {lag_index = theSequence;}
        void ChangeLagDepth(unsigned int theDepth)
        {
            theDepth = (theDepth > 20) ? 20 : theDepth;
            lag_depth = theDepth;
            lag_test_bit = 0x01 << theDepth;
        }
        int SequenceDelta(unsigned short a, unsigned short b);
    
};  // end class NormLossEstimator2

class NormAckingNode : public NormNode
{
    public:
        NormAckingNode(class NormSession& theSession, NormNodeId nodeId);
        ~NormAckingNode();
        bool IsPending() const
            {return (!ack_received && (req_count > 0));}
        void Reset(unsigned int maxAttempts)
        {
            ack_received = false;
            req_count = maxAttempts;   
        }
        void DecrementReqCount() {if (req_count > 0) req_count--;}
        void ResetReqCount(unsigned int maxAttempts) 
            {req_count = maxAttempts;}
        unsigned int GetReqCount() const {return req_count;}
        bool AckReceived() const {return ack_received;}
        void MarkAckReceived() {ack_received = true;}
        
        bool SetAckEx(const char* buffer, UINT16 numBytes);
        bool GetAckEx(char* buffer, unsigned int* buflen);
        
        /*
        const char* GetAppAckContent() const
            {return (const char*)ack_ex_buffer;}
        unsigned int GetAppAckLength() const
            {return ack_ex_length;} */
                
    private:
        bool            ack_received; // was ack received?
        unsigned int    req_count;    // remaining request attempts
        char*           ack_ex_buffer;
        unsigned int    ack_ex_length;
        
};  // end NormAckingNode

class NormCCNode : public NormNode
{
    public:
       NormCCNode(class NormSession& theSession, NormNodeId nodeId);
       ~NormCCNode();
       
       enum {ACTIVE_MAX = 7};
       
       bool IsClr() const {return is_clr;}
       bool IsPlr() const {return is_plr;}
       bool IsActive() const {return is_active;}
       bool HasRtt() const {return rtt_confirmed;}
       
       double GetRtt() const {return rtt;}
       double GetRttSample() const {return rtt_sample;}
       double GetRttSqMean() const {return rtt_sqmean;}
       double GetLoss() const {return loss;}
       double GetRate() const {return rate;}
       UINT16 GetCCSequence() const {return cc_sequence;}
       const struct timeval& GetFeedbackTime()
           {return feedback_time;}
       void SetFeedbackTime(struct timeval& theTime)
           {feedback_time = theTime;}
       void SetActive(bool state) {is_active = state;}
       void SetClrStatus(bool state) {is_clr = state;}
       void SetPlrStatus(bool state) {is_plr = state;}
       void SetRttStatus(bool state) {rtt_confirmed = state;}
       void SetRtt(double value)  
       {
           rtt_sqmean = sqrt(value);
           rtt = rtt_sample = value;
       }
       double UpdateRtt(double value)
       {
           rtt_sample = value;  // save last rtt sample
           rtt_sqmean = 0.9*rtt_sqmean + 0.1*sqrt(value);
           rtt = 0.9*rtt + 0.1*value;   
           return rtt;
       }
       void SetLoss(double value) {loss = value;}
       void SetRate(double value) {rate = value;}
       void SetCCSequence(UINT16 value) {cc_sequence = value;}
       
    private:
        bool            is_clr;         // true if worst path representative
        bool            is_plr;         // true if worst path candidate
        bool            rtt_confirmed;
        bool            is_active;
        struct timeval  feedback_time;  // time of last received feedback
        double          rtt;            // in seconds
        
        
        double          rtt_sqmean;  // ave sqrt(rtt), EWMA smoothed
        double          rtt_sample;  // last rtt sample value
        
        double          loss;           // loss fraction
        double          rate;           // in bytes per second
        UINT16          cc_sequence;
};  // end class NormCCNode

class NormSenderNode : public NormNode, public ProtoTree::Item
{
    public:
        enum ObjectStatus {OBJ_INVALID, OBJ_NEW, OBJ_PENDING, OBJ_COMPLETE};
    
        enum RepairBoundary {BLOCK_BOUNDARY, OBJECT_BOUNDARY};
        
        enum SyncPolicy
        {
            SYNC_CURRENT,  // sync to detect transmit point, iff NORM_DATA from first FEC block unless stream
            SYNC_STREAM,   // same as SYNC_CURRENT, but attempts to recover stream block zero
            SYNC_ALL      // permiscuously sync as far back as possible given rx cache size
        };
    
        NormSenderNode(class NormSession& theSession, NormNodeId nodeId);  
        ~NormSenderNode();
        
        void SetInstanceId(UINT16 instanceId)
            {instance_id = instanceId;}
        
        bool PreallocateRxStream(unsigned int bufferSize,
                                 UINT16       segmentSize  ,     
                                 UINT16       numData,           
                                 UINT16       numParity);
        bool GetFtiData(const NormObjectMsg& msg, NormFtiData& ftiData);
        
        // Parameters
        NormObject::NackingMode GetDefaultNackingMode() const 
            {return default_nacking_mode;}
        void SetDefaultNackingMode(NormObject::NackingMode nackingMode)
            {default_nacking_mode = nackingMode;}
        
        // Should generally inherit NormSession::GetRxRobustFactor()
        void SetRobustFactor(int value);
        
        RepairBoundary GetRepairBoundary() const 
            {return repair_boundary;}
        // (TBD) force an appropriate RepairCheck on boundary change???
        void SetRepairBoundary(RepairBoundary repairBoundary)
            {repair_boundary = repairBoundary;}
        
        SyncPolicy GetSyncPolicy() const
            {return sync_policy;}
        void SetSyncPolicy(SyncPolicy syncPolicy)
            {sync_policy = syncPolicy;}
        
        bool UnicastNacks() {return unicast_nacks;}
        void SetUnicastNacks(bool state) {unicast_nacks = state;}
        
        
        void UpdateGrttEstimate(UINT8 grttQuantized);
        double GetGrttEstimate() const {return grtt_estimate;}
        void ResetGrttNotification() {notify_on_grtt_update = true;}
        
        bool UpdateLossEstimate(const struct timeval&   currentTime,
                                unsigned short          theSequence, 
                                bool                    ecnStatus = false);
        double LossEstimate() {return loss_estimator.LossFraction();}
        unsigned short GetCurrentSequence() const
            {return loss_estimator.GetLagIndex();}
        double GetRttEstimate() const {return rtt_estimate;}
        
        void CheckCCFeedback();
        
        void UpdateRecvRate(const struct timeval& currentTime,
                            unsigned short        msgSize);
        
        void HandleCommand(const struct timeval& currentTime,
                           const NormCmdMsg&     cmd);
        
        void HandleObjectMessage(const NormObjectMsg& msg);
        void HandleCCFeedback(UINT8 ccFlags, double ccRate);
        void HandleNackMessage(const NormNackMsg& nack);
        void HandleAckMessage(const NormAckMsg& ack);
        
        bool Open(UINT16 instanceId);
        UINT16 GetInstanceId() {return instance_id;}
        bool IsOpen() const {return is_open;} 
        void Close();
        bool AllocateBuffers(unsigned int   bufferSpace,
                             UINT8          fecId, 
                             UINT16         fecInstanceId, 
                             UINT8          fecM, 
                             UINT16         segmentSize,
                             UINT16         numData, 
                             UINT16         numParity);
        bool BuffersAllocated() {return (NULL != retrieval_pool);}
        void FreeBuffers();
        void Activate(bool isObjectMsg);
        
        bool SyncTest(const NormObjectMsg& msg) const;
        void Sync(NormObjectId objectId);
        ObjectStatus UpdateSyncStatus(const NormObjectId& objectId);
        ObjectStatus GetObjectStatus(const NormObjectId& objectId) const;
        
        
        UINT8 GetFecFieldSize() const 
            {return fti_data.GetFecFieldSize();}
        
        bool GetFirstPending(NormObjectId& objectId) const
        {
            UINT32 index;
            bool result = rx_pending_mask.GetFirstSet(index);
            objectId = (UINT16)index;
            return result;   
        }
        bool GetNextPending(NormObjectId& objectId) const
        {
            UINT32 index = (UINT16)objectId;
            bool result = rx_pending_mask.GetNextSet(index);
            objectId = (UINT16)index;
            return result;   
        }
        bool GetLastPending(NormObjectId& objectId) const
        {
            UINT32 index;
            bool result = rx_pending_mask.GetLastSet(index);
            objectId = (UINT16)index;
            return result;   
        }
        void SetPending(NormObjectId objectId);
        
        void AbortObject(NormObject* obj);
        
        void DeleteObject(NormObject* obj);
        
        NormObject* GetNextPendingObject()
        {
            NormObjectId objid;
            if (GetNextPending(objid))
                return rx_table.Find(objid);
            else
                return NULL;
        }        
        
        UINT16 SegmentSize() const {return fti_data.GetSegmentSize();}
        UINT16 BlockSize() const {return fti_data.GetFecMaxBlockLen();}
        UINT16 NumParity() const {return fti_data.GetFecNumParity();}
        
        NormBlock* GetFreeBlock(NormObjectId objectId, NormBlockId blockId);
        void PutFreeBlock(NormBlock* block)
        {
            block->EmptyToPool(segment_pool);
            block_pool.Put(block);   
        }
        bool SegmentPoolIsEmpty() {return segment_pool.IsEmpty();}
        char* GetFreeSegment(NormObjectId objectId, NormBlockId blockId);
        void PutFreeSegment(char* segment)
            {segment_pool.Put(segment);}
        
        void SetErasureLoc(UINT16 index, UINT16 value)
        {
            ASSERT(index < NumParity());
            erasure_loc[index] = value;
        }
        UINT16 GetErasureLoc(UINT16 index) 
        {
            return erasure_loc[index];
        }
        void SetRetrievalLoc(UINT16 index, UINT16 value)
        {
            ASSERT(index < BlockSize());
            retrieval_loc[index] = value;
        }
        UINT16 GetRetrievalLoc(UINT16 index) 
        {
            return retrieval_loc[index];
        } 
        char* GetRetrievalSegment()
        {
            char* s = retrieval_pool[retrieval_index++];
            retrieval_index = (retrieval_index >= BlockSize()) ? 0 : retrieval_index;
            return s;   
        }
        
        UINT16 Decode(char** segmentList, UINT16 numData, UINT16 erasureCount)
        {
            return decoder->Decode(segmentList, numData, erasureCount, erasure_loc);
        }
        
        void CalculateGrttResponse(const struct timeval& currentTime,
                                   struct timeval&       grttResponse) const;
        
        // Statistics kept on sender
        unsigned long CurrentBufferUsage() const
            {return (SegmentSize() * segment_pool.CurrentUsage());}
        unsigned long PeakBufferUsage() const
            {return (SegmentSize() * segment_pool.PeakUsage());}
        unsigned long BufferOverunCount() const
            {return segment_pool.OverunCount() + block_pool.OverrunCount();}
        
        unsigned long CurrentStreamBufferUsage();
        unsigned long PeakStreamBufferUsage();
        unsigned long StreamBufferOverunCount();
        
        
        //unsigned long RecvTotal() const {return recv_total;}
        //unsigned long RecvGoodput() const {return recv_goodput;}
        
        // returns ave bytes/sec
        double GetRecvRate(double interval) const
            {return recv_total.GetScaledValue(1.0 / interval);}
        double GetRecvGoodput(double interval) const
            {return recv_goodput.GetScaledValue(1.0 / interval);}
        
        void IncrementRecvTotal(unsigned long count) 
            {recv_total.Increment(count);}
        void IncrementRecvGoodput(unsigned long count) 
            {recv_goodput.Increment(count);}
        void ResetRecvStats() 
        {
            recv_total.Reset();
            recv_goodput.Reset();
        }
        void IncrementResyncCount() {resync_count++;}
        void DecrementResyncCount() {resync_count--;}
        unsigned long ResyncCount() const {return resync_count;}
        unsigned long NackCount() const {return nack_count;}
        unsigned long SuppressCount() const {return suppress_count;}
        unsigned long CompletionCount() const {return completion_count;}
        unsigned long PendingCount() const {return rx_table.GetCount();}
        unsigned long FailureCount() const {return failure_count;}
        
        class CmdBuffer
        {
            public:
                CmdBuffer();
                ~CmdBuffer();
                
                enum {CMD_SIZE_MAX = 8192};
                
                void SetContent(const char* data, unsigned int numBytes)
                {
                    ASSERT(numBytes <= CMD_SIZE_MAX);
                    memcpy(buffer, data, numBytes);
                    length = numBytes;
                }
                
                const char* GetContent() const
                    {return buffer;}
                unsigned int GetContentLength() const
                    {return length;}
                
                void Append(CmdBuffer* nextBuffer)
                    {next = nextBuffer;}
                CmdBuffer* GetNext() const
                    {return next;}
            private:
                char        buffer[CMD_SIZE_MAX];
                unsigned    length;
                CmdBuffer*  next;  // to support singly-linked list
        };  // end class NormSenderNode::CmdBuffer
        
        CmdBuffer* NewCmdBuffer() const;
        
        bool ReadNextCmd(char* buffer, unsigned int* buflen);
        
        bool SendAckEx(const char* data, unsigned int numBytes);   
        bool GetWatermarkEx(char* buffer, unsigned int* buflen);
        
        void SetAddress(const ProtoAddress& address)
        {
            unsigned int len = address.GetLength();
            memcpy(key_buffer, address.GetRawHostAddress(), len);
            UINT16 port = htons(address.GetPort());
            memcpy(key_buffer+len, &port, 2);
            key_size = (len+2) << 3;
            NormNode::SetAddress(address);
        }
        
        UINT8 GetGrttQuantized() const
            {return grtt_quantized;}
        UINT8 GetBackoffFactor() const
            {return backoff_factor;}
        UINT8 GetGroupSizeQuantized() const
            {return gsize_quantized;}
        UINT16 GetCCSequence() const
            {return cc_sequence;}
        double GetSendRate() const
            {return send_rate;}
        
        
    private:
        const char* GetKey() const
            {return key_buffer;}    
        unsigned int GetKeysize() const
            {return key_size;}
            
        static const double DEFAULT_NOMINAL_INTERVAL;
        static const double ACTIVITY_INTERVAL_MIN;
        
        bool PassiveRepairCheck(NormObjectId    objectId,  
                                NormBlockId     blockId,
                                NormSegmentId   segmentId);
        void RepairCheck(NormObject::CheckLevel checkLevel,
                         NormObjectId           objectId,  
                         NormBlockId            blockId,
                         NormSegmentId          segmentId);
    
        bool OnActivityTimeout(ProtoTimer& theTimer);
        bool OnRepairTimeout(ProtoTimer& theTimer);
        bool OnCCTimeout(ProtoTimer& theTimer);
        bool OnAckTimeout(ProtoTimer& theTimer);
        
        void AttachCCFeedback(NormAckMsg& ack);
        void HandleRepairContent(const UINT32* buffer, UINT16 bufferLen);
        void FragmentNack(NormNackMsg& superNack);
        
        
         
        UINT16                  instance_id;
        int                     robust_factor;
        SyncPolicy              sync_policy;
        bool                    synchronized;
        NormObjectId            sync_id;  // only valid if(synchronized)
        NormObjectId            next_id;  // only valid if(synchronized)
        NormObjectId            max_pending_object; // index for NACK construction
        NormObjectId            current_object_id;  // index for suppression
        UINT16                  max_pending_range;  // max range of pending objs allowed
        
        bool                    is_open;
        // TBD - embed the FTI parameters into a NormFtiData object
        UINT8                   fec_id;
        NormFtiData             fti_data;
        bool                    preset_fti;
        //UINT16                  segment_size;
        //UINT8                   fec_id;
        //UINT8                   fec_m;
        //unsigned int            ndata;
        //unsigned int            nparity;
        NormStreamObject*       preset_stream;
        
        NormObjectTable         rx_table;
        ProtoSlidingMask        rx_pending_mask;
        ProtoSlidingMask        rx_repair_mask;
        RepairBoundary          repair_boundary;
        NormObject::NackingMode default_nacking_mode;
        bool                    unicast_nacks;
        NormBlockPool           block_pool;
        NormSegmentPool         segment_pool;
        NormDecoder*            decoder;
        unsigned int*           erasure_loc;
        unsigned int*           retrieval_loc;
        char**                  retrieval_pool;
        unsigned int            retrieval_index;
        
        bool                    sender_active;
        ProtoTimer              activity_timer;
        ProtoTimer              repair_timer;
        
        // Watermark acknowledgement
        ProtoTimer              ack_timer;
	    bool			        ack_pending;
        NormObjectId            watermark_object_id;
        NormBlockId             watermark_block_id;
        NormSegmentId           watermark_segment_id;
        bool                    ack_ex_pending;
        char*                   ack_ex_buffer;
        unsigned int            ack_ex_length;
        
        // Remote sender grtt measurement state       
        double                  grtt_estimate;
        UINT8                   grtt_quantized;
        struct timeval          grtt_send_time;
        struct timeval          grtt_recv_time;
        double                  gsize_estimate;
        UINT8                   gsize_quantized;
        double                  backoff_factor;
        bool                    notify_on_grtt_update;  // for API
        
        // Remote sender congestion control state
        NormLossEstimator2      loss_estimator;
        UINT16                  cc_sequence;
        bool                    cc_enable;
        bool                    cc_feedback_needed;
        double                  cc_rate;           // ccRate at start of cc_timer
        ProtoTimer              cc_timer;
        double                  rtt_estimate;
        UINT8                   rtt_quantized;
        bool                    rtt_confirmed;
        bool                    is_clr;
        bool                    is_plr;
        bool                    slow_start;        
        double                  send_rate;         // sender advertised rate
        double                  recv_rate;         // measured recv rate
        double                  recv_rate_prev;    // for recv_rate measurement
        struct timeval          prev_update_time;  // for recv_rate measurement
        Accumulator             recv_accumulator;  // for recv_rate measurement
        double                  nominal_packet_size;
        
        // Buffering of app-defined commands received from Remote sender
        CmdBuffer*              cmd_buffer_head;  // the oldest received command is here (for FIFO)
        CmdBuffer*              cmd_buffer_tail;  // newly-received commands appended here
        CmdBuffer*              cmd_buffer_pool;  // we "pool" allocated buffers for possible reuse here
        
        // Used for NormSocket API extension to index by addr/port
        char                    key_buffer[16+2];  // big enough for IPv6 plus port
        unsigned int            key_size;
        
        // For statistics tracking
        Accumulator             recv_total;        // total recvd accumulator
        Accumulator             recv_goodput;      // goodput recvd accumulator
        unsigned long           resync_count;
        unsigned long           nack_count;
        unsigned long           suppress_count;
        unsigned long           completion_count;
        unsigned long           failure_count;     // usually due to re-syncs
        
};  // end class NormSenderNode
    
    
// Used for binary trees of NormNodes sorted by NormNodeId
// (TBD - update to use ProtoTree instead?
class NormNodeTree
{    
    friend class NormNodeTreeIterator;
    
    public:
    // Methods
        NormNodeTree();
        ~NormNodeTree();
        NormNode* FindNodeById(NormNodeId nodeId) const;
        void AttachNode(NormNode *theNode);
        void DetachNode(NormNode *theNode);   
        NormNode* GetRoot() const {return root;}
        void Destroy();    // delete all nodes in tree
       
    private: 
    // Members
        NormNode* root;
};  // end class NormNodeTree

class NormNodeTreeIterator
{
    public:
        NormNodeTreeIterator(const NormNodeTree& nodeTree, NormNode* prevNode = NULL);
        void Reset(NormNode* prevNode = NULL);
        NormNode* GetNextNode();  

    private:
        const NormNodeTree& tree;
        NormNode*           next;
};  // end class NormNodeTreeIterator
        
class NormNodeList
{
    friend class NormNodeListIterator;
    
    public:
    // Construction
        NormNodeList();
        ~NormNodeList();
        unsigned int GetCount() {return count;}
        NormNode* FindNodeById(NormNodeId nodeId) const;
        void Append(NormNode* theNode);
        void Remove(NormNode* theNode);
        void DeleteNode(NormNode* theNode)
        {
            ASSERT(NULL != theNode);
            Remove(theNode);
            theNode->Release();
        }
        void Destroy();  // delete all nodes in list
        const NormNode* Head() {return head;}
               
    // Members
    private:
        NormNode*                    head;
        NormNode*                    tail;
        unsigned int                 count;
};  // end class NormNodeList

class NormNodeListIterator
{
    public:
        NormNodeListIterator(const NormNodeList& nodeList)
         : list(nodeList), next(nodeList.head) {}
        void Reset() {next = list.head;} 
        NormNode* GetNextNode()
        {
            NormNode* n = next;
            next = n ? n->right : NULL;
            return n;   
        }
    private:
        const NormNodeList& list;
        NormNode*           next;
};  // end class NormNodeListIterator

// Used to track remote client sender nodes for server/listener sessions
class NormClientTree : public ProtoTreeTemplate<NormSenderNode>
{
    public:
        void InsertNode(NormSenderNode& sender)
        {
            sender.Retain();
            Insert(sender);
        }      
        void RemoveNode(NormSenderNode& sender)
        {
            Remove(sender);
            sender.Release();
        }   
        NormSenderNode* FindNodeByAddress(const ProtoAddress& addr)
        {
            char key[16+2];
            unsigned int len = addr.GetLength();
            memcpy(key, addr.GetRawHostAddress(), len);
            UINT16 port = htons(addr.GetPort());
            memcpy(key+len, &port, 2);
            return Find(key, (len+2) << 3);
        }
};  // end class NormClientTree

#endif // NORM_NODE
