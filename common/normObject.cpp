#include "normObject.h"
#include "normSession.h"

#include <errno.h>

NormObject::NormObject(NormObject::Type      theType, 
                       class NormSession&    theSession, 
                       class NormServerNode* theServer,
                       const NormObjectId&   transportId)
 : type(theType), session(theSession), server(theServer), reference_count(0),
   transport_id(transportId), segment_size(0), pending_info(false), repair_info(false),
   current_block_id(0), next_segment_id(0), 
   max_pending_block(0), max_pending_segment(0),
   info_ptr(NULL), info_len(0), accepted(false), notify_on_update(true)
{
    if (theServer)
        nacking_mode = theServer->GetDefaultNackingMode();
    else 
        nacking_mode = NACK_NORMAL; // it doesn't really matter if !theServer
}

NormObject::~NormObject()
{
    Close();
    if (info_ptr) 
    {
        delete info_ptr;
        info_ptr = NULL;
    }
}

void NormObject::Retain()
{
    reference_count++;
    if (server) server->Retain();
}  // end NormObject::Retain()

void NormObject::Release()
{
    if (server) server->Release();
    if (reference_count)
        reference_count--;
    else
        DMSG(0, "NormObject::Release() releasing non-retained object?!\n");
    if (0 == reference_count) delete this;      
}  // end NormObject::Release()

// This is mainly used for debug messages
NormNodeId NormObject::LocalNodeId() const
{
    return session.LocalNodeId();    
}  // end NormObject::LocalNodeId()

NormNodeId NormObject::GetServerNodeId() const
{
    return server ? server->GetId() : NORM_NODE_NONE;   
}  // end NormObject::GetServerNodeId()

bool NormObject::Open(const NormObjectSize& objectSize, 
                      const char*           infoPtr, 
                      UINT16                infoLen,
                      UINT16                segmentSize,
                      UINT16                numData,
                      UINT16                numParity)
{
    // Note "objectSize" represents actual total object size for
    // DATA or FILE objects, buffer size for STREAM objects
    // In either case, we need our sliding bit masks to be of 
    // appropriate size.
    ASSERT(!IsOpen());
    if (server)
    {
        if (infoLen > 0) 
        {
            pending_info = true;
            info_len = 0;
            if (!(info_ptr = new char[segmentSize]))
            {
                DMSG(0, "NormObject::Open() info allocation error\n");
                return false;
            }               
        }
    }
    else
    {
        if (infoPtr)
        {
            if (info_ptr) delete []info_ptr;
            if (infoLen > segmentSize)
            {
                DMSG(0, "NormObject::Open() info too big error\n");
                info_len = 0;
                return false;
            }        
            if (!(info_ptr = new char[infoLen]))
            {
                DMSG(0, "NormObject::Open() info allocation error\n");
                info_len = 0;
                return false;
            } 
            memcpy(info_ptr, infoPtr, infoLen);
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
    if (!pending_mask.Init(numBlocks.LSB(), 0xffffffff))
    {
        DMSG(0, "NormObject::Open() init pending_mask error\n");  
        Close();
        return false; 
    }
    pending_mask.Clear();
    pending_mask.SetBits(0, numBlocks.LSB());
    
    // Init repair_mask
    if (!repair_mask.Init(numBlocks.LSB(), 0xffffffff))
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
        // not used for STREAM objects
        final_block_id = large_block_count + small_block_count - 1;  
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
            session.ServerPutFreeBlock(block);
    }
    repair_mask.Destroy();
    pending_mask.Destroy();
    block_buffer.Destroy();
    segment_size = 0;
}  // end NormObject::Close();

// Used by server
bool NormObject::HandleInfoRequest()
{
    bool increasedRepair = false;
    if (info_ptr)
    {
        if (!repair_info)
        {
            pending_info = true;
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
        NormBlockId blockId = block->GetId();
        if (blockId >= firstBlock)
        {
            increasedRepair |= block->TxReset(GetBlockSize(blockId), 
                                              nparity, 
                                              session.ServerAutoParity(), 
                                              segment_size);
        }
    }
    return increasedRepair;
}  // end NormObject::TxReset()

bool NormObject::TxResetBlocks(NormBlockId nextId, NormBlockId lastId)
{
    bool increasedRepair = false;
    UINT16 autoParity = session.ServerAutoParity();
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
            increasedRepair |= block->TxReset(GetBlockSize(block->GetId()), nparity, autoParity, segment_size);
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
    NormBlockId nextId;
    if (GetFirstRepair(nextId))
    {
        NormBlockId lastId;
        ASSERT(GetLastRepair(lastId));
        DMSG(6, "NormObject::ActivateRepairs() node>%lu obj>%hu activating blk>%lu->%lu block repairs ...\n",
                LocalNodeId(), (UINT16)transport_id, (UINT32)nextId, (UINT32)lastId);
        repairsActivated = true;
        UINT16 autoParity = session.ServerAutoParity();
        do
        {
            NormBlock* block = block_buffer.Find(nextId);
            if (block) block->TxReset(GetBlockSize(nextId), nparity, autoParity, segment_size);
            // (TBD) This check can be eventually eliminated if everything else is done right
            if (!pending_mask.Set(nextId)) 
                DMSG(0, "NormObject::ActivateRepairs() pending_mask.Set(%lu) error!\n", (UINT32)nextId);
            nextId++;
        } while (GetNextRepair(nextId));
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
                LocalNodeId(), (UINT16)transport_id, (UINT32)block->GetId());
            // (TBD) This check can be eventually eliminated if everything else is done right
            if (!pending_mask.Set(block->GetId()))
                DMSG(0, "NormObject::ActivateRepairs() pending_mask.Set(%lu) error!\n", (UINT32)block->GetId());
        }
    }
    return repairsActivated;
}  // end NormObject::ActivateRepairs()

// called by servers only
bool NormObject::AppendRepairAdv(NormCmdRepairAdvMsg& cmd)
{
    // Determine range of blocks possibly pending repair
    NormBlockId nextId = 0;
    GetFirstRepair(nextId);
    NormBlockId endId = 0;
    GetLastRepair(endId);
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
    bool requestAppended = false;
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
        //DMSG(0, "NormObject::AppendRepairAdv() testing block:%lu nextId:%lu endId:%lu\n", 
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
                {
                    if (0 == cmd.PackRepairRequest(req))
                    {
                        DMSG(0, "NormObject::AppendRepairAdv() warning: full msg\n");
                        return requestAppended;
                    } 
                    requestAppended = true;
                }
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
                    req.AppendRepairItem(transport_id, firstId, GetBlockSize(firstId), 0);
                    if (2 == blockCount) 
                        req.AppendRepairItem(transport_id, currentId, GetBlockSize(currentId), 0);
                    break;
                case NormRepairRequest::RANGES:
                    req.AppendRepairRange(transport_id, firstId, GetBlockSize(firstId), 0,
                                          transport_id, currentId, GetBlockSize(currentId), 0);
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
                    if (0 == cmd.PackRepairRequest(req))
                    {
                        DMSG(0, "NormObject::AppendRepairAdv() warning: full msg\n");
                        return requestAppended;
                    }  
                    prevForm = NormRepairRequest::INVALID;
                }
                block->AppendRepairAdv(cmd, transport_id, repair_info, GetBlockSize(currentId), segment_size);  // (TBD) error check        
                requestAppended = true;
            }
        }
    }  // end while(nextId < endId)
    if (NormRepairRequest::INVALID != prevForm) 
    {
        if (0 == cmd.PackRepairRequest(req))
        {
            DMSG(0, "NormObject::AppendRepairAdv() warning: full msg\n");
            return requestAppended;
        } 
        requestAppended = true;
    }
    else if (repair_info && !requestAppended)
    {
        // make the "req" an INFO-only request
        req.ClearFlag(NormRepairRequest::BLOCK);   
        req.SetForm(NormRepairRequest::ITEMS);
        req.AppendRepairItem(transport_id, 0, 0, 0);
        if (0 == cmd.PackRepairRequest(req))
        {
            DMSG(0, "NormObject::AppendRepairAdv() warning: full msg\n");
            return requestAppended;
        }    
    }
    return true;
}  //  end NormObject::AppendRepairAdv()

// This is used by server for watermark check
bool NormObject::FindRepairIndex(NormBlockId& blockId, NormSegmentId& segmentId) const
{
    if (repair_info)
    {
        blockId = 0;
        segmentId = 0;
        return true;   
    }
    NormBlockBuffer::Iterator iterator(block_buffer);
    NormBlock* block;
    while ((block = iterator.GetNextBlock()))
        if (block->IsRepairPending()) break;
    if (GetFirstRepair(blockId))
    {
        if (!block || (blockId <= block->GetId()))
        {
            segmentId = 0;
            return true;      
        }    
    } 
    if (block)
    {
#ifdef PROTO_DEBUG
        ASSERT(block->GetFirstRepair(segmentId));
#else
        block->GetFirstRepair(segmentId);
#endif  // if/else PROTO_DEBUG
        // The segmentId must < block length for watermarks
        if (segmentId >= GetBlockSize(block->GetId()))
            segmentId = GetBlockSize(block->GetId()) - 1;
        return true;   
    }   
    return false;
}  // end NormObject::FindRepairIndex()
        

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
        NormBlockId firstId;
        if (GetFirstPending(firstId))
        {
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
                        ASSERT(block->IsPending());
                        NormSegmentId firstPending = 0;
                        block->GetFirstPending(firstPending);
                        if (firstPending < max_pending_segment)
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

// This is a "passive" THRU_SEGMENT repair check
// (used to for watermark ack check)
bool NormObject::PassiveRepairCheck(NormBlockId   blockId,
                                    NormSegmentId segmentId)
{
    if (pending_info) return true;
    NormBlockId firstPendingBlock;
    if (GetFirstPending(firstPendingBlock))
    {
        if (firstPendingBlock < blockId)
        {
            return true;
        }
        else if (firstPendingBlock == blockId)
        {
        
            NormBlock* block = block_buffer.Find(firstPendingBlock);
            if (block)
            {
                NormSegmentId firstPendingSegment;
                if (block->GetFirstPending(firstPendingSegment))
                {
                    if (segmentId > firstPendingSegment)
                        return true;
                    else
                        return false;
                }
                else
                {
                    ASSERT(0);    
                }            
            }
            else
            {
                return true;  // entire block was pending
            }    
        }    
    }
    return false;
}  // end NormObject::PassiveRepairCheck()

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
        case TO_OBJECT:
            return false;
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
            break;
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
        NormBlockId nextId;
        if (GetFirstPending(nextId))
        {
            NormBlockId lastId;
            GetLastPending(lastId);
            if ((level < THRU_OBJECT) && (blockId < lastId)) lastId = blockId;
            if (level > TO_BLOCK) lastId++;
            while (nextId < lastId)
            {
                if (nextId > lastId) break;
                NormBlock* block = block_buffer.Find(nextId);
                if (block)
                {
                    if ((nextId == blockId) && (THRU_SEGMENT == level))
                    {
                        NormSymbolId firstPending = 0;
                        if (!block->GetFirstPending(firstPending)) ASSERT(0);
                        if (firstPending <= segmentId)
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
                if (!GetNextPending(nextId)) break;
            }  // end while (nextId < lastId) 
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

// Note this clears "repair_mask" state 
// (called on client repair_timer timeout)
bool NormObject::IsRepairPending(bool flush)
{
    ASSERT(server);
    if (pending_info && !repair_info) return true;
    // Calculate repair_mask = pending_mask - repair_mask 
    repair_mask.XCopy(pending_mask);
    NormBlockId nextId;
    if (GetFirstRepair(nextId))
    {
        do
        {
            if (!flush && (nextId > current_block_id)) break;
            NormBlock* block = block_buffer.Find(nextId);
            if (block)
            {
                bool isPending;
                UINT16 numData = GetBlockSize(nextId);
                if (flush || (nextId < current_block_id))
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
        } while (GetNextRepair(nextId));
    }
    return false;
}  // end NormObject::IsRepairPending()

bool NormObject::AppendRepairRequest(NormNackMsg&   nack, 
                                     bool           flush)
{ 
    // If !flush, we request only up _to_ max_pending_block::max_pending_segment.
    NormRepairRequest req;
    bool requestAppended = false;  // is set to true when content added to "nack"
    NormRepairRequest::Form prevForm = NormRepairRequest::INVALID;
    // First iterate over any pending blocks, appending any requests
    NormBlockId nextId;
    bool iterating = GetFirstPending(nextId);
    NormBlockId prevId = nextId;
    iterating = iterating && (flush || (nextId <= max_pending_block));  
    UINT32 consecutiveCount = 0;
    while (iterating || (0 != consecutiveCount))
    {
        NormBlockId lastId;
        ASSERT(GetLastPending(lastId));
        DMSG(6, "NormObject::AppendRepairRequest() node>%lu obj>%hu, blk>%lu->%lu (maxPending:%lu)\n",
               LocalNodeId(), (UINT16)transport_id,
               (UINT32)nextId, (UINT32)lastId, (UINT32)max_pending_block);
        bool appendRequest = false;
        NormBlock* block = iterating ? block_buffer.Find(nextId) : NULL;
        if (block)
            appendRequest = true;
        else if (iterating && ((UINT32)(nextId - prevId) == consecutiveCount))
            consecutiveCount++;
        else
            appendRequest = true;
        if (appendRequest)
        {
            NormRepairRequest::Form nextForm;
            switch(consecutiveCount)
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
                if ((NormRepairRequest::INVALID != prevForm) &&
                    (NACK_NONE != nacking_mode))
                {
                    if (0 == nack.PackRepairRequest(req)) 
                    {
                        DMSG(3, "NormObject::AppendRepairRequest() warning: full NACK msg\n");
                        return requestAppended;
                    }
                    requestAppended = true;
                }
                if (NormRepairRequest::INVALID != nextForm)
                {
                    nack.AttachRepairRequest(req, segment_size);
                    req.SetForm(nextForm);
                    req.ResetFlags();
                    if (NACK_NORMAL == nacking_mode)
                        req.SetFlag(NormRepairRequest::BLOCK);
                    if (pending_info)
                        req.SetFlag(NormRepairRequest::INFO);
                }
                prevForm = nextForm;
            }
            if (NormRepairRequest::INVALID != nextForm)
                DMSG(6, "NormObject::AppendRepairRequest() BLOCK request\n");
            switch (nextForm)
            {
                case NormRepairRequest::ITEMS:
                    req.AppendRepairItem(transport_id, prevId, GetBlockSize(prevId), 0);  // (TBD) error check
                    if (2 == consecutiveCount)
                    {
                        prevId++;
                        req.AppendRepairItem(transport_id, prevId, GetBlockSize(prevId), 0); // (TBD) error check
                    }
                    break;
                case NormRepairRequest::RANGES:
                {
                    NormBlockId lastId = prevId+consecutiveCount-1;
                    req.AppendRepairRange(transport_id, prevId, GetBlockSize(prevId), 0, 
                                          transport_id, lastId, GetBlockSize(lastId), 0); // (TBD) error check
                    break;
                }
                default:
                    break;
            }  // end switch(nextForm)
            if (block)
            {
                bool blockIsPending = false;
                if (nextId == max_pending_block)
                {
                    NormSymbolId firstPending = 0;
                    if (!block->GetFirstPending(firstPending)) ASSERT(0);
                    if (firstPending < max_pending_segment) blockIsPending = true;
                }
                else
                {
                    blockIsPending = true;   
                }
                if (blockIsPending && (NACK_NONE != nacking_mode))
                {
                    UINT16 numData = GetBlockSize(nextId);
                    if (NormRepairRequest::INVALID != prevForm)
                    {
                        if (0 == nack.PackRepairRequest(req))
                        {
                            DMSG(3, "NormObject::AppendRepairRequest() warning: full NACK msg\n");
                            return requestAppended;   
                        }
                    }
                    if (flush || (nextId != max_pending_block))
                    {
                        block->AppendRepairRequest(nack, numData, nparity, transport_id, 
                                                   pending_info, segment_size); // (TBD) error check
                    }
                    else
                    {
                        if (max_pending_segment < numData)
                            block->AppendRepairRequest(nack, max_pending_segment, 0, transport_id,
                                                        pending_info, segment_size); // (TBD) error check
                        else
                            block->AppendRepairRequest(nack, numData, nparity, transport_id, 
                                                       pending_info, segment_size); // (TBD) error check
                    }
                    requestAppended = true;
                }
                consecutiveCount = 0;
                prevForm = NormRepairRequest::INVALID;
            }
            else if (iterating)
            {
                consecutiveCount = 1;
            }
            else
            {
                consecutiveCount = 0;  // we're all done
            }
            prevId = nextId;
        }  // end if (appendRequest)
        nextId++;
        iterating = GetNextPending(nextId);
        //DMSG(0, "got next pending>%lu result:%d\n", (UINT32)nextId, iterating);
        iterating = iterating && (flush || (nextId <= max_pending_block));
    }  // end while (iterating || (0 != consecutiveCount))
    
    // This conditional makes sure any outstanding requests constructed
    // are packed into the nack message.
    if ((NormRepairRequest::INVALID != prevForm) &&
        (NACK_NONE != nacking_mode))
    {
        if (0 == nack.PackRepairRequest(req))
        {
            DMSG(3, "NormObject::AppendRepairRequest() warning: full NACK msg\n");
            return requestAppended;
        } 
        requestAppended = true;
        prevForm = NormRepairRequest::INVALID;
    }  
    if (!requestAppended && pending_info && (NACK_NONE != nacking_mode))
    {
        // INFO_ONLY repair request
        nack.AttachRepairRequest(req, segment_size);
        req.SetForm(NormRepairRequest::ITEMS);
        req.ResetFlags();
        req.SetFlag(NormRepairRequest::INFO); 
        req.AppendRepairItem(transport_id, 0, 0, 0);  // (TBD) error check
        if (0 == nack.PackRepairRequest(req))
        {
            DMSG(3, "NormObject::AppendRepairRequest() warning: full NACK msg\n");
            return requestAppended;
        }  
        requestAppended = true;
    }  
    return requestAppended;
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
                    "Warning! info too long.\n", LocalNodeId(), server->GetId(),
                         (UINT16)transport_id);   
            }
            memcpy(info_ptr, infoMsg.GetInfo(), info_len);
            pending_info = false;
            session.Notify(NormController::RX_OBJECT_INFO, server, this);
        }
        else
        {
            // (TBD) Verify info hasn't changed?   
            DMSG(6, "NormObject::HandleObjectMessage() node>%lu server>%lu obj>%hu "
                    "received duplicate info ...\n", LocalNodeId(),
                     server->GetId(), (UINT16)transport_id);
        }
    }
    else  // NORM_MSG_DATA
    {
        const NormDataMsg& data = (const NormDataMsg&)msg;
        UINT16 numData = GetBlockSize(blockId);
        
        // For stream objects, a little extra mgmt is required
        if (STREAM == type)
        {
            NormStreamObject* stream = static_cast<NormStreamObject*>(this);            
            if (!stream->StreamUpdateStatus(blockId))
            {
                DMSG(4, "NormObject::HandleObjectMessage() node:%lu server:%lu obj>%hu blk>%lu "
                        "broken stream ...\n", LocalNodeId(), server->GetId(), (UINT16)transport_id, (UINT32)blockId);
                
                //ASSERT(0);
                // ??? Ignore this new packet and try to fix stream ???
                //return;
                server->IncrementResyncCount();
                while (!stream->StreamUpdateStatus(blockId))
                {
                    // Server is too far ahead of me ...
                    NormBlockId firstId;
                    if (GetFirstPending(firstId))
                    {
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
        if (pending_mask.Test(blockId))
        {
            NormBlock* block = block_buffer.Find(blockId);
            if (!block)
            {
                if (!(block = server->GetFreeBlock(transport_id, blockId)))
                {
                    //DMSG(2, "NormObject::HandleObjectMessage() node>%lu server>%lu obj>%hu "
                    //        "Warning! no free blocks ...\n", LocalNodeId(), server->GetId(), 
                    //        (UINT16)transport_id);  
                    return;
                }
                block->RxInit(blockId, numData, nparity);
                block_buffer.Insert(block);
            }
            if (block->IsPending(segmentId))
            {
                UINT16 segmentLength = data.GetPayloadDataLength();
                if (segmentLength > segment_size)
                {
                    DMSG(0, "NormObject::HandleObjectMessage() node>%lu server>%lu obj>%hu "
                            "Error! segment too large ...\n", LocalNodeId(), server->GetId(), 
                            (UINT16)transport_id);  
                    return;  
                }
                UINT16 payloadLength = data.GetPayloadLength();
                UINT16 payloadMax = segment_size + NormDataMsg::GetStreamPayloadHeaderLength();
#ifdef SIMULATE
                // For simulations, we may need to cap the payloadLength
                payloadMax = MIN(payloadMax, SIM_PAYLOAD_MAX);
                payloadLength = MIN(payloadLength, SIM_PAYLOAD_MAX);
#endif // SIMULATE
                
                // Is this a source symbol or a parity symbol?
                bool isSourceSymbol = (segmentId < numData);
                
                // Try to cache segment in block buffer in case it's needed for decoding
                char* segment = (!isSourceSymbol || !server->SegmentPoolIsEmpty()) ?
                                    server->GetFreeSegment(transport_id, blockId) : NULL;
                
                if (segment)
                {
                    memcpy(segment, data.GetPayload(), payloadLength);
                    if (payloadLength < payloadMax)
                        memset(segment+payloadLength, 0, payloadMax-payloadLength);
                    block->AttachSegment(segmentId, segment);
                }
                else
                {
                    //DMSG(2, "NormObject::HandleObjectMessage() node>%lu server>%lu obj>%hu "
                    //        "Warning! no free segments ...\n", LocalNodeId(), server->GetId(), 
                    //        (UINT16)transport_id);  
                    if (!isSourceSymbol) return;
                }
                block->UnsetPending(segmentId);
                
                bool objectUpdated = false;
                // 2) Write segment to object (if it's source symbol (data))
                if (isSourceSymbol) 
                {
                    block->DecrementErasureCount();
                    if (WriteSegment(blockId, segmentId, data.GetPayload(), 
                                     data.FlagIsSet(NormObjectMsg::FLAG_MSG_START)))
                    {
                        objectUpdated = true;
                        // For statistics only (TBD) #ifdef NORM_DEBUG
                        server->IncrementRecvGoodput(segmentLength);
                    }
                    else
                    {
                        DMSG(4, "NormObject::HandleObjectMessage() WriteSegment() error\n");
                    }
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
                            (UINT16)transport_id, (UINT32)block->GetId());
                    UINT16 erasureCount = 0;
                    UINT16 nextErasure = 0;
                    UINT16 retrievalCount = 0;
                    if (block->GetFirstPending(nextErasure))
                    {
                        // Is the block missing _any_ source symbols?
                        if (nextErasure < numData)
                        {
                            // Use "NormObject::RetrieveSegment() method to "retrieve" 
                            // source symbol segments already received which weren't cached.
                            for (UINT16 nextSegment = 0; nextSegment < numData; nextSegment++)
                            {
                                if (block->IsPending(nextSegment))
                                {
                                    server->SetErasureLoc(erasureCount++, nextSegment);
                                    segment = server->GetRetrievalSegment();
                                    ASSERT(segment);
                                    UINT16 payloadMax = segment_size + NormDataMsg::GetStreamPayloadHeaderLength();
#ifdef SIMULATE                               
                                    payloadMax = MIN(payloadMax, SIM_PAYLOAD_MAX);
#endif // SIMULATE
                                    // Zeroize the missing segment payload in prep for decoding
                                    memset(segment, 0, payloadMax+1);
                                    server->SetRetrievalLoc(retrievalCount++, nextSegment);
                                    block->SetSegment(nextSegment, segment);
                                }
                                else if (!block->GetSegment(nextSegment))
                                {
                                    // "tempRetrieval" is set to "true" if the retrieved segment
                                    // comes from the "server" temp retrieval pool
                                    bool tempRetrieval = false;
                                    if (!(segment = RetrieveSegment(blockId, nextSegment, tempRetrieval)))
                                    {
                                        ASSERT(IsStream());
                                        block->SetPending(nextSegment);
                                        block->IncrementErasureCount();
                                        // Clear any erasure/retrieval segments
                                        UINT16 i;
                                        for (i = 0; i < erasureCount; i++) 
                                            block->DetachSegment(server->GetErasureLoc(i));
                                        for (i = 0; i < retrievalCount; i++) 
                                            block->DetachSegment(server->GetRetrievalLoc(i));
                                        return;   
                                    } 
                                    if (tempRetrieval)
                                        server->SetRetrievalLoc(retrievalCount++, nextSegment);
                                    block->SetSegment(nextSegment, segment);
                                }  
                            }
                            nextErasure = numData;
                            // Set erasure locs for any missing parity symbol segments
                            while (block->GetNextPending(nextErasure))
                                server->SetErasureLoc(erasureCount++, nextErasure++); 
                        }  // end if (nextErasure < numData)
                    }  // end (block->GetFirstPending(nextErasure))                 
                    
                    if (erasureCount)
                    {
                        server->Decode(block->SegmentList(), numData, erasureCount); 
                        for (UINT16 i = 0; i < erasureCount; i++) 
                        {
                            NormSegmentId sid = server->GetErasureLoc(i);
                            if (sid < numData)
                            {
                                if (WriteSegment(blockId, sid, block->GetSegment(sid), false))
                                {
                                    objectUpdated = true;
                                    // For statistics only (TBD) #ifdef NORM_DEBUG
                                    server->IncrementRecvGoodput(segmentLength);
                                }
                                block->DetachSegment(sid);   
                            }
                            else
                            {
                                break;
                            }
                        }
                    }
                    // Clear any temporarily retrieved (non-cached) segments for the block
                    for (UINT16 i = 0; i < retrievalCount; i++) 
                        block->DetachSegment(server->GetRetrievalLoc(i));
                    // OK, we're done with this block
                    pending_mask.Unset(blockId);
                    block_buffer.Remove(block);
                    server->PutFreeBlock(block);     
                }
                // Notify application of new data available
                // (TBD) this could be improved for stream objects
                //        so it's not called unnecessarily
                if (objectUpdated && notify_on_update)
                {
                    notify_on_update = false;
                    session.Notify(NormController::RX_OBJECT_UPDATED, server, this);
                }   
            }
            else
            {
                DMSG(6, "NormObject::HandleObjectMessage() node>%lu server>%lu obj>%hu "
                    "received duplicate segment ...\n", LocalNodeId(),
                     server->GetId(), (UINT16)transport_id);
            }
        }
        else
        {
            DMSG(6, "NormObject::HandleObjectMessage() node>%lu server>%lu obj>%hu "
                    "received duplicate block message ...\n", LocalNodeId(),
                     server->GetId(), (UINT16)transport_id);
        }  // end if/else pending_mask.Test(blockId)
    }  // end if/else (NORM_MSG_INFO)
}  // end NormObject::HandleObjectMessage()

// Returns source symbol segments to pool for first block with such resources
bool NormObject::ReclaimSourceSegments(NormSegmentPool& segmentPool)
{
    NormBlockBuffer::Iterator iterator(block_buffer);
    NormBlock* block;
    while ((block = iterator.GetNextBlock()))
    {
        bool reclaimed = false;
        UINT16 numData = GetBlockSize(block->GetId());
        for (UINT16 i = 0; i < numData; i++)
        {
            char* s = block->DetachSegment(i);
            if (s)
            {
                segmentPool.Put(s);
                reclaimed = true;
            }   
        }
        if (reclaimed) return true;
    }
    return false;
}  // end NormObject::ReclaimSourceSegments()


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
            NormBlockId bid = block->GetId();
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


// For client & server resource management, steals newer block resources when
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
        if (excludeBlock && (excludeId == block->GetId()))
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
        if (excludeBlock && (excludeId == block->GetId()))
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
    if (info_ptr) msg->SetFlag(NormObjectMsg::FLAG_INFO);
    msg->SetFecId(129);
    msg->SetObjectId(transport_id);
    
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
        infoMsg->SetInfo(info_ptr, info_len);
        pending_info = false;
        return true;
    }
    NormBlockId blockId;
    if (!GetFirstPending(blockId)) 
    {
        if (!IsStream())
            DMSG(0, "NormObject::NextServerMsg() pending object w/ no pending blocks?\n");
        return false;
    }
    
    NormDataMsg* data = (NormDataMsg*)msg;
    UINT16 numData = GetBlockSize(blockId);
    NormBlock* block = block_buffer.Find(blockId);
    if (!block)
    {
       if (!(block = session.ServerGetFreeBlock(transport_id, blockId)))
       {
            DMSG(2, "NormObject::NextServerMsg() node>%lu warning: server resource " 
                    "constrained (no free blocks).\n", LocalNodeId());
            return false; 
       }
       // Load block with zero initialized parity segments
       UINT16 totalBlockLen = numData + nparity;
       for (UINT16 i = numData; i < totalBlockLen; i++)
       {
            char* s = session.ServerGetFreeSegment(transport_id, blockId);
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
                DMSG(2, "NormObject::NextServerMsg() node>%lu warning: server resource " 
                        "constrained (no free segments).\n", LocalNodeId());
                session.ServerPutFreeBlock(block);
                return false;
            }
       }      
       
       block->TxInit(blockId, numData, session.ServerAutoParity());  
       if (!block_buffer.Insert(block))
       {
           ASSERT(STREAM == type);
           ASSERT(blockId > block_buffer.RangeLo());
           //if (blockId > block_buffer.RangeLo())
           {
               NormBlock* b = block_buffer.Find(block_buffer.RangeLo());
               ASSERT(b);
               block_buffer.Remove(b);
               session.ServerPutFreeBlock(b);
               bool success = block_buffer.Insert(block);
               ASSERT(success);
           }
           /*else
           {
                DMSG(0, "NormObject::NextServerMsg() node>%lu Warning! can't repair old block\n", LocalNodeId());
                session.ServerPutFreeBlock(block);
                pending_mask.Unset(blockId);
                return false;
           }*/
       }
    }
    NormSegmentId segmentId = 0;
    if (!block->GetFirstPending(segmentId)) 
    {
        DMSG(0, "NormObject::NextServerMsg() nothing pending!?\n");
        ASSERT(0);
    }
    
    // Try to read segment 
    if (segmentId < numData)
    {
        // Try to read data segment (Note "ReadSegment" copies in offset/length info also)
        char* buffer = data->AccessPayload(); 
        bool msgStart = false;
        UINT16 payloadLength = ReadSegment(blockId, segmentId, buffer, &msgStart);
        if (0 == payloadLength)
        {
            // (TBD) deal with read error 
            //(for streams, it currently means the stream is non-pending)
            if (!IsStream()) 
                DMSG(0, "NormObject::NextServerMsg() ReadSegment() error\n");          
            return false;  
        }
        data->SetPayloadLength(payloadLength);
        
        // Look for FLAG_MSG_START
        if (msgStart) 
        {
            data->SetFlag(NormObjectMsg::FLAG_MSG_START);
            //DMSG(0, "read start segment len:%hu\n", ntohs(*((UINT16*)(buffer+2))));
            //DMSG(0, "           message len:%hu\n", ntohs(*((UINT16*)(buffer+8))));
        }
        else
        {
            //DMSG(0, "non-start segment read ...\n");   
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
            session.ServerEncode(data->AccessPayload(), block->SegmentList(numData)); 
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
        char* segment = block->GetSegment(segmentId);
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
               DMSG(0, "NormStreamObject::StreamAdvance() warning: node>%lu Pending segment repairs (blk>%lu) "
                       "delaying stream advance ...\n", LocalNodeId(), (UINT32)block->GetId());
            } 
        }
    }
    else
    {
        DMSG(0, "NormStreamObject::StreamAdvance() pending block repair delaying stream advance ...\n");   
    }
}  // end NormStreamObject::StreamAdvance()

bool NormObject::CalculateBlockParity(NormBlock* block)
{
    char buffer[NormMsg::MAX_SIZE];
    UINT16 numData = GetBlockSize(block->GetId());
    for (UINT16 i = 0; i < numData; i++)
    {
        UINT16 payloadLength = ReadSegment(block->GetId(), i, buffer);
        if (0 != payloadLength)
        {
            UINT16 payloadMax = segment_size+NormDataMsg::GetStreamPayloadHeaderLength();
#ifdef SIMULATE
            payloadMax = MIN(payloadMax, SIM_PAYLOAD_MAX);
#endif // SIMULATE
            if (payloadLength < payloadMax)
                memset(buffer+payloadLength, 0, payloadMax-payloadLength+1);
            session.ServerEncode(buffer, block->SegmentList(numData));
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
    NormBlock* block = session.ServerGetFreeBlock(transport_id, blockId);
    if (block)
    {
        UINT16 numData = GetBlockSize(blockId);  
        // Init block parameters
        block->TxRecover(blockId, numData, nparity);
        // Fill block with zero initialized parity segments
        UINT16 totalBlockLen = numData + nparity;
        for (UINT16 i = numData; i < totalBlockLen; i++)
        {
            char* s = session.ServerGetFreeSegment(transport_id, blockId);
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
                //DMSG(2, "NormObject::ServerRecoverBlock() node>%lu Warning! server resource " 
                //        "constrained (no free segments).\n", LocalNodeId());
                session.ServerPutFreeBlock(block);
                return (NormBlock*)NULL;
            }
        }      
        // Attempt to re-generate parity
        if (CalculateBlockParity(block))
        {
            if (!block_buffer.Insert(block))
            {
                session.ServerPutFreeBlock(block);
                DMSG(4, "NormObject::ServerRecoverBlock() node>%lu couldn't buffer recovered block\n");
                return NULL;   
            }
            return block;
        }
        else
        {
            session.ServerPutFreeBlock(block);
            return (NormBlock*)NULL;
        }
    }
    else
    {
        //DMSG(2, "NormObject::ServerRecoverBlock() node>%lu Warning! server resource " 
        //                "constrained (no free blocks).\n", LocalNodeId());
        return (NormBlock*)NULL;
    }
}  // end NormObject::ServerRecoverBlock()

/////////////////////////////////////////////////////////////////
//
// NormFileObject Implementation
//
NormFileObject::NormFileObject(class NormSession&       theSession, 
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
            if (file.Open(thePath, O_RDWR | O_CREAT | O_TRUNC))
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
            off_t size = file.GetSize(); 
            if (size)
            {
                if (!NormObject::Open(NormObjectSize(size), 
                                      infoPtr, 
                                      infoLen,
                                      session.ServerSegmentSize(),
                                      session.ServerBlockSize(),
                                      session.ServerNumParity()))
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
                                  const char*   buffer,
                                  bool          /*msgStart*/)
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
    if (((UINT32)blockId) < large_block_count)
    {
        segmentOffset = (large_block_length*(UINT32)blockId) + (segmentSize*segmentId);
    }
    else
    {
        segmentOffset = large_block_length*large_block_count;  // (TBD) pre-calc this 
        UINT32 smallBlockIndex = (UINT32)blockId - large_block_count;
        segmentOffset = segmentOffset + small_block_length*smallBlockIndex +
                                        segmentSize*segmentId;
    }
    off_t offset = segmentOffset.GetOffset();
    if (offset != file.GetOffset())
    {
        if (!file.Seek(offset)) return false; 
    }
    UINT16 nbytes = file.Write(buffer, len);
    return (nbytes == len);
}  // end NormFileObject::WriteSegment()


UINT16 NormFileObject::ReadSegment(NormBlockId      blockId, 
                                   NormSegmentId    segmentId,
                                   char*            buffer,
                                   bool*            /*msgStart*/)            
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
    //off_t offsetScaleMSB = 0xffffffff + 1;  // yuk! can't we do better
    //off_t offset = (off_t)segmentOffset.LSB() + ((off_t)segmentOffset.MSB() * offsetScaleMSB);
    off_t offset = segmentOffset.GetOffset();
    if (offset != file.GetOffset())
    {
        if (!file.Seek(offset)) 
            return false;
    }
    UINT16 nbytes = file.Read(buffer, len);
    return (len == nbytes) ? len : 0;
}  // end NormFileObject::ReadSegment()

char* NormFileObject::RetrieveSegment(NormBlockId      blockId, 
                                      NormSegmentId    segmentId,
                                      bool&            tempRetrieval)
{
    if (server)
    {
        char* segment = server->GetRetrievalSegment();
        UINT16 len = ReadSegment(blockId, segmentId, segment);
        if (len)
        {
            // zeroize remainder for proper decodes
            if (len < segment_size)
                memset(segment+len, 0, segment_size-len);
            tempRetrieval = true;
            return segment;
        }
        else
        {
            DMSG(0, "NormFileObject::RetrieveSegment() error reading segment\n");
            return NULL;
        }          
    }   
    else
    {
        DMSG(0, "NormFileObject::RetrieveSegment() error: NULL server!\n");
        return NULL;   
    }
}  // end NormFileObject::RetrieveSegment()

/////////////////////////////////////////////////////////////////
//
// NormDataObject Implementation
//
NormDataObject::NormDataObject(class NormSession&       theSession, 
                               class NormServerNode*    theServer,
                               const NormObjectId&      objectId)
 : NormObject(DATA, theSession, theServer, objectId), 
   large_block_length(0,0), small_block_length(0,0),
   data_ptr(NULL), data_max(0), data_released(false)
{
    
}

NormDataObject::~NormDataObject()
{
    Close();
    if (data_released)
    {
        if (data_ptr)
        {
            delete[] data_ptr;
            data_ptr = NULL;
        }
        data_released = false;
    }
}

// Assign data object to data ptr
bool NormDataObject::Open(char*       dataPtr,
                          UINT32      dataLen,
                          bool        dataRelease,
                          const char* infoPtr,
                          UINT16      infoLen)
{
    if (data_released && (NULL != data_ptr))
    {
        delete[] data_ptr;
        data_ptr = NULL;
        data_released = false;   
    }
    if (server)  
    {
        // We're receiving this data object 
        ASSERT(NULL == infoPtr);
    }
    else
    {
        // We're sending this data object
        if (!NormObject::Open(dataLen, 
                              infoPtr, 
                              infoLen,
                              session.ServerSegmentSize(),
                              session.ServerBlockSize(),
                              session.ServerNumParity()))
        {
            DMSG(0, "NormDataObject::Open() send object open error\n");
            Close();
            return false;
        }
    }
    data_ptr = dataPtr;
    data_max = dataLen;
    data_released = dataRelease;
    large_block_length = NormObjectSize(large_block_size) * segment_size;
    small_block_length = NormObjectSize(small_block_size) * segment_size;
    return true;
}  // end NormDataObject::Open()
                
bool NormDataObject::Accept(char* dataPtr, UINT32 dataMax, bool dataRelease)
{
    ASSERT(NULL == server);
    if (Open(dataPtr, dataMax, dataRelease))
    {
        NormObject::Accept(); 
        return true;  
    }
    else
    {
        return false;
    }
}  // end NormDataObject::Accept()

void NormDataObject::Close()
{
    NormObject::Close();
}  // end NormDataObject::Close()

bool NormDataObject::WriteSegment(NormBlockId   blockId, 
                                  NormSegmentId segmentId, 
                                  const char*   buffer,
                                  bool          /*msgStart*/)
{
    if (NULL == data_ptr)
    {
        DMSG(0, "NormDataObject::WriteSegment() error: NULL data_ptr\n");
        return false;    
    }    
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
    ASSERT(0 == segmentOffset.MSB());
    if (data_max <= segmentOffset.LSB())
        return true;
    else if (data_max <= (segmentOffset.LSB() + len))
        len -= (segmentOffset.LSB() + len - data_max);
    memcpy(data_ptr + segmentOffset.LSB(), buffer, len);
    return true;
}  // end NormDataObject::WriteSegment()


UINT16 NormDataObject::ReadSegment(NormBlockId      blockId, 
                                   NormSegmentId    segmentId,
                                   char*            buffer,
                                   bool*            /*msgStart*/)            
{
    if (NULL == data_ptr)
    {
        DMSG(0, "NormDataObject::ReadSegment() error: NULL data_ptr\n");
        return 0;    
    }    
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
    ASSERT(0 == segmentOffset.MSB());
    if (data_max <= segmentOffset.LSB())
        return 0;
    else if (data_max <= (segmentOffset.LSB() + len))
        len -= (segmentOffset.LSB() + len - data_max);
    
    memcpy(buffer, data_ptr + segmentOffset.LSB(), len);
    return len;
}  // end NormDataObject::ReadSegment()

char* NormDataObject::RetrieveSegment(NormBlockId   blockId, 
                                      NormSegmentId segmentId,
                                      bool&         tempRetrieval)
{
    if (NULL == data_ptr)
    {
        DMSG(0, "NormDataObject::RetrieveSegment() error: NULL data_ptr\n");
        return NULL;    
    } 
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
    ASSERT(0 == segmentOffset.MSB());
    if ((len < segment_size) || (data_max < (segmentOffset.LSB() + len)))
    {
        if (server)
        {
            char* segment = server->GetRetrievalSegment();
            len = ReadSegment(blockId, segmentId, segment);
            memset(segment+len, 0, segment_size-len);
            tempRetrieval = true;
            return segment;
        }
        else
        {
            DMSG(0, "NormDataObject::RetrieveSegment() error: NULL server!\n");
            return NULL; 
        }
    }
    else
    {
        tempRetrieval = false;
        return (data_ptr + segmentOffset.LSB());
    }
}  // end NormDataObject::RetrieveSegment()

/////////////////////////////////////////////////////////////////
//
// NormStreamObject Implementation
//

NormStreamObject::NormStreamObject(class NormSession&       theSession, 
                                   class NormServerNode*    theServer,
                                   const NormObjectId&      objectId)
 : NormObject(STREAM, theSession, theServer, objectId), 
   stream_sync(false), sync_offset_valid(false), 
   flush_pending(false), msg_start(true),
   flush_mode(FLUSH_NONE), push_mode(false),
   block_pool_threshold(0)
{
}

NormStreamObject::~NormStreamObject()
{
    Close();
}  

bool NormStreamObject::Open(UINT32      bufferSize, 
                            bool        doubleBuffer,
                            const char* infoPtr, 
                            UINT16      infoLen)
{
    if (!bufferSize) 
    {
        DMSG(0, "NormStreamObject::Open() zero bufferSize error\n");
        return false;
    }
    
    UINT16 segmentSize, numData;
    if (server)
    {
        // receive streams have already be pre-opened
        segmentSize = segment_size;
        numData = ndata;
    }
    else
    {
        segmentSize = session.ServerSegmentSize();
        numData = session.ServerBlockSize();
    }
    
    UINT32 blockSize = segmentSize * numData;
    UINT32 numBlocks = bufferSize / blockSize;
    // Buffering requires at least 2 blocks
    numBlocks = MAX(2, numBlocks);
    if (doubleBuffer) numBlocks *= 2;
    UINT32 numSegments = numBlocks * numData;
        
    if (!block_pool.Init(numBlocks, numData))
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
    
    if (!stream_buffer.Init(numBlocks))
    {
        DMSG(0, "NormStreamObject::Open() stream_buffer init error\n");
        Close();
        return false;
    }    
    // (TBD) we really only need one set of indexes & offset
    // since our objects are exclusively read _or_ write
    read_init = true;
    read_index.block = read_index.segment = 0; 
    write_index.block = write_index.segment = 0;
    tx_offset = write_offset = read_offset = 0;    
    write_vacancy = true;
    posted_tx_queue_vacancy = false;
    
    if (!server)
    {
        if (!NormObject::Open(NormObjectSize((UINT32)bufferSize), 
                              infoPtr, 
                              infoLen,
                              session.ServerSegmentSize(),
                              session.ServerBlockSize(),
                              session.ServerNumParity()))
        {
            DMSG(0, "NormStreamObject::Open() object open error\n");
            Close();
            return false;
        }
        stream_next_id = pending_mask.Size();
    }
    
    stream_sync = false;
    sync_offset_valid = false;
    flush_pending = false;
    msg_start = true;
    return true;
}  // end NormStreamObject::Open()

bool NormStreamObject::Accept(UINT32 bufferSize, bool doubleBuffer)
{
    if (Open(bufferSize, doubleBuffer))
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
    tx_offset = write_offset = read_offset = 0;
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

bool NormStreamObject::LockBlocks(NormBlockId firstId, NormBlockId lastId)
{
    NormBlockId nextId = firstId;
    // First check that we _can_ lock them all
    while (nextId <= lastId)
    {
        NormBlock* block = stream_buffer.Find(nextId);
        if (NULL == block) return false;
        nextId++;
    }
    nextId = firstId;
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
                            GetFirstPending(stream_sync_id);
                        return true;
                    }
                    else
                    {
                        // Stream broken
                        //DMSG(0, "NormObject::StreamUpdateStatus() broken 1 ...\n");
                        return false;   
                    }
                }
                else
                {
                    NormBlockId delta = blockId - stream_next_id + 1;
                    if (delta > NormBlockId(pending_mask.Size()))
                    {
                        // Stream broken
                        //DMSG(0, "NormObject::StreamUpdateStatus() broken 2 ...\n");
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
        read_init = true;
        return true;  
    }
}  // end NormStreamObject::StreamUpdateStatus()


char* NormStreamObject::RetrieveSegment(NormBlockId     blockId, 
                                        NormSegmentId   segmentId,
                                        bool&           tempRetrieval)
{
    NormBlock* block = stream_buffer.Find(blockId);
    if (!block)
    {
        DMSG(0, "NormStreamObject::RetrieveSegment() segment block unavailable\n");
        return NULL;   
    }
    char* segment = block->GetSegment(segmentId);
    if (NULL == segment)
        DMSG(0, "NormStreamObject::RetrieveSegment() segment unavailable\n");
    tempRetrieval = false;
    return segment;
}  // end NormStreamObject::RetrieveSegment()

UINT16 NormStreamObject::ReadSegment(NormBlockId      blockId, 
                                     NormSegmentId    segmentId,
                                     char*            buffer,
                                     bool*            msgStart)
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
        //DMSG(0, "NormStreamObject::ReadSegment(blk>%lu seg>%hu) stream starved (2) (write_index>%lu:%hu)\n",
        //        (UINT32)blockId, (UINT16)segmentId, (UINT32)write_index.block, (UINT16)write_index.segment);
        return false;   
    }   
    block->UnsetPending(segmentId);    
    
    char* segment = block->GetSegment(segmentId);
    ASSERT(segment != NULL);    
        
    // Update tx_offset if ((segmentOffset - tx_offset) > 0)
    UINT32 segmentOffset = NormDataMsg::ReadStreamPayloadOffset(segment);
    INT32 offsetDelta = segmentOffset - tx_offset;
    if (offsetDelta > 0) tx_offset = segmentOffset;
    
    // Only advertise vacancy if stream_buffer.RangeLo() is non-pending _and_
    // (write_offset - tx_offset) > streamBufferSize
    if (!write_vacancy)
    {
        offsetDelta = write_offset - tx_offset;
        ASSERT(offsetDelta >= 0);
        if ((UINT32)offsetDelta < object_size.LSB())
        {
            NormBlock* b = stream_buffer.Find(stream_buffer.RangeLo());
            if (b && !b->IsPending()) write_vacancy = true; 
        }       
    }
    if (write_vacancy && !posted_tx_queue_vacancy)
    {
        posted_tx_queue_vacancy = true;
        session.Notify(NormController::TX_QUEUE_VACANCY, NULL, this);  
    }   
    
    UINT16 segmentLength = NormDataMsg::ReadStreamPayloadLength(segment);
    ASSERT(segmentLength <= segment_size);
    UINT16 payloadMax = segment_size + NormDataMsg::GetStreamPayloadHeaderLength();
    UINT16 payloadLength = segmentLength+NormDataMsg::GetStreamPayloadHeaderLength();
#ifdef SIMULATE    
    payloadMax = MIN(payloadMax, SIM_PAYLOAD_MAX);
    UINT16 copyMax = MIN(payloadMax, payloadLength);
    memcpy(buffer, segment, copyMax); 
#else
    memcpy(buffer, segment, payloadLength);
#endif // SIMULATE
    if (msgStart)
        *msgStart = (0 != (NormDataMsg::FLAG_MSG_START & segment[payloadMax]));
    return payloadLength;
}  // end NormStreamObject::ReadSegment()

bool NormStreamObject::WriteSegment(NormBlockId   blockId, 
                                    NormSegmentId segmentId, 
                                    const char*   segment,
                                    bool          msgStart)
{
    UINT32 segmentOffset = NormDataMsg::ReadStreamPayloadOffset(segment);
    
    if (read_init)
    {
        read_init = false;
        read_index.block = blockId;
        read_index.segment = segmentId;   
        read_offset = segmentOffset;
    } 
        
    if ((blockId < read_index.block) ||
        ((blockId == read_index.block) &&
         (segmentId < read_index.segment))) 
    {
        DMSG(4, "NormStreamObject::WriteSegment() block/segment < read_index!?\n");
        return false;
    }      
    
    // if (segmentOffset < read_offset)
    UINT32 diff = segmentOffset - read_offset;
    if ((diff > 0x80000000) || ((0x80000000 == diff) && (segmentOffset > read_offset)))
    {
        DMSG(0, "NormStreamObject::WriteSegment() diff:%lu segmentOffset:%lu < read_offset:%lu \n",
                 diff, segmentOffset, read_offset);
        ASSERT(0);
        return false;
    }
    
    NormBlock* block = stream_buffer.Find(blockId);
    if (!block)
    {
        bool broken = false;
        bool dataLost = false;
        while (block_pool.IsEmpty() || !stream_buffer.CanInsert(blockId))
        {
            block = stream_buffer.Find(stream_buffer.RangeLo());
            ASSERT(block);
            if (blockId < block->GetId())
            {
                DMSG(4, "NormStreamObject::WriteSegment() blockId too old!?\n"); 
                return false;   
            }
            while (block->IsPending())
            {
                broken = true;
                // Force read_index forward, giving app a chance to read data
                read_index.block = block->GetId();
                block->GetFirstPending(read_index.segment);
                NormBlock* tempBlock = block;
                UINT32 tempOffset = read_offset; 
                session.Notify(NormController::RX_OBJECT_UPDATED, server, this);  
                block = stream_buffer.Find(stream_buffer.RangeLo());
                if (tempBlock == block)
                {
                    if (tempOffset == read_offset)
                    {
                        // App didn't want any data here, purge segment
                        //ASSERT(0);
                        dataLost = true;
                        block->UnsetPending(read_index.segment++);
                        if (read_index.segment >= ndata)
                        {
                            read_index.block++;
                            read_index.segment = 0;
                            stream_buffer.Remove(block);
                            block->EmptyToPool(segment_pool);
                            block_pool.Put(block);
                            block = NULL;
                            Prune(read_index.block);
                            break;
                        }      
                    }
                }
                else
                {
                    // App consumed block via Read() 
                    block = NULL;
                    break;  
                }
            }  // end while (block->IsPending())   
            if (block)
            {
                if (block->GetId() == read_index.block)
                {
                    read_index.block++;
                    read_index.segment = 0;
                    Prune(read_index.block);   
                }
                stream_buffer.Remove(block);
                block->EmptyToPool(segment_pool);
                block_pool.Put(block);
            } 
        }  // end while (block_pool.IsEmpty() || !stream_buffer.CanInsert(blockId))
        if (broken)
        {
            DMSG(4, "NormStreamObject::WriteSegment() node>%lu obj>%hu blk>%lu seg>%hu broken stream ...\n",
                     LocalNodeId(), (UINT16)transport_id, (UINT32)blockId, (UINT16)segmentId);
            if (dataLost)
                DMSG(0, "NormStreamObject::WriteSegment() broken stream data not read by app!\n");
        }    
        block = block_pool.Get();
        block->SetId(blockId);
        block->ClearPending();
        ASSERT(blockId >= read_index.block);
        bool success = stream_buffer.Insert(block);
        ASSERT(success);
    }  // end if (!block)
    
    // Make sure this segment hasn't already been written
    if(!block->GetSegment(segmentId))
    {
        char* s = segment_pool.Get();
        ASSERT(s != NULL);  // for now, this should always succeed
        UINT16 payloadLength = NormDataMsg::ReadStreamPayloadLength(segment) + NormDataMsg::GetStreamPayloadHeaderLength();
        UINT16 payloadMax = segment_size + NormDataMsg::GetStreamPayloadHeaderLength();
#ifdef SIMULATE
        payloadMax = MIN(SIM_PAYLOAD_MAX, payloadMax);
        payloadLength = MIN(payloadMax, payloadLength);  
#endif // SIMULATE
        memcpy(s, segment, payloadLength);
        // Store "msgStart" info at end of payload space
        s[payloadMax] = msgStart ? NormDataMsg::FLAG_MSG_START : 0;
        
        block->AttachSegment(segmentId, s);
        block->SetPending(segmentId);
        ASSERT(block->GetSegment(segmentId) == s);
    }
    if (!sync_offset_valid)
    {
        // Set "sync_offset" on first received data buffered.
        sync_offset = segmentOffset;
        sync_offset_valid = true;   
    }
    return true;
}  // end NormStreamObject::WriteSegment()

void NormStreamObject::Prune(NormBlockId blockId)
{
    bool resync = false;
    NormBlock* block;
    while ((block = block_buffer.Find(block_buffer.RangeLo())))
    {
        if (block->GetId() < blockId)
        {
            resync = true;
            pending_mask.Unset(block->GetId()); 
            block_buffer.Remove(block);
            server->PutFreeBlock(block);
        }   
        else
        {
            break;
        }
    }
    NormBlockId firstId;
    if (GetFirstPending(firstId))
    {
        if (firstId < blockId)
        {
            resync = true;
            UINT32 count = blockId - firstId;
            pending_mask.UnsetBits(firstId, count);
        }
    }
    bool result = StreamUpdateStatus(blockId);
    ASSERT(result);
    if (resync) server->IncrementResyncCount();
    result = GetFirstPending(firstId);
}  // end NormStreamObject::Prune()

// Sequential (in order) read/write routines (TBD) Add a "Seek()" method
bool NormStreamObject::Read(char* buffer, unsigned int* buflen, bool findMsgStart)
{
    SetNotifyOnUpdate(true);  // reset notification when streams are read
    unsigned int bytesRead = 0;
    unsigned int bytesToRead = *buflen;
    //while (bytesToRead > 0)
    do
    {
        NormBlock* block = stream_buffer.Find(read_index.block);
        if (!block)
        {
           // DMSG(0, "NormStreamObject::Read() stream buffer empty (1) (sbEmpty:%d)\n", stream_buffer.IsEmpty());
            *buflen = bytesRead;
            if (bytesRead > 0)
            {
                return true;
            }
            else
            {
                if (block_pool.GetCount() < block_pool_threshold)
                {
                    // Force read_index forward and try again.
                    if (++read_index.segment >= ndata)
                    {
                        read_index.block++;  
                        read_index.segment = 0; 
                        Prune(read_index.block);
                    }
                    continue;
                }
                else
                {
                    return findMsgStart ? false : true;   
                }
            }
        }
        char* segment = block->GetSegment(read_index.segment);
        
        ASSERT(!segment || block->IsPending(read_index.segment));
        
        if (!segment)
        {
            //DMSG(0, "NormStreamObject::Read(%lu:%hu) stream buffer empty (2)\n",
            //        (UINT32)read_index.block, read_index.segment);
            *buflen = bytesRead;
            if (bytesRead > 0)
            {
                return true;
            }
            else
            {
                if (block_pool.GetCount() < block_pool_threshold)
                {
                    // Force read_index forward and try again.
                    if (++read_index.segment >= ndata)
                    {
                        stream_buffer.Remove(block);
                        block->EmptyToPool(segment_pool);
                        block_pool.Put(block);
                        read_index.block++;
                        read_index.segment = 0;
                        Prune(read_index.block);
                    }
                    continue;
                }
                else
                {
                    return findMsgStart ? false : true;   
                } 
            } 
        }
        
        UINT32 segmentOffset = NormDataMsg::ReadStreamPayloadOffset(segment);
        // if (read_offset < segmentOffset)
        UINT32 diff = read_offset - segmentOffset;
        if ((diff > 0x80000000) || ((0x80000000 == diff) && (read_offset > segmentOffset)))
        {
            DMSG(4, "NormStreamObject::Read() node>%lu obj>%hu blk>%lu seg>%hu broken stream! (read_offset>%lu segmentOffset>%lu)\n", 
                    LocalNodeId(), (UINT16)transport_id, (UINT32)read_index.block, read_index.segment, read_offset, segmentOffset);
            read_offset = segmentOffset;
            *buflen = bytesRead;
            return false;
        }
        
        UINT32 index = read_offset - segmentOffset;
        UINT16 length = NormDataMsg::ReadStreamPayloadLength(segment);
	    if (index >= length)
        {
            DMSG(0, "NormStreamObject::Read() node>%lu obj>%hu blk>%lu seg>%hu mangled stream! index:%hu length:%hu "
                    "read_offset:%lu segmentOffset:%lu\n",
                    LocalNodeId(), (UINT16)transport_id, (UINT32)read_index.block, read_index.segment, index, length, read_offset, segmentOffset);
            read_offset = segmentOffset;
            *buflen = bytesRead;
            ASSERT(0);
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
                UINT16 payloadMax = segment_size+NormDataMsg::GetStreamPayloadHeaderLength();
#ifdef SIMULATE
                payloadMax = MIN(payloadMax, SIM_PAYLOAD_MAX);
#endif // if/else SIMULATE               
                msgStart = (NormObjectMsg::FLAG_MSG_START == segment[payloadMax]);
            }
            if (!msgStart)
            {
                // Don't bother managing individual segments since
                // stream buffers are exact multiples of block size!
                //block->DetachSegment(read_index.segment);
                //segment_pool.Put(segment);
                block->UnsetPending(read_index.segment++);
                if (read_index.segment >= ndata) 
                {
                    stream_buffer.Remove(block);
                    block->EmptyToPool(segment_pool);
                    block_pool.Put(block);
                    read_index.block++;
                    read_index.segment = 0;
                    Prune(read_index.block);
                }
                continue;
            }
        }
        
#ifdef SIMULATE
        UINT16 simCount = index + count + NormDataMsg::GetStreamPayloadHeaderLength();
        simCount = (simCount < SIM_PAYLOAD_MAX) ? (SIM_PAYLOAD_MAX - simCount) : 0;
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
            // Don't bother managing individual segments since
            // stream buffers are multiples of block size!
            // block->DetachSegment(read_index.segment);
            // segment_pool.Put(segment);
            block->UnsetPending(read_index.segment++);
            if (read_index.segment >= ndata) 
            {
                stream_buffer.Remove(block);
                block->EmptyToPool(segment_pool);
                block_pool.Put(block);
                read_index.block++;
                read_index.segment = 0;
                Prune(read_index.block);
            }
        } 
    }  while (bytesToRead > 0); // end while (len > 0)
    *buflen = bytesRead;
    return true;
}  // end NormStreamObject::Read()


UINT32 NormStreamObject::Write(const char* buffer, UINT32 len, bool eom)
{
    UINT32 nBytes = 0;
    do
    {
        INT32 deltaOffset = write_offset - tx_offset;
        ASSERT(deltaOffset >= 0);
        if (deltaOffset >= (INT32)object_size.LSB())
        {
            write_vacancy = false;
            DMSG(8, "NormStreamObject::Write() stream buffer full (1)\n", len, eom);
            break;  // (TBD) skip break if push_mode is enabled???
        }
        NormBlock* block = stream_buffer.Find(write_index.block);
        if (!block)
        {
            if (!(block = block_pool.Get()))
            {
                block = stream_buffer.Find(stream_buffer.RangeLo());
                ASSERT(block);
                if (block->IsPending())
                {
                    write_vacancy = false;
                    if (push_mode)
                    {
                        NormBlockId blockId = block->GetId();
                        pending_mask.Unset(blockId);
                        repair_mask.Unset(blockId);
                        NormBlock* b = FindBlock(blockId);
                        if (b)
                        {
                            block_buffer.Remove(b);
                            session.ServerPutFreeBlock(b); 
                        }   
                        if (!pending_mask.IsSet()) 
                        {
                            pending_mask.Set(write_index.block);  
                            stream_next_id = write_index.block + 1;
                        }
                    }
                    else
                    {
                        DMSG(4, "NormStreamObject::Write() stream buffer full (2) len:%d eom:%d\n", len, eom);
                        break;
                    }
                }                             
                stream_buffer.Remove(block);
                block->EmptyToPool(segment_pool);
            }
            block->SetId(write_index.block);
            block->ClearPending();
            bool success = stream_buffer.Insert(block);
            ASSERT(success);
            
        }
        char* segment = block->GetSegment(write_index.segment);
        if (!segment)
        {
            if (!(segment = segment_pool.Get()))
            {
                NormBlock* b = stream_buffer.Find(stream_buffer.RangeLo());
                ASSERT(b != block);
                if (b->IsPending())
                {
                    write_vacancy = false;
                    if (push_mode)
                    {
                        NormBlockId blockId = b->GetId();
                        pending_mask.Unset(blockId);
                        repair_mask.Unset(blockId);
                        NormBlock* c = FindBlock(blockId);
                        if (c)
                        {
                            block_buffer.Remove(c);
                            session.ServerPutFreeBlock(c);
                        }  
                        if (!pending_mask.IsSet()) 
                        {
                            pending_mask.Set(write_index.block);  
                            stream_next_id = write_index.block + 1;
                        }  
                    }
                    else
                    {
                        DMSG(4, "NormStreamObject::Write() stream buffer full (3)\n");
                        break;
                    }
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
            UINT16 payloadMax = segment_size+NormDataMsg::GetStreamPayloadHeaderLength();
#ifdef SIMULATE
            payloadMax = MIN(SIM_PAYLOAD_MAX, payloadMax);
#endif // SIMULATE 
            segment[payloadMax] = msgStartValue;
        }
        else
        {
            msg_start = false;   
        }            
        
        UINT16 count = (UINT16)(len - nBytes);
        UINT16 space = segment_size - index;
        count = MIN(count, space);
#ifdef SIMULATE
        UINT16 simCount = index + NormDataMsg::GetStreamPayloadHeaderLength();
        simCount = (simCount < SIM_PAYLOAD_MAX) ? (SIM_PAYLOAD_MAX - simCount) : 0;
        simCount = MIN(count, simCount);
        memcpy(segment+index+NormDataMsg::GetStreamPayloadHeaderLength(), buffer+nBytes, simCount);
#else
        memcpy(segment+index+NormDataMsg::GetStreamPayloadHeaderLength(), buffer+nBytes, count);
#endif // if/else SIMULATE
        NormDataMsg::WriteStreamPayloadLength(segment, index+count);
        nBytes += count;
        write_offset += count;
        // Is the segment full? or flushing
        //if ((count == space) || ((FLUSH_NONE != flush_mode) && (0 != index) && (nBytes == len)))
        if ((count == space) || 
            ((FLUSH_NONE != flush_mode) && (nBytes == len) && ((0 != index) || (0 != len))))
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
        if (FLUSH_ACTIVE == flush_mode) 
            flush_pending = true;
        else
            flush_pending = false;
        if ((0 != nBytes) || (FLUSH_NONE != flush_mode))
            session.TouchServer();
    }
    else
    {
        if (0 != nBytes) 
            session.TouchServer();  
    }
    if ((0 != nBytes) && posted_tx_queue_vacancy)
        posted_tx_queue_vacancy = false;
    return nBytes;
}  // end NormStreamObject::Write()

#ifdef SIMULATE
/////////////////////////////////////////////////////////////////
//
// NormSimObject Implementation (dummy NORM_OBJECT_FILE or NORM_OBJECT_DATA)
//

NormSimObject::NormSimObject(class NormSession&       theSession,
                             class NormServerNode*    theServer,
                             const NormObjectId&      objectId)
 : NormObject(FILE, theSession, theServer, objectId)
{
    
}

NormSimObject::~NormSimObject()
{
    
}

bool NormSimObject::Open(UINT32        objectSize,
                         const char*   infoPtr ,
                         UINT16        infoLen)
{
    return (server ?
                true : 
                NormObject::Open(objectSize, infoPtr, infoLen, 
                                 session.ServerSegmentSize(),
                                 session.ServerBlockSize(),
                                 session.ServerNumParity()));
}  // end NormSimObject::Open()

UINT16 NormSimObject::ReadSegment(NormBlockId      blockId, 
                                  NormSegmentId    segmentId,
                                  char*            buffer,
                                  bool*            /*msgStart*/)            
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

char* NormSimObject::RetrieveSegment(NormBlockId   blockId,
                                     NormSegmentId segmentId,
                                     bool&         tempRetrieval)
{
    tempRetrieval = true;
    return server ? server->GetRetrievalSegment() : NULL;   
}  // end NormSimObject::RetrieveSegment()

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
            delete obj;  // (TBD - make this obj->Release() ???
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
        while (theObject && (objectId != theObject->GetId())) 
            theObject = theObject->next;
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
    const NormObjectId& objectId = theObject->GetId();
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
    while (entry && (entry->GetId() < objectId)) 
    {
        prev = entry;
        entry = entry->next;
    }  
    if (prev)
        prev->next = theObject;
    else
        table[index] = theObject;
    ASSERT((entry ? (objectId != entry->GetId()) : true));
    theObject->next = entry;
    count++;
    size = size + theObject->GetSize();
    return true;
}  // end NormObjectTable::Insert()

bool NormObjectTable::Remove(const NormObject* theObject)
{
    ASSERT(theObject);
    const NormObjectId& objectId = theObject->GetId();
    if (range)
    {
        if ((objectId < range_lo) || (objectId > range_hi)) return false;
        UINT16 index = ((UINT16)objectId) & hash_mask;
        NormObject* prev = NULL;
        NormObject* entry = table[index];
        while (entry && (entry->GetId() != objectId))
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
                        NormObjectId id = (UINT16)objectId + offset;
                        while(entry && (entry->GetId() != id)) 
                        {
                            if ((entry->GetId() > objectId) && 
                                (entry->GetId() < nextId)) nextId = entry->GetId();
                            entry = entry->next;
                               
                        }
                        if (entry) break;    
                    }
                } while (i != endex);
                if (entry)
                    range_lo = entry->GetId();
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
                        NormObjectId id = (UINT16)objectId - offset;
                        //printf("Looking for id:%lu at index:%lu\n", (UINT16)transport_id, i);
                        while(entry && (entry->GetId() != id)) 
                        {
                            if ((entry->GetId() < objectId) && 
                                (entry->GetId() > prevId)) prevId = entry->GetId();
                            entry = entry->next;
                        }
                        if (entry) break;    
                    }
                } while (i != endex);
                if (entry)
                    range_hi = entry->GetId();
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
        size = size - theObject->GetSize();
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
                while ((NULL != entry) && (entry->GetId() != id)) 
                {
                    if ((entry->GetId() > index) && (entry->GetId() < nextId))
                        nextId = entry->GetId();
                    entry = table.Next(entry);
                }
                if (entry)
                {
                    index = entry->GetId();
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
                while ((NULL != entry ) && (entry->GetId() != id)) 
                {
                    if ((entry->GetId() > index) && (entry->GetId() < nextId))
                        nextId = entry->GetId();
                    entry = table.Next(entry);
                }
                if (entry)
                {
                    index = entry->GetId();
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
