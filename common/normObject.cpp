#include "normObject.h"
#include "normSession.h"

#include <errno.h>

NormObject::NormObject(NormObject::Type      theType, 
                       class NormSession*    theSession, 
                       class NormServerNode* theServer,
                       const NormObjectId&   objectId)
 : type(theType), session(theSession), server(theServer),
   id(objectId), pending_info(false), repair_info(false),
   current_block_id(0), next_segment_id(0),
   info(NULL), info_len(0), accepted(false), stream_sync(false)
{
}

NormObject::~NormObject()
{
    /*if (server)
        server->DeleteObject(true); 
    else
        session->DeleteTxObject(true);*/
    Close();
    if (info) delete info;
}

// This is mainly used for debug messages
NormNodeId NormObject::LocalNodeId() const
{
    return session->LocalNodeId();    
}



bool NormObject::Open(const NormObjectSize& objectSize, 
                      const char*           infoPtr, 
                      UINT16                infoLen)
{
    // Note "objectSize" represents actual total object size for
    // DATA or FILE objects, buffer size for STREAM objects
    // In either case, we need our sliding bit masks to be of 
    // appropriate size.
    UINT16 segmentSize, numData, numParity;
    if (server)
    {
        segmentSize = server->SegmentSize();
        numData = server->BlockSize();
        numParity = server->NumParity();
        if (infoLen > 0) 
        {
            pending_info = true;
            info_len = 0;
            if (!(info = new char[segmentSize]))
            {
                DMSG(0, "NormObject::Open() info allocation error\n");
                return false;
            }               
        }
    }
    else
    {
        segmentSize = session->ServerSegmentSize();
        numData = session->ServerBlockSize();
        numParity = session->ServerNumParity();
        if (infoPtr)
        {
            if (info) delete []info;
            if (infoLen > segmentSize)
            {
                DMSG(0, "NormObject::Open() info too big error\n");
                info_len = 0;
                return false;
            }        
            if (!(info = new char[infoLen]))
            {
                DMSG(0, "NormObject::Open() info allocation error\n");
                info_len = 0;
                return false;
            } 
            memcpy(info, infoPtr, infoLen);
            info_len = infoLen;
            pending_info = true;
        }
    }
    
    // Make sure object size and block/segment sizes are compatible
    NormObjectSize blockSize((UINT32)segmentSize * (UINT32)numData);
    NormObjectSize numBlocks = objectSize / blockSize;
    ASSERT(0 == numBlocks.MSB());
    
    if (!block_buffer.Init(numBlocks.LSB()))
    {
        DMSG(0, "NormObject::Open() init block_buffer error\n");  
        Close();
        return false;
    }
    
    // Init pending_mask (everything pending)
    if (!pending_mask.Init(numBlocks.LSB()))
    {
        DMSG(0, "NormObject::Open() init pending_mask error\n");  
        Close();
        return false; 
    }
    pending_mask.SetBits(0, numBlocks.LSB());
    
    // Init repair_mask
    if (!repair_mask.Init(numBlocks.LSB()))
    {
        DMSG(0, "NormObject::Open() init pending_mask error\n");  
        Close();
        return false; 
    }
    repair_mask.Clear();
    
    if (STREAM == type)
    {
        last_block_id = 0;  // not applicable for STREAM
        last_block_size = numData;  // assumed for STREAM
        stream_next_id = numBlocks.LSB();
    }
    else
    {
        last_block_id = numBlocks.LSB() - 1;  
        NormObjectSize lastBlockBytes =  NormObjectSize(last_block_id) * blockSize;
        lastBlockBytes = objectSize - lastBlockBytes;
        NormObjectSize lastBlockSize = lastBlockBytes / NormObjectSize(segmentSize);
        ASSERT(!lastBlockSize.MSB());
        ASSERT(lastBlockSize.LSB() < numData);
        last_block_size = lastBlockSize.LSB();
        NormObjectSize lastSegmentSize = 
            NormObjectSize(0,last_block_size-1) * NormObjectSize(segmentSize);
        lastSegmentSize = lastBlockBytes - lastSegmentSize;
        ASSERT(!lastSegmentSize.MSB());
        ASSERT(lastSegmentSize.LSB() <= segmentSize);
        last_segment_size = lastSegmentSize.LSB();
    }
    
    object_size = objectSize;
    segment_size = segmentSize;
    ndata = numData;
    nparity = numParity;
    current_block_id = 0;
    next_segment_id = 0;
    return true;
}  // end NormObject::Open()

void NormObject::Close()
{
    NormBlock* block;
    while ((block = block_buffer.Find(block_buffer.RangeLo())))
    {
        block_buffer.Remove(block);
        if (server)
            server->PutFreeBlock(block);
        else
            session->ServerPutFreeBlock(block);
    }
    repair_mask.Destroy();
    pending_mask.Destroy();
    block_buffer.Destroy();
}  // end NormObject::Close();

bool NormObject::HandleInfoRequest()
{
    // (TBD) immediately make info pending?
    bool increasedRepair = false;
    if (info)
    {
        if (!repair_info)
        {
            repair_info = true;
            increasedRepair = true;
        }   
    }
    return increasedRepair;
}  // end NormObject::HandleInfoRequest()

bool NormObject::HandleBlockRequest(NormBlockId nextId, NormBlockId lastId)
{
    DMSG(6, "NormObject::HandleBlockRequest() node>%lu blk>%lu -> blk>%lu\n", 
            LocalNodeId(), (UINT32)nextId, (UINT32)lastId);
    bool increasedRepair = false;
    while (nextId <= lastId)
    {
        if (!repair_mask.Test(nextId))
        {
            // (TBD) these tests can probably go away if everything else is done right
            if (!pending_mask.CanSet(nextId))
                DMSG(0, "NormObject::HandleBlockRequest() pending_mask.CanSet(%lu) error\n",
                        (UINT32)nextId);
            if (!repair_mask.Set(nextId)) 
                DMSG(0, "NormObject::HandleBlockRequest() repair_mask.Set(%lu) error\n",
                        (UINT32)nextId);
            increasedRepair = true;   
        }
        nextId++;
    }
    return increasedRepair;
}  // end NormObject::HandleBlockRequest();

bool NormObject::TxReset(NormBlockId firstBlock)
{
    bool increasedRepair = false;
    if (!pending_info && HaveInfo())
    {
        increasedRepair = true;
        pending_info = true;
    }
    repair_info = false;
    repair_mask.Reset((UINT32)firstBlock);
    repair_mask.Xor(pending_mask);
    if (repair_mask.IsSet()) 
    {
        increasedRepair = true;
        pending_mask.Reset((UINT32)firstBlock);
    }
    repair_mask.Clear();
    NormBlockBuffer::Iterator iterator(block_buffer);
    NormBlock* block;
    while ((block = iterator.GetNextBlock()))
    {
        NormBlockId blockId = block->Id();
        if (blockId >= firstBlock)
        {
            increasedRepair |= block->TxReset(BlockSize(blockId), 
                                              nparity, 
                                              session->ServerAutoParity(), 
                                              segment_size);
        }
    }
    return increasedRepair;
}  // end NormObject::TxReset()

bool NormObject::TxResetBlocks(NormBlockId nextId, NormBlockId lastId)
{
    bool increasedRepair = false;
    UINT16 autoParity = session->ServerAutoParity();
    while (nextId <= lastId)
    {
        if (!pending_mask.Test(nextId))
        {
            pending_mask.Set(nextId);
            increasedRepair = true;
        }
        NormBlock* block = block_buffer.Find(nextId);
        if (block) 
            increasedRepair |= block->TxReset(BlockSize(block->Id()), nparity, autoParity, segment_size);
        nextId++;
    }
    return increasedRepair;
}  // end NormObject::TxResetBlocks()



bool NormObject::ActivateRepairs()
{
    bool repairsActivated = false;
    // Activate repair of info if applicable (TBD - how to flag info message as repair???)
    if (repair_info)
    {
        pending_info = true;
        repair_info = false;
        repairsActivated = true;
    }
    // Activate any complete block repairs
    if (repair_mask.IsSet())
    {
        repairsActivated = true;
        NormBlockId nextId = repair_mask.FirstSet();
        NormBlockId lastId = repair_mask.LastSet();
        DMSG(6, "NormObject::ActivateRepairs() node>%lu obj>%hu activated blk>%lu->%lu repairs\n",
                LocalNodeId(), (UINT16)id,
                (UINT32)nextId, (UINT32)lastId);
        UINT16 autoParity = session->ServerAutoParity();
        while (nextId <= lastId)
        {
            NormBlock* block = block_buffer.Find(nextId);
            if (block) block->TxReset(BlockSize(nextId), nparity, autoParity, segment_size);
            // (TBD) This check can be eventually eliminated if everything else is done right
            if (!pending_mask.Set(nextId)) 
                DMSG(0, "NormObject::ActivateRepairs() pending_mask.Set(%lu) error!\n",
                        (UINT32)nextId);
            nextId++; 
            nextId =  repair_mask.NextSet(nextId); 
        } 
        repair_mask.Clear();  
    }
    // Activate partial block (segment) repairs
    NormBlockBuffer::Iterator iterator(block_buffer);
    NormBlock* block;
    while ((block = iterator.GetNextBlock()))
    {
        if (block->ActivateRepairs(nparity)) 
        {
            repairsActivated = true;
            DMSG(6, "NormObject::ActivateRepairs() node>%lu obj>%hu activated blk>%lu segment repairs ...\n",
                LocalNodeId(), (UINT16)id, (UINT32)block->Id());
            // (TBD) This check can be eventually eliminated if everything else is done right
            if (!pending_mask.Set(block->Id()))
                DMSG(0, "NormObject::ActivateRepairs() pending_mask.Set(%lu) error!\n", (UINT32)block->Id());
        }
    }
    return repairsActivated;
}  // end NormObject::ActivateRepairs()

// Called by server only
bool NormObject::IsRepairPending() const
{
    ASSERT(!server);
    if (repair_info) return true;
    if (repair_mask.IsSet()) return true;
    NormBlockBuffer::Iterator iterator(block_buffer);
    NormBlock* block;
    while ((block = iterator.GetNextBlock()))
    {
        if (block->IsRepairPending()) return true;
    }
    return false;
}  // end NormObject::IsRepairPending()


bool NormObject::IsPending(bool flush) const
{
    if (pending_info) return true;
    if (flush)
    {
        return pending_mask.IsSet();
    }
    else
    {
        if (pending_mask.IsSet())
        {
            NormBlockId firstId = pending_mask.FirstSet();
            if (firstId < current_block_id)
            {
                return true;
            }
            else if (firstId > current_block_id)
            {
                return false;
            }
            else
            {
                if (next_segment_id > 0)
                {
                    NormBlock* block = block_buffer.Find(current_block_id);
                    if (block)
                    {
                        if (block->FirstPending() < next_segment_id)
                            return true;
                        else
                            return false;
                    }
                    else
                    {
                        return true;
                    }
                }
                else
                {
                    return false;
                }
            }
        }
        else
        {
            return false;
        }
    }
}  // end NormObject::IsPending()


bool NormObject::ClientRepairCheck(CheckLevel    level,
                                   NormBlockId   blockId,
                                   NormSegmentId segmentId,
                                   bool          timerActive)
{
    if (timerActive)
    {
        switch (level)
        {
            case THRU_INFO:
                current_block_id = 0;
                next_segment_id = 0;
                break;
            case TO_BLOCK:
                if (blockId < current_block_id)
                {
                    current_block_id = blockId;
                    next_segment_id = 0;   
                }
                break;
            case THRU_SEGMENT:
                if (blockId < current_block_id)
                {
                    current_block_id = blockId;
                    next_segment_id = segmentId + 1;   
                }
                else if (blockId == current_block_id)
                {
                    if (segmentId < next_segment_id)
                        next_segment_id = segmentId + 1;    
                }
                break;
            case THRU_BLOCK:
                if (blockId < current_block_id)
                {
                    current_block_id = blockId;
                    next_segment_id = BlockSize(blockId);
                }  
                break;
            default:
                break; 
        }
        return false;  // repair_timer already running 
    }
    bool needRepair = false;
    if (pending_info)
    {
        repair_info = false;
        needRepair = true;   
    }
    if ((level > THRU_INFO) && pending_mask.IsSet())
    {
        if (repair_mask.IsSet()) repair_mask.Clear();
        NormBlockId nextId = pending_mask.FirstSet();
        NormBlockId lastId = pending_mask.LastSet();
        if ((level < THRU_OBJECT) && (blockId < lastId))
            lastId = blockId;
        if (level > TO_BLOCK) lastId++;
        while (nextId < lastId)  
        { 
            NormBlock* block = block_buffer.Find(nextId);
            if (block)
            {
                if ((nextId == blockId) && (THRU_SEGMENT == level))
                {
                    if (block->FirstPending() <= segmentId)
                    {
                        block->ClearRepairs();
                        needRepair = true;
                    }
                }
                else
                {
                    block->ClearRepairs();
                    needRepair = true;
                }
            }  
            else
            {
                needRepair = true;
            } 
            nextId++;
            nextId = pending_mask.NextSet(nextId);
        }
    }
    switch (level)
    {
        case THRU_INFO:
            current_block_id = 0;
            next_segment_id = 0;
            break;
        case TO_BLOCK:
            current_block_id = blockId;
            next_segment_id = 0;
            break;
        case THRU_SEGMENT:
            current_block_id = blockId;
            next_segment_id = segmentId + 1;
            break;
        case THRU_BLOCK:
            current_block_id = blockId;  
            next_segment_id = BlockSize(blockId);
            break;
        case THRU_OBJECT:
            current_block_id = last_block_id;
            next_segment_id = BlockSize(blockId);
        default:
            break; 
    }
    return needRepair;
}  // end NormObject::ClientRepairCheck()

// Note this clears "repair_mask" state (called on client repair_timer timeout)
bool NormObject::IsRepairPending(bool flush)
{
    ASSERT(server);
    if (pending_info && !repair_info) return true;
    // Calculate repair_mask = pending_mask - repair_mask 
    repair_mask.XCopy(pending_mask);
    if (repair_mask.IsSet())
    {
        NormBlockId nextId = repair_mask.FirstSet();
        NormBlockId lastId = repair_mask.LastSet();
        if (!flush && (current_block_id < lastId)) lastId = current_block_id;
        while (nextId <= lastId)
        {
            NormBlock* block = block_buffer.Find(nextId);
            if (block)
            {
                bool isPending;
                UINT16 numData = BlockSize(nextId);
                if (flush || (nextId < lastId))
                {
                    isPending = block->IsRepairPending(numData, nparity);
                }
                else
                {
                    if (next_segment_id < numData)
                        isPending = block->IsRepairPending(next_segment_id, 0);
                    else
                        isPending = block->IsRepairPending(numData, nparity);
                }
                if (isPending) return true;
            }
            else
            {
                return true;  // We need the whole thing
            }
            nextId++;
            nextId = repair_mask.NextSet(nextId);
        }
    }
    return false;
}  // end NormObject::IsRepairPending()

bool NormObject::AppendRepairRequest(NormNackMsg&   nack, 
                                     bool           flush)
{ 
    // If !flush, we request only up to current_block_id::next_segment_id.
    NormRepairRequest req;
    NormRepairRequest::Form prevForm = NormRepairRequest::INVALID;
    NormBlockId prevId;
    UINT16 reqCount = 0;
    if (pending_mask.IsSet())
    {
       NormBlockId nextId = pending_mask.FirstSet();
       NormBlockId lastId = pending_mask.LastSet();
       if (!flush && (current_block_id < lastId)) lastId = current_block_id;
       lastId++;
       //DMSG(6, "NormObject::AppendRepairRequest() node>%lu obj>%hu, blk>%lu->%lu (current:%lu)\n",
       //        LocalNodeId(), (UINT16)id,
       //        (UINT32)nextId, (UINT32)lastId, (UINT32)current_block_id);
       while ((nextId <= lastId) || (reqCount > 0))
       {
            NormBlock* block = NULL;
            bool blockPending = false;
            if (nextId == lastId)
               nextId++;  // force break of possible ending consec. series
            else
                block = block_buffer.Find(nextId);
            if (block)
            {
                if (nextId == current_block_id)
                {
                    if (block->FirstPending() < next_segment_id)
                        blockPending = true;
                }
                else
                {
                    blockPending = true;
                }
            }  // end if (block)
            
            if (!blockPending && reqCount && (reqCount == (nextId - prevId)))
            {
                reqCount++;  // consecutive series of missing blocks continues
            }
            else
            {
                NormRepairRequest::Form nextForm;
                switch(reqCount)
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
                }  // end switch(reqCount)
                if (prevForm != nextForm)
                {
                    if (NormRepairRequest::INVALID != prevForm)
                        nack.PackRepairRequest(req);  // (TBD) error check
                    if (NormRepairRequest::INVALID != nextForm)
                    {
                        nack.AttachRepairRequest(req, segment_size);
                        req.SetForm(nextForm);
                        req.ResetFlags();
                        req.SetFlag(NormRepairRequest::BLOCK);
                        if (pending_info) req.SetFlag(NormRepairRequest::INFO);
                    }
                    prevForm = nextForm;
                }
                if (NormRepairRequest::INVALID != nextForm)
                    DMSG(6, "NormObject::AppendRepairRequest() BLOCK request\n");
                switch (nextForm)
                {
                    case NormRepairRequest::ITEMS:
                        req.AppendRepairItem(id, prevId, 0);  // (TBD) error check
                        if (2 == reqCount)
                            req.AppendRepairItem(id, prevId+1, 0); // (TBD) error check
                        break;
                    case NormRepairRequest::RANGES:
                        req.AppendRepairItem(id, prevId, 0); // (TBD) error check
                        req.AppendRepairItem(id, prevId+reqCount-1, 0); // (TBD) error check
                        break;
                    default:
                        break;
                }  // end switch(nextForm)
                prevId = nextId;
                if (block || (nextId >= lastId))
                    reqCount = 0;
                else
                    reqCount = 1;
            }  // end if/else (!blockPending && reqCount && (reqCount == (nextId - prevId)))
            if (blockPending)
            {
                UINT16 numData = BlockSize(nextId);
                if (NormRepairRequest::INVALID != prevForm)
                    nack.PackRepairRequest(req);  // (TBD) error check
                reqCount = 0;
                prevForm = NormRepairRequest::INVALID; 
                if (flush || (nextId != current_block_id))
                {
                    block->AppendRepairRequest(nack, numData, nparity, id, 
                                               pending_info, segment_size); // (TBD) error check
                }
                else
                {
                    if (next_segment_id < numData)
                        block->AppendRepairRequest(nack, next_segment_id, 0, id,
                                                    pending_info, segment_size); // (TBD) error check
                    else
                        block->AppendRepairRequest(nack, numData, nparity, id, 
                                                   pending_info, segment_size); // (TBD) error check
                }
            }
            nextId++;
            if (nextId <= lastId)
                nextId = pending_mask.NextSet(nextId);
        }  // end while (nextId <= lastId)
        if (NormRepairRequest::INVALID != prevForm)
            nack.PackRepairRequest(req);  // (TBD) error check
    }  
    else
    {
        // INFO_ONLY repair request
        ASSERT(pending_info);
        nack.AttachRepairRequest(req, segment_size);
        req.SetForm(NormRepairRequest::ITEMS);
        req.ResetFlags();
        req.SetFlag(NormRepairRequest::INFO); 
        req.AppendRepairItem(id, 0, 0);  // (TBD) error check
        nack.PackRepairRequest(req);  // (TBD) error check
    }  // end if/else pending_mask.IsSet()
    return true;
}  // end NormObject::AppendRepairRequest()


void NormObject::HandleObjectMessage(NormMessage&   msg, 
                                     NormMsgType    msgType,
                                     NormBlockId    blockId,
                                     NormSegmentId  segmentId)
{
    if (NORM_MSG_INFO == msgType)
    {
        if (pending_info)
        {
            info_len = msg.info.GetInfoLen();
            if (info_len > segment_size)
            {
                info_len = segment_size;
                DMSG(0, "NormObject::HandleObjectMessage() node>%lu server>%lu obj>%hu "
                    "Warning! info too long.\n", LocalNodeId(), server->Id(), (UINT16)id);   
            }
            memcpy(info, msg.info.GetInfo(), info_len);
            pending_info = false;
            session->Notify(NormController::RX_OBJECT_INFO, server, this);
        }
        else
        {
            // (TBD) Verify info hasn't changed?   
            DMSG(6, "NormObject::HandleObjectMessage() node>%lu server>%lu obj>%hu "
                    "received duplicate info ...\n", LocalNodeId(),
                     server->Id(), (UINT16)id);
        }
    }
    else  // NORM_MSG_DATA
    {
        // For stream objects, a little extra mgmt is required
        if (STREAM == type)
        {
            if (!StreamUpdateStatus(blockId))
            {
                //DMSG(0, "NormObject::HandleObjectMessage() node:%lu server:%lu obj>%hu blk>%lu "
                //        "broken stream ...\n", LocalNodeId(), server->Id(), (UINT16)id, (UINT32)blockId);
            
                // ??? Ignore this new packet and try to fix stream ???
                return;
                server->IncrementResyncCount();
            }
            while (!StreamUpdateStatus(blockId))
            {
                // Server is too far ahead of me ...
                if (pending_mask.IsSet())
                {
                    NormBlockId firstId = pending_mask.FirstSet();
                    NormBlock* block = block_buffer.Find(firstId);
                    if (block)
                    {
                        block_buffer.Remove(block);
                        server->PutFreeBlock(block);
                    }
                    pending_mask.Unset(firstId);
                }
                else
                {
                    stream_next_id = blockId - NormBlockId(pending_mask.Size());
                }          
            }
        }
        UINT16 numData = BlockSize(blockId);
        if (pending_mask.Test(blockId))
        {
            NormBlock* block = block_buffer.Find(blockId);
            if (!block)
            {
                if (!(block = server->GetFreeBlock(id, blockId)))
                {
                    DMSG(2, "NormObject::HandleObjectMessage() node>%lu server>%lu obj>%hu "
                            "Warning! no free blocks ...\n", LocalNodeId(), server->Id(), 
                            (UINT16)id);  
                    return;
                }
                block->RxInit(blockId, numData, nparity);
                block_buffer.Insert(block);
            }
            if (block->IsPending(segmentId))
            {
                // 1) Store data in block buffer in case its needed for decoding
                char* segment = server->GetFreeSegment(id, blockId);
                if (!segment)
                {
                    DMSG(2, "NormObject::HandleObjectMessage() node>%lu server>%lu obj>%hu "
                            "Warning! no free segments ...\n", LocalNodeId(), server->Id(), 
                            (UINT16)id);  
                    return;
                }
                UINT16 segmentLen = msg.data.GetDataLength();
                if (segmentLen > segment_size)
                {
                    DMSG(0, "NormObject::HandleObjectMessage() node>%lu server>%lu obj>%hu "
                            "Error! segment too large ...\n", LocalNodeId(), server->Id(), 
                            (UINT16)id);  
                    server->PutFreeSegment(segment);
                    return;  
                }
                memcpy(segment, msg.data.GetPayload(), segmentLen + NormDataMsg::PayloadHeaderLen());
                block->AttachSegment(segmentId, segment);
                block->UnsetPending(segmentId);
                // 2) Write segment to object (if it's data)
                if (segmentId < numData) 
                {
                    block->DecrementErasureCount();
                    if (WriteSegment(blockId, segmentId, segment))
                        server->IncrementRecvGoodput(segmentLen);
                }
                else
                {
                    block->IncrementParityCount();   
                }
                // 3) Decode block if ready and return to pool
                if (block->ErasureCount() <= block->ParityCount())
                {
                    // Decode (if pending_mask.FirstSet() < numData)
                    // and write any decoded data segments to object
                    DMSG(8, "NormObject::HandleObjectMessage() node>%lu server>%lu obj>%hu blk>%lu "
                            "completed block ...\n", LocalNodeId(), server->Id(), 
                            (UINT16)id, (UINT32)block->Id()); 
                    UINT16 nextErasure = block->FirstPending();
                    UINT16 erasureCount = 0;
                    UINT16 blockLen = numData + nparity;
                    while (nextErasure < blockLen)
                    {
                        server->SetErasureLoc(erasureCount++, nextErasure);
                        if (nextErasure < numData)
                        {
                            if (!(segment = server->GetFreeSegment(id, blockId)))
                            {
                                DMSG(2, "NormObject::HandleObjectMessage() node>%lu server>%lu obj>%hu "
                                        "Warning! no free segments ...\n", LocalNodeId(), server->Id(), 
                                        (UINT16)id);  
                                // (TBD) Dump the block ...???
                                return;
                            }
                            memset(segment, 0, segment_size+NormDataMsg::PayloadHeaderLen());
                            block->SetSegment(nextErasure, segment);
                        }
                        nextErasure = block->NextPending(nextErasure+1);   
                    }
                    if (erasureCount)
                    {
                        server->Decode(block->SegmentList(), numData, erasureCount); 
                        for (UINT16 i = 0; i < erasureCount; i++) 
                        {
                            NormSegmentId sid = server->GetErasureLoc(i);
                            if (sid < numData)
                            {
                                if (WriteSegment(blockId, sid, block->Segment(sid)))
                                {
                                    // For statistics only (TBD) #ifdef NORM_DEBUG
                                    server->IncrementRecvGoodput(NormDataMsg::ReadLength(block->Segment(sid)));
                                }
                            }
                            else
                            {
                                break;
                            }
                        }
                    }
                    // OK, we're done with this block
                    pending_mask.Unset(blockId);
                    block_buffer.Remove(block);
                    server->PutFreeBlock(block);     
                }
                
                // Notify application of new data available
                session->Notify(NormController::RX_OBJECT_UPDATE, server, this);
            }
            else
            {
                DMSG(6, "NormObject::HandleObjectMessage() node>%lu server>%lu obj>%hu "
                    "received duplicate segment ...\n", LocalNodeId(),
                     server->Id(), (UINT16)id);
            }
        }
        else
        {
            DMSG(6, "NormObject::HandleObjectMessage() node>%lu server>%lu obj>%hu "
                    "received duplicate block message ...\n", LocalNodeId(),
                     server->Id(), (UINT16)id);
        }  // end if/else pending_mask.Test(blockId)
    }  // end if/else (NORM_MSG_INFO)
}  // end NormObject::HandleObjectMessage()

bool NormObject::StreamUpdateStatus(NormBlockId blockId)
{
    if (stream_sync)
    {
        if (blockId < stream_sync_id)
        {
            // it's an old block or stream is _very_ broken
            // (nothing to  update)
            return true;
        }
        else
        {
            if (blockId < stream_next_id)
            {
                // it's pending or complete (nothing to update)
                return true;
            }
            else
            {
                if (pending_mask.IsSet())
                {
                    if (pending_mask.CanSet(blockId))
                    {
                        pending_mask.SetBits(stream_next_id, blockId - stream_next_id + 1);
                        stream_next_id = blockId + 1;
                        // Handle potential sync block id wrap
                        NormBlockId delta = stream_next_id - stream_sync_id;
                        if (delta > NormBlockId(2*pending_mask.Size()))
                            stream_sync_id = NormBlockId(pending_mask.FirstSet());
                        return true;
                    }
                    else
                    {
                        // Stream broken
                        return false;   
                    }
                }
                else
                {
                    NormBlockId delta = blockId - stream_next_id + 1;
                    if (delta > NormBlockId(pending_mask.Size()))
                    {
                        // Stream broken
                        return false;
                    }
                    else
                    {
                        pending_mask.SetBits(blockId, pending_mask.Size()); 
                        stream_next_id = blockId + NormBlockId(pending_mask.Size());
                        // Handle potential sync block id wrap
                        delta = stream_next_id - stream_sync_id;
                        if (delta > NormBlockId(2*pending_mask.Size()))
                            stream_sync_id = blockId;
                        return true;
                    }   
                    
                }
            }   
        }
    }
    else
    {
        // For now, let stream begin anytime
        pending_mask.Clear();
        pending_mask.SetBits(blockId, pending_mask.Size());
        stream_sync = true;
        stream_sync_id = blockId;
        stream_next_id = blockId + pending_mask.Size(); 
        return true;  
    }
}  // end NormObject::StreamUpdateStatus()

// Steals non-pending block (oldest first) for server resource management
// (optionally excludes block indicated by blockId)
NormBlock* NormObject::StealNonPendingBlock(bool excludeBlock, NormBlockId excludeId)
{
    if (block_buffer.IsEmpty())
    {
        return NULL;
    }
    else
    {
        NormBlockBuffer::Iterator iterator(block_buffer);
        NormBlock* block;
        while ((block = iterator.GetNextBlock()))
        {
            NormBlockId bid = block->Id();
            if (block->IsTransmitPending() ||
                pending_mask.Test(bid) ||
                repair_mask.Test(bid) ||
                (excludeBlock && (excludeId == bid)))
            {
                continue;
            }
            else
            {
                block_buffer.Remove(block);
                return block;
            }
        }
    }
    return NULL;
}  // end NormObject::StealNonPendingBlock()

// For client resource management, steals newer block resources when
// needing resources for ordinally older blocks.
NormBlock* NormObject::StealNewestBlock(bool excludeBlock, NormBlockId excludeId)
{
    if (block_buffer.IsEmpty())
    {
        return NULL;
    }
    else
    {
        NormBlock* block = block_buffer.Find(block_buffer.RangeHi());
        if (excludeBlock && (excludeId == block->Id()))
        {
            return NULL;
        }
        else
        {
            block_buffer.Remove(block);
            return block; 
        } 
    }
}  // end NormObject::StealNewestBlock()

bool NormObject::NextServerMsg(NormMessage* msg)
{
    msg->object.ResetFlags();
    switch(type)
    {
        case STREAM:
            msg->object.SetFlag(NormObjectMsg::FLAG_STREAM);
            break;
        case FILE:
            msg->object.SetFlag(NormObjectMsg::FLAG_FILE);
            break;
        default:
            break;
    }
    if (info) msg->object.SetFlag(NormObjectMsg::FLAG_INFO);
    msg->object.SetSegmentSize(segment_size);
    msg->object.SetObjectSize(object_size);
    msg->object.SetFecNumParity(nparity);
    msg->object.SetFecBlockLen(ndata);
    msg->object.SetObjectId(id);
           
    if (pending_info)
    {
        msg->generic.SetType(NORM_MSG_INFO);
        msg->info.SetInfo(info, info_len);
        pending_info = false;
        return true;
    }
    if (!pending_mask.IsSet()) return false;
    
    NormBlockId blockId = pending_mask.FirstSet();
    UINT16 numData = BlockSize(blockId);
    NormBlock* block = block_buffer.Find(blockId);
    if (!block)
    {
       if (!(block = session->ServerGetFreeBlock(id, blockId)))
       {
            DMSG(2, "NormObject::NextServerMsg() node>%lu Warning! server resource " 
                    "constrained (no free blocks).\n", LocalNodeId());
            return false; 
       }
       // Load block with zero initialized parity segments
       
       UINT16 totalBlockLen = numData + nparity;
       for (UINT16 i = numData; i < totalBlockLen; i++)
       {
            char* s = session->ServerGetFreeSegment(id, blockId);
            if (s)
            {
                memset(s, 0, segment_size + NormDataMsg::PayloadHeaderLen());
                block->AttachSegment(i, s); 
            }
            else
            {
                DMSG(2, "NormObject::NextServerMsg() node>%lu Warning! server resource " 
                    "constrained (no free segments).\n", LocalNodeId());
                session->ServerPutFreeBlock(block);
                return false;
            }
       }      
       
       block->TxInit(blockId, numData, session->ServerAutoParity());  
       if (!block_buffer.Insert(block))
       {
           ASSERT(STREAM == type);
           ASSERT(blockId > block_buffer.RangeLo());
           NormBlock* b = block_buffer.Find(block_buffer.RangeLo());
           ASSERT(b);
           block_buffer.Remove(b);
           session->ServerPutFreeBlock(b);
           bool success = block_buffer.Insert(block);
           ASSERT(success);
       }
    }
    ASSERT(block->IsPending());
    NormSegmentId segmentId = block->FirstPending();
    
    // Try to read segment 
    if (segmentId < numData)
    {
        // Try to read data segment
        char* buffer = msg->data.AccessPayload(); 
        if (!ReadSegment(blockId, segmentId, buffer))
        {
            // (TBD) deal with read error 
            //(for streams, it currently means the stream is non-pending) 
            TRACE("NormObject::NextServerMsg() ReadSegment() error\n");          
            return false;  
        }
        UINT16 length = NormDataMsg::ReadLength(buffer);
        
        msg->data.SetDataLength(length);
        // Perform incremental FEC encoding as needed
        if ((block->ParityReadiness() <= segmentId) &&
            nparity) // (TBD) && incrementalParity == true
        {
            // (TBD) for non-stream objects, catch alternate "last block/segment len"
            if (length < segment_size)
            {
                memset(msg->data.AccessData()+length, 0, segment_size-length);
            }
            session->ServerEncode(msg->data.AccessPayload(), block->SegmentList(numData)); 
            block->IncreaseParityReadiness();     
        }
    }
    else
    {
        if (!block->ParityReady(numData)) 
        {
            ASSERT(0 == block->ParityReadiness());
            CalculateBlockParity(block);
        }
        ASSERT(block->ParityReady(numData));
        char* segment = block->Segment(segmentId);
        ASSERT(segment);
        msg->data.SetPayload(segment, segment_size+NormDataMsg::PayloadHeaderLen());    
    }
    block->UnsetPending(segmentId);       
    msg->generic.SetType(NORM_MSG_DATA);
    if (block->InRepair()) msg->object.SetFlag(NormObjectMsg::FLAG_REPAIR);
    msg->data.SetFecBlockId(blockId);
    msg->data.SetFecSymbolId(segmentId);
    if (!block->IsPending()) 
    {
        block->ResetParityCount(nparity);
        pending_mask.Unset(blockId); 
    }
 
    // This lets NORM_STREAM objects continue indefinitely
    if (IsStream() && !pending_mask.IsSet()) 
    {
        NormBlockId nextBlockId = stream_next_id;
        // Make sure we won't prevent about any pending repairs
        if (repair_mask.CanSet(nextBlockId))  
        {
            if (block_buffer.CanInsert(nextBlockId))
            {
                if (pending_mask.Set(nextBlockId))
                    stream_next_id++;
                else
                    DMSG(0, "NormObject::NextServerMsg() error setting stream pending mask (1)\n");
            }
            else
            {
                block = block_buffer.Find(block_buffer.RangeLo());  
                ASSERT(block); 
                if (!block->IsTransmitPending())
                {
                    if (pending_mask.Set(nextBlockId))
                        stream_next_id++;
                    else
                        DMSG(0, "NormObject::NextServerMsg() error setting stream pending mask (2)\n");
                }
                else
                {
                   DMSG(4, "NormObject::NextServerMsg() node>%lu Pending segment repairs (blk>%lu) "
                           "delaying stream advance ...\n", LocalNodeId(), (UINT32)block->Id());
                } 
            }
        }
        else
        {
            DMSG(0, "NormObject::NextServerMsg() Pending block repair delaying stream advance ...\n");   
        }
    }   
    return true;
}  // end NormObject::NextServerMsg()

bool NormObject::CalculateBlockParity(NormBlock* block)
{
    char buffer[NORM_MSG_SIZE_MAX];
    UINT16 numData = BlockSize(block->Id());
    for (UINT16 i = 0; i < numData; i++)
    {
        if (ReadSegment(block->Id(), i, buffer))
        {
            UINT16 length = NormDataMsg::ReadLength(buffer);
            if (length < segment_size)
            {
                memset(buffer+NormDataMsg::PayloadHeaderLen()+length, 0, segment_size-length);
            }
            session->ServerEncode(buffer, block->SegmentList(numData));
        }
        else
        {
            return false;   
        }
    }
    block->SetParityReadiness(numData);
    return true;
}  // end NormObject::CalculateBlockParity()

NormBlock* NormObject::ServerRecoverBlock(NormBlockId blockId)
{
    NormBlock* block = session->ServerGetFreeBlock(id, blockId);
    if (block)
    {
        UINT16 numData = BlockSize(blockId);  
        // Init block parameters
        block->TxRecover(blockId, numData, nparity);
        // Fill block with zero initialized parity segments
        UINT16 totalBlockLen = numData + nparity;
        for (UINT16 i = numData; i < totalBlockLen; i++)
        {
            char* s = session->ServerGetFreeSegment(id, blockId);
            if (s)
            {
                memset(s, 0, segment_size + NormDataMsg::PayloadHeaderLen());
                block->AttachSegment(i, s); 
            }
            else
            {
                DMSG(2, "NormObject::ServerRecoverBlock() node>%lu Warning! server resource " 
                        "constrained (no free segments).\n", LocalNodeId());
                session->ServerPutFreeBlock(block);
                return (NormBlock*)NULL;
            }
        }      
        // Attempt to re-generate parity
        if (CalculateBlockParity(block))
        {
            return block;
        }
        else
        {
            session->ServerPutFreeBlock(block);
            return (NormBlock*)NULL;
        }
    }
    else
    {
        DMSG(2, "NormObject::ServerRecoverBlock() node>%lu Warning! server resource " 
                        "constrained (no free blocks).\n", LocalNodeId());
        return (NormBlock*)NULL;
    }
}  // end NormObject::ServerRecoverBlock()

/////////////////////////////////////////////////////////////////
//
// NormFileObject Implementation
//
NormFileObject::NormFileObject(class NormSession*       theSession, 
                               class NormServerNode*    theServer,
                               const NormObjectId&      objectId)
 : NormObject(FILE, theSession, theServer, objectId), block_size(0,0)
{
    path[0] = '\0';
}

NormFileObject::~NormFileObject()
{
    Close();
}

// Open file 
bool NormFileObject::Open(const char* thePath,
                          const char* infoPtr,
                          UINT16      infoLen)
{
    if (server)  
    {
        // We're receiving this file 
        if (NormFile::IsLocked(thePath))
        {
            DMSG(0, "NormFileObject::Open() Error trying to open locked file for recv!\n");
            return false;   
        }
        else
        {
            if (file.Open(thePath, O_WRONLY | O_CREAT | O_TRUNC))
            {
                file.Lock();   
            }   
            else
            {
                DMSG(0, "NormFileObject::Open() recv file.Open() error!\n");
                return false;
            }
        }  
        block_size = NormObjectSize(server->BlockSize()) * 
                     NormObjectSize(server->SegmentSize());
    }
    else
    {
        // We're sending this file
        if (file.Open(thePath, O_RDONLY))
        {
            unsigned long size = file.GetSize(); 
            if (size)
            {
                if (!NormObject::Open(size, infoPtr, infoLen))
                {
                    DMSG(0, "NormFileObject::Open() send object open error\n");
                    Close();
                    return false;
                }
            }
            else
            {
                DMSG(0, "NormFileObject::Open() send file.GetSize() error!\n"); 
                file.Close();
                return false;
            }  
            block_size = NormObjectSize(session->ServerBlockSize()) * 
                         NormObjectSize(session->ServerSegmentSize());
        } 
        else
        {
            DMSG(0, "NormFileObject::Open() send file.Open() error!\n");
            return false;
        }
    }
    strncpy(path, thePath, PATH_MAX);
    unsigned int len = strlen(thePath);
    len = MIN(len, PATH_MAX);
    if (len < PATH_MAX) path[len] = '\0';
    return true;
}  // end NormFileObject::Open()
                
bool NormFileObject::Accept(const char* thePath)
{
    if (Open(thePath))
    {
        NormObject::Accept(); 
        return true;  
    }
    else
    {
        return false;
    }
}  // end NormFileObject::Accept()

void NormFileObject::Close()
{
    NormObject::Close();
    file.Close();
}  // end NormFileObject::Close()

bool NormFileObject::WriteSegment(NormBlockId   blockId, 
                                  NormSegmentId segmentId, 
                                  const char*   buffer)
{
    UINT16 len;  
    if ((blockId == last_block_id) &&
        (segmentId == (last_block_size-1)))
        len = last_segment_size;
    else
        len = segment_size;
    NormObjectSize segmentOffset = NormDataMsg::ReadOffset(buffer);
    off_t offset = segmentOffset.LSB() + (segmentOffset.MSB() * 0x100000000LL);
    if (offset != file.GetOffset())
    {
        if (!file.Seek(offset)) return false; 
    }
    UINT16 nbytes = file.Write(buffer+NormDataMsg::PayloadHeaderLen(), len);
    return (nbytes == len);
}  // end NormFileObject::WriteSegment()


bool NormFileObject::ReadSegment(NormBlockId      blockId, 
                                 NormSegmentId    segmentId,
                                 char*            buffer)            
{
    // Determine segment length
    UINT16 len;
    if ((blockId == last_block_id) &&
        (segmentId == (last_block_size - 1)))
        len = last_segment_size;
    else
        len = segment_size;
    // Determine segment offset from blockId::segmentId
    NormObjectSize segmentOffset = NormObjectSize(blockId) * block_size;
    segmentOffset = segmentOffset + (NormObjectSize(segmentId) * NormObjectSize(segment_size));    
    NormDataMsg::WriteLength(buffer, len);
    NormDataMsg::WriteOffset(buffer, segmentOffset);
    off_t offset = segmentOffset.LSB() + (segmentOffset.MSB() * 0x100000000LL);
    if (offset != file.GetOffset())
    {
        if (!file.Seek(offset)) return false;
    }
    UINT16 nbytes = file.Read(buffer+NormDataMsg::PayloadHeaderLen(), len);
    return (len == nbytes);
}  // end NormFileObject::ReadSegment()


/////////////////////////////////////////////////////////////////
//
// NormStreamObject Implementation
//

NormStreamObject::NormStreamObject(class NormSession*       theSession, 
                                   class NormServerNode*    theServer,
                                   const NormObjectId&      objectId)
 : NormObject(STREAM, theSession, theServer, objectId), flush_pending(false)
{
}

NormStreamObject::~NormStreamObject()
{
    Close();
}  

bool NormStreamObject::Open(unsigned long   bufferSize, 
                            const char*     infoPtr, 
                            UINT16          infoLen)
{
    if (!bufferSize) 
    {
        DMSG(0, "NormStreamObject::Open() zero bufferSize error\n");
        return false;
    }
    
    UINT16 segmentSize, numData, numParity;
    if (server)
    {
        segmentSize = server->SegmentSize();
        numData = server->BlockSize();
        numParity = server->NumParity();
    }
    else
    {
        segmentSize = session->ServerSegmentSize();
        numData = session->ServerBlockSize();
        numParity = session->ServerNumParity();
    }
    
    unsigned long numSegments = bufferSize / segmentSize;
    if ((numSegments*segmentSize) < bufferSize) numSegments++; 
    NormObjectSize blockSize((UINT32)segmentSize * (UINT32)numData);
    NormObjectSize numBlocks = NormObjectSize(bufferSize) / blockSize;
    ASSERT(0 == numBlocks.MSB());
    
    // Buffering requires at least 2 segments & 2 blocks
    numSegments = MAX(2, numSegments);
    numBlocks = MAX(2, numBlocks.LSB());
    
    if (!block_pool.Init(numBlocks.LSB(), numData))
    {
        DMSG(0, "NormStreamObject::Open() block_pool init error\n");
        Close();
        return false;
    }
    
    if (!segment_pool.Init(numSegments, segmentSize+NormDataMsg::PayloadHeaderLen()))
    {
        DMSG(0, "NormStreamObject::Open() segment_pool init error\n");
        Close();
        return false;
    }
    
    if (!stream_buffer.Init(numBlocks.LSB()))
    {
        DMSG(0, "NormStreamObject::Open() stream_buffer init error\n");
        Close();
        return false;
    }    
    // (TBD) we really only need one set of indexes
    // since our objects are exclusively read _or_ write
    write_index.block = write_index.segment = 0;
    read_index.block = read_index.segment = 0; 
    write_offset = read_offset = NormObjectSize((UINT32)0);    
    if (!server)
    {
        if (!NormObject::Open(NormObjectSize(bufferSize), infoPtr, infoLen))
        {
            DMSG(0, "NormStreamObject::Open() object open error\n");
            Close();
            return false;
        }
    }
    return true;
}  // end NormStreamObject::Open()

bool NormStreamObject::Accept(unsigned long bufferSize)
{
    if (Open(bufferSize))
    {
        NormObject::Accept(); 
        return true;  
    }
    else
    {
        return false;
    }
}  // end NormStreamObject::Accept()

void NormStreamObject::Close()
{
    NormObject::Close();
    write_offset = read_offset = NormObjectSize((UINT32)0);
    NormBlock* b;
    while ((b = stream_buffer.Find(stream_buffer.RangeLo())))
    {
        stream_buffer.Remove(b);
        b->EmptyToPool(segment_pool);
        block_pool.Put(b);   
    }
    stream_buffer.Destroy();
    segment_pool.Destroy();
    block_pool.Destroy();
}  // end NormStreamObject::Close()

bool NormStreamObject::LockBlocks(NormBlockId nextId, NormBlockId lastId)
{
    while (nextId <= lastId)
    {
        NormBlock* block = stream_buffer.Find(nextId);
        if (block)
        {
            block->SetPending(0, ndata);
        }
        else
        {
            return false;
        }   
        nextId++;   
    }
    return true;
}  // end NormStreamObject::LockBlocks()


bool NormStreamObject::LockSegments(NormBlockId blockId, NormSegmentId firstId, NormSegmentId lastId)
{
    NormBlock* block = stream_buffer.Find(blockId);
    if (block)
    {
        ASSERT(firstId <= lastId);
        block->SetPending(firstId, (lastId - firstId + 1));
        return true;
    }
    else
    {
        return false;
    }
}  // end NormStreamObject::LockSegments()

bool NormStreamObject::ReadSegment(NormBlockId      blockId, 
                                   NormSegmentId    segmentId,
                                   char*            buffer)
{
    // (TBD) compare blockId with stream_buffer.RangeLo() and stream_buffer.RangeHi()
    NormBlock* block = stream_buffer.Find(blockId);
    if (!block)
    {
        //DMSG(0, "NormStreamObject::ReadSegment() stream starved (1)\n");
        return false;   
    }
    if ((blockId == write_index.block) &&
        (segmentId >= write_index.segment))
    {
        //DMSG(0, "NormStreamObject::ReadSegment() stream starved (2)\n");
        return false;   
    }   
    block->UnsetPending(segmentId);    
    char* segment = block->Segment(segmentId);
    ASSERT(segment);    
    UINT16 length = NormDataMsg::ReadLength(segment);
    ASSERT(length <= segment_size);
    memcpy(buffer, segment, length+NormDataMsg::PayloadHeaderLen());
    return true;
}  // end NormStreamObject::Read()

bool NormStreamObject::WriteSegment(NormBlockId   blockId, 
                                    NormSegmentId segmentId, 
                                    const char*   segment)
{
    if ((blockId < read_index.block) ||
        ((blockId == read_index.block) &&
         (segmentId < read_index.segment))) 
    {
        //DMSG(0, "NormStreamObject::WriteSegment() block/segment < read_index!?\n");
        return false;
    }
    NormBlock* block = stream_buffer.Find(blockId);
    if (!block)
    {
        bool broken = false;
        // Prune (if necessary) stream_buffer (stream is broken)
        while (!stream_buffer.CanInsert(blockId) || block_pool.IsEmpty())
        {
            block = stream_buffer.Find(stream_buffer.RangeLo());
            stream_buffer.Remove(block);
            if (block->IsPending()) broken = true;
            block->EmptyToPool(segment_pool);
            block_pool.Put(block);
        }
        block = block_pool.Get();
        block->SetId(blockId);
        block->ClearPending();
        bool success = stream_buffer.Insert(block);
        ASSERT(success);
        if (broken)
        {
            DMSG(2, "NormStreamObject::WriteSegment() node>%lu obj>%hu blk>%lu seg>%hu broken stream ...\n",
                     LocalNodeId(), (UINT16)id, (UINT32)blockId, (UINT16)segmentId);
            NormBlock* first = stream_buffer.Find(stream_buffer.RangeLo());
            read_index.block = first->Id();
            read_index.segment = 0;   
        }
        
    }
    // Make sure this segment hasn't already been written
    if(!block->Segment(segmentId))
    {
        char* s = segment_pool.Get();
        ASSERT(s);  // for now, this should always succeed
        UINT16 length = NormDataMsg::ReadLength(segment);
        memcpy(s, segment, length + NormDataMsg::PayloadHeaderLen());
        block->AttachSegment(segmentId, s);
        block->SetPending(segmentId);
    }
    return true;
}  // end NormStreamObject::WriteSegment()

void NormStreamObject::Prune(NormBlockId blockId)
{
    bool resync = false;
    NormBlock* block;
    while ((block = block_buffer.Find(block_buffer.RangeLo())))
    {
        if (block->Id() < blockId)
        {
            resync = true;
            pending_mask.Unset(block->Id()); 
            block_buffer.Remove(block);
            server->PutFreeBlock(block);
        }   
        else
        {
            break;
        }
    }
    if (pending_mask.IsSet())
    {
        NormBlockId firstId = pending_mask.FirstSet();
        if (firstId < blockId)
        {
            resync = true;
            UINT32 count = blockId - firstId;
            pending_mask.UnsetBits(firstId, count);
        }
    }
    if (resync) server->IncrementResyncCount();
    StreamUpdateStatus(blockId);
}  // end NormStreamObject::Prune()

// Sequential (in order) read/write routines (TBD) Add a "Seek()" method
unsigned long NormStreamObject::Read(char* buffer, unsigned long len)
{
    unsigned long nBytes = 0;
    while (len > 0)
    {
        NormBlock* block = stream_buffer.Find(read_index.block);
        if (!block)
        {
            //DMSG(0, "NormStreamObject::Read() stream buffer empty (1)\n");
            return nBytes;   
        }
        char* segment = block->Segment(read_index.segment);
        if (!segment)
        {
            //DMSG(0, "NormStreamObject::Read() stream buffer empty (2)\n");
            return nBytes;
        }
        
        NormObjectSize segmentOffset = NormDataMsg::ReadOffset(segment);
        if (segmentOffset > read_offset)
        {
            DMSG(0, "NormStreamObject::Read() node>%lu broken stream!\n", LocalNodeId());
            read_offset = segmentOffset;
            return nBytes;
        }
        NormObjectSize delta = read_offset - segmentOffset;
        ASSERT(!delta.MSB());
        UINT16 index = delta.LSB(); 
        UINT16 length = NormDataMsg::ReadLength(segment);
        ASSERT(index < length);
        UINT16 count = length - index;
        count = MIN(count, len);
        memcpy(buffer+nBytes, segment+index+NormDataMsg::PayloadHeaderLen(), count);
        index += count;
        nBytes += count;
        read_offset += count;
        len -= count;
        if (index >= length)
        {
            block->DetachSegment(read_index.segment);
            segment_pool.Put(segment);
            block->UnsetPending(read_index.segment++);
            if (read_index.segment >= ndata) 
            {
                stream_buffer.Remove(block);
                block->EmptyToPool(segment_pool);
                block_pool.Put(block);
                read_index.block++;
                read_index.segment = 0;
            }
        }   
    }  // end while (len > 0)
    return nBytes;
}  // end NormStreamObject::Read()

unsigned long NormStreamObject::Write(char* buffer, unsigned long len, bool flush)
{
    ASSERT(!server);
    unsigned long nBytes = 0;
    do 
    {
        NormBlock* block = stream_buffer.Find(write_index.block);
        if (!block)
        {
            if (!(block = block_pool.Get()))
            {
                block = stream_buffer.Find(stream_buffer.RangeLo());
                ASSERT(block);
                if (block->IsPending())
                {
                    DMSG(0, "NormStreamObject::Write() stream buffer full (1)\n");
                    break;
                }
                else
                {
                    stream_buffer.Remove(block);
                    block->EmptyToPool(segment_pool);
                }
            }
            block->SetId(write_index.block);
            block->ClearPending();
            bool success = stream_buffer.Insert(block);
            ASSERT(success);
        }
        char* segment = block->Segment(write_index.segment);
        if (!segment)
        {
            if (!(segment = segment_pool.Get()))
            {
                NormBlock* b = stream_buffer.Find(stream_buffer.RangeLo());
                ASSERT(b != block);
                if (b->IsPending())
                {
                    DMSG(0, "NormStreamObject::Write() stream buffer full (2)\n");
                    break;
                }
                stream_buffer.Remove(b);
                b->EmptyToPool(segment_pool);
                block_pool.Put(b);
                segment = segment_pool.Get();
                ASSERT(segment);
            }
            NormDataMsg::WriteOffset(segment, write_offset);  
            NormDataMsg::WriteLength(segment, 0);
            block->AttachSegment(write_index.segment, segment);   
        }
        UINT16 index = NormDataMsg::ReadLength(segment);
        ASSERT(write_offset == (NormDataMsg::ReadOffset(segment)+NormObjectSize(index)));
        UINT16 space = segment_size - index;
        UINT16 count = MIN(space, len);
        memcpy(segment+index+NormDataMsg::PayloadHeaderLen(), buffer+nBytes, count);
        NormDataMsg::WriteLength(segment, index+count);
        len -= count;
        nBytes += count;
        write_offset += count;
        // Is the segment full?
        if ((count == space) || (flush && (index > 0)))
        {   
            block->SetPending(write_index.segment);
            if (++write_index.segment >= ndata) 
            {
                write_index.block++;
                write_index.segment = 0;
            }
        }
    } while (len > 0);
    if (flush) 
        flush_pending = true;
    else
        flush_pending = false;
    if (nBytes) session->TouchServer();
    return nBytes;
}  // end NormStreamObject::Write()

#ifdef SIMULATE
/////////////////////////////////////////////////////////////////
//
// NormSimObject Implementation
//

NormSimObject::NormSimObject(class NormSession*       theSession,
                             class NormServerNode*    theServer,
                             const NormObjectId&      objectId)
 : NormObject(FILE, theSession, theServer, objectId)
{
    
}

NormSimObject::~NormSimObject()
{
    
}

bool NormSimObject::ReadSegment(NormBlockId      blockId, 
                                NormSegmentId    segmentId,
                                char*            buffer)            
{
    // Determine segment length
    UINT16 len;
    if ((blockId == last_block_id) &&
        (segmentId == (last_block_size - 1)))
        len = last_segment_size;
    else
        len = segment_size;
    // the "len" is needed to build the correct size message
    NormDataMsg::WriteLength(buffer, len);
    return true;
}  // end NormSimObject::ReadSegment()

#endif // SIMULATE      

/////////////////////////////////////////////////////////////////
//
// NormObjectTable Implementation
//

NormObjectTable::NormObjectTable()
 : table((NormObject**)NULL), range_max(0), range(0),
   count(0), size(0,0)
{
}

NormObjectTable::~NormObjectTable()
{
    Destroy();
}

bool NormObjectTable::Init(UINT16 rangeMax, UINT16 tableSize)
{
    if (table) Destroy();
    // Make sure tableSize is greater than 0 and 2^n
    if (!rangeMax || !tableSize) return false;
    if (0 != (tableSize & 0x07)) tableSize = (tableSize >> 3) + 1;
    if (!(table = new NormObject*[tableSize]))
    {
        DMSG(0, "NormObjectTable::Init() table allocation error: %s\n", strerror(errno));
        return false;         
    }
    memset(table, 0, tableSize*sizeof(char*));
    hash_mask = tableSize - 1;
    range_max = rangeMax;
    range = 0;
    return true;
}  // end NormObjectTable::Init()

void NormObjectTable::Destroy()
{
    if (table)
    {
        NormObject* obj;
        while((obj = Find(range_lo)))
        {
            Remove(obj);
            delete obj;   
        }
        delete []table;
        table = (NormObject**)NULL;
        range = range_max = 0;
    }     
}  // end NormObjectTable::Destroy()

NormObject* NormObjectTable::Find(const NormObjectId& objectId) const
{
    if (range)
    {
        if ((objectId < range_lo)  || (objectId > range_hi)) return (NormObject*)NULL;
        NormObject* theObject = table[((UINT16)objectId) & hash_mask];
        while (theObject && (objectId != theObject->Id())) theObject = theObject->next;
        return theObject;
    }
    else
    {
        return (NormObject*)NULL;
    }   
}  // end NormObjectTable::Find()

bool NormObjectTable::CanInsert(NormObjectId objectId) const
{
    if (0 != range)
    {
        if (objectId < range_lo)
        {
            if ((range_lo - objectId + range) > range_max)
                return false;
            else
                return true;
        }
        else if (objectId > range_hi)
        {
            if ((objectId - range_hi + range) > range_max)
                return false;
            else
                return true;
        }
        else
        {
            return true;
        }        
    }
    else
    {
        return true;
    }    
}  // end NormObjectTable::CanInsert()


bool NormObjectTable::Insert(NormObject* theObject)
{
    const NormObjectId& objectId = theObject->Id();
    if (!range)
    {
        range_lo = range_hi = objectId;
        range = 1;   
    }
    if (objectId < range_lo)
    {
        UINT16 newRange = range_lo - objectId + range;
        if (newRange > range_max) return false;
        range_lo = objectId;
        range = newRange;
    }
    else if (objectId > range_hi)
    {            
        UINT16 newRange = objectId - range_hi + range;
        if (newRange > range_max) return false;
        range_hi = objectId;
        range = newRange;
    }
    UINT16 index = ((UINT16)objectId) & hash_mask;
    NormObject* prev = NULL;
    NormObject* entry = table[index];
    while (entry && (entry->Id() < objectId)) 
    {
        prev = entry;
        entry = entry->next;
    }  
    if (prev)
        prev->next = theObject;
    else
        table[index] = theObject;
    ASSERT((entry ? (objectId != entry->Id()) : true));
    theObject->next = entry;
    count++;
    size = size + theObject->Size();
    return true;
}  // end NormObjectTable::Insert()

bool NormObjectTable::Remove(const NormObject* theObject)
{
    ASSERT(theObject);
    const NormObjectId& objectId = theObject->Id();
    if (range)
    {
        if ((objectId < range_lo) || (objectId > range_hi)) return false;
        UINT16 index = ((UINT16)objectId) & hash_mask;
        NormObject* prev = NULL;
        NormObject* entry = table[index];
        while (entry && (entry->Id() != objectId))
        {
            prev = entry;
            entry = entry->next;
        }
        if (entry != theObject) return false;
        if (prev)
            prev->next = entry->next;
        else
            table[index] = entry->next;
        if (range > 1)
        {
            if (objectId == range_lo)
            {
                // Find next entry for range_lo
                UINT16 i = index;
                UINT16 endex;
                if (range <= hash_mask)
                    endex = (index + range - 1) & hash_mask;
                else
                    endex = index;
                entry = NULL;
                UINT16 offset = 0;
                NormObjectId nextId = range_hi;
                do
                {
                    ++i &= hash_mask;
                    offset++;
                    if ((entry = table[i]))
                    {
                        NormObjectId id = (UINT16)index + offset;
                        while(entry && (entry->Id() != id)) 
                        {
                            if ((entry->Id() > objectId) && 
                                (entry->Id() < nextId)) nextId = entry->Id();
                            entry = entry->next;
                               
                        }
                        if (entry) break;    
                    }
                } while (i != endex);
                if (entry)
                    range_lo = entry->Id();
                else
                    range_lo = nextId;
                range = range_hi - range_lo + 1;
            }
            else if (objectId == range_hi)
            {
                // Find prev entry for range_hi
                UINT16 i = index;
                UINT16 endex;
                if (range <= hash_mask)
                    endex = (index - range + 1) & hash_mask;
                else
                    endex = index;
                entry = NULL;
                UINT16 offset = 0;
                //printf("preving i:%lu endex:%lu lo:%lu hi:%lu\n", i, endex, (UINT16)range_lo, (UINT16) range_hi);
                NormObjectId prevId = range_lo;
                do
                {
                    --i &= hash_mask;
                    offset++;
                    if ((entry = table[i]))
                    {
                        NormObjectId id = (UINT16)index - offset;
                        //printf("Looking for id:%lu at index:%lu\n", (UINT16)id, i);
                        while(entry && (entry->Id() != id)) 
                        {
                            if ((entry->Id() < objectId) && 
                                (entry->Id() > prevId)) prevId = entry->Id();
                            entry = entry->next;
                        }
                        if (entry) break;    
                    }
                } while (i != endex);
                if (entry)
                    range_hi = entry->Id();
                else
                    range_hi = prevId;
                range = range_hi - range_lo + 1;
            } 
        }
        else
        {
            range = 0;
        }  
        count--;
        size = size - theObject->Size();
        return true;
    }
    else
    {
        return false;
    }
}  // end NormObjectTable::Remove()

NormObjectTable::Iterator::Iterator(const NormObjectTable& objectTable)
 : table(objectTable), reset(true)
{
}

NormObject* NormObjectTable::Iterator::GetNextObject()
{
    if (reset)
    {
        if (table.range)
        {
            reset = false;
            index = table.range_lo;
            return table.Find(index);
        }
        else
        {
            return (NormObject*)NULL;
        }
    }
    else
    {
        if (table.range && 
            (index < table.range_hi) && 
            (index >= table.range_lo))
        {
            // Find next entry _after_ current "index"
            UINT16 i = index;
            UINT16 endex;
            if ((UINT16)(table.range_hi - index) <= table.hash_mask)
                endex = table.range_hi & table.hash_mask;
            else
                endex = index;
            UINT16 offset = 0;
            NormObjectId nextId = table.range_hi;
            do
            {
                ++i &= table.hash_mask;
                offset++;
                NormObjectId id = (UINT16)index + offset;
                NormObject* entry = table.table[i];
                while ((NULL != entry) && (entry->Id() != id)) 
                {
                    if ((entry->Id() > index) && (entry->Id() < nextId))
                        nextId = entry->Id();
                    entry = table.Next(entry);
                }
                if (entry)
                {
                    index = entry->Id();
                    return entry;   
                } 
            } while (i != endex);
            // If we get here, use nextId value
            index = nextId;
            return table.Find(nextId);
        }
        else
        {
            return (NormObject*)NULL;
        }
    }   
}  // end NormObjectTable::Iterator::GetNextObject()

NormObject* NormObjectTable::Iterator::GetPrevObject()
{
    if (reset)
    {
        if (table.range)
        {
            reset = false;
            index = table.range_hi;
            return table.Find(index);
        }
        else
        {
            return (NormObject*)NULL;
        }
    }
    else
    {
        if (table.range && 
            (index <= table.range_hi) && 
            (index > table.range_lo))
        {
            // Find prev entry _before_ current "index"
            UINT16 i = index;
            UINT16 endex;
            if ((UINT16)(index - table.range_lo) <= table.hash_mask)
                endex = table.range_lo & table.hash_mask;
            else
                endex = index;
            UINT16 offset = 0;
            NormObjectId nextId = table.range_hi;
            do
            {
                --i &= table.hash_mask;
                offset--;
                NormObjectId id = (UINT16)index + offset;
                NormObject* entry = table.table[i];
                while ((NULL != entry ) && (entry->Id() != id)) 
                {
                    if ((entry->Id() > index) && (entry->Id() < nextId))
                        nextId = entry->Id();
                    entry = table.Next(entry);
                }
                if (entry)
                {
                    index = entry->Id();
                    return entry;   
                } 
            } while (i != endex);
            // If we get here, use nextId value
            index = nextId;
            return table.Find(nextId);
        }
        else
        {
            return (NormObject*)NULL;
        }
    }
}  // end NormObjectTable::Iterator::GetPrevObject()
