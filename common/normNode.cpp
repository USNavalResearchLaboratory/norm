
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
   is_open(false), segment_size(0), ndata(0), nparity(0), erasure_loc(NULL),
   recv_total(0), recv_goodput(0), resync_count(0),
   nack_count(0), suppress_count(0), completion_count(0), failure_count(0)
{
    repair_timer.Init(0.0, 1, (ProtocolTimerOwner*)this, 
                      (ProtocolTimeoutFunc)&NormServerNode::OnRepairTimeout);
    grtt_send_time.tv_sec = 0;
    grtt_send_time.tv_usec = 0;
    grtt_quantized = NormQuantizeRtt(NormSession::DEFAULT_GRTT_ESTIMATE);
    grtt_estimate = NormUnquantizeRtt(grtt_quantized);
    gsize_quantized = NormQuantizeGroupSize(NormSession::DEFAULT_GSIZE_ESTIMATE);
    gsize_estimate = NormUnquantizeGroupSize(gsize_quantized);
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
    NormObject* obj;
    while ((obj = rx_table.Find(rx_table.RangeLo()))) DeleteObject(obj);
    segment_pool.Destroy();
    block_pool.Destroy();
    rx_repair_mask.Destroy();
    rx_pending_mask.Destroy();
    rx_table.Destroy();
    segment_size = ndata = nparity = 0;    
    is_open = false;
}  // end NormServerNode::Close()


void NormServerNode::HandleCommand(NormCommandMsg& cmd)
{
    UINT8 grttQuantized = cmd.generic.GetGrtt();
    if (grttQuantized != grtt_quantized)
    {
        grtt_quantized = grttQuantized;
        grtt_estimate = NormUnquantizeRtt(grttQuantized);
        DMSG(4, "NormServerNode::HandleCommand() node>%lu server>%lu new grtt: %lf sec\n",
                LocalNodeId(), Id(), grtt_estimate);
    }
    UINT8 gsizeQuantized = cmd.generic.GetGroupSize();
    if (gsizeQuantized != gsize_quantized)
    {
        gsize_quantized = gsizeQuantized;
        gsize_estimate = NormUnquantizeGroupSize(gsizeQuantized);
        DMSG(4, "NormServerNode::HandleCommand() node>%lu server>%lu new group size: %lf\n",
                LocalNodeId(), Id(), gsize_estimate);
    }
    
    NormCmdMsg::Flavor flavor = cmd.generic.GetFlavor();
    switch (flavor)
    {
        case NormCmdMsg::NORM_CMD_SQUELCH:
            if (IsOpen())
            {
                // 1) Sync to squelch
                NormObjectId objectId = cmd.squelch.GetObjectId();
                Sync(objectId);
                // 2) Prune stream object if applicable
                NormObject* obj = rx_table.Find(objectId);
                if (obj && (NormObject::STREAM == obj->GetType()))
                {
                    NormBlockId blockId = cmd.squelch.GetFecBlockId();
                    ((NormStreamObject*)obj)->Prune(blockId);   
                }
                // 3) (TBD) Discard any invalidated objects 
            }
            break;
            
        case NormCmdMsg::NORM_CMD_ACK_REQ:
            GetSystemTime(&grtt_recv_time);
            cmd.ack_req.GetRttSendTime(grtt_send_time);
            switch(cmd.ack_req.GetAckFlavor())
            {
                case NormCmdAckReqMsg::RTT:
                    break;
                default:
                    break;
            }
            break;
            
        case NormCmdMsg::NORM_CMD_FLUSH:
            if (synchronized) UpdateSyncStatus(cmd.flush.GetObjectId());
            RepairCheck(NormObject::THRU_SEGMENT, 
                        cmd.flush.GetObjectId(), 
                        cmd.flush.GetFecBlockId(), 
                        cmd.flush.GetFecSymbolId());
            break;
            
        default:
            DMSG(0, "NormServerNode::HandleCommand() recv'd unimplemented command!\n");
            break;
    }  // end switch(flavor)
    
}  // end NormServerNode::HandleCommand()

void NormServerNode::HandleNackMessage(NormNackMsg& nack)
{
    // Clients only care about recvd NACKS for suppression
    if (repair_timer.IsActive() && repair_timer.RepeatCount())
    {
        // Parse NACK and incorporate into repair state masks
        NormRepairRequest req;
        UINT16 requestOffset = 0;
        UINT16 requestLength = 0;
        bool freshObject = true;
        NormObjectId prevObjectId;
        NormObject* object = NULL;
        bool freshBlock = true;
        NormBlockId prevBlockId;
        NormBlock* block = NULL;
        while ((requestLength = nack.UnpackRepairRequest(req, requestOffset)))
        {
            enum NormRequestLevel {SEGMENT, BLOCK, INFO, OBJECT};
            NormRepairRequest::Form requestForm = req.GetForm();
            requestOffset += requestLength;
            NormRequestLevel requestLevel;
            if (req.FlagIsSet(NormRepairRequest::SEGMENT))
                requestLevel = SEGMENT;
            else if (req.FlagIsSet(NormRepairRequest::BLOCK))
                requestLevel = BLOCK;
            else if (req.FlagIsSet(NormRepairRequest::OBJECT))
                requestLevel = OBJECT;
            else
            {
                requestLevel = INFO;
                ASSERT(req.FlagIsSet(NormRepairRequest::INFO));
            }
            bool repairInfo = req.FlagIsSet(NormRepairRequest::INFO);
            
            NormRepairRequest::Iterator iterator(req);
            NormObjectId nextObjectId, lastObjectId;
            NormBlockId nextBlockId, lastBlockId;
            NormSegmentId nextSegmentId, lastSegmentId;
            while (iterator.NextRepairItem(&nextObjectId, &nextBlockId, &nextSegmentId))
            {
                if (NormRepairRequest::RANGES == requestForm)
                {
                    if (!iterator.NextRepairItem(&lastObjectId, &lastBlockId, &lastSegmentId))
                    {
                        DMSG(0, "NormSession::ServerHandleNackMessage() node>%lu recvd incomplete RANGE request!\n",
                                LocalNodeId());
                        continue;  // (TBD) break/return instead???  
                    }  
                    // (TBD) test for valid range form/level
                }
                else
                {
                    lastObjectId = nextObjectId;
                    lastBlockId = nextBlockId;
                    lastSegmentId = nextSegmentId;
                }
                switch(requestLevel)
                {
                    case INFO:
                    {
                        while (nextObjectId <= lastObjectId)
                        {
                            NormObject* obj = rx_table.Find(nextObjectId);
                            if (obj) obj->SetRepairInfo();  
                            nextObjectId++;
                        }
                        break;
                    }
                    case OBJECT:
                        rx_repair_mask.SetBits(nextObjectId, lastObjectId - nextObjectId + 1);
                        break;
                    case BLOCK:
                    {
                        if (nextObjectId != prevObjectId) freshObject = true;
                        if (freshObject) 
                        {
                            object = rx_table.Find(nextObjectId);
                            prevObjectId = nextObjectId;
                        }
                        if (object) 
                        {
                            if (repairInfo) object->SetRepairInfo();
                            object->SetRepairs(nextBlockId, lastBlockId); 
                        } 
                        break;
                    }
                    case SEGMENT:
                    {
                        if (nextObjectId != prevObjectId) freshObject = true;
                        if (freshObject) 
                        {
                            object = rx_table.Find(nextObjectId);
                            prevObjectId = nextObjectId;
                        }
                        if (object)
                        {
                            if (repairInfo) object->SetRepairInfo(); 
                            if (nextBlockId != prevBlockId) freshBlock = true;
                            if (freshBlock)
                            {
                                block = object->FindBlock(nextBlockId);
                                prevBlockId = nextBlockId;
                            }
                            if (block) block->SetRepairs(nextSegmentId,lastSegmentId);
                        }
                        break;
                    }
                }  // end switch(requestLevel)
            }  // end while (iterator.NextRepairItem())
        }  // end while (nack.UnpackRepairRequest())
    }  // end if (repair_timer.IsActive() && repair_timer.RepeatCount())
}  // end NormServerNode::HandleNackMessage()


void NormServerNode::CalculateGrttResponse(struct timeval& grttResponse)
{
    grttResponse.tv_sec = grttResponse.tv_usec = 0;
    if (grtt_send_time.tv_sec || grtt_send_time.tv_usec)
    {
        // 1st - Get current time
        ::GetSystemTime(&grttResponse);    
        // 2nd - Calculate hold_time (current_time - recv_time)
        if (grttResponse.tv_usec < grtt_recv_time.tv_usec)
        {
            grttResponse.tv_sec = grttResponse.tv_sec - grtt_recv_time.tv_sec - 1;
            grttResponse.tv_usec = 1000000 - (grtt_recv_time.tv_usec - 
                                              grttResponse.tv_usec);
        }
        else
        {
            grttResponse.tv_sec = grttResponse.tv_sec - grtt_recv_time.tv_sec;
            grttResponse.tv_usec = grttResponse.tv_usec - grtt_recv_time.tv_usec;
        }
        // 3rd - Calculate adjusted grtt_send_time (hold_time + send_time)
        grttResponse.tv_sec += grtt_send_time.tv_sec;
        grttResponse.tv_usec += grtt_send_time.tv_usec;
        if (grttResponse.tv_usec > 1000000)
        {
            grttResponse.tv_usec -= 1000000;
            grttResponse.tv_sec += 1;
        }    
    }
}  // end NormServerNode::CalculateGrttResponse()

void NormServerNode::DeleteObject(NormObject* obj)
{
    // (TBD) Notify app of object's closing/demise?
    obj->Close();
    rx_table.Remove(obj);
    rx_pending_mask.Unset(obj->Id());
    delete obj;
}  // end NormServerNode::DeleteObject()

NormBlock* NormServerNode::GetFreeBlock(NormObjectId objectId, NormBlockId blockId)
{
    NormBlock* b = block_pool.Get();
    if (!b)
    {
        // reverse iteration to find newer object with resources
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
    while (segment_pool.IsEmpty())
    {
        NormBlock* b = GetFreeBlock(objectId, blockId);
        if (b)
            block_pool.Put(b);
        else
            break;
    }
    return segment_pool.Get();
}  // end NormServerNode::GetFreeSegment()

void NormServerNode::HandleObjectMessage(NormMessage& msg)
{
    UINT8 grttQuantized = msg.object.GetGrtt();
    if (grttQuantized != grtt_quantized)
    {
        grtt_quantized = grttQuantized;
        grtt_estimate = NormUnquantizeRtt(grttQuantized);
        DMSG(4, "NormServerNode::HandleCommand() node>%lu server>%lu new grtt: %lf sec\n",
                LocalNodeId(), Id(), grtt_estimate);
    }
    UINT8 gsizeQuantized = msg.object.GetGroupSize();
    if (gsizeQuantized != gsize_quantized)
    {
        gsize_quantized = gsizeQuantized;
        gsize_estimate = NormUnquantizeGroupSize(gsizeQuantized);
        DMSG(4, "NormServerNode::HandleCommand() node>%lu server>%lu new group size: %lf\n",
                LocalNodeId(), Id(), gsize_estimate);
    }
    if (IsOpen())
    {
        // (TBD - also verify encoder name ...)
        if ((msg.object.GetSegmentSize() != segment_size) &&
            (ndata != msg.object.GetFecBlockLen()) &&
            (nparity != msg.object.GetFecNumParity()))
        {
            DMSG(2, "NormServerNode::HandleObjectMessage() node>%lu server>%lu parameter change - resyncing.\n",
                     LocalNodeId(), Id());
            Close();
            if (!Open(msg.object.GetSegmentSize(), 
                      msg.object.GetFecBlockLen(), 
                      msg.object.GetFecNumParity()))
            {
                DMSG(0, "NormServerNode::HandleObjectMessage() node>%lu server>%lu open error\n",
                        LocalNodeId(), Id());
                // (TBD) notify app of error ??
                return;
            }
            resync_count++;
        }
    }
    else
    {
        if (!Open(msg.object.GetSegmentSize(), 
                  msg.object.GetFecBlockLen(),
                  msg.object.GetFecNumParity()))
        {
            DMSG(0, "NormServerNode::HandleObjectMessage() node>%lu server>%lu open error\n",
                    LocalNodeId(), Id());
            // (TBD) notify app of error ??
            return;
        }       
    }
    NormMsgType msgType = msg.generic.GetType();
    NormObjectId objectId = msg.object.GetObjectId();
    NormBlockId blockId;
    NormSegmentId segmentId;
    if (NORM_MSG_INFO == msgType)
    {
        blockId = 0;
        segmentId = 0;
    }
    else
    {
        blockId = msg.data.GetFecBlockId();
        segmentId = msg.data.GetFecSymbolId();   
    }
    
    ObjectStatus status;
    if (synchronized)
    {
        status = UpdateSyncStatus(objectId);
    }
    else
    {      
        // Does this object message meet our sync policy?
        if (SyncTest(msg))
        {
            Sync(objectId);
            SetPending(objectId); 
            status = OBJ_NEW;
        }
        else
        {
            DMSG(0, "NormServerNode::HandleObjectMessage() waiting to sync ...\n");
            return;   
        }
    }    
       
    NormObject* obj = NULL;
    switch (status)
    {
        case OBJ_PENDING:
            if ((obj = rx_table.Find(objectId))) break;
        case OBJ_NEW:
        {
            if (msg.object.FlagIsSet(NormObjectMsg::FLAG_STREAM))
            {
                if (!(obj = new NormStreamObject(session, this, objectId)))
                {
                    DMSG(0, "NormServerNode::HandleObjectMessage() new NORM_OBJECT_STREAM error: %s\n",
                            strerror(errno));
                }
            }
            else if (msg.object.FlagIsSet(NormObjectMsg::FLAG_FILE))
            {
#ifdef SIMULATE
                if (!(obj = new NormSimObject(session, this, objectId)))
#else
                if (!(obj = new NormFileObject(session, this, objectId)))
#endif
                {
                    DMSG(0, "NormServerNode::HandleObjectMessage() new NORM_OBJECT_FILE error: %s\n",
                            strerror(errno));
                }
            }
            else
            {
                obj = NULL;
                DMSG(0, "NormServerNode::HandleObjectMessage() NORM_OBJECT_DATA not yet supported!\n");
            }
            
            if (obj)
            { 
                // Open receive object and notify app for accept.
                NormObjectSize objectSize = msg.object.GetObjectSize();
                if (obj->Open(objectSize, msg.object.FlagIsSet(NormObjectMsg::FLAG_INFO)))
                {
                    session->Notify(NormController::RX_OBJECT_NEW, this, obj);
                    if (obj->Accepted())
                    {
                        rx_table.Insert(obj);
                        DMSG(8, "NormServerNode::HandleObjectMessage() node>%lu server>%lu new obj>%hu\n", 
                            LocalNodeId(), Id(), (UINT16)objectId);
                    }
                    else
                    {
                        DeleteObject(obj);
                        obj = NULL;    
                    }
                }
                else        
                {
                    DeleteObject(obj);
                    obj = NULL;   
                }
            }
            break;
        }
        case OBJ_COMPLETE:
            obj = NULL;
            break;
        default:
            ASSERT(0);
            break;
    }  // end switch(status)   
    
    if (obj)
    {
        obj->HandleObjectMessage(msg, msgType, blockId, segmentId);
        if (!obj->IsPending())
        {

            if (NormObject::FILE == obj->GetType()) 
#ifdef SIMULATE
                ((NormSimObject*)obj)->Close();           
#else
                ((NormFileObject*)obj)->Close();
#endif // !SIMULATE
            session->Notify(NormController::RX_OBJECT_COMPLETE, this, obj);
            DeleteObject(obj);
            completion_count++;
        } 
    }     
    RepairCheck(NormObject::TO_BLOCK, objectId, blockId, segmentId);
}  // end NormServerNode::HandleObjectMessage()

bool NormServerNode::SyncTest(const NormMessage& msg) const
{
    // (TBD) Additional sync policies
    
    // Sync if non-repair and (INFO or block zero message)
    bool result = !msg.object.FlagIsSet(NormObjectMsg::FLAG_REPAIR) &&
                  (NORM_MSG_INFO == msg.generic.GetType()) ? 
                        true : (0 == msg.data.GetFecBlockId());
    return result;
    
}  // end NormServerNode::SyncTest()

void NormServerNode::Sync(NormObjectId objectId)
{
    if (synchronized)
    {
        if (rx_pending_mask.IsSet())
        {
            NormObjectId firstSet = NormObjectId(rx_pending_mask.FirstSet());
            if (objectId > NormObjectId(rx_pending_mask.LastSet()))
            {
                NormObject* obj;
                while ((obj = rx_table.Find(rx_table.RangeLo()))) 
                {
                    DeleteObject(obj);
                    failure_count++;
                }
                rx_pending_mask.Clear(); 
            }
            else if (objectId > firstSet)
            {
               NormObject* obj;
               while ((obj = rx_table.Find(rx_table.RangeLo())) &&
                      (obj->Id() < objectId)) 
               {
                   DeleteObject(obj);
                   failure_count++;
               }
               unsigned long numBits = (UINT16)(objectId - firstSet) + 1;
               rx_pending_mask.UnsetBits(firstSet, numBits); 
            }
        }  
        if (next_id < objectId) next_id = objectId;
        sync_id = objectId;
        ASSERT(OBJ_INVALID != GetObjectStatus(objectId));
    }
    else
    {
        ASSERT(!rx_pending_mask.IsSet());
        sync_id = next_id = objectId;
        synchronized = true;
    }
}  // end NormServerNode::Sync()

NormServerNode::ObjectStatus NormServerNode::UpdateSyncStatus(const NormObjectId& objectId)
{
    ASSERT(synchronized);
    ObjectStatus status = GetObjectStatus(objectId);
    switch (status)
    {
        case OBJ_INVALID:
            // (TBD) We may want to control re-sync policy options
            //       or revert to fresh sync if sync is totally lost,
            //       otherwise SQUELCH process will get things in order
            DMSG(2, "NormServerNode::HandleObjectMessage() node>%lu re-syncing to server>%lu...\n",
                    LocalNodeId(), Id());
            Sync(objectId);
            resync_count++;
            status = OBJ_NEW;
        case OBJ_NEW:
            SetPending(objectId);
            break;
        default:
            break;
    }
    return status;
}  // end NormServerNode::UpdateSyncStatus()

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
        rx_pending_mask.SetBits(next_id, objectId - next_id + 1);
        next_id = objectId + 1; 
        // This prevents the "sync_id" from getting stale
        sync_id = rx_pending_mask.FirstSet();
    }
}  // end NormServerNode::SetPending()


NormServerNode::ObjectStatus NormServerNode::GetObjectStatus(const NormObjectId& objectId) const
{
   if (synchronized)
   {
       if (objectId < sync_id) 
       {
           if ((sync_id - objectId) > 256)
                return OBJ_INVALID;
           else
                return OBJ_COMPLETE;  
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
                    NormObject::CheckLevel level;
                    if (nextId < lastId)
                        level = NormObject::THRU_OBJECT;
                    else
                        level = checkLevel;
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
                double backoffTime = 
                    ExponentialRand(grtt_estimate*session->BackoffFactor(), 
                                    gsize_estimate);
                repair_timer.SetInterval(backoffTime);
                DMSG(4, "NormServerNode::RepairCheck() node>%lu begin NACK back-off: %lf sec)...\n",
                        LocalNodeId(), backoffTime);
                session->InstallTimer(&repair_timer);  
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
        NormObject* obj = rx_table.Find(objectId);
        if (obj) obj->ClientRepairCheck(checkLevel, blockId, segmentId, true);
        if (objectId < current_object_id) current_object_id = objectId;
    }
    else
    {
        // Holding-off on repair cycle initiation   
    }
}  // end NormServerNode::RepairCheck()

// When repair timer fires, possibly build a NACK
// and queue for transmission to this server node
bool NormServerNode::OnRepairTimeout()
{
    
    switch(repair_timer.RepeatCount())
    {
        case 0:  // hold-off time complete
            DMSG(4, "NormServerNode::OnRepairTimeout() node>%lu end NACK hold-off ...\n",
                    LocalNodeId());
            break;
            
        case 1:  // back-off timeout complete
        {
            DMSG(4, "NormServerNode::OnRepairTimeout() node>%lu end NACK back-off ...\n",
                    LocalNodeId());
            // 1) Were we suppressed? 
            if (rx_pending_mask.IsSet())
            {
                bool repairPending = false;
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
                            repairPending = true;
                            break;
                        }                        
                    }
                    nextId++;
                    nextId = rx_pending_mask.NextSet(nextId);
                } // end while (nextId <= current_block_id)
                if (repairPending)
                {
                    // We weren't completely suppressed, so build NACK
                    NormMessage* msg = session->GetMessageFromPool();
                    if (!msg)
                    {
                        DMSG(0, "NormServerNode::OnRepairTimeout() node>%lu Warning! "
                                "message pool empty ...\n", LocalNodeId());
                        repair_timer.Deactivate();
                        return false;   
                    }
                    NormNackMsg& nack = msg->nack;
                    nack.ResetNackContent();
                    NormRepairRequest req;
                    NormObjectId prevId;
                    UINT16 reqCount = 0;
                    NormRepairRequest::Form prevForm = NormRepairRequest::INVALID;
                    nextId = rx_pending_mask.FirstSet();
                    lastId = rx_pending_mask.LastSet();
                    if (current_object_id < lastId) lastId = current_object_id;
                    lastId++;  // force loop to fully flush nack building.
                    while ((nextId <= lastId) || (reqCount > 0))
                    {
                        NormObject* obj = NULL;
                        bool objPending = false;
                        if (nextId == lastId)
                            nextId++;  // force break of possible ending consecutive series
                        else
                            obj = rx_table.Find(nextId);
                        if (obj) objPending = obj->IsPending(nextId != current_object_id);
                        
                        if (!objPending && reqCount && (reqCount == (nextId - prevId)))
                        {
                            reqCount++;  // consecutive series of missing objs continues  
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
                                    req.ResetFlags();
                                    req.SetFlag(NormRepairRequest::OBJECT);
                                }
                                prevForm = nextForm;
                            }
                            if (NormRepairRequest::INVALID != nextForm)
                                DMSG(6, "NormServerNode::AppendRepairRequest() OBJECT request\n");
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
                                    break;
                                default:
                                    break;  
                            }
                            prevId = nextId;
                            if (obj || (nextId >= lastId))
                                reqCount = 0;
                            else
                                reqCount = 1;
                        }  // end if/else (!objPending && reqCount && (reqCount == (nextId - prevId)))
                        if (objPending)
                        {
                            if (NormRepairRequest::INVALID != prevForm)
                                nack.PackRepairRequest(req);  // (TBD) error check
                            prevForm = NormRepairRequest::INVALID; 
                            reqCount = 0;
                            bool flush = (nextId != current_object_id);
                            obj->AppendRepairRequest(nack, flush);  // (TBD) error check
                        }
                        nextId++;
                        if (nextId <= lastId) 
                            nextId = rx_pending_mask.NextSet(nextId);
                    }  // end while(nextId <= lastId)
                    if (NormRepairRequest::INVALID != prevForm)
                        nack.PackRepairRequest(req);  // (TBD) error check 
                    // (TBD) Queue NACK for transmission
                    msg->generic.SetType(NORM_MSG_NACK);
                    msg->nack.SetServerId(Id());
                    msg->generic.SetDestination(session->Address());
                    session->QueueMessage(msg);
                    nack_count++;
                }
                else
                {
                    suppress_count++;
                    DMSG(6, "NormServerNode::OnRepairTimeout() node>%lu NACK SUPPRESSED ...\n",
                            LocalNodeId());
                }  // end if/else(repairPending)
                repair_timer.SetInterval(grtt_estimate*(session->BackoffFactor() + 2.0));
                DMSG(4, "NormServerNode::OnRepairTimeout() node>%lu begin NACK hold-off: %lf sec ...\n",
                         LocalNodeId(), repair_timer.Interval());
                
            }
            else
            {
                DMSG(4, "NormServerNode::OnRepairTimeout() node>%lu nothing pending ...\n",
                        LocalNodeId());
                // (TBD) cancel hold-off timeout ???  
            }  // end if/else (repair_mask.IsSet())       
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

