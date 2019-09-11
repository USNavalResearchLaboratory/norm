#include "normObject.h"
#include "normSession.h"

#include <errno.h>

NormObject::NormObject(NormObject::Type      theType, 
                       class NormSession*    theSession, 
                       class NormServerNode* theServer,
                       const NormObjectId&   objectId)
 : type(theType), session(theSession), server(theServer),
   id(objectId), segment_size(0), pending_info(false), repair_info(false),
   current_block_id(0), next_segment_id(0), 
   max_pending_block(0), max_pending_segment(0),
   info(NULL), info_len(0), accepted(false)
{
}

NormObject::~NormObject()
{
    /*if (server)
        server->DeleteObject(true); 
    else
        session->DeleteTxObject(true);*/
    Close();
    if (info) 
    {
        delete info;
        info = NULL;
    }
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
        numData = server->BlockSize();   // max source symbols per FEC block
        numParity = server->NumParity(); // max parity symbols per FEC block
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
        numData = session->ServerBlockSize();   // max source symbols per FEC block
        numParity = session->ServerNumParity(); // max parity symbols per FEC block
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
    
    // Compute number of segments and coding blocks for the object
    // (Note NormObjectSize divide operator always rounds _up_)
    NormObjectSize numSegments = objectSize / NormObjectSize(segmentSize);
    NormObjectSize numBlocks = numSegments / NormObjectSize(numData);
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
    pending_mask.Clear();
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
        small_block_size = large_block_size = numData;
        small_block_count = large_block_count = numBlocks.LSB();
        final_segment_size = segmentSize;
        NormStreamObject* stream = static_cast<NormStreamObject*>(this);
        stream->StreamResync(numBlocks.LSB());
    }
    else
    {
        // Compute FEC block structure per NORM Protocol Spec Section 5.1.1
        // (Note NormObjectSize divide operator always rounds _up_)
        NormObjectSize largeBlockSize = numSegments / numBlocks;
        ASSERT(0 == largeBlockSize.MSB());
        large_block_size = largeBlockSize.LSB();
        if (numSegments == (numBlocks*largeBlockSize))
        {
            small_block_size = large_block_size;
            small_block_count = numBlocks.LSB();
            large_block_count = 0;
        }
        else
        {
            small_block_size = large_block_size - 1;
            NormObjectSize largeBlockCount = numSegments - numBlocks*small_block_size;
            ASSERT(0 == largeBlockCount.MSB());
            large_block_count = largeBlockCount.LSB();
            NormObjectSize smallBlockCount = numBlocks - largeBlockCount;
            ASSERT(0 == smallBlockCount.MSB());
            small_block_count = smallBlockCount.LSB();
        }
        
        final_block_id = large_block_count + small_block_count - 1;  // not used for STREAM objects
        NormObjectSize finalSegmentSize = 
            objectSize - (numSegments - NormObjectSize((UINT32)1))*segmentSize;
        ASSERT(0 == finalSegmentSize.MSB());
        final_segment_size = finalSegmentSize.LSB();
    }
    
    object_size = objectSize;
    segment_size = segmentSize;
    ndata = numData;
    nparity = numParity;
    current_block_id = 0;
    next_segment_id = 0;
    max_pending_block = 0;
    max_pending_segment = 0;
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
    segment_size = 0;
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
    lastId++;
    while (nextId != lastId)
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
            increasedRepair |= block->TxReset(GetBlockSize(blockId), 
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
    lastId++;
    while (nextId != lastId)
    {
        if (!pending_mask.Test(nextId))
        {
            pending_mask.Set(nextId);
            increasedRepair = true;
        }
        NormBlock* block = block_buffer.Find(nextId);
        if (block) 
            increasedRepair |= block->TxReset(GetBlockSize(block->Id()), nparity, autoParity, segment_size);
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
            if (block) block->TxReset(GetBlockSize(nextId), nparity, autoParity, segment_size);
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

// called by servers only
bool NormObject::AppendRepairAdv(NormCmdRepairAdvMsg& cmd)
{
    // Determine range of blocks possibly pending repair
    NormBlockId nextId = repair_mask.FirstSet();
    NormBlockId endId = repair_mask.LastSet();
    if (block_buffer.IsEmpty())
    {
        if (repair_mask.IsSet()) endId++;
    }
    else
    {
        NormBlockId lo = block_buffer.RangeLo();
        NormBlockId hi = block_buffer.RangeHi();
        if (repair_mask.IsSet())
        {
            nextId = (lo < nextId) ? lo : nextId;
            endId = (hi > endId) ? hi : endId;
        }
        else
        {
            nextId = lo;
            endId = hi;
        }
        endId++;
    }
    // Instantiate a repair request for BLOCK level repairs
    NormRepairRequest req;
    req.SetFlag(NormRepairRequest::BLOCK);
    if (repair_info) req.SetFlag(NormRepairRequest::INFO);
    NormRepairRequest::Form prevForm = NormRepairRequest::INVALID;
    // Iterate through the range of blocks possibly pending repair
    NormBlockId firstId;
    UINT32 blockCount = 0;
    while (nextId < endId)
    {
        NormBlockId currentId = nextId;
        nextId++;
        //TRACE("NormObject::AppendRepairAdv() testing block:%lu nextId:%lu endId:%lu\n", 
        //        (UINT32)currentId, (UINT32)nextId, (UINT32)endId);
        bool repairEntireBlock = repair_mask.Test(currentId);
        if (repairEntireBlock)
        {
            if (!blockCount) firstId = currentId;
            blockCount++;    
        }        
        // Check for break in continuity or end
        if (blockCount && (!repairEntireBlock || (nextId >= endId)))
        {
            NormRepairRequest::Form form;
            switch (blockCount)
            {
                case 0:
                    form = NormRepairRequest::INVALID;
                    break;
                case 1:
                case 2:
                    form = NormRepairRequest::ITEMS;
                    break;
                default:
                    form = NormRepairRequest::RANGES;
                    break;   
            }
            if (form != prevForm)
            {
                if (NormRepairRequest::INVALID != prevForm)
                    cmd.PackRepairRequest(req);             // (TBD) error check 
                cmd.AttachRepairRequest(req, segment_size); // (TBD) error check 
                req.SetForm(form);
                prevForm = form;
            }
            switch (form)
            {
                case NormRepairRequest::INVALID:
                    ASSERT(0);  // can't happen
                    break;
                case NormRepairRequest::ITEMS:
                    req.AppendRepairItem(id, firstId, ndata, 0);
                    if (2 == blockCount) 
                        req.AppendRepairItem(id, currentId, ndata, 0);
                    break;
                case NormRepairRequest::RANGES:
                    req.AppendRepairRange(id, firstId, ndata, 0,
                                          id, currentId, ndata, 0);
                    break;
                case NormRepairRequest::ERASURES:
                    // erasure counts not used
                    break;
            } 
            blockCount = 0; 
        }
        if (!repairEntireBlock)
        {
            NormBlock* block = block_buffer.Find(currentId);
            if (block && block->IsRepairPending())
            {
                if (NormRepairRequest::INVALID != prevForm) 
                {
                    cmd.PackRepairRequest(req); // (TBD) error check 
                    prevForm = NormRepairRequest::INVALID;
                }
                block->AppendRepairAdv(cmd, id, repair_info, ndata, segment_size);  // (TBD) error check        
            }
        }
    }  // end while(nextId < endId)
    if (NormRepairRequest::INVALID != prevForm) 
        cmd.PackRepairRequest(req); // (TBD) error check 
    return true;
}  //  end NormObject::AppendRepairAdv()

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
            if (firstId < max_pending_block)
            {
                return true;
            }
            else if (firstId > max_pending_block)
            {
                return false;
            }
            else
            {
                if (max_pending_segment > 0)
                {
                    NormBlock* block = block_buffer.Find(max_pending_block);
                    if (block)
                    {
                        if (block->FirstPending() < max_pending_segment)
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
                                   bool          timerActive,
                                   bool          holdoffPhase)
{
    // (TBD) make sure it's OK for "max_pending_segment"
    //       and "next_segment_id" to be > blockSize
    switch (level)
    {
        case THRU_INFO:
            break;
        case TO_BLOCK:
            if (blockId >= max_pending_block)
            {
                max_pending_block = blockId;
                max_pending_segment = 0;
            }                        
            break;
        case THRU_SEGMENT:
            if (blockId > max_pending_block)
            {
                max_pending_block = blockId;
                max_pending_segment = segmentId + 1;
            }
            else if (blockId == max_pending_block)
            {
                if (segmentId >= max_pending_segment) 
                    max_pending_segment = segmentId + 1;  
            }
            break;
        case THRU_BLOCK:
            if (blockId > max_pending_block)
            {
                max_pending_block = blockId;
                max_pending_segment = GetBlockSize(blockId);
            }
            break;
        case THRU_OBJECT:
            if (!IsStream()) 
                max_pending_block = final_block_id;
            else if (blockId > max_pending_block)
                max_pending_block = blockId;
            max_pending_segment = GetBlockSize(max_pending_block);
        default:
            break;
    }  // end switch (level)
       
    if (timerActive)
    {
        if (holdoffPhase) return false;
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
                    next_segment_id = GetBlockSize(blockId);
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
            next_segment_id = GetBlockSize(blockId);
            break;
        case THRU_OBJECT:
            if (IsStream())
                current_block_id = max_pending_block;
            else
                current_block_id = final_block_id;
            next_segment_id = GetBlockSize(current_block_id);
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
                UINT16 numData = GetBlockSize(nextId);
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
    // If !flush, we request only up _to_ max_pending_block::max_pending_segment.
    NormRepairRequest req;
    NormRepairRequest::Form prevForm = NormRepairRequest::INVALID;
    NormBlockId prevId;
    UINT16 reqCount = 0;
    if (pending_mask.IsSet())
    {
       NormBlockId nextId = pending_mask.FirstSet();
       NormBlockId lastId = pending_mask.LastSet();
       if (!flush && (max_pending_block < lastId)) lastId = max_pending_block;
       lastId++;
       DMSG(6, "NormObject::AppendRepairRequest() node>%lu obj>%hu, blk>%lu->%lu (maxPending:%lu)\n",
               LocalNodeId(), (UINT16)id,
               (UINT32)nextId, (UINT32)lastId, (UINT32)max_pending_block);
       
       // (TBD) simplify this loop code
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
                if (nextId == max_pending_block)
                {
                    if (block->FirstPending() < max_pending_segment)
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
                        req.AppendRepairItem(id, prevId, ndata, 0);  // (TBD) error check
                        if (2 == reqCount)
                            req.AppendRepairItem(id, prevId+1, ndata, 0); // (TBD) error check
                        break;
                    case NormRepairRequest::RANGES:
                        req.AppendRepairItem(id, prevId, ndata, 0); // (TBD) error check
                        req.AppendRepairItem(id, prevId+reqCount-1, ndata, 0); // (TBD) error check
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
                UINT16 numData = GetBlockSize(nextId);
                if (NormRepairRequest::INVALID != prevForm)
                    nack.PackRepairRequest(req);  // (TBD) error check
                reqCount = 0;
                prevForm = NormRepairRequest::INVALID; 
                if (flush || (nextId != max_pending_block))
                {
                    block->AppendRepairRequest(nack, numData, nparity, id, 
                                               pending_info, segment_size); // (TBD) error check
                }
                else
                {
                    if (max_pending_segment < numData)
                        block->AppendRepairRequest(nack, max_pending_segment, 0, id,
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
        req.AppendRepairItem(id, 0, ndata, 0);  // (TBD) error check
        nack.PackRepairRequest(req);  // (TBD) error check
    }  // end if/else pending_mask.IsSet()
    
    return true;
}  // end NormObject::AppendRepairRequest()


void NormObject::HandleObjectMessage(const NormObjectMsg& msg, 
                                     NormMsg::Type        msgType,
                                     NormBlockId          blockId,
                                     NormSegmentId        segmentId)
{
    if (NormMsg::INFO == msgType)
    {
        if (pending_info)
        {
            const NormInfoMsg& infoMsg = (const NormInfoMsg&)msg;
            info_len = infoMsg.GetInfoLen();
            if (info_len > segment_size)
            {
                info_len = segment_size;
                DMSG(0, "NormObject::HandleObjectMessage() node>%lu server>%lu obj>%hu "
                    "Warning! info too long.\n", LocalNodeId(), server->GetId(), (UINT16)id);   
            }
            memcpy(info, infoMsg.GetInfo(), info_len);
            pending_info = false;
            session->Notify(NormController::RX_OBJECT_INFO, server, this);
        }
        else
        {
            // (TBD) Verify info hasn't changed?   
            DMSG(6, "NormObject::HandleObjectMessage() node>%lu server>%lu obj>%hu "
                    "received duplicate info ...\n", LocalNodeId(),
                     server->GetId(), (UINT16)id);
        }
    }
    else  // NORM_MSG_DATA
    {
        const NormDataMsg& data = (const NormDataMsg&)msg;
        // For stream objects, a little extra mgmt is required
        if (STREAM == type)
        {
            NormStreamObject* stream = static_cast<NormStreamObject*>(this);            
            if (!stream->StreamUpdateStatus(blockId))
            {
                DMSG(4, "NormObject::HandleObjectMessage() node:%lu server:%lu obj>%hu blk>%lu "
                        "broken stream ...\n", LocalNodeId(), server->GetId(), (UINT16)id, (UINT32)blockId);
                
                //ASSERT(0);
                // ??? Ignore this new packet and try to fix stream ???
                //return;
                server->IncrementResyncCount();
            
                while (!stream->StreamUpdateStatus(blockId))
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
                        stream->StreamResync(blockId);// - NormBlockId(pending_mask.Size()));
                    }          
                }
            }
        }
        UINT16 numData = GetBlockSize(blockId);
        if (pending_mask.Test(blockId))
        {
            NormBlock* block = block_buffer.Find(blockId);
            if (!block)
            {
                if (!(block = server->GetFreeBlock(id, blockId)))
                {
                    DMSG(2, "NormObject::HandleObjectMessage() node>%lu server>%lu obj>%hu "
                            "Warning! no free blocks ...\n", LocalNodeId(), server->GetId(), 
                            (UINT16)id);  
                    return;
                }
                block->RxInit(blockId, numData, nparity);
                block_buffer.Insert(block);
            }
            if (block->IsPending(segmentId))
            {
                // 1) Cache segment in block buffer in case its needed for decoding
                char* segment = server->GetFreeSegment(id, blockId);
                if (!segment)
                {
                    DMSG(2, "NormObject::HandleObjectMessage() node>%lu server>%lu obj>%hu "
                            "Warning! no free segments ...\n", LocalNodeId(), server->GetId(), 
                            (UINT16)id);  
                    return;
                }
                UINT16 segmentLength = data.GetPayloadDataLength();
                if (segmentLength > segment_size)
                {
                    DMSG(0, "NormObject::HandleObjectMessage() node>%lu server>%lu obj>%hu "
                            "Error! segment too large ...\n", LocalNodeId(), server->GetId(), 
                            (UINT16)id);  
                    server->PutFreeSegment(segment);
                    return;  
                }
                UINT16 payloadLength = data.GetPayloadLength();
                UINT16 payloadMax = segment_size + NormDataMsg::GetStreamPayloadHeaderLength();
#ifdef SIMULATE
                // For simulations, we may need to cap the payloadLength
                payloadMax = MIN(payloadMax, SIM_PAYLOAD_MAX);
#endif // SIMULATE
                if (payloadLength < payloadMax)
                    memset(segment+payloadLength, 0, payloadMax-payloadLength);
                memcpy(segment, data.GetPayload(), payloadLength);
                if (data.FlagIsSet(NormObjectMsg::FLAG_MSG_START))
                    segment[payloadMax] = NormObjectMsg::FLAG_MSG_START;
                else
                    segment[payloadMax] = 0;
                block->AttachSegment(segmentId, segment);
                block->UnsetPending(segmentId);
                // 2) Write segment to object (if it's data)
                if (segmentId < numData) 
                {
                    block->DecrementErasureCount();
                    if (WriteSegment(blockId, segmentId, segment))
                        server->IncrementRecvGoodput(segmentLength);
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
                            "completed block ...\n", LocalNodeId(), server->GetId(), 
                            (UINT16)id, (UINT32)block->Id()); 
                    UINT16 nextErasure = block->FirstPending();
                    UINT16 erasureCount = 0;
                    if (nextErasure < numData)
                    {
                        UINT16 blockLen = numData + nparity;
                        while (nextErasure < blockLen)
                        {
                            server->SetErasureLoc(erasureCount++, nextErasure);
                            if (nextErasure < numData)
                            {
                                if (!(segment = server->GetFreeSegment(id, blockId)))
                                {
                                    DMSG(2, "NormObject::HandleObjectMessage() node>%lu server>%lu obj>%hu "
                                            "Warning! no free segments ...\n", LocalNodeId(), server->GetId(), 
                                            (UINT16)id);  
                                    // (TBD) Dump the block ...???
                                    return;
                                }
                                UINT16 payloadMax = segment_size + NormDataMsg::GetStreamPayloadHeaderLength();
#ifdef SIMULATE                               
                                payloadMax = MIN(payloadMax, SIM_PAYLOAD_MAX);
#endif // SIMULATE
                                memset(segment, 0, payloadMax+1);
                                block->SetSegment(nextErasure, segment);
                            }
                            nextErasure = block->NextPending(nextErasure+1);   
                        }
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
                                    server->IncrementRecvGoodput(segmentLength);
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
                // (TBD) this could be improved for stream objects
                //        so it's not called unnecessarily
                session->Notify(NormController::RX_OBJECT_UPDATE, server, this);
            }
            else
            {
                DMSG(6, "NormObject::HandleObjectMessage() node>%lu server>%lu obj>%hu "
                    "received duplicate segment ...\n", LocalNodeId(),
                     server->GetId(), (UINT16)id);
            }
        }
        else
        {
            DMSG(6, "NormObject::HandleObjectMessage() node>%lu server>%lu obj>%hu "
                    "received duplicate block message ...\n", LocalNodeId(),
                     server->GetId(), (UINT16)id);
        }  // end if/else pending_mask.Test(blockId)
    }  // end if/else (NORM_MSG_INFO)
}  // end NormObject::HandleObjectMessage()



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


// For silent client resource management, steals newer block resources when
// needing resources for ordinally older blocks.
NormBlock* NormObject::StealOldestBlock(bool excludeBlock, NormBlockId excludeId)
{
    if (block_buffer.IsEmpty())
    {
        return NULL;
    }
    else
    {
        NormBlock* block = block_buffer.Find(block_buffer.RangeLo());
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
}  // end NormObject::StealOldestBlock()



bool NormObject::NextServerMsg(NormObjectMsg* msg)
{
    // Init() the message
    if (pending_info)
        ((NormInfoMsg*)msg)->Init();
    else
        ((NormDataMsg*)msg)->Init();
    
    // General object message flags
    msg->ResetFlags();
    switch(type)
    {
        case STREAM:
            msg->SetFlag(NormObjectMsg::FLAG_STREAM);
            break;
        case FILE:
            msg->SetFlag(NormObjectMsg::FLAG_FILE);
            break;
        default:
            break;
    }
    if (info) msg->SetFlag(NormObjectMsg::FLAG_INFO);
    msg->SetFecId(129);
    msg->SetObjectId(id);
    
    // We currently always apply the FTI extension
    NormFtiExtension fti;
    msg->AttachExtension(fti);
    
    fti.SetSegmentSize(segment_size);
    fti.SetObjectSize(object_size);
    fti.SetFecMaxBlockLen(ndata);
    fti.SetFecNumParity(nparity);
             
    if (pending_info)
    {
        // (TBD) set REPAIR_FLAG for retransmitted info
        NormInfoMsg* infoMsg = (NormInfoMsg*)msg;
        infoMsg->SetInfo(info, info_len);
        pending_info = false;
        return true;
    }
    if (!pending_mask.IsSet()) return false;
    
    NormDataMsg* data = (NormDataMsg*)msg;
    
    NormBlockId blockId = pending_mask.FirstSet();
    UINT16 numData = GetBlockSize(blockId);
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
                UINT16 payloadMax = segment_size + NormDataMsg::GetStreamPayloadHeaderLength();
#ifdef SIMULATE
                payloadMax = MIN(payloadMax, SIM_PAYLOAD_MAX);
#endif // SIMULATE
                memset(s, 0, payloadMax+1);  // extra byte for msg flags
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
        // Try to read data segment (Note "ReadSegment" copies in offset/length info also)
        char* buffer = data->AccessPayload(); 
        UINT16 payloadLength = ReadSegment(blockId, segmentId, buffer);
        if (0 == payloadLength)
        {
            // (TBD) deal with read error 
            //(for streams, it currently means the stream is non-pending)
            if (!IsStream()) DMSG(0, "NormObject::NextServerMsg() ReadSegment() error\n");          
            return false;  
        }
        data->SetPayloadLength(payloadLength);
        
        if (IsStream())
        {
            // Look for FLAG_MSG_START
            UINT16 payloadMax = segment_size+NormDataMsg::GetStreamPayloadHeaderLength();
#ifdef SIMULATE
            payloadMax = MIN(payloadMax, SIM_PAYLOAD_MAX);            
#endif // SIMULATE   
            if (buffer[payloadMax]) data->SetFlag(NormObjectMsg::FLAG_MSG_START);         
        }
        
        // Perform incremental FEC encoding as needed
        if ((block->ParityReadiness() == segmentId) && nparity) 
           // (TBD) && incrementalParity == true
        {
            // (TBD) for non-stream objects, catch alternate "last block/segment len"
            // ZERO pad any "runt" segments before encoding
            UINT16 payloadMax = segment_size + NormDataMsg::GetStreamPayloadHeaderLength();
#ifdef SIMULATE
            payloadMax = MIN(payloadMax, SIM_PAYLOAD_MAX);
#endif // SIMULATE
            if (payloadLength < payloadMax)
                memset(buffer+payloadLength, 0, payloadMax-payloadLength);
            // (TBD) the encode routine could update the block's parity readiness
            session->ServerEncode(data->AccessPayload(), block->SegmentList(numData)); 
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
        UINT16 payloadLength = segment_size;
        if (IsStream()) payloadLength += NormDataMsg::GetStreamPayloadHeaderLength();
        data->SetPayload(segment, payloadLength);    
    }
    block->UnsetPending(segmentId); 
    if (block->InRepair()) 
        data->SetFlag(NormObjectMsg::FLAG_REPAIR);
    data->SetFecBlockId(blockId);
    data->SetFecBlockLen(numData);
    data->SetFecSymbolId(segmentId);
    if (!block->IsPending()) 
    {
        block->ResetParityCount(nparity);
        pending_mask.Unset(blockId); 
    }
 
    // This lets NORM_STREAM objects continue indefinitely
    if (IsStream() && !pending_mask.IsSet()) 
        static_cast<NormStreamObject*>(this)->StreamAdvance();
    return true;
}  // end NormObject::NextServerMsg()

void NormStreamObject::StreamAdvance()
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
                DMSG(0, "NormStreamObject::StreamAdvance() error setting stream pending mask (1)\n");
        }
        else
        {
            NormBlock* block = block_buffer.Find(block_buffer.RangeLo());  
            ASSERT(block); 
            if (!block->IsTransmitPending())
            {
                // ??? (TBD) Should this block be returned to the pool right now???
                if (pending_mask.Set(nextBlockId))
                    stream_next_id++;
                else
                    DMSG(0, "NormStreamObject::StreamAdvance() error setting stream pending mask (2)\n");
            }
            else
            {
               DMSG(4, "NormStreamObject::StreamAdvance() node>%lu Pending segment repairs (blk>%lu) "
                       "delaying stream advance ...\n", LocalNodeId(), (UINT32)block->Id());
            } 
        }
    }
    else
    {
        DMSG(0, "NormStreamObject::StreamAdvance() Pending block repair delaying stream advance ...\n");   
    }
}  // end NormStreamObject::StreamAdvance()

bool NormObject::CalculateBlockParity(NormBlock* block)
{
    char buffer[NormMsg::MAX_SIZE];
    UINT16 numData = GetBlockSize(block->Id());
    for (UINT16 i = 0; i < numData; i++)
    {
        UINT16 payloadLength = ReadSegment(block->Id(), i, buffer);
        if (0 != payloadLength)
        {
            UINT16 payloadMax = segment_size+NormDataMsg::GetStreamPayloadHeaderLength();
#ifdef SIMULATE
            payloadMax = MIN(payloadMax, SIM_PAYLOAD_MAX);
#endif // SIMULATE
            if (payloadLength < payloadMax)
                memset(buffer+payloadLength, 0, payloadMax-payloadLength);
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
        UINT16 numData = GetBlockSize(blockId);  
        // Init block parameters
        block->TxRecover(blockId, numData, nparity);
        // Fill block with zero initialized parity segments
        UINT16 totalBlockLen = numData + nparity;
        for (UINT16 i = numData; i < totalBlockLen; i++)
        {
            char* s = session->ServerGetFreeSegment(id, blockId);
            if (s)
            {
                UINT16 payloadMax = segment_size + NormDataMsg::GetStreamPayloadHeaderLength();                
#ifdef SIMULATE
                payloadMax = MIN(payloadMax, SIM_PAYLOAD_MAX);
#endif // SIMULATE
                memset(s, 0, payloadMax+1);  // extra byte for msg flags
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
            if (!block_buffer.Insert(block))
            {
                session->ServerPutFreeBlock(block);
                DMSG(4, "NormObject::ServerRecoverBlock() node>%lu couldn't buffer recovered block\n");
                return NULL;   
            }
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
 : NormObject(FILE, theSession, theServer, objectId), 
   large_block_length(0,0), small_block_length(0,0)
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
        //block_size = NormObjectSize(server->BlockSize()) * 
        //             NormObjectSize(server->SegmentSize());
    }
    else
    {
        // We're sending this file
        if (file.Open(thePath, O_RDONLY))
        {
            UINT32 size = file.GetSize(); 
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
        } 
        else
        {
            DMSG(0, "NormFileObject::Open() send file.Open() error!\n");
            return false;
        }
    }
    large_block_length = NormObjectSize(large_block_size) * segment_size;
    small_block_length = NormObjectSize(small_block_size) * segment_size;
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
    if (blockId == final_block_id)
    { 
        if (segmentId == (GetBlockSize(blockId)-1))
            len = final_segment_size;
        else
            len = segment_size;
    }
    else
    {
        len = segment_size;
    }
    // Determine segment offset from blockId::segmentId
    NormObjectSize segmentOffset;
    NormObjectSize segmentSize = NormObjectSize(segment_size);
    if ((UINT32)blockId < large_block_count)
    {
        segmentOffset = large_block_length*(UINT32)blockId + segmentSize*segmentId;
    }
    else
    {
        segmentOffset = large_block_length*large_block_count;  // (TBD) pre-calc this 
        UINT32 smallBlockIndex = (UINT32)blockId - large_block_count;
        segmentOffset = segmentOffset + small_block_length*smallBlockIndex +
                                        segmentSize*segmentId;
    }
    off_t offsetScaleMSB = 0xffffffff + 1;
    off_t offset = (off_t)segmentOffset.LSB() + ((off_t)segmentOffset.MSB() * offsetScaleMSB);
    if (offset != file.GetOffset())
    {
        if (!file.Seek(offset)) return false; 
    }
    UINT16 nbytes = file.Write(buffer, len);
    return (nbytes == len);
}  // end NormFileObject::WriteSegment()


UINT16 NormFileObject::ReadSegment(NormBlockId      blockId, 
                                   NormSegmentId    segmentId,
                                   char*            buffer)            
{
    // Determine segment length from blockId::segmentId
    UINT16 len;
    if (blockId == final_block_id)
    {
        if (segmentId == (GetBlockSize(blockId)-1))
            len = final_segment_size;
        else
            len = segment_size;
    }
    else
    {
        len = segment_size;
    }
    
    
    // Determine segment offset from blockId::segmentId
    NormObjectSize segmentOffset;
    NormObjectSize segmentSize = NormObjectSize(segment_size);
    if ((UINT32)blockId < large_block_count)
    {
        segmentOffset = large_block_length*(UINT32)blockId + segmentSize*segmentId;
    }
    else
    {
        segmentOffset = large_block_length*large_block_count;  // (TBD) pre-calc this  
        UINT32 smallBlockIndex = (UINT32)blockId - large_block_count;
        segmentOffset = segmentOffset + small_block_length*smallBlockIndex +
                                        segmentSize*segmentId;
    }
    off_t offsetScaleMSB = 0xffffffff + 1;
    off_t offset = (off_t)segmentOffset.LSB() + ((off_t)segmentOffset.MSB() * offsetScaleMSB);
    if (offset != file.GetOffset())
    {
        if (!file.Seek(offset)) 
            return false;
    }
    UINT16 nbytes = file.Read(buffer, len);
    return (len == nbytes) ? len : 0;
}  // end NormFileObject::ReadSegment()


/////////////////////////////////////////////////////////////////
//
// NormStreamObject Implementation
//

NormStreamObject::NormStreamObject(class NormSession*       theSession, 
                                   class NormServerNode*    theServer,
                                   const NormObjectId&      objectId)
 : NormObject(STREAM, theSession, theServer, objectId), 
   stream_sync(false), flush_pending(false), msg_start(true)
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
    
    UINT16 segmentSize, numData;
    if (server)
    {
        segmentSize = server->SegmentSize();
        numData = server->BlockSize();
    }
    else
    {
        segmentSize = session->ServerSegmentSize();
        numData = session->ServerBlockSize();
    }
    
    NormObjectSize blockSize((UINT32)segmentSize * (UINT32)numData);
    NormObjectSize numBlocks = NormObjectSize((UINT32)bufferSize) / blockSize;
    ASSERT(0 == numBlocks.MSB());
    // Buffering requires at least 2 blocks
    numBlocks = MAX(2, numBlocks.LSB());
    unsigned long numSegments = numBlocks.LSB() * numData;
        
    if (!block_pool.Init(numBlocks.LSB(), numData))
    {
        DMSG(0, "NormStreamObject::Open() block_pool init error\n");
        Close();
        return false;
    }
    
    if (!segment_pool.Init(numSegments, segmentSize+NormDataMsg::GetStreamPayloadHeaderLength()+1))
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
    write_offset = read_offset = 0;    
    if (!server)
    {
        if (!NormObject::Open(NormObjectSize((UINT32)bufferSize), infoPtr, infoLen))
        {
            DMSG(0, "NormStreamObject::Open() object open error\n");
            Close();
            return false;
        }
    }
    flush_pending = false;
    msg_start = true;
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
    write_offset = read_offset = 0;
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

bool NormStreamObject::StreamUpdateStatus(NormBlockId blockId)
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
                        //TRACE("NormObject::StreamUpdateStatus() broken 1 ...\n");
                        return false;   
                    }
                }
                else
                {
                    NormBlockId delta = blockId - stream_next_id + 1;
                    if (delta > NormBlockId(pending_mask.Size()))
                    {
                        // Stream broken
                        //TRACE("NormObject::StreamUpdateStatus() broken 2 ...\n");
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
        read_index.block = blockId;
        return true;  
    }
}  // end NormStreamObject::StreamUpdateStatus()


UINT16 NormStreamObject::ReadSegment(NormBlockId      blockId, 
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
    ASSERT(segment != NULL);    
    UINT16 segmentLength = NormDataMsg::ReadStreamPayloadLength(segment);
    ASSERT(segmentLength <= segment_size);
#ifdef SIMULATE
    UINT16 simLen = segmentLength + NormDataMsg::GetStreamPayloadHeaderLength();
    simLen = MIN(simLen, SIM_PAYLOAD_MAX);
    buffer[simLen] = segment[simLen];        
#else    
    buffer[segment_size+NormDataMsg::GetStreamPayloadHeaderLength()] =
        segment[segment_size+NormDataMsg::GetStreamPayloadHeaderLength()];
#endif // if/else SIMULATE
    UINT16 payloadLength = segmentLength+NormDataMsg::GetStreamPayloadHeaderLength();
    memcpy(buffer, segment, payloadLength);
    return payloadLength;
}  // end NormStreamObject::ReadSegment()

bool NormStreamObject::WriteSegment(NormBlockId   blockId, 
                                    NormSegmentId segmentId, 
                                    const char*   segment)
{
    /*if ((blockId < read_index.block) ||
        ((blockId == read_index.block) &&
         (segmentId < read_index.segment))) 
    {
        //DMSG(0, "NormStreamObject::WriteSegment() block/segment < read_index!?\n");
        return false;
    }*/   
            
    UINT32 segmentOffset = NormDataMsg::ReadStreamPayloadOffset(segment);
    // if (segmentOffset < read_offset)
    UINT32 diff = segmentOffset - read_offset;
    if ((diff > 0x80000000) || ((0x80000000 == diff) && (segmentOffset > read_offset)))
    {
        DMSG(0, "NormStreamObject::WriteSegment() diff:%lu segmentOffset:%lu < read_offset:%lu \n",
                 diff, segmentOffset, read_offset);
        return false;
    }
    
    NormBlock* block = stream_buffer.Find(blockId);
    if (!block)
    {
        bool broken = false;
        // Prune (if necessary) stream_buffer (stream might be broken)
        while (!stream_buffer.CanInsert(blockId) || block_pool.IsEmpty())
        {
            block = stream_buffer.Find(stream_buffer.RangeLo());
            if (block->IsPending()) broken = true;
            // This loop feeds any received user data segments to the application
            // (if the application doesn't want the data, it's lost)
            while (block->IsPending())
            {             
                // Notify app for stream data salvage  
                read_index.block = block->Id();
                read_index.segment = block->FirstPending();
                NormBlock* tempBlock = block;
                UINT32 tempOffset = read_offset;
                session->Notify(NormController::RX_OBJECT_UPDATE, server, this);
                block = stream_buffer.Find(stream_buffer.RangeLo());
                if (tempBlock == block)
                {
                    if (tempOffset == read_offset)
                    {
                        // App didn't want any data here, purge segment
                        TRACE("NormStreamObject::WriteSegment() app didn't want data ?!\n");
                        ASSERT(0);
                        char* s = block->DetachSegment(read_index.segment);
                        segment_pool.Put(s);
                        block->UnsetPending(read_index.segment);
                    }
                }
                else
                {
                    // App consumed block via Read()
                    block = NULL;   
                    break;
                }              
            }
            if (block)
            {
                // if the app didn't consume the block, we must
                // return it to the pool
                ASSERT(!block->IsPending());
                stream_buffer.Remove(block);
                block->EmptyToPool(segment_pool);
                block_pool.Put(block);
            }
        }
        block = block_pool.Get();
        block->SetId(blockId);
        block->ClearPending();
        bool success = stream_buffer.Insert(block);
        ASSERT(success);
        if (broken)
        {
            DMSG(4, "NormStreamObject::WriteSegment() node>%lu obj>%hu blk>%lu seg>%hu broken stream ...\n",
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
        ASSERT(s != NULL);  // for now, this should always succeed
        UINT16 segmentLength = NormDataMsg::ReadStreamPayloadLength(segment);
        UINT16 flagsOffset = segment_size + NormDataMsg::GetStreamPayloadHeaderLength();
#ifdef SIMULATE
        flagsOffset = MIN(SIM_PAYLOAD_MAX, flagsOffset);
        segmentLength = MIN((SIM_PAYLOAD_MAX-NormDataMsg::GetStreamPayloadHeaderLength()), segmentLength);  
#endif // SIMULATE
        s[flagsOffset] = segment[flagsOffset];
        memcpy(s, segment, segmentLength + NormDataMsg::GetStreamPayloadHeaderLength());
        block->AttachSegment(segmentId, s);
        block->SetPending(segmentId);
        ASSERT(block->Segment(segmentId) == s);
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
bool NormStreamObject::Read(char* buffer, unsigned int* buflen, bool findMsgStart)
{
    unsigned int bytesRead = 0;
    unsigned int bytesToRead = *buflen;
    while (bytesToRead > 0)
    {
        NormBlock* block = stream_buffer.Find(read_index.block);
        if (!block)
        {
            //DMSG(0, "NormStreamObject::Read() stream buffer empty (1)\n");
            *buflen = bytesRead;
            return true;   
        }
        char* segment = block->Segment(read_index.segment);
        if (!segment)
        {
            //DMSG(0, "NormStreamObject::Read(%lu:%hu) stream buffer empty (2)\n",
            //        (UINT32)read_index.block, read_index.segment);
            *buflen = bytesRead;
            return true;   
        }
        
        UINT32 segmentOffset = NormDataMsg::ReadStreamPayloadOffset(segment);
        // if (read_offset < segmentOffset)
        UINT32 diff = read_offset - segmentOffset;
        if ((diff > 0x80000000) || ((0x80000000 == diff) && (read_offset > segmentOffset)))
        {
            DMSG(0, "NormStreamObject::Read() node>%lu broken stream!\n", LocalNodeId());
            read_offset = segmentOffset;
            *buflen = bytesRead;
            return false;
        }
        
        UINT32 index = read_offset - segmentOffset;
        UINT16 length = NormDataMsg::ReadStreamPayloadLength(segment);
	    if (index >= length)
        {
            DMSG(0, "NormStreamObject::Read() node>%lu mangled stream! index:%hu length:%hu\n",
                    LocalNodeId(), index, length);
            read_offset = segmentOffset;
            *buflen = bytesRead;
            return false;
        }


        UINT16 count = length - index;
        count = MIN(count, bytesToRead);
        
        if (findMsgStart)
        {
            bool msgStart;
            if (0 != index)
            {
                msgStart = false;
            }
            else
            {
                UINT16 flagsOffset = segment_size+NormDataMsg::GetStreamPayloadHeaderLength();
#ifdef SIMULATE
                flagsOffset = MIN(flagsOffset, SIM_PAYLOAD_MAX);
#endif // if/else SIMULATE               
                msgStart = (NormObjectMsg::FLAG_MSG_START == segment[flagsOffset]);
            }
            if (!msgStart)
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
                continue;
            }
        }
        
#ifdef SIMULATE
        UINT16 simCount = ((index+count) < SIM_PAYLOAD_MAX) ? 
                            count : ((index < SIM_PAYLOAD_MAX) ?
                                        (SIM_PAYLOAD_MAX - index) : 0);
        memcpy(buffer+bytesRead, segment+index+NormDataMsg::GetStreamPayloadHeaderLength(), simCount);
#else
        memcpy(buffer+bytesRead, segment+index+NormDataMsg::GetStreamPayloadHeaderLength(), count);
#endif // if/else SIMULATE
        index += count;
        bytesRead += count;
        read_offset += count;
        bytesToRead -= count;
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
    *buflen = bytesRead;
    return true;
}  // end NormStreamObject::Read()

unsigned long NormStreamObject::Write(const char* buffer, unsigned long len, 
                                      bool flush, bool eom, bool push)
{
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
                if (push)
                {
                    NormBlockId blockId = block->Id();
                    pending_mask.Unset(blockId);
                    repair_mask.Unset(blockId);
                    NormBlock* b = FindBlock(blockId);
                    if (b)
                    {
                        block_buffer.Remove(b);
                        session->ServerPutFreeBlock(b); 
                    }     
                }
                else if (block->IsPending())
                {
                    DMSG(0, "NormStreamObject::Write() stream buffer full (1) len:%d eom:%d\n", len, eom);
                    break;
                }
                stream_buffer.Remove(block);
                block->EmptyToPool(segment_pool);
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
                if (push)
                {
                    NormBlockId blockId = block->Id();
                    pending_mask.Unset(blockId);
                    repair_mask.Unset(blockId);
                    NormBlock* c = FindBlock(blockId);
                    if (c)
                    {
                        block_buffer.Remove(c);
                        session->ServerPutFreeBlock(c); 
                    }  
                }
                else if (b->IsPending())
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
            NormDataMsg::WriteStreamPayloadOffset(segment, write_offset);  
            NormDataMsg::WriteStreamPayloadLength(segment, 0);
            block->AttachSegment(write_index.segment, segment);
        }
        
        UINT16 index = NormDataMsg::ReadStreamPayloadLength(segment);
        
        if (0 == index)
        {
            char msgStartValue = 0;
            if (msg_start && len)
            {
                ASSERT(0 == index);
                msgStartValue = NormObjectMsg::FLAG_MSG_START; 
                msg_start = false;
            }
            UINT16 flagsOffset = segment_size+NormDataMsg::GetStreamPayloadHeaderLength();
#ifdef SIMULATE
            flagsOffset = MIN(SIM_PAYLOAD_MAX, flagsOffset);
#endif // SIMULATE  
            segment[flagsOffset] = msgStartValue;
        }
        else
        {
            msg_start = false;   
        }            
        
        UINT16 count = (UINT16)(len - nBytes);
        UINT16 space = segment_size - index;
        count = MIN(count, space);
#ifdef SIMULATE
        UINT16 simCount = ((index+count) < SIM_PAYLOAD_MAX) ? 
                            count : ((index < SIM_PAYLOAD_MAX) ?
                                        (SIM_PAYLOAD_MAX - index) : 0);
        memcpy(segment+index+NormDataMsg::GetStreamPayloadHeaderLength(), buffer+nBytes, simCount);
#else
        memcpy(segment+index+NormDataMsg::GetStreamPayloadHeaderLength(), buffer+nBytes, count);
#endif // if/else SIMULATE
        NormDataMsg::WriteStreamPayloadLength(segment, index+count);
        nBytes += count;
        write_offset += count;
        // Is the segment full? or flushing
        if ((count == space) || ((flush || eom) && (index > 0) && (nBytes == len)))
        {   
            block->SetPending(write_index.segment);
            if (++write_index.segment >= ndata) 
            {
                write_index.block++;
                write_index.segment = 0;
            }
        }
    } while (nBytes < len);
    
    // if this was end-of-message next Write() will be considered a new message     
    if (nBytes == len)
    {
        if (eom) 
            msg_start = true;
        if (flush) 
            flush_pending = true;
        else
            flush_pending = false;
    }
    else
    {
        flush = false;   
    }
    if (nBytes || flush) session->TouchServer();
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

UINT16 NormSimObject::ReadSegment(NormBlockId      blockId, 
                                  NormSegmentId    segmentId,
                                  char*            buffer)            
{
    // Determine segment length
    UINT16 len;
    if (blockId == final_block_id)
    {
        if (segmentId == (GetBlockSize(blockId)-1))
            len = final_segment_size;
        else
            len = segment_size;
    }
    else
    {
        len = segment_size;
    }
    return len;
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
