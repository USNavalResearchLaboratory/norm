#ifndef _NORM_NODE
#define _NORM_NODE

#include "normMessage.h"
#include "normObject.h"
#include "normEncoder.h"
#include "protocolTimer.h"
class NormNode
{
    friend class NormNodeTree;
    friend class NormNodeTreeIterator;
    friend class NormNodeList;
    friend class NormNodeListIterator;
    
    public:
        NormNode(class NormSession* theSession, NormNodeId nodeId);
        virtual ~NormNode();
        
        void SetAddress(const NetworkAddress& address)
            {addr = address;}
        const NetworkAddress Address() const {return addr;} 
        
        const NormNodeId& Id() {return id;}
        inline const NormNodeId& LocalNodeId(); 
    
    protected:
        class NormSession*    session;
        
    private:
        NormNodeId            id;
        NetworkAddress        addr;
        
        NormNode*             parent;
        NormNode*             right;
        NormNode*             left;
        NormNode*             prev;
        NormNode*             next;
        
};  // end class NormNode

class NormServerNode : public NormNode
{
    public:
        enum ObjectStatus {OBJ_INVALID, OBJ_NEW, OBJ_PENDING, OBJ_COMPLETE};
    
        NormServerNode(class NormSession* theSession, NormNodeId nodeId);  
        ~NormServerNode();
        
        void HandleCommand(NormCommandMsg& cmd);
        void HandleObjectMessage(NormMessage& msg);
        void HandleNackMessage(NormNackMsg& nack);
                
        bool Open(UINT16 segmentSize, UINT16 numData, UINT16 numParity);
        void Close();
        bool IsOpen() const {return is_open;}        
        
        bool SyncTest(const NormMessage& msg) const;
        void Sync(NormObjectId objectId);
        ObjectStatus UpdateSyncStatus(const NormObjectId& objectId);
        void SetPending(NormObjectId objectId);
        ObjectStatus GetObjectStatus(const NormObjectId& objectId) const;
        
        void DeleteObject(NormObject* obj);
        
        UINT16 SegmentSize() {return segment_size;}
        UINT16 BlockSize() {return ndata;}
        UINT16 NumParity() {return nparity;}
        //NormBlockPool* BlockPool() {return &block_pool;}
        //NormSegmentPool* SegmentPool() {return &segment_pool;}
        
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
        
        void CalculateGrttResponse(struct timeval& grttResponse);
        
        unsigned long CurrentBufferUsage()
            {return (segment_size * segment_pool.CurrentUsage());}
        unsigned long PeakBufferUsage()
            {return (segment_size * segment_pool.PeakUsage());}
        unsigned long BufferOverunCount()
            {return segment_pool.OverunCount() + block_pool.OverrunCount();}
        
        unsigned long RecvTotal() {return recv_total;}
        unsigned long RecvGoodput() {return recv_goodput;}
        void IncrementRecvTotal(unsigned long count) {recv_total += count;}
        void IncrementRecvGoodput(unsigned long count) {recv_goodput += count;}
        void ResetRecvStats() {recv_total = recv_goodput = 0;}
        void IncrementResyncCount() {resync_count++;}
        unsigned long ResyncCount() {return resync_count;}
        unsigned long NackCount() {return nack_count;}
        unsigned long SuppressCount() {return suppress_count;}
        unsigned long CompletionCount() {return completion_count;}
        unsigned long PendingCount() {return rx_table.Count();}
        unsigned long FailureCount() {return failure_count;}
        
    private:
        void RepairCheck(NormObject::CheckLevel checkLevel,
                         NormObjectId           objectId,  
                         NormBlockId            blockId,
                         NormSegmentId          segmentId);
    
        bool OnRepairTimeout();
            
            
        bool                synchronized;
        NormObjectId        sync_id;  // only valid if(synchronized)
        NormObjectId        next_id;  // only valid if(synchronized)
        
        bool                is_open;
        UINT16              segment_size;
        UINT16              ndata;
        UINT16              nparity;
        
        NormObjectTable     rx_table;
        NormSlidingMask     rx_pending_mask;
        NormSlidingMask     rx_repair_mask;
        NormBlockPool       block_pool;
        NormSegmentPool     segment_pool;
        NormDecoder         decoder;
        UINT16*             erasure_loc;
        
        ProtocolTimer       repair_timer;
        NormObjectId        current_object_id; // index for repair
                
        double              grtt_estimate;
        UINT8               grtt_quantized;
        struct timeval      grtt_send_time;
        struct timeval      grtt_recv_time;
        double              gsize_estimate;
        UINT8               gsize_quantized;
        
        // For statistics tracking
        unsigned long       recv_total;   // total recvd accumulator
        unsigned long       recv_goodput; // goodput recvd accumulator
        unsigned long       resync_count;
        unsigned long       nack_count;
        unsigned long       suppress_count;
        unsigned long       completion_count;
        unsigned long       failure_count;  // due to re-syncs
        
};  // end class NodeServerNode
    
    
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
        
        
        
    // Members
    private:
        NormNode*                    head;
        NormNode*                    tail;
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
            next = n ? n->next : NULL;
            return n;   
        }
    private:
        const NormNodeList& list;
        NormNode*           next;
};  // end class NormNodeListIterator
#endif // NORM_NODE
