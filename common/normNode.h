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
        
        void HandleObjectMessage(NormMessage& msg);
        
        bool Open(UINT16 segmentSize, UINT16 numData, UINT16 numParity);
        void Close();
        bool IsOpen() const {return is_open;}        
        
        bool SyncTest(const NormMessage& msg) const;
        void Sync(NormObjectId objectId);
        ObjectStatus GetObjectStatus(NormObjectId objectId) const;
        void SetPending(NormObjectId objectId);
        
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
            ASSERT(index < (nparity));
            erasure_loc[index] = value;
        }
        UINT16 GetErasureLoc(UINT16 index) 
            {return erasure_loc[index];} 
        UINT16 Decode(char** segmentList, UINT16 numData, UINT16 erasureCount)
        {
            return decoder.Decode(segmentList, numData, erasureCount, erasure_loc);   
        }
        

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
