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
        NormNode(class NormSession& theSession, NormNodeId nodeId);
        virtual ~NormNode();
        void Retain();
        void Release();
        
        const ProtoAddress& GetAddress() const {return addr;} 
        void SetAddress(const ProtoAddress& address) {addr = address;}
        const NormNodeId& GetId() const {return id;}
        void SetId(const NormNodeId& nodeId) {id = nodeId;}
        inline const NormNodeId& LocalNodeId() const; 
    
    protected:
        class NormSession&  session;
        
    private:
        NormNodeId          id;
        ProtoAddress        addr;
        unsigned int        reference_count;
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
        void SetLossEventWindow(double lossWindow) {event_window = lossWindow;}
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
        void SetEventWindow(unsigned short windowDepth)
            {event_window = windowDepth;}
        void SetLossEventWindow(double theTime)
            {event_window_time = theTime;}
        bool Update(const struct timeval&   currentTime,
                    unsigned short          seqNumber, 
                    bool                    ecn = false);
        double LossFraction();
        double MdpLossFraction() 
            {return ((loss_interval > 0.0) ? (1.0/loss_interval) : 0.0);}
        double TfrcLossFraction();
        bool NoLoss() {return no_loss;}
        void SetInitialLoss(double lossFraction) 
        {
            memset(history, 0, (DEPTH+1)*sizeof(unsigned int));
            history[1] = (unsigned int)((1.0 / lossFraction) + 0.5);
        }
        unsigned long CurrentLossInterval() {return history[0];}
        unsigned int LastLossInterval() {return history[1];}

    private:
        enum {DEPTH = 8};
    // Members  
        bool                init;
        unsigned long       lag_mask;
        unsigned int        lag_depth;
        unsigned long       lag_test_bit;
        unsigned short      lag_index;
        
        unsigned short      event_window;
        unsigned short      event_index;
        double              event_window_time;
        double              event_index_time;
        bool                seeking_loss_event;
        
        bool                no_loss;
        double              initial_loss;
        
        double              loss_interval;  // EWMA of loss event interval
        
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
            {return (!ack_received &&( req_count > 0));}
        void Reset(unsigned int maxAttempts = NORM_ROBUST_FACTOR)
        {
            ack_received = false;
            req_count = maxAttempts;   
        }
        void DecrementReqCount() {if (req_count > 0) req_count--;}
        unsigned int GetReqCount() const {return req_count;}
        bool AckReceived() const {return ack_received;}
        void MarkAckReceived() {ack_received = true;}
                
    private:
        bool            ack_received; // was ack received?
        unsigned int    req_count;    // remaining request attempts
        
};  // end NormAckingNode

class NormCCNode : public NormNode
{
    public:
       NormCCNode(class NormSession& theSession, NormNodeId nodeId);
       ~NormCCNode();
       
       bool IsClr() const {return is_clr;}
       bool IsActive() const {return is_active;}
       bool HasRtt() const {return rtt_confirmed;}
       
       double GetRtt() const {return rtt;}
       double GetLoss() const {return loss;}
       double GetRate() const {return rate;}
       UINT16 GetCCSequence() const {return cc_sequence;}
       
       void SetActive(bool state) {is_active = state;}
       void SetClrStatus(bool state) {is_clr = state;}
       void SetRttStatus(bool state) {rtt_confirmed = state;}
       void SetRtt(double value)  {rtt = value;}
       double UpdateRtt(double value)
       {
            rtt = 0.9*rtt + 0.1 * value;   
            return rtt;
       }
       void SetLoss(double value) {loss = value;}
       void SetRate(double value) {rate = value;}
       void SetCCSequence(UINT16 value) {cc_sequence = value;}
       
    private:
        bool    is_clr; // true if worst path representative
        bool    is_plr; // true if worst path candidate
        bool    is_active;
        bool    rtt_confirmed;
        double  rtt;    // in seconds
        double  loss;   // loss fraction
        double  rate;   // in bytes per second
        UINT16  cc_sequence;
};  // end class NormCCNode

class NormServerNode : public NormNode
{
    public:
        enum ObjectStatus {OBJ_INVALID, OBJ_NEW, OBJ_PENDING, OBJ_COMPLETE};
    
        enum RepairBoundary {BLOCK_BOUNDARY, OBJECT_BOUNDARY};
    
        NormServerNode(class NormSession& theSession, NormNodeId nodeId);  
        ~NormServerNode();
        
        // Parameters
        NormObject::NackingMode GetDefaultNackingMode() const 
            {return default_nacking_mode;}
        void SetDefaultNackingMode(NormObject::NackingMode nackingMode)
            {default_nacking_mode = nackingMode;}
        
        NormServerNode::RepairBoundary GetRepairBoundary() const 
            {return repair_boundary;}
        // (TBD) force an appropriate RepairCheck on boundary change???
        void SetRepairBoundary(RepairBoundary repairBoundary)
            {repair_boundary = repairBoundary;}
        
        bool UnicastNacks() {return unicast_nacks;}
        void SetUnicastNacks(bool state) {unicast_nacks = state;}
        
        bool UpdateLossEstimate(const struct timeval&   currentTime,
                                unsigned short          theSequence, 
                                bool                    ecnStatus = false);
        double LossEstimate() {return loss_estimator.LossFraction();}
        
        void UpdateRecvRate(const struct timeval& currentTime,
                            unsigned short        msgSize);
        
        void HandleCommand(const struct timeval& currentTime,
                           const NormCmdMsg&     cmd);
        
        void HandleObjectMessage(const NormObjectMsg& msg);
        void HandleCCFeedback(UINT8 ccFlags, double ccRate);
        void HandleNackMessage(const NormNackMsg& nack);
        void HandleAckMessage(const NormAckMsg& ack);
        
        bool Open(UINT16 sessionId);
        UINT16 GetSessionId() {return session_id;}
        bool IsOpen() const {return is_open;} 
        void Close();
        bool AllocateBuffers(UINT16 segmentSize, UINT16 numData, UINT16 numParity);
        bool BuffersAllocated() {return (0 != segment_size);}
        void FreeBuffers();
        void Activate();
               
        
        bool SyncTest(const NormObjectMsg& msg) const;
        void Sync(NormObjectId objectId);
        ObjectStatus UpdateSyncStatus(const NormObjectId& objectId);
        ObjectStatus GetObjectStatus(const NormObjectId& objectId) const;
        
        bool GetFirstPending(NormObjectId& objectId)
        {
            UINT32 index;
            bool result = rx_pending_mask.GetFirstSet(index);
            objectId = (UINT16)index;
            return result;   
        }
        bool GetNextPending(NormObjectId& objectId)
        {
            UINT32 index = (UINT16)objectId;
            bool result = rx_pending_mask.GetNextSet(index);
            objectId = (UINT16)index;
            return result;   
        }
        bool GetLastPending(NormObjectId& objectId)
        {
            UINT32 index;
            bool result = rx_pending_mask.GetLastSet(index);
            objectId = (UINT16)index;
            return result;   
        }
        void SetPending(NormObjectId objectId);
        
        void DeleteObject(NormObject* obj, int which);
        
        UINT16 SegmentSize() {return segment_size;}
        UINT16 BlockSize() {return ndata;}
        UINT16 NumParity() {return nparity;}
        
        NormBlock* GetFreeBlock(NormObjectId objectId, NormBlockId blockId);
        void PutFreeBlock(NormBlock* block)
        {
            block->EmptyToPool(segment_pool);
            block_pool.Put(block);   
        }
        char* GetFreeSegment(NormObjectId objectId, NormBlockId blockId);
        void PutFreeSegment(char* segment)
            {segment_pool.Put(segment);}
        
        void SetErasureLoc(UINT16 index, UINT16 value)
        {
            ASSERT(index < nparity);
            erasure_loc[index] = value;
        }
        UINT16 GetErasureLoc(UINT16 index) 
            {return erasure_loc[index];} 
        UINT16 Decode(char** segmentList, UINT16 numData, UINT16 erasureCount)
        {
            return decoder.Decode(segmentList, numData, erasureCount, erasure_loc);
        }
        
        void CalculateGrttResponse(const struct timeval& currentTime,
                                   struct timeval&       grttResponse) const;
        
        // Statistics kept on server
        unsigned long CurrentBufferUsage() const
            {return (segment_size * segment_pool.CurrentUsage());}
        unsigned long PeakBufferUsage() const
            {return (segment_size * segment_pool.PeakUsage());}
        unsigned long BufferOverunCount() const
            {return segment_pool.OverunCount() + block_pool.OverrunCount();}
        unsigned long RecvTotal() const {return recv_total;}
        unsigned long RecvGoodput() const {return recv_goodput;}
        void IncrementRecvTotal(unsigned long count) {recv_total += count;}
        void IncrementRecvGoodput(unsigned long count) {recv_goodput += count;}
        void ResetRecvStats() {recv_total = recv_goodput = 0;}
        void IncrementResyncCount() {resync_count++;}
        unsigned long ResyncCount() const {return resync_count;}
        unsigned long NackCount() const {return nack_count;}
        unsigned long SuppressCount() const {return suppress_count;}
        unsigned long CompletionCount() const {return completion_count;}
        unsigned long PendingCount() const {return rx_table.Count();}
        unsigned long FailureCount() const {return failure_count;}
        
        
    private:
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
        void HandleRepairContent(const char* buffer, UINT16 bufferLen);
            
        UINT16                  session_id;
        bool                    synchronized;
        NormObjectId            sync_id;  // only valid if(synchronized)
        NormObjectId            next_id;  // only valid if(synchronized)
        NormObjectId            max_pending_object; // index for NACK construction
        NormObjectId            current_object_id;  // index for suppression
        UINT16                  max_pending_range;  // max range of pending objs allowed
        
        bool                    is_open;
        UINT16                  segment_size;
        UINT16                  ndata;
        UINT16                  nparity;
        
        NormObjectTable         rx_table;
        NormSlidingMask         rx_pending_mask;
        NormSlidingMask         rx_repair_mask;
        RepairBoundary          repair_boundary;
        NormObject::NackingMode default_nacking_mode;
        bool                    unicast_nacks;
        NormBlockPool           block_pool;
        NormSegmentPool         segment_pool;
        NormDecoder             decoder;
        UINT16*                 erasure_loc;
        
        bool                    server_active;
        ProtoTimer              activity_timer;
        ProtoTimer              repair_timer;
        
        // Watermark acknowledgement
        ProtoTimer              ack_timer;
        NormObjectId            watermark_object_id;
        NormBlockId             watermark_block_id;
        NormSegmentId           watermark_segment_id;
        
        // Remote server grtt measurement state       
        double                  grtt_estimate;
        UINT8                   grtt_quantized;
        struct timeval          grtt_send_time;
        struct timeval          grtt_recv_time;
        double                  gsize_estimate;
        UINT8                   gsize_quantized;
        double                  backoff_factor;
        
        // Remote server congestion control state
        NormLossEstimator2      loss_estimator;
        UINT16                  cc_sequence;
        bool                    cc_enable;
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
        struct timeval          prev_update_time;  // for recv_rate measurement
        unsigned long           recv_accumulator;  // for recv_rate measurement
        double                  nominal_packet_size;
        
        // For statistics tracking
        unsigned long           recv_total;        // total recvd accumulator
        unsigned long           recv_goodput;      // goodput recvd accumulator
        unsigned long           resync_count;
        unsigned long           nack_count;
        unsigned long           suppress_count;
        unsigned long           completion_count;
        unsigned long           failure_count;     // usually due to re-syncs
        
};  // end class NormServerNode
    
    
    // Used for binary trees of NormNodes
class NormNodeTree
{    
    friend class NormNodeTreeIterator;
    
    public:
    // Methods
        NormNodeTree();
        ~NormNodeTree();
        NormNode* FindNodeById(NormNodeId nodeId) const;
        void DeleteNode(NormNode *theNode)
        {
            ASSERT(theNode);
            DetachNode(theNode);
            delete theNode;
        }
        void AttachNode(NormNode *theNode);
        void DetachNode(NormNode *theNode);   
        void Destroy();    // delete all nodes in tree
       
    private: 
    // Members
        NormNode* root;
};  // end class NormNodeTree

class NormNodeTreeIterator
{
    public:
        NormNodeTreeIterator(const NormNodeTree& nodeTree);
        void Reset();
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
            ASSERT(theNode);
            Remove(theNode);
            delete theNode;
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
#endif // NORM_NODE
