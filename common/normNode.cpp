
#include "normNode.h"
#include "normSession.h"

#include <errno.h>

NormNode::NormNode(class NormSession* theSession, NormNodeId nodeId)
 : session(theSession), id(nodeId),
   parent(NULL), right(NULL), left(NULL),
   prev(NULL), next(NULL)
{
    
}

NormNode::~NormNode()
{
}

const NormNodeId& NormNode::LocalNodeId() {return session->LocalNodeId();}


NormServerNode::NormServerNode(class NormSession* theSession, NormNodeId nodeId)
 : NormNode(theSession, nodeId), synchronized(false),
   is_open(false), segment_size(0), ndata(0), nparity(0), erasure_loc(NULL)
{
    repair_timer.Init(0.0, -1, (ProtocolTimerOwner*)this, 
                      (ProtocolTimeoutFunc)&NormServerNode::OnRepairTimeout);
}

NormServerNode::~NormServerNode()
{
    Close();
}

bool NormServerNode::Open(UINT16 segmentSize, UINT16 numData, UINT16 numParity)
{
    if (!rx_table.Init(256))
    {
        DMSG(0, "NormServerNode::Open() rx_table init error\n");
        Close();
        return false;
    }
    if (!rx_pending_mask.Init(256))
    {
        DMSG(0, "NormServerNode::Open() rx_pending_mask init error\n");
        Close();
        return false;
    }
    if (!rx_repair_mask.Init(256))
    {
        DMSG(0, "NormServerNode::Open() rx_repair_mask init error\n");
        Close();
        return false;
    }    
    // Calculate how much memory each buffered block will require
    UINT16 blockSize = numData + numParity;
    unsigned long maskSize = blockSize >> 3;
    if (0 != (blockSize & 0x07)) maskSize++;
    unsigned long blockSpace = sizeof(NormBlock) + 
                               blockSize * sizeof(char*) + 
                               2*maskSize  +
                               numData * (segmentSize + NormDataMsg::PayloadHeaderLen());  
    unsigned long bufferSpace = session->RemoteServerBufferSize();
    unsigned long numBlocks = bufferSpace / blockSpace;
    if (bufferSpace > (numBlocks*blockSpace)) numBlocks++;
    if (numBlocks < 2) numBlocks = 2;
    unsigned long numSegments = numBlocks * numData;
    
    if (!block_pool.Init(numBlocks, blockSize))
    {
        DMSG(0, "NormServerNode::Open() block_pool init error\n");
        Close();
        return false;
    }
    
    if (!segment_pool.Init(numSegments, segmentSize+NormDataMsg::PayloadHeaderLen()))
    {
        DMSG(0, "NormServerNode::Open() segment_pool init error\n");
        Close();
        return false;
    }
    
    if (!decoder.Init(numParity, segmentSize+NormDataMsg::PayloadHeaderLen()))
    {
        DMSG(0, "NormServerNode::Open() decoder init error: %s\n",
                 strerror(errno));
        Close();
        return false; 
    }
    if (!(erasure_loc = new UINT16[numParity]))
    {
        DMSG(0, "NormServerNode::Open() erasure_loc allocation error: %s\n",
                 strerror(errno));
        Close();
        return false;   
    }
    segment_size = segmentSize;
    ndata = numData;
    nparity = numParity;
    is_open= true;
    return true;
}  // end NormServerNode::Open()

void NormServerNode::Close()
{
    decoder.Destroy();
    if (erasure_loc)
    {
        delete []erasure_loc;
        erasure_loc = NULL;
    }
    NormObjectTable::Iterator iterator(rx_table);
    NormObject* obj;
    while ((obj = iterator.GetNextObject()))
    {
        // (TBD) Notify app of object closing
        obj->Close();
        delete obj;
    }
    rx_repair_mask.Destroy();
    rx_pending_mask.Destroy();
    rx_table.Destroy();
    segment_size = ndata = nparity = 0;    
    is_open = false;
}  // end NormServerNode::Close()

void NormServerNode::HandleObjectMessage(NormMessage& msg)
{
    if (IsOpen())
    {
        // (TBD - also verify encoder name ...)
        if ((msg.object.GetSegmentSize() != segment_size) &&
            (ndata != msg.object.GetFecBlockLen()) &&
            (nparity != msg.object.GetFecNumParity()))
        {
            DMSG(0, "NormServerNode::HandleObjectMessage() node:%lu remote server:%lu parameter change.\n",
                     LocalNodeId(), Id());
            Close();
            if (!Open(msg.object.GetSegmentSize(), msg.object.GetFecBlockLen(), 
                      msg.object.GetFecNumParity()))
            {
                DMSG(0, "NormServerNode::HandleObjectMessage() node:%lu remote server:%lu open error\n",
                     LocalNodeId(), Id());
                // (TBD) notify app of error ??
                return;
            }
        }
    }
    else
    {
        if (!Open(msg.object.GetSegmentSize(), 
                  msg.object.GetFecBlockLen(),
                  msg.object.GetFecNumParity()))
        {
            DMSG(0, "NormServerNode::HandleObjectMessage() node:%lu remote server:%lu open error\n",
                 LocalNodeId(), Id());
            // (TBD) notify app of error ??
            return;
        }       
    }
    
    NormObjectId objectId = msg.object.GetObjectId();
    ObjectStatus objectStatus = GetObjectStatus(objectId);
    if (synchronized)
    {
        if (OBJ_INVALID == objectStatus)
        {
            // (TBD) We may want to control re-sync policy options
            //       or at least revert to fresh sync if sync is totally lost.
            DMSG(0, "NormServerNode::HandleObjectMessage() re-syncing ...\n");
            Sync(objectId); 
            objectStatus = OBJ_NEW;  
        }
    }
    else
    {      
        // Does this object message meet our sync policy?
        if (SyncTest(msg))
        {
            Sync(objectId); 
        }
        else
        {
            DMSG(0, "NormServerNode::HandleObjectMessage() waiting to sync ...\n");
            return;   
        }
    }       
    NormObject* obj = NULL;
    switch (objectStatus)
    {
        case OBJ_NEW:
            SetPending(objectId);
            break;
            
        case OBJ_PENDING:
            obj = rx_table.Find(objectId);
            break;
            
        case OBJ_COMPLETE:
            return;
    }
    if (!obj)
    {
        if (msg.object.FlagIsSet(NormObjectMsg::FLAG_STREAM))
        {
            if (!(obj = new NormStreamObject(session, this, objectId)))
            {
                DMSG(0, "NormServerNode::HandleObjectMessage() new NORM_OBJECT_STREAM error\n");
                return;
            }
            
        }
        else if (msg.object.FlagIsSet(NormObjectMsg::FLAG_FILE))
        {
            DMSG(0, "NormServerNode::HandleObjectMessage() NORM_OBJECT_FILE not yet supported!\n");
            return;
        }
        else
        {
            DMSG(0, "NormServerNode::HandleObjectMessage() NORM_OBJECT_DATA not yet supported!\n");
            return;
        } 
        // Open receive object and notify app for accept.
        NormObjectSize objectSize = msg.object.GetObjectSize();
        if (!obj->Open(objectSize, msg.object.FlagIsSet(NormObjectMsg::FLAG_INFO)))
        {
            DMSG(0, "NormServerNode::HandleObjectMessage() node:%lu server:%lu "
                    "obj:%hu was not opened.\n", LocalNodeId(), Id(), (UINT16)objectId);
            delete obj;
            rx_pending_mask.Unset(objectId);
            return;   
        }
        
        session->Notify(NormController::RX_OBJECT_NEW, this, obj);
        
        if (!obj->Accepted())
        {
            delete obj;
            rx_pending_mask.Unset(objectId);
            return;    
        }
        
        rx_table.Insert(obj);
        DMSG(0, "NormServerNode::HandleObjectMessage() node:%lu server:%lu new obj:%hu\n", 
                LocalNodeId(), Id(), (UINT16)objectId);
    }
    obj->HandleObjectMessage(msg);
}  // end NormServerNode::HandleObjectMessage()

void NormServerNode::SetPending(NormObjectId objectId)
{
    ASSERT(synchronized);
    ASSERT(OBJ_NEW == GetObjectStatus(objectId));
    if (objectId < next_id)
    {
        rx_pending_mask.Set(objectId);
    }
    else
    {
        rx_pending_mask.SetBits(next_id, next_id - objectId + 1);
        next_id = objectId + 1; 
    }
}  // end NormServerNode::SetPending()


void NormServerNode::Sync(NormObjectId objectId)
{
    if (synchronized)
    {
        // Dump pending objects < objectId
        if (rx_pending_mask.IsSet() && (objectId > sync_id))
        {
            NormObjectTable::Iterator iterator(rx_table);
            NormObject* obj = iterator.GetNextObject();
            while (obj && (obj->Id() < objectId))
            {
                DeleteObject(obj);
                obj = iterator.GetNextObject();
            }
            rx_pending_mask.UnsetBits(sync_id, (objectId - sync_id));
        } 
        sync_id = objectId;
        if (objectId > next_id) next_id = objectId;      
    }
    else
    {
        ASSERT(!rx_pending_mask.IsSet());
        sync_id = next_id = objectId;
        synchronized = true;   
    }    
}  // end NormServerNode::Sync()


bool NormServerNode::SyncTest(const NormMessage& msg) const
{
    // (TBD) Additional sync policies
    
    // Sync if non-repair and (INFO or block zero message)
    bool result = !msg.object.FlagIsSet(NormObjectMsg::FLAG_REPAIR) &&
                  (NORM_MSG_INFO == msg.generic.GetType()) ? 
                        true : (0 == msg.data.GetFecBlockId());
    return result;
    
}  // end NormServerNode::SyncTest()

void NormServerNode::DeleteObject(NormObject* obj)
{
    rx_table.Remove(obj);
    delete obj;
}  // end NormServerNode::DeleteObject()

NormBlock* NormServerNode::GetFreeBlock(NormObjectId objectId, NormBlockId blockId)
{
    NormBlock* b = block_pool.Get();
    if (!b)
    {
        // reverse iteration to find newest object with resources
        NormObjectTable::Iterator iterator(rx_table);
        NormObject* obj;
        while ((obj = iterator.GetPrevObject()))
        {
            if (obj->Id() < objectId) 
            {
                break;
            }
            else
            {
                if (obj->Id() > objectId)
                    b = obj->StealNewestBlock(false); 
                else 
                    b = obj->StealNewestBlock(true, blockId);
                if (b) 
                {
                    b->EmptyToPool(segment_pool);
                    break;
                }
            }
        }
    }  
    return b;
}  // end NormServerNode::GetFreeBlock()

char* NormServerNode::GetFreeSegment(NormObjectId objectId, NormBlockId blockId)
{
    char* ptr = segment_pool.Get();
    while (!ptr)
    {
        NormBlock* b = GetFreeBlock(objectId, blockId);
        if (b)
            ptr = segment_pool.Get();
        else
            break;
    }
    return ptr;
}  // end NormServerNode::GetFreeSegment()

NormServerNode::ObjectStatus NormServerNode::GetObjectStatus(NormObjectId objectId) const
{
   if (synchronized)
   {
       if (objectId < sync_id) 
       {
           return OBJ_INVALID;  
       }
       else
       {
            if (objectId < next_id)
            {
                if (rx_pending_mask.Test(objectId))
                    return OBJ_PENDING;
                else
                    return OBJ_COMPLETE;
            }
            else
            {
                if (rx_pending_mask.IsSet())
                {
                    if (rx_pending_mask.CanSet(objectId))
                        return OBJ_NEW;
                    else
                        return OBJ_INVALID;
                }
                else
                {
                    NormObjectId delta = objectId - next_id + 1;
                    if (delta > NormObjectId(rx_pending_mask.Size()))
                        return OBJ_INVALID;
                    else
                        return OBJ_NEW;
                }
            }  
        }
   }
   else
   {
        return OBJ_NEW;   
   } 
}  // end NormServerNode::ObjectStatus()

// (TBD) mod repair check to do full server flush?
void NormServerNode::RepairCheck(NormObject::CheckLevel checkLevel,
                                 NormObjectId           objectId,  
                                 NormBlockId            blockId,
                                 NormSegmentId          segmentId)
{
    ASSERT(synchronized);
    if (!repair_timer.IsActive())
    {
        bool startTimer = false;
        if (rx_pending_mask.IsSet())
        {
            if (rx_repair_mask.IsSet()) rx_repair_mask.Clear();
            NormObjectId nextId = rx_pending_mask.FirstSet();
            NormObjectId lastId = rx_pending_mask.LastSet();
            if (objectId < lastId) lastId = objectId;
            while (nextId <= lastId)
            {
                NormObject* obj = rx_table.Find(nextId);
                if (obj)
                {
                    NormObject::CheckLevel level = 
                        (nextId == lastId) ? checkLevel : NormObject::THRU_OBJECT;
                    startTimer |= 
                        obj->ClientRepairCheck(level, blockId, segmentId, false);
                }
                else
                {
                    startTimer = true;
                }
                nextId++;
                nextId = rx_pending_mask.NextSet(nextId);
            }
            current_object_id = objectId;
            if (startTimer)
            {
                DMSG(0, "NormServerNode::RepairCheck() starting NACK back-off ...\n");   
            }
        }
        else
        {
            // No repairs needed
            return;
        } 
    }
    else if (repair_timer.RepeatCount())
    {
        // Repair timer in back-off phase
        // Trim server current transmit position reference
        if (objectId < current_object_id)
            current_object_id = objectId;
    }
}  // end NormServerNode::RepairCheck()

// When repair timer fires, possibly build a NACK
// and queue for transmission to this server node
bool NormServerNode::OnRepairTimeout()
{
    DMSG(0, "NormServerNode::OnRepairTimeout() ...\n");
    switch(repair_timer.RepeatCount())
    {
        case 0:  // hold-off time complete
            break;
            
        case 1:  // back-off timeout complete
        {
            // 1) Were we suppressed? 
            if (rx_pending_mask.IsSet())
            {
                bool repair_pending = false;
                NormObjectId nextId = rx_pending_mask.FirstSet();
                NormObjectId lastId = rx_pending_mask.LastSet();
                if (current_object_id < lastId) lastId = current_object_id;
                while (nextId <= lastId)
                {
                    if (!rx_repair_mask.Test(nextId)) 
                    {
                        NormObject* obj = rx_table.Find(nextId);
                        if (!obj || obj->IsRepairPending(nextId != current_object_id))
                        {
                            repair_pending = true;
                            break;
                        }                        
                    }
                    nextId++;
                    nextId = rx_pending_mask.NextSet(nextId);
                } // end while (nextId <= current_block_id)
                if (repair_pending)
                {
                    // Build NACK
                    NormMessage msg;
                    NormNackMsg& nack = msg.nack;
                    nack.ResetNackContent();
                    NormRepairRequest req;
                    NormObjectId prevId;
                    UINT16 reqCount = 0;
                    NormRepairRequest::Form prevForm = NormRepairRequest::INVALID;
                    nextId = rx_pending_mask.FirstSet();
                    lastId = rx_pending_mask.LastSet();
                    if (current_object_id < lastId) lastId = current_object_id;
                    lastId++;  // force loop to fully flush nack building.
                    while (nextId <= lastId)
                    {
                        NormObject* obj = NULL;
                        if (nextId == lastId)
                            nextId++;  // force break of possible ending consecutive series
                        else
                            obj = rx_table.Find(nextId);
                        if (obj)
                        {
                            if (obj->IsPending(nextId != current_object_id))
                            {
                                if (NormRepairRequest::INVALID != prevForm)
                                {
                                    nack.PackRepairRequest(req);
                                    prevForm = NormRepairRequest::INVALID;   
                                }
                                obj->AppendRepairRequest(nack);  // (TBD) error check
                                reqCount = 0; 
                            }
                        }
                        else
                        {
                            if (reqCount && (reqCount == (nextId - prevId)))
                            {
                                // Consecutive series of missing objects continues
                                reqCount++;    
                            }
                            else
                            {
                                NormRepairRequest::Form nextForm;
                                switch (reqCount)
                                {
                                    case 0:
                                        nextForm = NormRepairRequest::INVALID;
                                        break;
                                    case 1:
                                    case 2:
                                        nextForm = NormRepairRequest::ITEMS;
                                        break;
                                    default:
                                        nextForm = NormRepairRequest::RANGES;
                                        break;
                                }    
                                if (prevForm != nextForm)
                                {
                                    if (NormRepairRequest::INVALID != prevForm)
                                        nack.PackRepairRequest(req); // (TBD) error check
                                    if (NormRepairRequest::INVALID != nextForm)
                                    {
                                        nack.AttachRepairRequest(req, segment_size); // (TBD) error check
                                        req.SetForm(nextForm);
                                        req.SetFlag(NormRepairRequest::OBJECT);
                                    }
                                    prevForm = nextForm;
                                }
                                switch (nextForm)
                                {
                                    case NormRepairRequest::ITEMS:
                                        req.AppendRepairItem(prevId, 0, 0);
                                        if (2 == reqCount)
                                            req.AppendRepairItem(prevId+1, 0, 0);
                                        break;
                                    case NormRepairRequest::RANGES:
                                        req.AppendRepairItem(prevId, 0, 0);
                                        req.AppendRepairItem(prevId+reqCount-1, 0, 0);
                                    default:
                                        break;  
                                }
                                prevId = nextId;
                                reqCount = 1;
                            }
                        }
                        nextId++;
                        if (nextId <= lastId)
                            nextId = rx_pending_mask.NextSet(nextId);
                    }  // end while (nextId <= lastId)
                    
                    // (TBD) Queue NACK for transmission
                    DMSG(0, "NormServerNode::OnRepairTimeout() NACK TRANSMITTED ...\n");
                }
                else
                {
                    DMSG(0, "NormServerNode::OnRepairTimeout() NACK SUPPRESSED ...\n");
                    // (TBD) repair_timer.SetInterval(HOLD_OFF_INTERVAL)   
                }
            }
            else
            {
                DMSG(0, "NormServerNode::OnRepairTimeout() nothing pending ...\n");
                // (TBD) cancel hold-off timeout ???  
            }  // end if/else (repair_pending)       
        }
        break;
        
        default: // should never occur
            ASSERT(0);
            break;
    }
    return true;
}  // end NormServerNode::OnRepairTimeout()

  


NormNodeTree::NormNodeTree()
 : root(NULL)
{

}

NormNodeTree::~NormNodeTree()
{
    Destroy();
}


NormNode *NormNodeTree::FindNodeById(unsigned long nodeId) const
{
    NormNode* x = root;
    while(x && (x->id != nodeId))
    {
		if (nodeId < x->id)
            x = x->left;
        else
            x = x->right;
    }
    return x;   
}  // end NormNodeTree::FindNodeById() 



void NormNodeTree::AttachNode(NormNode *node)
{
    ASSERT(node);
    node->left = NULL;
    node->right = NULL;
    NormNode *x = root;
    while (x)
    {
        if (node->id < x->id)
        {
            if (!x->left)
            {
                x->left = node;
                node->parent = x;
                return;
            }
            else
            {
                x = x->left;
            }
        }
        else
        {
           if (!x->right)
           {
               x->right = node;
               node->parent = x;
               return;
           }
           else
           {
               x = x->right;
           }   
        }
    }
    root = node;  // root _was_ NULL
}  // end NormNodeTree::AddNode()


void NormNodeTree::DetachNode(NormNode* node)
{
    ASSERT(node);
    NormNode* x;
    NormNode* y;
    if (!node->left || !node->right)
    {
        y = node;
    }
    else
    {
        if (node->right)
        {
            y = node->right;
            while (y->left) y = y->left;
        }
        else
        {
            x = node;
            y = node->parent;
            while(y && (y->right == x))
            {
                x = y;
                y = y->parent;
            }
        }
    }
    if (y->left)
        x = y->left;
    else
        x = y->right;
    if (x) x->parent = y->parent;
    if (!y->parent)
        root = x;
    else if (y == y->parent->left)
        y->parent->left = x;
    else
        y->parent->right = x;
    
    if (node != y)
    {
        if ((y->parent = node->parent))
        {
            if (y->id < y->parent->id)
                y->parent->left = y;
            else
                y->parent->right = y;
        }
        else
        {
            root = y;
        }
        if ((y->left = node->left)) y->left->parent = y;
        if ((y->right = node->right)) y->right->parent = y;
    }         
}  // end NormNodeTree::DetachNode()


void NormNodeTree::Destroy()
{
    NormNode* n;
    while ((n = root)) 
    {
        DetachNode(n);
        delete n;
    }
}  // end NormNodeTree::Destroy()

NormNodeTreeIterator::NormNodeTreeIterator(const NormNodeTree& t)
 : tree(t)
{
    NormNode* x = t.root;
    if (x)
    {
        while (x->left) x = x->left;
        next = x;
    }
}  

void NormNodeTreeIterator::Reset()
{
    NormNode* x = tree.root;
    if (x)
    {
        while (x->left) x = x->left;
        next = x;
    }
}  // end NormNodeTreeIterator::Reset()

NormNode* NormNodeTreeIterator::GetNextNode()
{
    NormNode* n = next;
    if (n)
    {
        if (next->right)
        {
            NormNode* y = n->right;
            while (y->left) y = y->left;
            next = y;
        }
        else
        {
            NormNode* x = n;
            NormNode* y = n->parent;
            while(y && (y->right == x))
            {
                x = y;
                y = y->parent;
            }
            next = y;
        }
    }
    return n;
}  // end NormNodeTreeIterator::GetNextNode()

NormNodeList::NormNodeList()
    : head(NULL), tail(NULL)
{
}

NormNodeList::~NormNodeList()
{
    Destroy();
}

NormNode* NormNodeList::FindNodeById(NormNodeId nodeId) const
{
    NormNode *next = head;
    while (next)
    {
        if (nodeId == next->id)
            return next;
        else
            next = next->right;
    }
    return NULL;
}  // NormNodeList::Find()

void NormNodeList::Append(NormNode *theNode)
{
    ASSERT(theNode);
    theNode->left = tail;
    if (tail)
        tail->right = theNode;
    else
        head = theNode;
    tail = theNode;
    theNode->right = NULL;
}  // end NormNodeList::Append()

void NormNodeList::Remove(NormNode *theNode)
{
    ASSERT(theNode);
    if (theNode->right)
        theNode->right->left = theNode->left;
    else
        tail = theNode->left;
    if (theNode->left)
        theNode->left->right = theNode->right;
    else
        head = theNode->right;
}  // end NormNodeList::Remove()

void NormNodeList::Destroy()
{
    NormNode* n;
    while ((n = head))
    {
        Remove(n);
        delete n;
    }   
}  // end NormNodeList::Destroy()

