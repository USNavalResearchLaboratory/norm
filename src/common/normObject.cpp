#include "normObject.h"
#include "normSession.h"

#ifndef _WIN32_WCE
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#endif // !_WIN32_WCE

NormObject::NormObject(NormObject::Type      theType, 
                       class NormSession&    theSession, 
                       class NormSenderNode* theSender,
                       const NormObjectId&   transportId)
 : type(theType), session(theSession), sender(theSender), reference_count(1),
   transport_id(transportId), segment_size(0), pending_info(false), repair_info(false),
   current_block_id(0), next_segment_id(0), 
   max_pending_block(0), max_pending_segment(0),
   info_ptr(NULL), info_len(0), first_pass(true), accepted(false), notify_on_update(true),
   user_data(NULL)
#ifndef USE_PROTO_TREE
   , next(NULL)
#endif
{
    if (theSender)
    {
        nacking_mode = theSender->GetDefaultNackingMode();
        theSender->Retain();
    }
    else
    { 
        nacking_mode = NACK_NORMAL; // it doesn't really matter if !theSender
    }
}

NormObject::~NormObject()
{
    Close();
    if (NULL != info_ptr) 
    {
        delete[] info_ptr;
        info_ptr = NULL;
    }
}

void NormObject::Retain()
{
    reference_count++;
    if (sender) sender->Retain();
}  // end NormObject::Retain()

void NormObject::Release()
{
    if (sender) sender->Release();
    if (reference_count)
    {
        reference_count--;
    }
    else
    {
        PLOG(PL_ERROR, "NormObject::Release() releasing non-retained object?!\n");
    }
    if (0 == reference_count) delete this;      
}  // end NormObject::Release()

// This is mainly used for debug messages
NormNodeId NormObject::LocalNodeId() const
{
    return session.LocalNodeId();    
}  // end NormObject::LocalNodeId()

NormNodeId NormObject::GetSenderNodeId() const
{
    return sender ? sender->GetId() : NORM_NODE_NONE;   
}  // end NormObject::GetSenderNodeId()

bool NormObject::Open(const NormObjectSize& objectSize, 
                      const char*           infoPtr, 
                      UINT16                infoLen,
                      UINT16                segmentSize,
                      UINT8                 fecId,
                      UINT8                 fecM,
                      UINT16                numData,
                      UINT16                numParity)
{
    // Note "objectSize" represents actual total object size for
    // DATA or FILE objects, buffer size for STREAM objects
    // In either case, we need our sliding bit masks to be of 
    // appropriate size.
    if (sender)
    {
        if (infoLen > 0) 
        {
            pending_info = true;
            if (!(info_ptr = new char[segmentSize]))
            {
                PLOG(PL_FATAL, "NormObject::Open() info allocation error\n");
                return false;
            }               
        }
        info_len = 0;  // will be set properly upon NORM_INFO arrival
        last_nack_time.GetCurrentTime(); // init to now
    }
    else
    {
        if (NULL != infoPtr)
        {
            if (NULL != info_ptr) delete[] info_ptr;
            if (infoLen > segmentSize)
            {
                PLOG(PL_FATAL, "NormObject::Open() info too big error\n");
                info_len = 0;
                return false;
            }        
            if (!(info_ptr = new char[infoLen]))
            {
                PLOG(PL_FATAL, "NormObject::Open() info allocation error\n");
                info_len = 0;
                return false;
            } 
            memcpy(info_ptr, infoPtr, infoLen);
            info_len = infoLen;
            pending_info = true;
        }
        else 
        {
            if (NormSession::FTI_INFO == session.SenderFtiMode())
            {
                
                pending_info = true;
            }
        }
    }
    
    // Compute number of segments and coding blocks for the object
    // (Note NormObjectSize divide operator always rounds _upwards_)
    NormObjectSize numSegments = objectSize / NormObjectSize(segmentSize);    
    NormObjectSize numBlocks = numSegments / NormObjectSize(numData);
    
    // TBD - make sure numBlocks is in range of fec_block_mask
    fec_block_mask = NormPayloadId::GetFecBlockMask(fecId, fecM);
    
    UINT32 blockIdMax = fec_block_mask;
    if (IsStream()) blockIdMax = (blockIdMax / 2) - 1;
    if ((numBlocks.MSB() > 0) || (numBlocks.LSB() > blockIdMax))
    {
        PLOG(PL_FATAL, "NormObject::Open() error: object size exceeds FEC blocking and segmentation parameter capability\n");  
        Close();
        return false;
    }
        
    // No NormBlockId wrapping for NORM_DATA or NORM_FILE objects
    // (so we zero fec_block_mask for "unsigned" value behaviors
    // in NormBlockId and ProtoBitmask classes    

    if (!IsStream()) fec_block_mask = 0;  
    
    if (!block_buffer.Init(numBlocks.LSB(), 256, fec_block_mask))
    {
        PLOG(PL_FATAL, "NormObject::Open() init block_buffer error\n");  
        Close();
        return false;
    }
    
    // Init pending_mask (everything pending)
    // Note the pending_mask and repair_mask both need to be inited with a
    //       "rangeMask" according to their fecId (e.g. 24-bit mask for some)!!!
    if (!pending_mask.Init(numBlocks.LSB(), fec_block_mask))
    {
        PLOG(PL_FATAL, "NormObject::Open() init pending_mask (%lu) error: %s\n", 
                        (unsigned long)numBlocks.LSB(), GetErrorString());  
        Close();
        return false; 
    }
    
    // Init repair_mask
    if (!repair_mask.Init(numBlocks.LSB(), fec_block_mask))
    {
        PLOG(PL_FATAL, "NormObject::Open() init pending_mask error\n");  
        Close();
        return false; 
    }
    repair_mask.Clear();
        
    if (STREAM == type)
    {
        small_block_size = large_block_size = numData;
        small_block_count = large_block_count = numBlocks.LSB();
        final_segment_size = segmentSize; 
        if (NULL == sender)
        {
            // This inits sender stream state to set things tx pending
            // (the receiver stream is sync'd according to sync policy)
            NormStreamObject* stream = static_cast<NormStreamObject*>(this);
            stream->StreamResync(NormBlockId(0));
        }
    }
    else
    {
        // Set everything pending (i.e, nothing sent or received yet)
        pending_mask.Clear();
        pending_mask.SetBits(0, numBlocks.LSB());
        // Compute FEC block structure per NORM Protocol Spec Section 5.1.1
        // (Note NormObjectSize divide operator always rounds _up_, i.e., ceil(numSegments/numBlocks))
        NormObjectSize largeBlockSize;
        if ((0 != numBlocks.MSB()) || (0 != numBlocks.LSB()))
            largeBlockSize = numSegments / numBlocks;
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
    fec_id = fecId;
    fec_m = fecM;
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
        if (sender)
            sender->PutFreeBlock(block);
        else
            session.SenderPutFreeBlock(block);
    }
    repair_mask.Destroy();
    pending_mask.Destroy();
    block_buffer.Destroy();
    segment_size = 0;
}  // end NormObject::Close()

NormObjectSize NormObject::GetBytesPending() const
{
    NormBlockId nextId;
    if (!IsStream() && GetFirstPending(nextId))
    {
        NormObjectSize largeBlockBytes = NormObjectSize(large_block_size) * 
                                         NormObjectSize(segment_size);
        NormObjectSize smallBlockBytes = NormObjectSize(small_block_size) * 
                                         NormObjectSize(segment_size);
        NormObjectSize lastBlockBytes  = smallBlockBytes - NormObjectSize(segment_size) +
                                         NormObjectSize(final_segment_size);
        NormObjectSize pendingBytes(0);
        do
        {
            NormBlock* block = block_buffer.Find(nextId);
            if (block)
                pendingBytes += block->GetBytesPending(GetBlockSize(nextId), segment_size, 
                                                       final_block_id, final_segment_size);
            else if (nextId.GetValue() < large_block_count)
                pendingBytes += largeBlockBytes;
            else if (nextId == final_block_id)
                pendingBytes += lastBlockBytes;
            else
                pendingBytes += smallBlockBytes;  
            Increment(nextId);
        } while (GetNextPending(nextId));
        return pendingBytes;
    }
    else
    {
        return NormObjectSize(0);   
    }    
}  // end NormObject::GetBytesPending()

// Used by sender
bool NormObject::HandleInfoRequest(bool holdoff)
{
    bool increasedRepair = false;
    if (info_ptr || (NormSession::FTI_INFO == session.SenderFtiMode()))
    {
        if (!repair_info)
        {
            increasedRepair = true;
            if (holdoff)
            {
                if (pending_info)
                    increasedRepair = false;
                else
                    pending_info = true;
            }
            else
            {
                pending_info = true;  // does this really need to be done?
                repair_info = true;
            }
        }   
    }
    return increasedRepair;
}  // end NormObject::HandleInfoRequest()

/*
bool NormObject::HandleBlockRequest(const NormBlockId& firstId, const NormBlockId& lastId)
{
    PLOG(PL_TRACE, "NormObject::HandleBlockRequest() node>%lu blk>%lu -> blk>%lu\n", 
                    (unsigned long)LocalNodeId(), 
                    (unsigned long)firstId.GetValue(), 
                    (unsigned long)lastId.GetValue());
    bool increasedRepair = false;
    NormBlockId nextId = firstId;
    while (Compare(nextId, lastId) <= 0)
    {
        if (!repair_mask.Test(nextId.GetValue()))
        {
            // (TBD) these tests can probably go away if everything else is done right
            if (!pending_mask.CanSet(nextId.GetValue()))
                PLOG(PL_ERROR, "NormObject::HandleBlockRequest() pending_mask.CanSet(%lu) error\n",
                                (unsigned long)nextId.GetValue());
            if (!repair_mask.Set(nextId.GetValue()))
                PLOG(PL_ERROR, "NormObject::HandleBlockRequest() repair_mask.Set(%lu) error\n",
                                (unsigned long)nextId.GetValue());
            increasedRepair = true;   
        }
        Increment(nextId);
    }
    return increasedRepair;
}  // end NormObject::HandleBlockRequest();
*/
        
bool NormObject::HandleBlockRequest(const NormBlockId& firstId, const NormBlockId& lastId)
{
    PLOG(PL_TRACE, "NormObject::HandleBlockRequest() node>%lu blk>%lu -> blk>%lu\n", 
                    (unsigned long)LocalNodeId(), 
                    (unsigned long)firstId.GetValue(), 
                    (unsigned long)lastId.GetValue());
    bool blockInRange = true;
    NormBlockId nextId = firstId;
    while (Compare(nextId, lastId) <= 0)
    {
        if (!repair_mask.Test(nextId.GetValue()))
        {
            // (TBD) these tests can probably go away if everything else is done right
            if (!pending_mask.CanSet(nextId.GetValue()))
            {
                PLOG(PL_ERROR, "NormObject::HandleBlockRequest() pending_mask.CanSet(%lu) error\n",
                                (unsigned long)nextId.GetValue());
                blockInRange = false;
                if (IsStream())
                    static_cast<NormStreamObject*>(this)->UnlockBlock(nextId);
            }
            else if (!repair_mask.Set(nextId.GetValue()))
            {
                PLOG(PL_ERROR, "NormObject::HandleBlockRequest() repair_mask.Set(%lu) error\n",
                                (unsigned long)nextId.GetValue());
                blockInRange = false;
                if (IsStream())
                    static_cast<NormStreamObject*>(this)->UnlockBlock(nextId);
            }  
        }
        Increment(nextId);
    }
    return blockInRange;
}  // end NormObject::HandleBlockRequest();


bool NormObject::TxReset(NormBlockId firstBlock, bool requeue)
{
    bool increasedRepair = false;
    if (!pending_info && (HaveInfo() || (NormSession::FTI_INFO == session.SenderFtiMode())))
    {
        increasedRepair = true;
        pending_info = true;
    }
    repair_info = false;
    repair_mask.Reset(firstBlock.GetValue());
    repair_mask.Xor(pending_mask);
    if (repair_mask.IsSet()) 
    {
        increasedRepair = true;
        pending_mask.Reset(firstBlock.GetValue());
    }
    repair_mask.Clear();
    // TBD - Use "firstBlock" as starting point for the iterator
    NormBlockBuffer::Iterator iterator(block_buffer);
    NormBlock* block;
    while ((block = iterator.GetNextBlock()))
    {
        NormBlockId blockId = block->GetId();
        //if (blockId >= firstBlock)
        if (Compare(blockId, firstBlock) >= 0)
        {
            increasedRepair |= block->TxReset(GetBlockSize(blockId), 
                                              nparity, 
                                              session.SenderAutoParity(), 
                                              segment_size);
            if (requeue) block->ClearFlag(NormBlock::IN_REPAIR);  // since we're requeuing
        }
    }
    if (requeue) 
    {
        first_pass = true;
        max_pending_block = 0;
    }
    return increasedRepair;
}  // end NormObject::TxReset()

bool NormObject::TxResetBlocks(const NormBlockId& firstId, const NormBlockId& lastId)
{
    bool increasedRepair = false;
    UINT16 autoParity = session.SenderAutoParity();
    NormBlockId nextId = firstId;
    while (Compare(nextId, lastId) <= 0)
    {
        if (!pending_mask.Test(nextId.GetValue()))
        {
            pending_mask.Set(nextId.GetValue());
            increasedRepair = true;
        }
        NormBlock* block = block_buffer.Find(nextId);
        if (NULL != block) 
            increasedRepair |= block->TxReset(GetBlockSize(nextId), nparity, autoParity, segment_size);
        Increment(nextId);
    }
    return increasedRepair;
}  // end NormObject::TxResetBlocks()

bool NormObject::TxUpdateBlock(NormBlock*       theBlock, 
                               NormSegmentId    firstSegmentId, 
                               NormSegmentId    lastSegmentId,
                               UINT16           numErasures)
{
    NormBlockId blockId = theBlock->GetId();
    if (pending_mask.CanSet(blockId.GetValue()))
    {
        if (theBlock->TxUpdate(firstSegmentId, lastSegmentId, 
                               GetBlockSize(blockId), nparity, 
                               numErasures))
        {
            pending_mask.Set(blockId.GetValue());
            return true;
        }
    }
    else if (IsStream())
    {
        static_cast<NormStreamObject*>(this)->UnlockBlock(blockId);
    }
    return false;
}  // end NormObject::TxUpdateBlock()

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
        GetLastRepair(lastId);  // for debug output only
        PLOG(PL_DEBUG, "NormObject::ActivateRepairs() node>%lu obj>%hu activating blk>%lu->%lu block repairs ...\n",
                        (unsigned long)LocalNodeId(), 
                        (UINT16)transport_id, 
                        (unsigned long)nextId.GetValue(), 
                        (unsigned long)lastId.GetValue());
        //repairsActivated = true;
        UINT16 autoParity = session.SenderAutoParity();
        do
        {
            if (pending_mask.CanSet(nextId.GetValue()))
            {
                NormBlock* block = block_buffer.Find(nextId);
                if (NULL != block) block->TxReset(GetBlockSize(nextId), nparity, autoParity, segment_size);
                pending_mask.Set(nextId.GetValue());
                repairsActivated = true;
            }
            else if (IsStream())
            {
                static_cast<NormStreamObject*>(this)->UnlockBlock(nextId);
            }
            repair_mask.Unset(nextId.GetValue());
            Increment(nextId);
        } while (GetNextRepair(nextId));
        ASSERT(!repair_mask.IsSet());
    }
    // Activate partial block (segment) repairs
    // TBD - Can we make this more efficient for larger block_buffer sizes
    //       (e.g., have a separate list (or bitmask) of repair pending blocks?)
    NormBlockBuffer::Iterator iterator(block_buffer);
    NormBlock* block;
    while ((block = iterator.GetNextBlock()))
    {
        if (block->ActivateRepairs(nparity)) 
        {
            PLOG(PL_TRACE, "NormObject::ActivateRepairs() node>%lu obj>%hu activated blk>%lu segment repairs ...\n",
                            (unsigned long)LocalNodeId(), (UINT16)transport_id, (unsigned long)block->GetId().GetValue());
            if (!pending_mask.Set(block->GetId().GetValue()))
            {
                block->ClearPending();
                if (IsStream())
                    static_cast<NormStreamObject*>(this)->UnlockBlock(block->GetId());
            }
            else
            {
                repairsActivated = true;
            }
        }
    }
    return repairsActivated;
}  // end NormObject::ActivateRepairs()

// called by senders only
bool NormObject::AppendRepairAdv(NormCmdRepairAdvMsg& cmd)
{
    // Determine range of blocks possibly pending repair
    NormBlockId nextId = 0;
    GetFirstRepair(nextId);
    NormBlockId endId = 0;
    GetLastRepair(endId);
    if (block_buffer.IsEmpty())
    {
        if (repair_mask.IsSet()) Increment(endId);
    }
    else
    {
        NormBlockId lo = block_buffer.RangeLo();
        NormBlockId hi = block_buffer.RangeHi();
        if (repair_mask.IsSet())
        {
            nextId = (Compare(lo, nextId) < 0) ? lo : nextId;
            endId = (Compare(hi, endId) > 0) ? hi : endId;
        }
        else
        {
            nextId = lo;
            endId = hi;
        }
        Increment(endId);
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
    //while (nextId < endId)
    while (Compare(nextId, endId) < 0)
    {
        NormBlockId currentId = nextId;
        Increment(nextId);
        bool repairEntireBlock = repair_mask.Test(currentId.GetValue());
        if (repairEntireBlock)
        {
            if (!blockCount) firstId = currentId;
            blockCount++;    
        }        
        // Check for break in continuity or end
        //if (blockCount && (!repairEntireBlock || (nextId >= endId)))
        if (blockCount && (!repairEntireBlock || (Compare(nextId, endId) >= 0)))
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
                        PLOG(PL_ERROR, "NormObject::AppendRepairAdv() warning: full msg\n");
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
                    req.AppendRepairItem(fec_id, fec_m, transport_id, firstId, GetBlockSize(firstId), 0);
                    if (2 == blockCount) 
                        req.AppendRepairItem(fec_id, fec_m, transport_id, currentId, GetBlockSize(currentId), 0);
                    break;
                case NormRepairRequest::RANGES:
                    req.AppendRepairRange(fec_id, fec_m, transport_id, firstId, GetBlockSize(firstId), 0,
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
                        PLOG(PL_ERROR, "NormObject::AppendRepairAdv() warning: full msg\n");
                        return requestAppended;
                    }  
                    prevForm = NormRepairRequest::INVALID;
                }
                if (block->AppendRepairAdv(cmd, transport_id, repair_info, fec_id, fec_m, GetBlockSize(currentId), segment_size))      
                    requestAppended = true;
                else
                    return requestAppended;
            }
        }
    }  // end while(nextId < endId)
    if (NormRepairRequest::INVALID != prevForm) 
    {
        if (0 == cmd.PackRepairRequest(req))
        {
            PLOG(PL_ERROR, "NormObject::AppendRepairAdv() warning: full msg\n");
            return requestAppended;
        } 
        requestAppended = true;
    }
    else if (repair_info && !requestAppended)
    {
        // make the "req" an INFO-only request
        req.ClearFlag(NormRepairRequest::BLOCK);   
        req.SetForm(NormRepairRequest::ITEMS);
        req.AppendRepairItem(fec_id, fec_m, transport_id, 0, 0, 0);
        if (0 == cmd.PackRepairRequest(req))
        {
            PLOG(PL_ERROR, "NormObject::AppendRepairAdv() warning: full msg\n");
            return requestAppended;
        }    
    }
    return requestAppended;
}  //  end NormObject::AppendRepairAdv()

// This is used by sender (only) for watermark check
bool NormObject::FindRepairIndex(NormBlockId& blockId, NormSegmentId& segmentId)
{
    //ASSERT(NULL == sender);
    if (repair_info)
    {
        blockId = 0;
        segmentId = 0;
        return true;   
    }
    // TBD - Can we make this more efficient for large block_buffer sizes
    // (e.g., by maintaining a separate bitmask for blocks needing partial repair)
    NormBlockBuffer::Iterator iterator(block_buffer);
    NormBlock* block;
    while ((block = iterator.GetNextBlock()))
        if (block->IsRepairPending()) break;
    if (GetFirstRepair(blockId))
    {
        //if (!block || (blockId <= block->GetId()))
        if ((NULL == block) || (Compare(blockId, block->GetId()) <= 0))
        {
            segmentId = 0;
            return true;      
        }    
    } 
    if (block)
    {   
        block->GetFirstRepair(segmentId);
        // The segmentId must < block length for watermarks
        if (segmentId >= GetBlockSize(block->GetId()))
            segmentId = GetBlockSize(block->GetId()) - 1;
        return true;   
    }   
    return false;
}  // end NormObject::FindRepairIndex()
        

// Called by sender only
bool NormObject::IsRepairPending()
{
    //ASSERT(NULL == sender);
    if (repair_info) return true;
    if (repair_mask.IsSet()) return true;
    // TBD - Can we make this more efficient for large block_buffer sizes
    // (e.g., by maintaining a separate bitmask for blocks needing partial repair)
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
        // This should only be invoked by NORM receiver code
        //ASSERT(NULL != sender);
        NormBlockId firstId;
        if (GetFirstPending(firstId))
        {
            //if (firstId < max_pending_block)
            if (Compare(firstId, max_pending_block) < 0)
            {
                return true;
            }
            //else if (firstId > max_pending_block)
            else if (Compare(firstId, max_pending_block) > 0)
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
// return "true" if repair is pending up through blockId::segmentId
bool NormObject::PassiveRepairCheck(NormBlockId   blockId,
                                    NormSegmentId segmentId)
{
    if (pending_info) return true;
    NormBlockId firstPendingBlock;
    if (GetFirstPending(firstPendingBlock))
    {
        //if (firstPendingBlock < blockId)
        if (Compare(firstPendingBlock, blockId) < 0)
        {
            return true;
        }
        else if (firstPendingBlock == blockId)
        {
            NormBlock* block = block_buffer.Find(firstPendingBlock);
            if (NULL != block)
            {
                NormSegmentId firstPendingSegment;
                if (block->GetFirstPending(firstPendingSegment))
                {
                    if (firstPendingSegment <= segmentId)
                        return true;
                }
                else
                {
                    ASSERT(0);  // this will never happen  
                }            
            }
            else
            {
                return true;  // entire block was pending
            }    
        }    
    }
    // If it's a stream, make sure the app has read the data
    if (IsStream()) 
        return static_cast<NormStreamObject*>(this)->PassiveReadCheck(blockId, segmentId);
    return false;
}  // end NormObject::PassiveRepairCheck()

bool NormObject::ReceiverRepairCheck(CheckLevel    level,
                                     NormBlockId   blockId,
                                     NormSegmentId segmentId,
                                     bool          timerActive,
                                     bool          holdoffPhase)
{
    // (TBD) make sure it's OK for "max_pending_segment"
    //       and "next_segment_id" to be > blockSize
    // These block ids are the range (depending on CheckLevel)
    // for which repair_masks need to reset for NACK suppression use
    NormBlockId nextBlockId = 0;
    NormBlockId endBlockId = 0;
    bool thruObject = false;
    bool startRepairTimer = false;
    switch (level)
    {
        case TO_OBJECT:
            return false;
        case THRU_INFO:
            if (timerActive)
            {
                if (!holdoffPhase)
                {
                    current_block_id = 0;
                    next_segment_id = 0;
                }
            }
            else
            {
                if (pending_info)
                    startRepairTimer = true;
                current_block_id = 0;
                next_segment_id = 0;
            }
            // (TBD) set "thruObject" if info-only obj
            break;
        case TO_BLOCK:
            //if (blockId >= max_pending_block)
            if (Compare(blockId, max_pending_block) >= 0)
            {
                max_pending_block = blockId;
                max_pending_segment = 0;
            }    
            if (timerActive)
            {
                if (!holdoffPhase)
                {
                    //if (blockId < current_block_id)
                    if (Compare(blockId, current_block_id) < 0)
                    {
                        current_block_id = blockId;
                        next_segment_id = 0;   
                    }
                }
            }
            else
            {
                if (pending_info)
                {
                    startRepairTimer = true;
                    // Cache pending blockId range for TO_BLOCK
                    GetFirstPending(nextBlockId);
                    endBlockId = blockId;
                }
                else
                {
                    NormBlockId firstPending;
                    if (GetFirstPending(firstPending))
                    {
                        //if (firstPending < blockId)
                        if (Compare(firstPending, blockId) < 0)
                        {
                            startRepairTimer = true;
                            // Cache pending blockId range for TO_BLOCK
                            nextBlockId = firstPending;
                            endBlockId = blockId;
                        }
                    }
                }
                current_block_id = blockId;
                next_segment_id = 0;
            }    
            // (TBD) set "thruObject" if info-only obj                
            break;
        case THRU_SEGMENT:
            //if (blockId > max_pending_block)
            if (Compare(blockId, max_pending_block) > 0)
            {
                max_pending_block = blockId;
                max_pending_segment = segmentId + 1;
            }
            else if (blockId == max_pending_block)
            {
                if (segmentId >= max_pending_segment) 
                    max_pending_segment = segmentId + 1;  
            }
            if (!IsStream() && (blockId == final_block_id))
            {
                unsigned int finalSegment = GetBlockSize(blockId) - 1;
                if (finalSegment <= segmentId)
                    thruObject = true;
            }
            if (timerActive)
            {
                if (!holdoffPhase)
                {
                    //if (blockId < current_block_id)
                    if (Compare(blockId, current_block_id) < 0)
                    {
                        current_block_id = blockId;
                        next_segment_id = segmentId + 1;   
                    }
                    else if (blockId == current_block_id)
                    {
                        if (segmentId < next_segment_id)
                            next_segment_id = segmentId + 1;    
                    }
                }
            }
            else
            {
                if (pending_info)
                {
                    startRepairTimer = true;
                    // Cache pending blockId range for THRU_SEGMENT
                    GetFirstPending(nextBlockId);
                    endBlockId = blockId;
                    Increment(endBlockId);
                }
                else
                {
                    NormBlockId firstPending;
                    if (GetFirstPending(firstPending))
                    {
                        //if (firstPending < blockId)
                        if (Compare(firstPending, blockId) < 0)
                        {
                            startRepairTimer = true;
                        }   
                        else if (firstPending == blockId)
                        {
                            NormBlock* block = block_buffer.Find(blockId);
                            if (NULL != block)
                            {
                                ASSERT(block->IsPending());
                                NormSymbolId firstPendingSegment;
                                block->GetFirstPending(firstPendingSegment);
                                if (firstPendingSegment <= segmentId)
                                    startRepairTimer = true;
                            }
                            else
                            {
                                startRepairTimer = true;
                            }
                        }
                        if (startRepairTimer)
                        {
                            // Cache pending blockId range for THRU_SEGMENT
                            nextBlockId = firstPending;
                            endBlockId = blockId;
                            Increment(endBlockId);
                        }
                    }
                }
                current_block_id = blockId;
                next_segment_id = segmentId + 1;
            }
            break;
        case THRU_BLOCK:
            //if (blockId > max_pending_block)
            if (Compare(blockId, max_pending_block) > 0)
            {
                max_pending_block = blockId;
                max_pending_segment = GetBlockSize(blockId);
            }
            if (!IsStream() && (blockId == final_block_id))
                thruObject = true;
            if (timerActive)
            {
                if (!holdoffPhase)
                {
                    //if (blockId < current_block_id)
                    if (Compare(blockId, current_block_id) < 0)
                    {
                        current_block_id = blockId;
                        next_segment_id = GetBlockSize(blockId);
                    }  
                }
            }
            else
            {   
                if (pending_info)
                {
                    startRepairTimer = true;
                    // Cache pending blockId range for THRU_BLOCK
                    GetFirstPending(nextBlockId);
                    endBlockId = blockId;
                    Increment(endBlockId);
                }
                else
                {
                    NormBlockId firstPending;
                    if (GetFirstPending(firstPending))
                    {
                        //if (firstPending <= blockId)
                        if (Compare(firstPending, blockId) <= 0)
                        {
                            startRepairTimer = true;
                            // Cache pending blockId range for THRU_BLOCK
                            nextBlockId = firstPending;
                            endBlockId = blockId;
                            Increment(endBlockId);
                        }
                    }
                }
                current_block_id = blockId;  
                next_segment_id = GetBlockSize(blockId);
            }
            break;
        case THRU_OBJECT:
            if (!IsStream()) 
                max_pending_block = final_block_id;
            //else if (blockId > max_pending_block)
            else if (Compare(blockId, max_pending_block) > 0)
                max_pending_block = blockId;
            max_pending_segment = GetBlockSize(max_pending_block);
            thruObject = true;
            if (!timerActive)
            {   
                if (pending_info || pending_mask.IsSet())
                {
                    startRepairTimer = true;
                    // Cache pending blockId range for THRU_OBJECT
                    GetFirstPending(nextBlockId);
                    GetLastPending(endBlockId);
                    Increment(endBlockId);
                }
                if (IsStream())
                    current_block_id = max_pending_block;
                else
                    current_block_id = final_block_id;
                next_segment_id = GetBlockSize(current_block_id);
            }
            break;
    }  // end switch (level)
    
    // This special object abort is for silent receivers configured as "low delay"
    // so they immediately deliver even partially completed objects (aborted) to
    // the app as soon as a new object is begun or old object transmission completed
    if (thruObject && session.RcvrIsLowDelay() && !IsStream())
    {
        sender->AbortObject(this);
        return false;
    }
    if (startRepairTimer)
    {
        // Reset repair_info, NormObject::repair_mask and NormBlock::repair_masks
        // (These are used for NACK suppression state accumulation and are reset 
        //  here at the start of a fresh NACK cycle)
        repair_info = false;  // info repair mask
        if (repair_mask.IsSet()) repair_mask.Clear();
        //while (nextBlockId < endBlockId)
        while (Compare(nextBlockId, endBlockId) < 0)
        {
            NormBlock* block = block_buffer.Find(nextBlockId);
            if (NULL != block) block->ClearRepairs();
            Increment(nextBlockId);
            if (!GetNextPending(nextBlockId)) break;
        }
        return true;
    }
    else
    {
        return false;
    }
}  // end NormObject::ReceiverRepairCheck()

bool NormObject::ReceiverRewindCheck(NormBlockId    blockId,
                                     NormSegmentId  segmentId)
{
    //if (blockId > current_block_id)
    if (Compare(blockId, current_block_id) > 0)
        return false;
    else if (blockId == current_block_id)
        return ((segmentId+1) < next_segment_id);
    else // if (blockId < current_block_id)
        return true;
}  // end NormObject::ReceiverRewindCheck()

// Note this partially clears "repair_mask" state 
// Thus should only be called once at end of repair cycle
// (called on receiver repair_timer timeout)
// This uses the repair_mask that has accumulated state from
// oveheard NACKs or NORM_CMD(REPAIR_ADV) to possibly suppress NACKing
// up through the "current_block_id"
bool NormObject::IsRepairPending(bool flush)
{
    // Only NORM receiver code should call this
    if (pending_info && !repair_info) return true;
    // Calculate repair_mask = pending_mask - repair_mask 
    repair_mask.XCopy(pending_mask);
    NormBlockId nextId;
    if (GetFirstRepair(nextId))
    {
        do
        {
            //if (!flush && (nextId > current_block_id)) break;
            if (!flush && (Compare(nextId, current_block_id) > 0)) break;
            NormBlock* block = block_buffer.Find(nextId);
            if (block)
            {
                bool isPending;
                UINT16 numData = GetBlockSize(nextId);
                //if (flush || (nextId < current_block_id))
                if (flush || (Compare(nextId, current_block_id) < 0))
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
            Increment(nextId);
        } while (GetNextRepair(nextId));
    }
    return false;
}  // end NormObject::IsRepairPending()

bool NormObject::AppendRepairRequest(NormNackMsg&   nack, 
                                     bool           flush,
                                     UINT16         payloadMax)
{ 
    // If !flush, we request only up _to_ max_pending_block::max_pending_segment.
    NormRepairRequest req;
    bool requestAppended = false;  // is set to true when content added to "nack"
    NormRepairRequest::Form prevForm = NormRepairRequest::INVALID;
    // First iterate over any pending blocks, appending any requests
    NormBlockId nextId;
    bool iterating = GetFirstPending(nextId);
    NormBlockId prevId = nextId;
    //iterating = iterating && (flush || (nextId <= max_pending_block)); 
    iterating = iterating && (flush || (Compare(nextId, max_pending_block) <= 0)); 
    UINT32 consecutiveCount = 0;
    while (iterating || (0 != consecutiveCount))
    {
        NormBlockId lastId;
        GetLastPending(lastId);  // for debug output only
        // Two PLOG() statements because one doesn't work right for some reason?!
        if (PL_TRACE <= GetDebugLevel())
        {
            PLOG(PL_TRACE, "NormObject::AppendRepairRequest() node>%lu obj>%hu, blk>%lu->%lu ",
                            (unsigned long)LocalNodeId(), (UINT16)GetId(), 
                            (unsigned long)nextId.GetValue(), (unsigned long)lastId.GetValue());
            PLOG(PL_ALWAYS, "(maxPending = %lu)\n", (unsigned long)max_pending_block.GetValue());
        }
        bool appendRequest = false;
        NormBlock* block = iterating ? block_buffer.Find(nextId) : NULL;
        if (NULL != block)
            appendRequest = true;
        else if (iterating && ((UINT32)Difference(nextId, prevId) == consecutiveCount))
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
                        PLOG(PL_WARN, "NormObject::AppendRepairRequest() warning: full NACK msg\n");
                        return requestAppended;
                    }
                    requestAppended = true;
                }
                if (NormRepairRequest::INVALID != nextForm)
                {
                    nack.AttachRepairRequest(req, payloadMax);
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
                PLOG(PL_TRACE, "NormObject::AppendRepairRequest() BLOCK request\n");
            switch (nextForm)
            {
                case NormRepairRequest::ITEMS:
                    req.AppendRepairItem(fec_id, fec_m, transport_id, prevId, GetBlockSize(prevId), 0);  // (TBD) error check
                    if (2 == consecutiveCount)
                    {
                        Increment(prevId);
                        req.AppendRepairItem(fec_id, fec_m, transport_id, prevId, GetBlockSize(prevId), 0); // (TBD) error check
                    }
                    break;
                case NormRepairRequest::RANGES:
                {
                    // lastId = prevId + consecutiveCount - 1
                    NormBlockId endId = prevId;
                    Increment(endId, consecutiveCount - 1);
                    req.AppendRepairRange(fec_id, fec_m, 
                                          transport_id, prevId, GetBlockSize(prevId), 0, 
                                          transport_id, endId, GetBlockSize(endId), 0); // (TBD) error check
                    break;
                }
                default:
                    break;
            }  // end switch(nextForm)
            if (NULL != block)
            {
                // Note our NACK construction is limited by "max_pending_block:max_pending_segment"
                // based on most recent transmissions from sender
                bool blockIsPending = false;
                if (nextId == max_pending_block)
                {
                    ASSERT(block->IsPending());
                    NormSymbolId firstPending;
                    block->GetFirstPending(firstPending);
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
                            PLOG(PL_WARN, "NormObject::AppendRepairRequest() warning: full NACK msg\n");
                            return requestAppended;   
                        }
                        requestAppended = true;
                    }
                    bool blockRequestAppended;
		            if (flush || (nextId != max_pending_block))
                    {
                        blockRequestAppended = 
                            block->AppendRepairRequest(nack, fec_id, fec_m, numData, nparity, 
                                                       transport_id, pending_info, payloadMax);
                    }
                    else
                    {
                        if (max_pending_segment < numData)
                        {
                            blockRequestAppended = 
                                block->AppendRepairRequest(nack, fec_id, fec_m, max_pending_segment, 0,
                                                           transport_id, pending_info, payloadMax); 
                        }
                        else
                        {
                            blockRequestAppended = 
                                block->AppendRepairRequest(nack, fec_id, fec_m, numData, nparity, 
                                                           transport_id, pending_info, payloadMax); 
                        }
                    }
                    if (blockRequestAppended)
		                requestAppended = true;
                    else  // Must have filled NACK message
                        return requestAppended;
		            prevForm = NormRepairRequest::INVALID;
                }
                consecutiveCount = 0;
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
        Increment(nextId);
        iterating = GetNextPending(nextId);
        //iterating = iterating && (flush || (nextId <= max_pending_block));
        iterating = iterating && (flush || (Compare(nextId, max_pending_block) <= 0));
    }  // end while (iterating || (0 != consecutiveCount))
    
    // This conditional makes sure any outstanding requests constructed
    // are packed into the nack message.
    if ((NormRepairRequest::INVALID != prevForm) && (NACK_NONE != nacking_mode))
    {
        if (0 == nack.PackRepairRequest(req))
        {
            PLOG(PL_WARN, "NormObject::AppendRepairRequest() warning: full NACK msg\n");
            return requestAppended;
        } 
        requestAppended = true;
        prevForm = NormRepairRequest::INVALID;
    }  
    if (!requestAppended && pending_info && (NACK_NONE != nacking_mode))
    {
        // INFO_ONLY repair request
        nack.AttachRepairRequest(req, payloadMax);
        req.SetForm(NormRepairRequest::ITEMS);
        req.ResetFlags();
        req.SetFlag(NormRepairRequest::INFO); 
        req.AppendRepairItem(fec_id, fec_m, transport_id, 0, 0, 0);  // (TBD) error check
        if (0 == nack.PackRepairRequest(req))
        {
            PLOG(PL_WARN, "NormObject::AppendRepairRequest() warning: full NACK msg\n");
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
                PLOG(PL_WARN, "NormObject::HandleObjectMessage() node>%lu sender>%lu obj>%hu "
                              "Warning! info too long.\n", (unsigned long)LocalNodeId(), 
                                (unsigned long)sender->GetId(), (UINT16)transport_id);   
            }
            else if (0 == info_len)
            {
                // NORM_INFO used to convey FTI only
                if (NULL != info_ptr)
                {
                    delete[] info_ptr;
                    info_ptr = NULL;
                }
                pending_info = false;
            }
            else
            {
                memcpy(info_ptr, infoMsg.GetInfo(), info_len);
                pending_info = false;
                session.Notify(NormController::RX_OBJECT_INFO, sender, this);
            }
        }
        else
        {
            // (TBD) Verify info hasn't changed?   
            PLOG(PL_DEBUG, "NormObject::HandleObjectMessage() node>%lu sender>%lu obj>%hu "
                           "received duplicate info ...\n", 
                            (unsigned long)LocalNodeId(), (unsigned long)sender->GetId(), 
                            (UINT16)transport_id);
        }
    }
    else  // NORM_MSG_DATA
    {
        const NormDataMsg& data = (const NormDataMsg&)msg;
        UINT16 numData = GetBlockSize(blockId);
        
        // For stream objects, a little extra mgmt is required
        NormStreamObject* stream = NULL;
        if (STREAM == type)
        {
            stream = static_cast<NormStreamObject*>(this);            
            if (!stream->StreamUpdateStatus(blockId))
            {
                PLOG(PL_WARN, "NormObject::HandleObjectMessage() node:%lu sender:%lu obj>%hu blk>%lu "
                              "broken stream ...\n", (unsigned long)LocalNodeId(), (unsigned long)sender->GetId(), 
                              (UINT16)transport_id, (unsigned long)blockId.GetValue());
                sender->IncrementResyncCount();
                while (!stream->StreamUpdateStatus(blockId))
                {
                    // Sender is too far ahead of me ...
                    NormBlockId firstId;
                    if (GetFirstPending(firstId))
                    {
                        NormBlock* block = block_buffer.Find(firstId);
                        if (block)
                        {
                            block_buffer.Remove(block);
                            sender->PutFreeBlock(block);
                        }
                        pending_mask.Unset(firstId.GetValue());
                    }
                    else
                    {
                        // If we try to resync to too far back, we end up chasing our tail here
                        // so we just sync to the current block under these circumstances
                        stream->StreamResync(blockId); // - pending_mask.GetSize()/2);
                        break;
                    }      
                }
            }
        }
        if (pending_mask.Test(blockId.GetValue()))
        {
            NormBlock* block = block_buffer.Find(blockId);
            if (!block)
            {
                if (!(block = sender->GetFreeBlock(transport_id, blockId)))
                {
                    //DMSG(2, "NormObject::HandleObjectMessage() node>%lu sender>%lu obj>%hu "
                    //        "Warning! no free blocks ...\n", (unsigned long)LocalNodeId(), (unsigned long)
                    //        sender->GetId(), (UINT16)transport_id);  
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
                    PLOG(PL_ERROR, "NormObject::HandleObjectMessage() node>%lu sender>%lu obj>%hu "
                            "Error! segment too large ...\n", (unsigned long)LocalNodeId(), 
                            (unsigned long)sender->GetId(),(UINT16)transport_id);  
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
                char* segment = (!isSourceSymbol || !sender->SegmentPoolIsEmpty()) ?
                                    sender->GetFreeSegment(transport_id, blockId) : NULL;
                
                if (segment)
                {
                    memcpy(segment, data.GetPayload(), payloadLength);
                    if (payloadLength < payloadMax)
                        memset(segment+payloadLength, 0, payloadMax-payloadLength);
                    block->AttachSegment(segmentId, segment);
                }
                else
                {
                    //DMSG(2, "NormObject::HandleObjectMessage() node>%lu sender>%lu obj>%hu "
                    //        "Warning! no free segments ...\n", (unsigned long)LocalNodeId(), 
                    //          (unsigned long)sender->GetId(), (UINT16)transport_id);  
                    if (!isSourceSymbol) return;
                }
                block->UnsetPending(segmentId);
                
                bool objectUpdated = false;
                // 2) Write segment to object (if it's source symbol (data))
                if (isSourceSymbol) 
                {
                    block->DecrementErasureCount();
                    if (WriteSegment(blockId, segmentId, data.GetPayload()))
                    {
                        objectUpdated = true;
                        // For statistics only (TBD) #ifdef NORM_DEBUG
                        sender->IncrementRecvGoodput(segmentLength);
                    }
                    else
                    {
                        if (IsStream())
                            PLOG(PL_DEBUG, "NormObject::HandleObjectMessage() WriteSegment() error\n");
                        else
                            PLOG(PL_ERROR, "NormObject::HandleObjectMessage() WriteSegment() error\n");
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
                    PLOG(PL_DETAIL, "NormObject::HandleObjectMessage() node>%lu sender>%lu obj>%hu blk>%lu "
                                    "completed block ...\n", (unsigned long)LocalNodeId(), 
                                    (unsigned long)sender->GetId(), (UINT16)transport_id, 
                                    (unsigned long)block->GetId().GetValue());
                    UINT16 erasureCount = 0;
                    UINT16 nextErasure = 0;
                    UINT16 retrievalCount = 0;
                    if (block->GetFirstPending(nextErasure))
                    {
                        // Is the block missing _any_ source symbols?
                        if (nextErasure < numData)
                        {
                            // Use "NormObject::RetrieveSegment() method to "retrieve" 
                            // source symbol segments already received which aren't still cached.
                            for (UINT16 nextSegment = 0; nextSegment < numData; nextSegment++)
                            {
                                if (block->IsPending(nextSegment))
                                {
                                    sender->SetErasureLoc(erasureCount++, nextSegment);
                                    segment = sender->GetRetrievalSegment();
                                    ASSERT(NULL != segment);
                                    UINT16 payloadMax = segment_size + NormDataMsg::GetStreamPayloadHeaderLength();
#ifdef SIMULATE                               
                                    payloadMax = MIN(payloadMax, SIM_PAYLOAD_MAX);
#endif // SIMULATE
                                    // Zeroize the missing segment payload in prep for decoding
                                    memset(segment, 0, payloadMax);
                                    sender->SetRetrievalLoc(retrievalCount++, nextSegment);
                                    block->SetSegment(nextSegment, segment);
                                }
                                else if (!block->GetSegment(nextSegment))
                                {
                                    if (!(segment = RetrieveSegment(blockId, nextSegment)))
                                    {
                                        // Stream objects should be the only ones that fail
                                        // to retrieve segments (due to stream buffer size limit)
                                        ASSERT(IsStream());
                                        block->SetPending(nextSegment);
                                        block->IncrementErasureCount();
                                        // Clear block of any retrieval (temp) segments
                                        for (UINT16 i = 0; i < retrievalCount; i++) 
                                            block->DetachSegment(sender->GetRetrievalLoc(i));
                                        return;   
                                    } 
                                    sender->SetRetrievalLoc(retrievalCount++, nextSegment);
                                    block->SetSegment(nextSegment, segment); 
                                }  
                            }
                            nextErasure = numData;
                            // Set erasure locs for any missing parity symbol segments, too!
                            while (block->GetNextPending(nextErasure))
                                sender->SetErasureLoc(erasureCount++, nextErasure++); 
                        }  // end if (nextErasure < numData)
                    }  // end if (block->GetFirstPending(nextErasure))                 
                    
                    if (erasureCount)
                    {
                        sender->Decode(block->SegmentList(), numData, erasureCount); 
                        for (UINT16 i = 0; i < erasureCount; i++) 
                        {
                            NormSegmentId sid = sender->GetErasureLoc(i);
                            if (sid < numData)
                            {
                                if (WriteSegment(blockId, sid, block->GetSegment(sid)))
                                {
                                    objectUpdated = true;
                                    // For statistics only (TBD) #ifdef NORM_DEBUG
                                    // "segmentLength" is not necessarily correct here (TBD - fix this)
                                    sender->IncrementRecvGoodput(segmentLength);
                                }  
                                else
                                {
                                    if (IsStream())
                                        PLOG(PL_DEBUG, "NormObject::HandleObjectMessage() WriteSegment() error\n");
                                    else
                                        PLOG(PL_ERROR, "NormObject::HandleObjectMessage() WriteSegment() error\n");
                                } 
                            }
                            else
                            {
                                break;
                            }
                        }
                    }
                    // Clear any temporarily retrieved segments for the block
                    for (UINT16 i = 0; i < retrievalCount; i++) 
                        block->DetachSegment(sender->GetRetrievalLoc(i));
                    // OK, we're done with this block
                    pending_mask.Unset(blockId.GetValue());
                    block_buffer.Remove(block);
                    sender->PutFreeBlock(block); 
                }  // if erasureCount <= parityCount (i.e., block complete)
                // Notify application of new data available
                // (TBD) this could be improved for stream objects
                //        so it's not called unnecessarily
                if (objectUpdated && notify_on_update)
                {
                    if ((NULL == stream) || stream->DetermineReadReadiness() || session.RcvrIsLowDelay())
                    {
                        notify_on_update = false;
                        session.Notify(NormController::RX_OBJECT_UPDATED, sender, this);
                    }
                }   
            }
            else
            {
                PLOG(PL_DEBUG, "NormObject::HandleObjectMessage() node>%lu sender>%lu obj>%hu "
                                "received duplicate segment blk>%lu segment>%hu...\n", (unsigned long)LocalNodeId(),
                                (unsigned long)sender->GetId(), (UINT16)transport_id,(unsigned long)blockId.GetValue(),
                                 segmentId);
            }
        }
        else
        {
            PLOG(PL_DEBUG, "NormObject::HandleObjectMessage() node>%lu sender>%lu obj>%hu "
                            "received duplicate block message blk>%lu ...\n", (unsigned long)LocalNodeId(),
                            (unsigned long)sender->GetId(), (UINT16)transport_id, (unsigned long)blockId.GetValue());
        }  // end if/else pending_mask.Test(blockId)
    }  // end if/else (NORM_MSG_INFO)
                    
}  // end NormObject::HandleObjectMessage()

// Returns source symbol segments to pool for ordinally _first_ block with such resources
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


// Steals non-pending block (ordinally _first_) for _sender_ resource management
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
                pending_mask.Test(bid.GetValue()) ||
                repair_mask.Test(bid.GetValue()) ||
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


// For receiver & sender resource management, steals newer block resources when
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


// For silent receiver resource management, steals older block resources when
// needing resources for ordinally newer blocks.
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

bool NormObject::NextSenderMsg(NormObjectMsg* msg)
{             
    // Init() the message
    if (pending_info)
    {
        NormInfoMsg* infoMsg = static_cast<NormInfoMsg*>(msg);
        infoMsg->Init();
        infoMsg->SetFecId(fec_id);
    }
    else
    {
        NormDataMsg* dataMsg = static_cast<NormDataMsg*>(msg);
        dataMsg->Init();
        dataMsg->SetFecId(fec_id);  // we do this here so the base header length is properly set
    }
    
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
    NormSession::FtiMode ftiMode = session.SenderFtiMode();
    if ((NULL != info_ptr) || (NormSession::FTI_INFO == ftiMode))
        msg->SetFlag(NormObjectMsg::FLAG_INFO);
    
    msg->SetObjectId(transport_id);
    
    // Apply FTI extension as needed
    if ((NormSession::FTI_ALWAYS == ftiMode) || 
        (pending_info && (NormSession::FTI_INFO == ftiMode)))
    {
        switch (fec_id)
        {
            case 2:
            {
                NormFtiExtension2 fti;
                msg->AttachExtension(fti);
                fti.SetObjectSize(object_size);
                fti.SetFecFieldSize(fec_m);
                fti.SetFecGroupSize(1);
                fti.SetSegmentSize(segment_size);
                fti.SetFecMaxBlockLen(ndata);
                fti.SetFecNumParity(nparity);
                break;
            }
            case 5:
            {
                NormFtiExtension5 fti;
                msg->AttachExtension(fti);
                fti.SetObjectSize(object_size);
                fti.SetSegmentSize(segment_size);
                fti.SetFecMaxBlockLen((UINT8)ndata);
                fti.SetFecNumParity((UINT8)nparity);
                break;
            }
            case 129:
            {
                NormFtiExtension129 fti;
                msg->AttachExtension(fti);
                fti.SetObjectSize(object_size);
                fti.SetFecInstanceId(0);   // ZERO is for legacy MDP/NORM FEC encoder (TBD - use appropriate instanceId)
                fti.SetSegmentSize(segment_size);
                fti.SetFecMaxBlockLen(ndata);
                fti.SetFecNumParity(nparity);
                break;
            }
            default:
                ASSERT(0);
                return false;
        }
    }
    if (pending_info)
    {
        // (TBD) set REPAIR_FLAG for retransmitted info
        NormInfoMsg* infoMsg = static_cast<NormInfoMsg*>(msg);
        infoMsg->SetInfo(info_ptr, info_len);
        pending_info = false;
        return true;
    }
    // This block gets the next pending block/segment
    // (The loop handles NORM_OBJECT_STREAM advancement 
    //  without the prior approach that used recursion)
    NormDataMsg* data = static_cast<NormDataMsg*>(msg);
    NormBlock* block = NULL;
    NormBlockId blockId;
    UINT16 numData = 0;
    NormSegmentId segmentId;
    bool squelchQueued = false;
    while (NULL == block)
    {
        if (!GetFirstPending(blockId)) 
        {
            // Attempt to advance stream (probably had been repair-delayed)
            if (IsStream())
            {
                if (static_cast<NormStreamObject*>(this)->StreamAdvance())
                {   
                    continue;  // return NextSenderMsg(msg);
                }
                else
                {
                    //ASSERT(IsRepairPending()); 
                    return false;
                }
            }
            else
            {
                PLOG(PL_FATAL, "NormObject::NextSenderMsg() pending object w/ no pending blocks?!\n");
                return false;
            }
        }
        numData = GetBlockSize(blockId);
        block = block_buffer.Find(blockId);
        if (!block)
        {
           if (NULL == (block = session.SenderGetFreeBlock(transport_id, blockId)))
           {
                PLOG(PL_INFO, "NormObject::NextSenderMsg() node>%lu warning: sender resource " 
                              "constrained (no free blocks).\n", (unsigned long)LocalNodeId());
                return false; 
           }
           // Load block with zero initialized parity segments
           UINT16 totalBlockLen = numData + nparity;
           for (UINT16 i = numData; i < totalBlockLen; i++)
           {
                char* s = session.SenderGetFreeSegment(transport_id, blockId);
                if (s)
                {
                    UINT16 payloadMax = segment_size + NormDataMsg::GetStreamPayloadHeaderLength();
#ifdef SIMULATE
                    payloadMax = MIN(payloadMax, SIM_PAYLOAD_MAX);
#endif // SIMULATE
                    memset(s, 0, payloadMax);  // extra byte for msg flags
                    block->AttachSegment(i, s); 
                }
                else
                {
                    PLOG(PL_INFO, "NormObject::NextSenderMsg() node>%lu warning: sender resource " 
                                  "constrained (no free segments).\n", (unsigned long)LocalNodeId());
                    session.SenderPutFreeBlock(block);
                    return false;
                }
           }    
           block->TxInit(blockId, numData, session.SenderAutoParity());  
           //if (blockId < max_pending_block) 
           if (Compare(blockId, max_pending_block) < 0)
               block->SetFlag(NormBlock::IN_REPAIR);
           while (!block_buffer.Insert(block))
           {
               //ASSERT(STREAM == type);
               //if (blockId > block_buffer.RangeLo())
               if (Compare(blockId, block_buffer.RangeLo()) > 0)
               {
                   NormBlock* lowBlock = block_buffer.Find(block_buffer.RangeLo());
                   NormBlockId lowBlockId = lowBlock->GetId();
                   bool push = static_cast<NormStreamObject*>(this)->GetPushMode();
                   if (!push && (lowBlock->IsRepairPending() || IsRepairSet(lowBlockId)))
                   {
                       // Pending repairs delaying stream advance
                       PLOG(PL_DEBUG, "NormObject::NextSenderMsg() node>%lu pending repairs delaying stream progress\n", 
                                        (unsigned long)LocalNodeId());
                       session.SenderPutFreeBlock(block);
                       return false; 
                   }
                   else
                   {
                        // Prune old non-pending block (or even pending if "push" enabled stream)
                        block_buffer.Remove(lowBlock);
                        repair_mask.Unset(lowBlockId.GetValue());  // just in case
                        pending_mask.Unset(lowBlockId.GetValue());
                        if (IsStream())  // always true
                            static_cast<NormStreamObject*>(this)->UnlockBlock(lowBlockId);
                        session.SenderPutFreeBlock(lowBlock);
                        continue;
                   }
               }
               else if (IsStream())
               {
                    PLOG(PL_WARN, "NormObject::NextSenderMsg() node>%lu Warning! can't repair old stream block\n", 
                                    (unsigned long)LocalNodeId());
                    if (!squelchQueued) 
                    {
                        session.SenderQueueSquelch(transport_id);
                        squelchQueued = true;
                    }
                    session.SenderPutFreeBlock(block);
                    repair_mask.Unset(blockId.GetValue());  // just in case
                    pending_mask.Unset(blockId.GetValue());
                    // Unlock (set to non-pending status) the corresponding stream_buffer block
                    static_cast<NormStreamObject*>(this)->UnlockBlock(blockId); 
                    block = NULL;
                    break; //return NextSenderMsg(msg);
               }
               else
               {
                    PLOG(PL_FATAL, "NormObject::NextSenderMsg() invalid non-stream state!\n");
                    ASSERT(0);
                    return false;
               }
           }  // end while (!block_buffer.Insert())
           if (NULL == block) continue;
        }  // end if (!block)
        if (!block->GetFirstPending(segmentId)) 
        {
            PLOG(PL_ERROR, "NormObject::NextSenderMsg() warning: found pending block %lu with nothing pending!?\n", 
                            (unsigned long)blockId.GetValue());
            pending_mask.Unset(blockId.GetValue());
            block = NULL;
            continue; //return NextSenderMsg(msg);
        }
        // Try to read segment 
        if (segmentId < numData)
        {
            // Try to read data segment (Note "ReadSegment" copies in offset/length info also)
            char* buffer = data->AccessPayload(); 
            UINT16 payloadLength = ReadSegment(blockId, segmentId, buffer);
            if (0 == payloadLength)
            {
                // (TBD) deal with read error 
                //(for streams, it currently means the stream is pending, but app hasn't yet written data)
                if (!IsStream())
                { 
                    PLOG(PL_FATAL, "NormObject::NextSenderMsg() ReadSegment() error\n"); 
                    return false;
                }
                else if (static_cast<NormStreamObject*>(this)->IsOldBlock(blockId))
                {
                    PLOG(PL_ERROR, "NormObject::NextSenderMsg() node>%lu Warning! can't repair old stream segment\n", 
                                    (unsigned long)LocalNodeId());
                    block->UnsetPending(segmentId); 
                    if (!block->IsPending())
                    {
                        // End of old block reached
                        block->ResetParityCount(nparity);
                        pending_mask.Unset(blockId.GetValue()); 
                        // for EMCON sending, mark NORM_INFO for re-transmission, if applicable
                        if (session.SndrEmcon() && (HaveInfo() || (NormSession::FTI_INFO == session.SenderFtiMode())))
                            pending_info = true;
                    }
                    block = NULL;
                    continue;  //return NextSenderMsg(msg);
                }
                else
                {
                    // App hasn't written data for this block yet
                    return false;
                }
            }
            data->SetPayloadLength(payloadLength);

            // Perform incremental FEC encoding as needed
            if ((block->ParityReadiness() == segmentId) && (0 != nparity)) 
               // (TBD) && ((incrementalParity == true) || (auto_parity != 0))
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
                block->UpdateSegSizeMax(payloadLength);
                session.SenderEncode(segmentId, data->AccessPayload(), block->SegmentList(numData)); 
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
            char* segment = block->GetSegment(segmentId);
            ASSERT(NULL != segment);
            // We only need to send FEC content to cover the biggest segment
            // sent for the block.
#ifdef SIMULATE
            UINT16 payloadMax = MIN(block->GetSegSizeMax(), SIM_PAYLOAD_MAX);
            data->SetPayload(segment, payloadMax);
            data->SetPayloadLength(block->GetSegSizeMax());  // correct the msg length
#else
            data->SetPayload(segment, block->GetSegSizeMax());
#endif // if/else SIMULATE
        }
    }  // end while (NULL == block)
    block->UnsetPending(segmentId); 
    //if (block->InRepair()) 
    //    data->SetFlag(NormObjectMsg::FLAG_REPAIR);
    data->SetFecPayloadId(fec_id, blockId.GetValue(), segmentId, numData, fec_m);
    if (!block->IsPending()) 
    {
        // End of block reached
        block->ResetParityCount(nparity);       
        pending_mask.Unset(blockId.GetValue()); 
        // for EMCON sending, mark NORM_INFO for re-transmission, if applicable
        if (session.SndrEmcon() && (HaveInfo() || (NormSession::FTI_INFO == session.SenderFtiMode())))
            pending_info = true;
        // Advance sender use of "max_pending_block" so we always
        // know when a block should be flagged as IN_REPAIR
        if (blockId == max_pending_block)
            Increment(max_pending_block);
    }
    
    // We update the object/block flow control timestamp to
    // mark the "recent" activity for this object or block
    if (session.GetFlowControl() > 0.0)
    {
        ProtoTime currentTime;
        currentTime.GetCurrentTime();
        SetLastNackTime(currentTime);
        if (IsStream()) 
            static_cast<NormStreamObject*>(this)->SetLastNackTime(blockId, currentTime);
    }
 
    if (!pending_mask.IsSet())
    {
        if (IsStream())
        {
            // This lets NORM_STREAM objects continue indefinitely
            static_cast<NormStreamObject*>(this)->StreamAdvance();
            // (TBD) Is there a case where posting TX_OBJECT_SENT for a stream
            //       makes sense?  E.g., so we could use NormRequeueObject() for streams?
        }
        else if (first_pass)
        {
            // "First pass" transmission of object (and any auto parity) has completed
            first_pass = false;
            session.Notify(NormController::TX_OBJECT_SENT, NULL, this);
        }
    }   
    return true;
}  // end NormObject::NextSenderMsg()


bool NormStreamObject::StreamAdvance()
{
    // (TBD) should we make sure !pending_mask.IsSet()???
    NormBlockId nextBlockId = stream_next_id;
    // Make sure we won't prevent any pending repairs
    if (repair_mask.CanSet(nextBlockId.GetValue()))  
    {
        if (block_buffer.CanInsert(nextBlockId.GetValue()))
        {
            if (pending_mask.Set(nextBlockId.GetValue()))
            {
                Increment(stream_next_id);
                return true;
            }
            else
            {
                PLOG(PL_ERROR, "NormStreamObject::StreamAdvance() error: node>%lu couldn't set set stream pending mask (1)\n",
                                (unsigned long)LocalNodeId());
            }
        }
        else
        {
            NormBlock* block = block_buffer.Find(block_buffer.RangeLo());  
            ASSERT(NULL != block); 
            if (!block->IsTransmitPending())
            {
                // ??? (TBD) Should this block be returned to the pool right now???
                //     especially for "push" enabled streams
                if (pending_mask.Set(nextBlockId.GetValue()))
                {
                    Increment(stream_next_id);
                    return true;
                }
                else
                {
                    PLOG(PL_ERROR, "NormStreamObject::StreamAdvance() error: node>%lu couldn't set stream pending mask (2)\n",
                                    (unsigned long)LocalNodeId());
                }
            }
            else
            {
               PLOG(PL_DEBUG, "NormStreamObject::StreamAdvance() warning: node>%lu pending segment repairs (blk>%lu) "
                              "delaying stream advance ...\n", (unsigned long)LocalNodeId(), 
                                (unsigned long)block->GetId().GetValue());
            } 
        }
    }
    else
    {
        PLOG(PL_WARN, "NormStreamObject::StreamAdvance() warning: node>%lu pending block repair delaying stream advance ...\n",
                        (unsigned long)LocalNodeId());   
    }
    return false;
}  // end NormStreamObject::StreamAdvance()

NormBlockId NormStreamObject::RepairWindowLo() const
{
    NormBlockId blockId(0);
    if (!stream_buffer.IsEmpty())
        blockId = StreamBufferLo();
    if (!block_buffer.IsEmpty())
    {
        NormBlockId rangeMin = block_buffer.RangeMin();
        if (Compare(rangeMin, blockId) > 0)
            blockId = rangeMin;
    }
    return blockId;
    
    /*
    // TBD - use block_buffer.range_hi information to compute
    // (i.e. range_hi - range_max
    while (!block_buffer.CanInsert(blockId))
    {
        Increment(blockId);
    }
    return blockId;
    */
}  // end NormStreamObject::RepairWindowLo()

bool NormObject::CalculateBlockParity(NormBlock* block)
{
    if (0 == nparity) return true;
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
            block->UpdateSegSizeMax(payloadLength);
            session.SenderEncode(i, buffer, block->SegmentList(numData));
        }
        else
        {
            return false;   
        }
    }
    block->SetParityReadiness(numData);
    return true;
}  // end NormObject::CalculateBlockParity()

NormBlock* NormObject::SenderRecoverBlock(NormBlockId blockId)
{
    NormBlock* block = session.SenderGetFreeBlock(transport_id, blockId);
    if (block)
    {
        UINT16 numData = GetBlockSize(blockId);  
        // Init block parameters
        block->TxRecover(blockId, numData, nparity);
        // Fill block with zero initialized parity segments
        UINT16 totalBlockLen = numData + nparity;
        for (UINT16 i = numData; i < totalBlockLen; i++)
        {
            char* s = session.SenderGetFreeSegment(transport_id, blockId);
            if (s)
            {
                UINT16 payloadMax = segment_size + NormDataMsg::GetStreamPayloadHeaderLength();                
#ifdef SIMULATE
                payloadMax = MIN(payloadMax, SIM_PAYLOAD_MAX);
#endif // SIMULATE
                memset(s, 0, payloadMax);  // extra byte for msg flags
                block->AttachSegment(i, s); 
            }
            else
            {
                //DMSG(2, "NormObject::SenderRecoverBlock() node>%lu Warning! sender resource " 
                //        "constrained (no free segments).\n", LocalNodeId());
                session.SenderPutFreeBlock(block);
                return (NormBlock*)NULL;
            }
        }      
        // Attempt to re-generate parity for the block
        if (CalculateBlockParity(block))
        {
            if (!block_buffer.Insert(block))
            {
                session.SenderPutFreeBlock(block);
                PLOG(PL_DEBUG, "NormObject::SenderRecoverBlock() node>%lu couldn't buffer recovered block\n",
                                (unsigned long)LocalNodeId());
                return NULL;   
            }
            return block;
        }
        else
        {
            session.SenderPutFreeBlock(block);
            return (NormBlock*)NULL;
        }
    }
    else
    {
        //DMSG(2, "NormObject::SenderRecoverBlock() node>%lu Warning! sender resource " 
        //                "constrained (no free blocks).\n", (unsigned long)LocalNodeId());
        return (NormBlock*)NULL;
    }
}  // end NormObject::SenderRecoverBlock()

/////////////////////////////////////////////////////////////////
//
// NormFileObject Implementation
//
NormFileObject::NormFileObject(class NormSession&       theSession, 
                               class NormSenderNode*    theSender,
                               const NormObjectId&      objectId)
 : NormObject(FILE, theSession, theSender, objectId), 
   large_block_length(0), small_block_length(0)
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
    if (sender)  
    {
        // We're receiving this file 
        if (NormFile::IsLocked(thePath))
        {
            PLOG(PL_FATAL, "NormFileObject::Open() Error trying to open locked file for recv!\n");
            return false;   
        }
        else
        {
            if (file.Open(thePath, O_RDWR | O_CREAT | O_TRUNC))
            {
                if (!file.Lock())
                    PLOG(PL_WARN, "NormFileObject::Open() warning: NormFile::Lock() failure\n");
            }   
            else
            {
                PLOG(PL_FATAL, "NormFileObject::Open() recv file.Open() error!\n");
                return false;
            }
        }  
    }
    else
    {
        // Verify that it _is_ a file
        if (NormFile::NORMAL != NormFile::GetType(thePath))
        {
            PLOG(PL_FATAL, "NormFileObject::Open() send file \"%s\" is not a file "
                    "(a directory perhaps?)\n", thePath);
            return false;    
        }        
			
        // We're sending this file
        if (file.Open(thePath, O_RDONLY))
        {
            NormObjectSize::Offset size = file.GetSize(); 
            //if (size)
            {
                if (!NormObject::Open(NormObjectSize(size), 
                                      infoPtr, 
                                      infoLen,
                                      session.SenderSegmentSize(),
                                      session.GetSenderFecId(),
                                      session.GetSenderFecFieldSize(),
                                      session.SenderBlockSize(),
                                      session.SenderNumParity()))
                {
                    PLOG(PL_FATAL, "NormFileObject::Open() send object open error\n");
                    Close();
                    return false;
                }
            }
            /*
            else
            {
                PLOG(PL_FATAL, "NormFileObject::Open() send file.GetSize() error!\n"); 
                file.Close();
                return false;
            }*/ 
        } 
        else
        {
            PLOG(PL_FATAL, "NormFileObject::Open() send file.Open() error!\n");
            return false;
        }
    }
    large_block_length = NormObjectSize(large_block_size) * segment_size;
    small_block_length = NormObjectSize(small_block_size) * segment_size;
    strncpy(path, thePath, PATH_MAX);
    size_t len = strlen(thePath);
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
    if (file.IsOpen())
    {
        if (NULL != sender)  // we've been receiving this file
            file.Unlock();
        file.Close();
    }
    NormObject::Close();
}  // end NormFileObject::Close()

bool NormFileObject::WriteSegment(NormBlockId   blockId, 
                                  NormSegmentId segmentId, 
                                  const char*   buffer)
{
    size_t len;  
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
    if (blockId.GetValue() < large_block_count)
    {
        segmentOffset = (large_block_length*blockId.GetValue()) + (segmentSize*segmentId);
    }
    else
    {
        segmentOffset = large_block_length*large_block_count;  // (TBD) pre-calc this 
        UINT32 smallBlockIndex = blockId.GetValue() - large_block_count;
        segmentOffset = segmentOffset + small_block_length*smallBlockIndex +
                                        segmentSize*segmentId;
    }
	NormFile::Offset offset = segmentOffset.GetOffset();
    if (offset != file.GetOffset())
    {
        if (!file.Seek(offset)) return false; 
    }
    size_t nbytes = file.Write(buffer, len);
    return (nbytes == len);
}  // end NormFileObject::WriteSegment()


UINT16 NormFileObject::ReadSegment(NormBlockId      blockId, 
                                   NormSegmentId    segmentId,
                                   char*            buffer)            
{
    // Determine segment length from blockId::segmentId
    size_t len;
    if (blockId == final_block_id)
    {
        if (segmentId == (GetBlockSize(blockId) - 1))
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
    if (blockId.GetValue() < large_block_count)
    {
        segmentOffset = large_block_length*blockId.GetValue() + segmentSize*segmentId;
    }
    else
    {
        segmentOffset = large_block_length*large_block_count;  // (TBD) pre-calc this  
        UINT32 smallBlockIndex = blockId.GetValue() - large_block_count;
        segmentOffset = segmentOffset + small_block_length*smallBlockIndex +
                                        segmentSize*segmentId;
    }
	NormFile::Offset offset = segmentOffset.GetOffset();
    if (offset != file.GetOffset())
    {
        if (!file.Seek(offset))
        {
            PLOG(PL_FATAL, "NormFileObject::ReadSegment() error seeking to file offset\n");
            return 0;
        }
    }
    
    size_t nbytes = file.Read(buffer, len);
    
    if (len == nbytes)
        return (UINT16)len;
    else
        return 0;
}  // end NormFileObject::ReadSegment()

char* NormFileObject::RetrieveSegment(NormBlockId      blockId, 
                                      NormSegmentId    segmentId)
{
    if (sender)
    {
        char* segment = sender->GetRetrievalSegment();
        UINT16 len = ReadSegment(blockId, segmentId, segment);
        if (len)
        {
            // zeroize remainder for proper decodes
            if (len < segment_size)
                memset(segment+len, 0, segment_size-len);
            return segment;
        }
        else
        {
            PLOG(PL_FATAL, "NormFileObject::RetrieveSegment() error reading segment\n");
            return NULL;
        }          
    }   
    else
    {
        PLOG(PL_FATAL, "NormFileObject::RetrieveSegment() error: NULL sender!\n");
        return NULL;   
    }
}  // end NormFileObject::RetrieveSegment()

/////////////////////////////////////////////////////////////////
//
// NormDataObject Implementation
//
NormDataObject::NormDataObject(class NormSession&       theSession, 
                               class NormSenderNode*    theSender,
                               const NormObjectId&      objectId,
                               DataFreeFunctionHandle   dataFreeFunc)
 : NormObject(DATA, theSession, theSender, objectId), 
   large_block_length(0), small_block_length(0),
   data_ptr(NULL), data_max(0), data_released(false),
   data_free_func(dataFreeFunc)
{
    
}

NormDataObject::~NormDataObject()
{
    Close();
    if (data_released)
    {
        if (NULL != data_ptr)
        {
            if (NULL != data_free_func)
                data_free_func(data_ptr);
            else
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
        if (NULL != data_free_func)
            data_free_func(data_ptr);
        else
            delete[] data_ptr;
        data_ptr = NULL;
        data_released = false;   
    }
    if (NULL == sender)
    {
        // We're sending this data object
        if (!NormObject::Open(dataLen, 
                              infoPtr, 
                              infoLen,
                              session.SenderSegmentSize(),
                              session.GetSenderFecId(),
                              session.GetSenderFecFieldSize(),
                              session.SenderBlockSize(),
                              session.SenderNumParity()))
        {
            PLOG(PL_FATAL, "NormDataObject::Open() send object open error\n");
            Close();
            return false;
        }
    }
    else
    {
        // We're receiving this data object
        //ASSERT(NULL == infoPtr);
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
                                  const char*   buffer)
{
    if (NULL == data_ptr)
    {
        PLOG(PL_FATAL, "NormDataObject::WriteSegment() error: NULL data_ptr\n");
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
    if (blockId.GetValue() < large_block_count)
    {
        segmentOffset = large_block_length*blockId.GetValue() + segmentSize*segmentId;
    }
    else
    {
        segmentOffset = large_block_length*large_block_count;  // (TBD) pre-calc this 
        UINT32 smallBlockIndex = blockId.GetValue() - large_block_count;
        segmentOffset = segmentOffset + small_block_length*smallBlockIndex +
                                        segmentSize*segmentId;
    }
    ASSERT(0 == segmentOffset.MSB());  // we don't yet support super-sized "data" objects
    if (data_max <= segmentOffset.LSB())
        return true;
    else if (data_max <= (segmentOffset.LSB() + len))
        len -= (segmentOffset.LSB() + len - data_max);
    memcpy(data_ptr + segmentOffset.LSB(), buffer, len);
    return true;
}  // end NormDataObject::WriteSegment()


UINT16 NormDataObject::ReadSegment(NormBlockId      blockId, 
                                   NormSegmentId    segmentId,
                                   char*            buffer)            
{
    if (NULL == data_ptr)
    {
        PLOG(PL_FATAL, "NormDataObject::ReadSegment() error: NULL data_ptr\n");
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
    if (blockId.GetValue() < large_block_count)
    {
        segmentOffset = large_block_length*blockId.GetValue() + segmentSize*segmentId;
    }
    else
    {
        segmentOffset = large_block_length*large_block_count;  // (TBD) pre-calc this  
        UINT32 smallBlockIndex = blockId.GetValue() - large_block_count;
        segmentOffset = segmentOffset + small_block_length*smallBlockIndex +
                                        segmentSize*segmentId;
    }
    ASSERT(0 == segmentOffset.MSB());    // we don't yet support super-sized "data" objects
    if (data_max <= segmentOffset.LSB())
        return 0;
    else if (data_max <= (segmentOffset.LSB() + len))
        len -= (segmentOffset.LSB() + len - data_max);
    
    memcpy(buffer, data_ptr + segmentOffset.LSB(), len);
    return len;
}  // end NormDataObject::ReadSegment()

char* NormDataObject::RetrieveSegment(NormBlockId   blockId, 
                                      NormSegmentId segmentId)
{
    if (NULL == data_ptr)
    {
        PLOG(PL_FATAL, "NormDataObject::RetrieveSegment() error: NULL data_ptr\n");
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
    if (blockId.GetValue() < large_block_count)
    {
        segmentOffset = large_block_length*blockId.GetValue() + segmentSize*segmentId;
    }
    else
    {
        segmentOffset = large_block_length*large_block_count;  // (TBD) pre-calc this  
        UINT32 smallBlockIndex = blockId.GetValue() - large_block_count;
        segmentOffset = segmentOffset + small_block_length*smallBlockIndex +
                                        segmentSize*segmentId;
    }
    ASSERT(0 == segmentOffset.MSB());  // we don't yet support super-sized "data" objects
    if ((len < segment_size) || (data_max < (segmentOffset.LSB() + len)))
    {
        if (sender)
        {
            char* segment = sender->GetRetrievalSegment();
            len = ReadSegment(blockId, segmentId, segment);
            memset(segment+len, 0, segment_size-len);
            return segment;
        }
        else
        {
            PLOG(PL_FATAL, "NormDataObject::RetrieveSegment() error: NULL sender!\n");
            return NULL; 
        }
    }
    else
    {
        return (data_ptr + segmentOffset.LSB());
    }
}  // end NormDataObject::RetrieveSegment()

/////////////////////////////////////////////////////////////////
//
// NormStreamObject Implementation
//

NormStreamObject::NormStreamObject(class NormSession&       theSession, 
                                   class NormSenderNode*    theSender,
                                   const NormObjectId&      objectId)
 : NormObject(STREAM, theSession, theSender, objectId), 
   stream_sync(false), write_vacancy(false), 
   read_init(true), read_ready(false),
   flush_pending(false), msg_start(true),
   flush_mode(FLUSH_NONE), push_mode(false),
   stream_broken(false), stream_closing(false),
   block_pool_threshold(0)
{
}

NormStreamObject::~NormStreamObject()
{
    Close();    
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
}  

NormBlockId NormStreamObject::FlushBlockId() const
{
    if (0 != write_index.segment)
    {
        return write_index.block;
    }
    else
    {
        // blockId = write_index.block - 1
        NormBlockId blockId = write_index.block;
        Decrement(blockId);
        return blockId;
    }
}  // end NormStreamObject::FlushBlockId()

bool NormStreamObject::Open(UINT32      bufferSize, 
                            bool        doubleBuffer,
                            const char* infoPtr, 
                            UINT16      infoLen)
{
    if (0 == bufferSize) 
    {
        PLOG(PL_FATAL, "NormStreamObject::Open() zero bufferSize error\n");
        return false;
    }
    UINT16 segmentSize, numData;
    if (NULL != sender)
    {
        // receive streams have already beeen pre-opened via RxOpen()
        segmentSize = segment_size;
        numData = ndata;
    }
    else
    {
        segmentSize = session.SenderSegmentSize();
        numData = session.SenderBlockSize();
        stream_next_id = pending_mask.GetSize();
    }
    UINT32 blockSize = segmentSize * numData;
    UINT32 numBlocks = bufferSize / blockSize;
    // Buffering requires at least 2 blocks
    numBlocks = MAX(2, numBlocks);
    // Recompute "bufferSize" to match any adjustments
    bufferSize = numBlocks * blockSize;
    
    if (NULL == sender)
    {
        // This is a tx stream object, open with adjusted "bufferSize"
        if (!NormObject::Open(NormObjectSize((UINT32)bufferSize), 
                              infoPtr, 
                              infoLen,
                              session.SenderSegmentSize(),
                              session.GetSenderFecId(),
                              session.GetSenderFecFieldSize(),
                              session.SenderBlockSize(),
                              session.SenderNumParity()))
        {
            PLOG(PL_FATAL, "NormStreamObject::Open() object open error\n");
            Close();
            return false;
        }
    }
    
    if (doubleBuffer) numBlocks *= 2;
    UINT32 numSegments = numBlocks * numData;
    
    if (!block_pool.Init(numBlocks, numData))
    {
        PLOG(PL_FATAL, "NormStreamObject::Open() block_pool init error\n");
        Close();
        return false;
    }
    
    if (!segment_pool.Init(numSegments, segmentSize+NormDataMsg::GetStreamPayloadHeaderLength()))
    {
        PLOG(PL_FATAL, "NormStreamObject::Open() segment_pool init error\n");
        Close();
        return false;
    }
    
    if (!stream_buffer.Init(numBlocks, 256, fec_block_mask))
    {
        PLOG(PL_FATAL, "NormStreamObject::Open() stream_buffer init error\n");
        Close();
        return false;
    }    
    // (TBD) we really only need one set of indexes & offset
    // since our objects are exclusively read _or_ write
    read_init = true;
    
    read_index.block = read_index.segment = read_index.offset = 0; 
    write_index.block = write_index.segment = 0;
    tx_index.block = tx_index.segment = 0;
    tx_offset = write_offset = read_offset = 0;    
    write_vacancy = true;
    stream_sync = false;
    flush_pending = false;
    msg_start = true;
    stream_closing = false;
    return true;
}  // end NormStreamObject::Open()

bool NormStreamObject::Accept(UINT32 bufferSize, bool doubleBuffer)
{
    if (Accepted()) return true;  // was preset_stream (already pre-accepted)
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

void NormStreamObject::Close(bool graceful)
{
    if (graceful && (NULL == sender))
    {
        Terminate();
        //SetFlushMode(FLUSH_ACTIVE);
        //Flush();
    }
    else
    {
        NormObject::Close();
        write_vacancy = false;
    }
}  // end NormStreamObject::Close()

bool NormStreamObject::LockBlocks(NormBlockId firstId, NormBlockId lastId, const ProtoTime& currentTime)
{
    NormBlockId nextId = firstId;
    // First check that we _can_ lock/reset them all
    //while (nextId <= lastId)
    while (Compare(nextId, lastId) <= 0)
    {
        NormBlock* block = stream_buffer.Find(nextId);
        if (NULL == block) return false;
        Increment(nextId);
    }
    nextId = firstId;
    //while (nextId <= lastId)
    while (Compare(nextId, lastId) <= 0)
    {
        NormBlock* block = stream_buffer.Find(nextId);
        if (NULL != block)
        {
            UINT16 numData = GetBlockSize(nextId);
            block->SetPending(0, numData);  // TBD - should this reset auto-parity, too?
            block->SetLastNackTime(currentTime);
        }
        Increment(nextId);   
    }
    return true;
}  // end NormStreamObject::LockBlocks()

void NormStreamObject::UnlockBlock(NormBlockId blockId)
{
    NormBlock* block = stream_buffer.Find(blockId);
    if (NULL != block) block->ClearPending();
}  // end NormStreamObject::UnlockBlock()


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
        //if (blockId < stream_sync_id)
        if (Compare(blockId, stream_sync_id) < 0)
        {
            // it's an old block or stream is _very_ broken
            // (nothing to  update)
            return true;
        }
        else
        {
            //if (blockId < stream_next_id)
            if (Compare(blockId, stream_next_id) < 0)
            {
                // it's pending or complete (nothing to update)
                return true;
            }
            else
            {
                if (pending_mask.IsSet())
                {
                    if (pending_mask.CanSet(blockId.GetValue()))
                    {
                        UINT32 numBits = (UINT32)Difference(blockId, stream_next_id) + 1;
                        pending_mask.SetBits(stream_next_id.GetValue(), numBits);
                        //stream_next_id = blockId + 1;
                        stream_next_id = blockId;
                        Increment(stream_next_id);
                        // Handle potential stream_sync_id wrap
                        // Note: stream_next_id is always greater than stream_sync_id
                        UINT32 delta = (UINT32)Difference(stream_next_id, stream_sync_id);
                        if (delta > 2*pending_mask.GetSize())
                            GetFirstPending(stream_sync_id);
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
                    UINT32 delta = (UINT32)Difference(blockId, stream_next_id) + 1;
                    if (delta > pending_mask.GetSize())
                    {
                        // Stream broken
                        return false;
                    }
                    else
                    {
                        pending_mask.SetBits(blockId.GetValue(), pending_mask.GetSize()); 
                        // stream_next_id = block_id + pending_mask.GetSize();
                        stream_next_id = blockId;
                        Increment(stream_next_id, pending_mask.GetSize());
                        // Handle potential stream_sync_id wrap
                        UINT32 delta = (UINT32)Difference(stream_next_id, stream_sync_id);
                        if (delta > (2*pending_mask.GetSize()))
                            GetFirstPending(stream_sync_id);
                        return true;
                    }  
                }
            }   
        }
    }
    else
    {
        // For now, let stream begin anytime
        // First, clean out block_buffer
        NormBlock* block;
        while (NULL != (block = block_buffer.Find(block_buffer.RangeLo())))
        {
            block_buffer.Remove(block);
            sender->PutFreeBlock(block);
        }
        pending_mask.Clear();
        
        pending_mask.SetBits(blockId.GetValue(), pending_mask.GetSize());
        stream_sync = true;
        stream_sync_id = blockId;
        //stream_next_id = blockId + pending_mask.GetSize(); 
        stream_next_id = blockId;
        Increment(stream_next_id, pending_mask.GetSize());
        if (NULL != sender)
        {
            if (read_init && (NormSenderNode::SYNC_CURRENT != sender->GetSyncPolicy()))
            {
                // This is a fresh rx stream, so init the read indices
                read_init = false;
                PLOG(PL_DEBUG, "NormStreamObject::StreamUpdateStatus() syncing stream to blockId: %lu\n",
                        (unsigned long)blockId.GetValue());
                read_index.block = blockId;
                if (0 != blockId.GetValue()) stream_broken = true;
                read_index.segment = 0;  
                read_index.offset = 0; 
                read_offset = 0;
                sender->DecrementResyncCount();  // correction since stream sync here will falsely increment
            }
        }
        
        // Since we're doing a resync including "read_init", dump any buffered data
        // (TBD) this may not be necessary??? and is thus currently commented-out code
        /*read_init = true;
        NormBlock* block;
        while (NULL != (block = stream_buffer.Find(stream_buffer.RangeLo())))
        {
            stream_buffer.Remove(block);
            block->EmptyToPool(segment_pool);
            block_pool.Put(block);
        }*/
        return true;  
    }
}  // end NormStreamObject::StreamUpdateStatus()


char* NormStreamObject::RetrieveSegment(NormBlockId     blockId, 
                                        NormSegmentId   segmentId)
{
    NormBlock* block = stream_buffer.Find(blockId);
    if (!block)
    {
        PLOG(PL_FATAL, "NormStreamObject::RetrieveSegment() segment block unavailable\n");
        return NULL;   
    }
    char* segment = block->GetSegment(segmentId);
    if (NULL == segment)
        PLOG(PL_FATAL, "NormStreamObject::RetrieveSegment() segment unavailable\n");
    return segment;
}  // end NormStreamObject::RetrieveSegment()

UINT16 NormStreamObject::ReadSegment(NormBlockId      blockId, 
                                     NormSegmentId    segmentId,
                                     char*            buffer)
{
    // (TBD) compare blockId with stream_buffer.RangeLo() and stream_buffer.RangeHi()
    NormBlock* block = stream_buffer.Find(blockId);
    if (NULL == block)
    {
        //DMSG(0, "NormStreamObject::ReadSegment() stream starved (1)\n");
        //if (!stream_buffer.IsEmpty() && (blockId < stream_buffer.RangeLo()))
        if (!stream_buffer.IsEmpty() && (Compare(blockId, stream_buffer.RangeLo()) < 0))
        {
            PLOG(PL_ERROR, "NormStreamObject::ReadSegment() error: attempted to read old block> %lu\n", 
                            (unsigned long)blockId.GetValue());
        }
        return 0;   
    }
    // (TBD) should we check to see if "blockId > write_index.block" ?
    if ((blockId == write_index.block) && (segmentId >= write_index.segment))
    {
        //DMSG(0, "NormStreamObject::ReadSegment(blk>%lu seg>%hu) stream starved (2) (write_index>%lu:%hu)\n",
        //          (unsigned long)blockId.GetValue(), (UINT16)segmentId, 
        //          (unsigned long)write_index.block.GetValue(), (UINT16)write_index.segment);
        return 0;   
    }   
    block->UnsetPending(segmentId);    
    
    char* segment = block->GetSegment(segmentId);
    ASSERT(segment != NULL);    
        
    // Update tx_offset if ((segmentOffset - tx_offset) > 0)  (TBD) deprecate "tx_offset"
    //UINT32 segmentOffset = NormDataMsg::ReadStreamPayloadOffset(segment);
    //INT32 offsetDelta = segmentOffset - tx_offset;
    //if (offsetDelta > 0) tx_offset = segmentOffset;
    // Update tx_index if (blockId::segmentId > tx_index)
    //if (blockId > tx_index.block)
    if (Compare(blockId, tx_index.block) > 0)
    {
        tx_index.block = blockId;
        tx_index.segment = segmentId;
    }
    else if ((blockId == tx_index.block) && (segmentId > tx_index.segment))
    {
        tx_index.segment = segmentId;
    }
    
    // Only advertise vacancy if stream_buffer.RangeLo() is non-pending _and_
    // (write_index.block - tx_index.block) < block_pool.GetTotal() / 2
    if (!write_vacancy)
    {
        //offsetDelta = write_offset - tx_offset;
        //ASSERT(offsetDelta >= 0);
        //if ((UINT32)offsetDelta < object_size.LSB())
        //ASSERT(write_index.block >= tx_index.block);
        ASSERT(Compare(write_index.block, tx_index.block) >= 0);
        UINT32 blockDelta = (UINT32)Difference(write_index.block, tx_index.block);
        if (blockDelta <= (block_pool.GetTotal() >> 1))
        {
            NormBlock* b = stream_buffer.Find(stream_buffer.RangeLo());
            if (NULL != b)
            {
                if (!b->IsPending())
                {
                    // make sure no recent nacking
                    double delay = session.GetFlowControlDelay() - b->GetNackAge();
                    if (delay < 1.0e-06)
                        
                    {
                        if (session.FlowControlIsActive() && (session.GetFlowControlObject() == GetId()))
                            session.DeactivateFlowControl();
                        write_vacancy = true;
                    }
                    else
                    {
                        if (!session.FlowControlIsActive())
                        {
                            session.ActivateFlowControl(delay, GetId(), NormController::TX_QUEUE_VACANCY);
                            PLOG(PL_DEBUG, "NormStreamObject::ReadSegment() asserting flow control for stream (postedEmpty:%d)\n", 
                                            session.GetPostedTxQueueEmpty());
                        }
                    }
                }
            }
            else
            {
                write_vacancy = true; 
            }
            if (write_vacancy) 
                session.Notify(NormController::TX_QUEUE_VACANCY, NULL, this); 
        }       
    }
    
    UINT16 segmentLength = NormDataMsg::ReadStreamPayloadLength(segment);
    ASSERT(segmentLength <= segment_size);
    UINT16 payloadLength = segmentLength+NormDataMsg::GetStreamPayloadHeaderLength();
#ifdef SIMULATE   
    UINT16 payloadMax = segment_size + NormDataMsg::GetStreamPayloadHeaderLength();
    payloadMax = MIN(payloadMax, SIM_PAYLOAD_MAX);
    UINT16 copyMax = MIN(payloadMax, payloadLength);
    memcpy(buffer, segment, copyMax); 
#else
    memcpy(buffer, segment, payloadLength);
#endif // SIMULATE
    return payloadLength;
}  // end NormStreamObject::ReadSegment()

bool NormStreamObject::WriteSegment(NormBlockId   blockId, 
                                    NormSegmentId segmentId, 
                                    const char*   segment)
{
    UINT32 segmentOffset = NormDataMsg::ReadStreamPayloadOffset(segment);
    if (read_init)
    {
        read_init = false;
        read_index.block = blockId;
        read_index.segment = segmentId;   
        read_index.offset = 0;
        read_offset = segmentOffset;
        read_ready = true;
    } 
    
    //if ((blockId < read_index.block) ||
    if ((Compare(blockId, read_index.block) < 0) ||
        ((blockId == read_index.block) && (segmentId < read_index.segment))) 
    {
        PLOG(PL_DEBUG, "NormStreamObject::WriteSegment() block/segment < read_index!?\n");
        return false;
    }  
    
    /*
    // if (segmentOffset < read_offset)
    UINT32 diff = segmentOffset - read_offset;
    if ((diff > 0x80000000) || ((0x80000000 == diff) && (segmentOffset > read_offset)))
    {
        PLOG(PL_DEBUG, "NormStreamObject::WriteSegment() diff:%lu segmentOffset:%lu < read_offset:%lu \n",
                        (unsigned long)diff, (unsigned long)segmentOffset, (unsigned long)read_offset);
        return false;
    }
    */
    
    NormBlock* block = stream_buffer.Find(blockId);
    if (NULL == block)
    {
        bool broken = false;
        bool dataLost = false;
        while (block_pool.IsEmpty() || !stream_buffer.CanInsert(blockId))
        {
            block = stream_buffer.Find(stream_buffer.RangeLo());
            ASSERT(NULL != block);
            //if (blockId < block->GetId())
            if (Compare(blockId, block->GetId()) < 0)
            {
                PLOG(PL_DEBUG, "NormStreamObject::WriteSegment() blockId too old!?\n"); 
                return false;   
            }
            while (block->IsPending())
            {
                // Force read_index forward, giving app a chance to read data
                NormSegmentId firstPending;
                block->GetFirstPending(firstPending);
                if (read_index.block != block->GetId())
                {
                    // The block's id > read_index, so force read_index forward
                    read_index.block = block->GetId();
                    read_index.segment = firstPending;
                    read_index.offset = 0;
                    broken = true;
                    stream_broken = true;
                }
                if (read_index.segment < firstPending)
                {
                    read_index.segment = firstPending;
                    read_index.offset = 0;
                    broken = true;
                    stream_broken = true;
                }
                NormBlock* tempBlock = block;
                //UINT32 tempOffset = read_offset;
                NormStreamObject::Index tempIndex = read_index;
                // (TBD) uncomment the code so that only a single
                // UPDATED notification is posted???
                if (notify_on_update)
                {
                    notify_on_update = false;
                    session.Notify(NormController::RX_OBJECT_UPDATED, sender, this);
                } 
                block = stream_buffer.Find(stream_buffer.RangeLo());
                if (tempBlock == block)
                {
                    if (//(tempOffset == read_offset) && 
                        (tempIndex.block == read_index.block) && 
                        (tempIndex.segment == read_index.segment) &&
                        (tempIndex.offset == read_index.offset))
                    {
                        // App didn't grab data here, purge segment
                        dataLost = true;
                        broken = true;
                        stream_broken = true;
                        block->UnsetPending(read_index.segment++);
                        read_index.offset = 0;
                        if (read_index.segment >= ndata)
                        {
                            Increment(read_index.block);
                            read_index.segment = 0;
                            stream_buffer.Remove(block);
                            block->EmptyToPool(segment_pool);
                            block_pool.Put(block);
                            block = NULL;
                            Prune(read_index.block, false);
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
            if (NULL != block)
            {
                NormBlockId bid = block->GetId();
                stream_buffer.Remove(block);
                block->EmptyToPool(segment_pool);
                block_pool.Put(block);
                if (bid == read_index.block)
                {
                    stream_broken = true;
                    broken = true;
                    Increment(read_index.block);
                    read_index.segment = 0;
                    read_index.offset = 0;
                    Prune(read_index.block, false);   
                }
            } 
        }  // end while (block_pool.IsEmpty() || !stream_buffer.CanInsert(blockId))
        if (broken)
        {
            PLOG(PL_WARN, "NormStreamObject::WriteSegment() node>%lu obj>%hu blk>%lu seg>%hu broken stream ...\n",
                            (unsigned long)LocalNodeId(), (UINT16)transport_id, 
                            (unsigned long)blockId.GetValue(), (UINT16)segmentId);
            if (dataLost)
                PLOG(PL_ERROR, "NormStreamObject::WriteSegment() broken stream data not read by app!\n");
        }    
        block = block_pool.Get();
        block->SetId(blockId);
        block->ClearPending();
        //ASSERT(blockId >= read_index.block);
        ASSERT(Compare(blockId, read_index.block) >= 0);
        bool success = stream_buffer.Insert(block);
        ASSERT(success);
    }  // end if (!block)
    
    // Make sure this segment hasn't already been written
    if(!block->GetSegment(segmentId))
    {
        char* s = segment_pool.Get();
        ASSERT(s != NULL);  // for now, this should always succeed
        UINT16 payloadLength = NormDataMsg::ReadStreamPayloadLength(segment) + NormDataMsg::GetStreamPayloadHeaderLength();
#ifdef SIMULATE
        UINT16 payloadMax = segment_size + NormDataMsg::GetStreamPayloadHeaderLength();
        payloadMax = MIN(SIM_PAYLOAD_MAX, payloadMax);
        payloadLength = MIN(payloadMax, payloadLength);  
#endif // SIMULATE
        memcpy(s, segment, payloadLength);
        block->AttachSegment(segmentId, s);
        block->SetPending(segmentId);
        
        if (!read_ready)
        {
            // Did this segment make our stream ready for reading
            if ((blockId == read_index.block) && (segmentId == read_index.segment))
            {
                read_ready = true;
            }
            else if (block_pool.GetCount() < block_pool_threshold)
            {
                // We're starting to fill up receive stream buffer
                // with alot of partially received coding blocks
                read_ready = true;
            }
            else if (session.RcvrIsLowDelay())
            {
                INT32 delta = (UINT32)Difference(blockId, read_index.block);
                if (delta > session.RcvrGetMaxDelay())
                    read_ready = true;
            }
        }
    }
    return true;
}  // end NormStreamObject::WriteSegment()

void NormStreamObject::Prune(NormBlockId blockId, bool updateStatus)
{
    if (updateStatus || StreamUpdateStatus(blockId))
    {
        bool resync = false;
        NormBlock* block;
        while (NULL != (block = block_buffer.Find(block_buffer.RangeLo())))
        {
            //if (block->GetId() < blockId)
            if (Compare(block->GetId(), blockId) < 0)
            {
                resync = true;
                pending_mask.Unset(block->GetId().GetValue()); 
                block_buffer.Remove(block);
                sender->PutFreeBlock(block);
            }   
            else
            {
                break;
            }
        }
        NormBlockId firstId;
        if (GetFirstPending(firstId))
        {
            //if (firstId < blockId)
            if (Compare(firstId, blockId) < 0)
            {
                resync = true;
                UINT32 numBits = (UINT32)Difference(blockId, firstId);
                pending_mask.UnsetBits(firstId.GetValue(), numBits);
            }
        }
        while (!StreamUpdateStatus(blockId))
        {
            // Sender is too far ahead of me ...
            resync = true;
            NormBlockId firstId;
            if (GetFirstPending(firstId))
            {
                NormBlock* block = block_buffer.Find(firstId);
                if (NULL != block)
                {
                    block_buffer.Remove(block);
                    sender->PutFreeBlock(block);
                }
                pending_mask.Unset(firstId.GetValue());
            }
            else
            {
                StreamResync(blockId);
                break;
            }          
        }
        if (resync) 
        {
            //if (updateStatus && (read_index.block < blockId) && notify_on_update)
            //if (updateStatus && notify_on_update && (Compare(read_index.block, blockId) < 0))
            if (notify_on_update)
            {
                // If "updateStatus" is true, then Prune() was invoked due to SQUELCH
                // This will prompt the app to read from the stream which, in turn
                // will force the stream read_index forward
                notify_on_update = false;
                session.Notify(NormController::RX_OBJECT_UPDATED, sender, this);
            }
            sender->IncrementResyncCount();
        }
    }
}  // end NormStreamObject::Prune()

// returns "true" if given blockId::segmentId has _not_ been read yet
// i.e., "true" if still pending reading by application
bool NormStreamObject::PassiveReadCheck(NormBlockId blockId, NormSegmentId segmentId)
{
    bool result;
    //if (read_index.block < blockId) 
    if (Compare(read_index.block, blockId) < 0)
        result = true;
    else if (read_index.block == blockId)
        result = (read_index.segment <= segmentId);
    else //if (read_index.block > blockId)
        result = false;
    /*if (result)
    {
        TRACE("NormStreamObject::PassiveReadCheck() %s\n", result ? "data not yet read by app" : "OK");
        TRACE("    (read_index>%lu:%hu  check>%lu:%hu usage:%u)\n", 
                   (unsigned long)read_index.block.GetValue(), (UINT16)read_index.segment, 
                   (unsigned long)blockId.GetValue(), (UINT16)segmentId, GetCurrentBufferUsage());
    }
    */
    return result;
}  // end NormStreamObject::PassiveReadCheck()

bool NormStreamObject::Read(char* buffer, unsigned int* buflen, bool seekMsgStart)
{
    // NOTES:  ReadPrivate() always returns "true" if it reads any bytes
    //         It returns "false" upon a stream break.
    //         To properly reset "update" notification, it needs to 
    //         be called until it passes back a read byte count of zero.
    //         So, upon read byte count < bytesWanted, we add an extra
    //         call to ReadPrivate() to do that. I.e., so the "update"
    //         notification is reset upon a short read count.  The 
    //         "stream_broken" state variable is used for when this
    //         extra ReadPrivate() call reveals the broken stream condition.
    if (stream_broken && !seekMsgStart)
    {
        if (NULL != buflen) *buflen = 0;
        stream_broken = false;
        return false;
    }
    unsigned int bytesWanted;
    if (NULL == buflen)
    {
        bytesWanted = 0;
        buflen = &bytesWanted;
    }
    else
    {
        bytesWanted = *buflen;
    }
    bool result = ReadPrivate(buffer, buflen, seekMsgStart);
    if (!read_ready) 
    {
        notify_on_update = true;
    }
    if (!seekMsgStart && result && (0 != *buflen) && (*buflen < bytesWanted))
    {
        char dummyBuffer[8];
        unsigned int dummyCount = 8;
        stream_broken = ReadPrivate(dummyBuffer, &dummyCount, false) ? false : true;
        ASSERT(0 == dummyCount);
        if (!read_ready) 
        {
            notify_on_update = true;
        }
    }
    return result;
}  // end NormStreamObject::Read()


// Sequential (in order) read/write routines (TBD) Add a "Seek()" method
bool NormStreamObject::ReadPrivate(char* buffer, unsigned int* buflen, bool seekMsgStart)
{
    if (stream_closing || read_init)
    {
        if (stream_closing)
            PLOG(PL_DEBUG, "NormStreamObject::Read() attempted to read from closed stream\n");
        *buflen = 0;
        return seekMsgStart ? false : true;
    }   
    Retain();
    unsigned int bytesRead = 0;
    unsigned int bytesToRead = *buflen;
    bool brokenStream = false;
    do
    {
        NormBlock* block = stream_buffer.Find(read_index.block);
        if (NULL == block)
        {
            PLOG(PL_DETAIL, "NormStreamObject::ReadPrivate() stream buffer empty (1) (sbEmpty:%d)\n", stream_buffer.IsEmpty());
            read_ready = false;
            *buflen = bytesRead;
            if (bytesRead > 0)
            {
                Release();
                return true;
            }
            else
            {
                bool forceForward = false;
                if (block_pool.GetCount() < block_pool_threshold)
                {
                    // We're starting to fill up receive stream buffer
                    // with alot of partially received coding blocks
                    forceForward = true;
                }
                else if (session.RcvrIsLowDelay())
                {
                    // Has the sender moved forward to the next FEC blocks
                    if (Compare(max_pending_block, read_index.block) >= 0)
                    {
                        INT32 delta = (INT32)Difference(max_pending_block, read_index.block);
                        if (delta > session.RcvrGetMaxDelay())
                        {
                           forceForward = true;
                        }
                    }
                }
                else
                {
                    NormBlockId firstPending;
                    if (GetFirstPending(firstPending))
                    {
                        //if (read_index.block < firstPending)
                        if (Compare(read_index.block, firstPending) < 0)
                            forceForward = true;
                    }
                }
                if (forceForward)
                {
                    Increment(read_index.block);  
                    read_index.segment = 0; 
                    read_index.offset = 0;
                    if (!seekMsgStart) brokenStream = true;
                    Prune(read_index.block, false);
                    continue;
                }
                else
                {
                    Release();
                    return (seekMsgStart || brokenStream) ? false : true;   
                }
            }
        }
        char* segment = block->GetSegment(read_index.segment);
        
        ASSERT((NULL == segment) || block->IsPending(read_index.segment));
        
        if (NULL == segment)
        {
            PLOG(PL_DETAIL, "NormStreamObject::ReadPrivate(%lu:%hu) stream buffer empty (read_offset>%lu) (2)\n",
                    (unsigned long)read_index.block.GetValue(), read_index.segment, (unsigned long)read_offset);
            read_ready = false;
            *buflen = bytesRead;
            if (bytesRead > 0)
            {
                Release();
                return true;
            }
            else
            {
                bool forceForward = false;
                if (block_pool.GetCount() < block_pool_threshold)
                {
                    // We're starting to fill up receive stream buffer
                    // with alot of partially received coding blocks
                    forceForward = true;
                }
                else if (session.RcvrIsLowDelay())
                {
                    // Has the sender moved forward to the next FEC block
                    if (Compare(max_pending_block, read_index.block) >= 0)
                    {
                        INT32 delta = (UINT32)Difference(max_pending_block, read_index.block);
                        if (delta > session.RcvrGetMaxDelay())
                            forceForward = true;
                    }
                }
                else
                {
                    NormBlockId firstPending;
                    if (GetFirstPending(firstPending))
                    {
                        //if (read_index.block < firstPending)
                        if (Compare(read_index.block, firstPending) < 0)
                            forceForward = true;
                    }
                }
                if (forceForward)
                {
                    // Force read_index forward and try again if seeking msg start
                    if (++read_index.segment >= ndata)
                    {
                        stream_buffer.Remove(block);
                        block->EmptyToPool(segment_pool);
                        block_pool.Put(block);
                        Increment(read_index.block);
                        read_index.segment = 0;
                        read_index.offset = 0;
                        Prune(read_index.block, false); // prevents repair requests for data we 
                    }                                   // no longer care about (i.e, prior to this block)
                    if (!seekMsgStart) brokenStream = true;
                    continue;  // attempt to find next valid read_index beyond break
                }
                else
                {
                    Release();
                    return (seekMsgStart || brokenStream) ? false : true;   
                } 
            } 
        }  // end if (NULL = segment)
        ASSERT(NULL != segment);
        read_ready = true;
        if (brokenStream) 
        {
            // We have found the next valid read_index and block/segment but stream
            // continuity was broken so report immediately by returning false
            Release();
            return false;
        }
        
        UINT16 length = NormDataMsg::ReadStreamPayloadLength(segment);
        if (0 == length)
        {
            // It's a "stream control" message, so interpret "payload_msg_start" as control code
            switch (NormDataMsg::ReadStreamPayloadMsgStart(segment))
            {
                case 0:
                    // This is the "stream end" control code
                    break;
                default:
                    // Invalid stream control code, skip this invalid segment
                    PLOG(PL_ERROR, "NormStreamObject::ReadPrivate() invalid stream control message\n");
                    if (++read_index.segment >= ndata)
                    {
                        stream_buffer.Remove(block);
                        block->EmptyToPool(segment_pool);
                        block_pool.Put(block);
                        Increment(read_index.block);
                        read_index.segment = 0;
                        read_index.offset = 0;
                        //Prune(read_index.block, false);
                    }
                    continue;
                    break;
            }  // end switch(NormDataMsg::ReadStreamPayloadMsgStart(segment))
        }
        else if (length > segment_size)
        {
            // This segment is an invalid segment because its length is too long
            read_ready = false; 
            if (bytesRead > 0)
            {
                // Go ahead and return data read thus so far
                *buflen = bytesRead;
                Release();
                return true;       
            }
            else
            {
                // Skip this invalid segment
                PLOG(PL_ERROR, "NormStreamObject::ReadPrivate() node>%lu obj>%hu blk>%lu seg>%hu invalid stream segment!\n", 
                                (unsigned long)LocalNodeId(), (UINT16)transport_id, 
                                (unsigned long)read_index.block.GetValue(), (UINT16)read_index.segment);
                if (++read_index.segment >= ndata)
                {
                    stream_buffer.Remove(block);
                    block->EmptyToPool(segment_pool);
                    block_pool.Put(block);
                    Increment(read_index.block);
                    read_index.segment = 0;
                    read_index.offset = 0;
                    Prune(read_index.block, false);
                }
                *buflen = 0;
                Release();
                return false;
            }
        }
        
        UINT32 segmentOffset = NormDataMsg::ReadStreamPayloadOffset(segment);
        
        /*// if (read_offset < segmentOffset)
        UINT32 diff = read_offset - segmentOffset;
        if ((diff > 0x80000000) || ((0x80000000 == diff) && (read_offset > segmentOffset)))
        {
            read_ready = false; //DetermineReadReadiness();
            if (bytesRead > 0)
            {
                // Go ahead and return data read thus so far
                *buflen = bytesRead;
                Release();
                return true; 
            }
            else
            {
                PLOG(PL_WARN, "NormStreamObject::ReadPrivate() node>%lu obj>%hu blk>%lu seg>%hu broken stream (read_offset:%lu segmentOffset:%lu)!\n", 
                                (unsigned long)LocalNodeId(), (UINT16)transport_id, 
                                (unsigned long)read_index.block.GetValue(), (UINT16)read_index.segment,
                                (unsigned long)read_offset, (unsigned long)segmentOffset);
                read_offset = segmentOffset;
                *buflen = 0;
                Release();
                return false;
            }
        }*/
        //UINT32 index = read_offset - segmentOffset;
        
	    if ((length > 0) && (read_index.offset >= length))
        {
            read_ready = false; //DetermineReadReadiness();
            if (bytesRead > 0)
            {
                // Go ahead and return data read thus so far
                *buflen = bytesRead;
                Release();
                return true; 
            }
            else
            {
                PLOG(PL_ERROR, "NormStreamObject::ReadPrivate() node>%lu obj>%hu blk>%lu seg>%hu mangled stream! "
                               "offset:%hu length:%hu read_offset:%lu segmentOffset:%lu\n",
                                (unsigned long)read_index.block.GetValue(), (UINT16)read_index.segment, 
                                read_index.offset, length, (unsigned long)read_offset, (unsigned long)segmentOffset);
                // Reset our read_offset ...
                read_offset = segmentOffset;
                read_index.offset = 0;
                *buflen = 0;
                Release();
                return false;
            }
        }
        
        if (seekMsgStart)
        {
            UINT16 msgStart = NormDataMsg::ReadStreamPayloadMsgStart(segment);
            if (0 == msgStart)
            {
                // make sure msg start searches don't miss the stream end ...
                if (0 == NormDataMsg::ReadStreamPayloadLength(segment))
                {
                    PLOG(PL_DEBUG, "NormStreamObject::ReadPrivate() stream ended by sender 1\n");
                    session.Notify(NormController::RX_OBJECT_COMPLETED, sender, this);
                    stream_closing = true;
                    sender->DeleteObject(this);
                }
                // Don't bother managing individual segments since
                // stream buffers are exact multiples of block size!
                //block->DetachSegment(read_index.segment);
                //segment_pool.Put(segment);
                block->UnsetPending(read_index.segment++);
                read_index.offset = 0;
                if (read_index.segment >= ndata) 
                {
                    stream_buffer.Remove(block);
                    block->EmptyToPool(segment_pool);
                    block_pool.Put(block);
                    Increment(read_index.block);
                    read_index.segment = 0;
                    Prune(read_index.block, false);
                }
                continue;
            }
            else
            {
                read_offset += (msgStart - 1);
                read_index.offset = (msgStart - 1);
                seekMsgStart = false; 
            }            
        }
        UINT16 count = length - read_index.offset;
        count = MIN(count, bytesToRead);
#ifdef SIMULATE
        UINT16 simCount = read_index.offset + count + NormDataMsg::GetStreamPayloadHeaderLength();
        simCount = (simCount < SIM_PAYLOAD_MAX) ? (SIM_PAYLOAD_MAX - simCount) : 0;
        memcpy(buffer+bytesRead, segment+read_index.offset+NormDataMsg::GetStreamPayloadHeaderLength(), simCount);
#else
        memcpy(buffer+bytesRead, segment+read_index.offset+NormDataMsg::GetStreamPayloadHeaderLength(), count);
#endif // if/else SIMULATE
        
        read_index.offset += count;
        bytesRead += count;
        read_offset += count;
        bytesToRead -= count;
        if (read_index.offset >= length)
        {            
            bool streamEnded = (0 == NormDataMsg::ReadStreamPayloadLength(segment));
            // NormDataMsg::StreamPayloadFlagIsSet(segment, NormDataMsg::FLAG_STREAM_END);
            // Don't bother managing individual segments since
            // stream buffers are multiples of block size!
            // block->DetachSegment(read_index.segment);
            // segment_pool.Put(segment);
            block->UnsetPending(read_index.segment++);
            read_index.offset = 0;
            if (read_index.segment >= ndata) 
            {
                stream_buffer.Remove(block);
                block->EmptyToPool(segment_pool);
                block_pool.Put(block);
                Increment(read_index.block);
                read_index.segment = 0;
                Prune(read_index.block, false);
                if (0 == bytesToRead)
                    read_ready = DetermineReadReadiness();
            }
            else
            {
                if (0 == bytesToRead)
                    read_ready = (NULL != block->GetSegment(read_index.segment));
            }
            if (streamEnded)
            {
                PLOG(PL_DEBUG, "NormStreamObject::ReadPrivate() stream ended by sender 2\n");
                session.Notify(NormController::RX_OBJECT_COMPLETED, sender, this);
                stream_closing = true;
                sender->DeleteObject(this);  
            }
        } 
    }  while ((bytesToRead > 0) || seekMsgStart); 
    *buflen = bytesRead;
    Release();
    return true;
}  // end NormStreamObject::ReadPrivate()


void NormStreamObject::Terminate()
{
    // Flush stream and create a ZERO length segment to send
    Flush();  // should eom be set for this call?
    stream_closing = true;
    NormBlock* block = stream_buffer.Find(write_index.block);
    if (NULL == block)
    {
        if (NULL == (block = block_pool.Get()))
        {
            block = stream_buffer.Find(stream_buffer.RangeLo());
            ASSERT(NULL != block);
            if (block->IsPending())
            {
                NormBlockId blockId = block->GetId();
                pending_mask.Unset(blockId.GetValue());
                repair_mask.Unset(blockId.GetValue());
                NormBlock* b = FindBlock(blockId);
                if (b)
                {
                    block_buffer.Remove(b);
                    session.SenderPutFreeBlock(b); 
                }   
                if (!pending_mask.IsSet()) 
                {
                    pending_mask.Set(write_index.block.GetValue());  
                    //stream_next_id = write_index.block + 1;
                    stream_next_id = write_index.block;
                    Increment(stream_next_id);
                }
            }                             
            stream_buffer.Remove(block);
            block->EmptyToPool(segment_pool);
        }
        block->SetId(write_index.block);
        block->ClearPending();
        bool success = stream_buffer.Insert(block);
        ASSERT(success);
    }  // end if (!block)
    char* segment = block->GetSegment(write_index.segment);
    if (NULL == segment)
    {
        if (NULL == (segment = segment_pool.Get()))
        {
            NormBlock* b = stream_buffer.Find(stream_buffer.RangeLo());
            ASSERT(b != block);
            if (b->IsPending())
            {
                NormBlockId blockId = b->GetId();
                pending_mask.Unset(blockId.GetValue());
                repair_mask.Unset(blockId.GetValue());
                NormBlock* c = FindBlock(blockId);
                if (c)
                {
                    block_buffer.Remove(c);
                    session.SenderPutFreeBlock(c);
                }  
                if (!pending_mask.IsSet()) 
                {
                    pending_mask.Set(write_index.block.GetValue());  
                    //stream_next_id = write_index.block + 1;
                    stream_next_id = write_index.block;
                    Increment(stream_next_id);
                }  
            }
            stream_buffer.Remove(b);
            b->EmptyToPool(segment_pool);
            block_pool.Put(b);
            segment = segment_pool.Get();
            ASSERT(NULL != segment);
        }
        block->AttachSegment(write_index.segment, segment);
        NormDataMsg::WriteStreamPayloadMsgStart(segment, 0);
        NormDataMsg::WriteStreamPayloadLength(segment, 0);
    }
    else
    {
        // Make sure the segment is not still referenced (TBD - why was I checking this???)
        //ASSERT(0 == NormDataMsg::ReadStreamPayloadMsgStart(segment));
        //ASSERT(0 == NormDataMsg::ReadStreamPayloadLength(segment));
    }
    NormDataMsg::WriteStreamPayloadOffset(segment, write_offset);  
    
    block->SetPending(write_index.segment);
    if (++write_index.segment >= ndata) 
    {
        Increment(write_index.block);
        write_index.segment = 0;
    }
    flush_pending = true;
    session.TouchSender();
}  // end NormStreamObject::Terminate()

unsigned int NormStreamObject::GetVacancy(unsigned int wanted)
{
    // Computes how many bytes are available for _immediate_ writing
    ASSERT(Compare(write_index.block, tx_index.block) >= 0);
    unsigned int maxDelta = block_pool.GetTotal() >> 1;
    UINT32 blockDelta = (UINT32)Difference(write_index.block, tx_index.block);
    if (blockDelta > maxDelta) return 0;
    UINT32 nBytes = 0;
    NormBlock* block = stream_buffer.Find(write_index.block);
    if (NULL != block)
    {
        char* segment = block->GetSegment(write_index.segment);
        if (NULL != segment)
        {
            UINT16 index = NormDataMsg::ReadStreamPayloadLength(segment);
            nBytes += segment_size - index;
        }
        else
        {
            nBytes += segment_size;
        }
        nBytes += (ndata - write_index.segment - 1) * segment_size;
    }
    unsigned int blocksAllowed = maxDelta - blockDelta;
    unsigned int poolCount = block_pool.GetCount();
    if (poolCount >= blocksAllowed)
        poolCount = blocksAllowed;
    nBytes += poolCount * ndata * segment_size;
    
    NormBlockBuffer::Iterator iterator(block_buffer);
    while ((NULL != (block = iterator.GetNextBlock())) &&
           (blocksAllowed > 0) &&
           ((0 == wanted) || (nBytes < wanted)))
    {
        double delay = session.GetFlowControlDelay() - block->GetNackAge();
        if (block->IsPending() || (delay >= 1.0e-06)) break;
        nBytes += (segment_size * ndata);
        blocksAllowed--;
    }
    return nBytes;
}  // end NormStreamObject::GetVacancy()

UINT32 NormStreamObject::Write(const char* buffer, UINT32 len, bool eom)
{               
    UINT32 nBytes = 0;
    do
    {
        if (stream_closing)
        {
            if (0 != len)
            {
                PLOG(PL_ERROR, "NormStreamObject::Write() error: stream is closing (len:%lu eom:%d)\n", 
                                (unsigned long)len, eom);
                len = 0;
            }
            break;
        }
        // This old code detected buffer "fullness" by offset instead of segment index
        // but, the problem there was when apps wrote & flushed messages smaller than
        // the segment_size, the buffer was used up before this detected it.
        //INT32 deltaOffset = write_offset - tx_offset;  // (TBD) deprecate tx_offset
        //ASSERT(deltaOffset >= 0);
        //if (deltaOffset >= (INT32)object_size.LSB())
        //ASSERT(write_index.block >= tx_index.block);
        ASSERT(Compare(write_index.block, tx_index.block) >= 0);
        UINT32 deltaBlock = (UINT32)Difference(write_index.block, tx_index.block);
        if (deltaBlock > (block_pool.GetTotal() >> 1))  
        {
            write_vacancy = false;
            PLOG(PL_DEBUG, "NormStreamObject::Write() stream buffer full (1)\n");
            if (!push_mode) break;  
        }
        NormBlock* block = stream_buffer.Find(write_index.block);
        if (NULL == block)
        {   
            block = block_pool.Get();
            if (NULL == block)
            {
                block = stream_buffer.Find(stream_buffer.RangeLo());
                ASSERT(NULL != block);
                double delay = session.GetFlowControlDelay() - block->GetNackAge();
                if (block->IsPending() || (delay >= 1.0e-06))
                {
                    write_vacancy = false;
                    if (push_mode)
                    {
                        NormBlockId blockId = block->GetId();
                        pending_mask.Unset(blockId.GetValue());
                        repair_mask.Unset(blockId.GetValue());
                        NormBlock* b = FindBlock(blockId);
                        if (b)
                        {
                            block_buffer.Remove(b);
                            session.SenderPutFreeBlock(b); 
                        }   
                        if (!pending_mask.IsSet()) 
                        {
                            pending_mask.Set(write_index.block.GetValue());  
                            //stream_next_id = write_index.block + 1;
                            stream_next_id = write_index.block;
                            Increment(stream_next_id);
                        }
                    }
                    else
                    {
                        // The timer activated here makes sure a deferred TX_QUEUE_VACANCY is posted
                        // when flow control has been asserted.
                        if (!block->IsPending())
                        {
                            PLOG(PL_DEBUG, "NormStreamObject::Write() asserting flow control for stream (postedEmpty:%d)\n", 
                                           session.GetPostedTxQueueEmpty());
                            if (session.GetPostedTxQueueEmpty())
                                session.ActivateFlowControl(delay, GetId(), NormController::TX_QUEUE_EMPTY);
                            else
                                session.ActivateFlowControl(delay, GetId(), NormController::TX_QUEUE_VACANCY);
                        }
                        PLOG(PL_DEBUG, "NormStreamObject::Write() stream buffer full (2) len:%d eom:%d\n", len, eom);
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
        }  // end if (NULL == block)
        char* segment = block->GetSegment(write_index.segment);
        if (NULL == segment)
        {
            if (NULL == (segment = segment_pool.Get()))
            {
                NormBlock* b = stream_buffer.Find(stream_buffer.RangeLo());
                ASSERT(b != block);
                if (b->IsPending())
                {
                    write_vacancy = false;
                    if (push_mode)
                    {
                        NormBlockId blockId = b->GetId();
                        pending_mask.Unset(blockId.GetValue());
                        repair_mask.Unset(blockId.GetValue());
                        NormBlock* c = FindBlock(blockId);
                        if (c)
                        {
                            block_buffer.Remove(c);
                            session.SenderPutFreeBlock(c);
                        }  
                        if (!pending_mask.IsSet()) 
                        {
                            pending_mask.Set(write_index.block.GetValue());  
                            //stream_next_id = write_index.block + 1;
                            stream_next_id = write_index.block;
                            Increment(stream_next_id);
                        }  
                    }
                    else
                    {
                        PLOG(PL_DEBUG, "NormStreamObject::Write() stream buffer full (3)\n");
                        break;
                    }
                }
                stream_buffer.Remove(b);
                b->EmptyToPool(segment_pool);
                block_pool.Put(b);
                segment = segment_pool.Get();
                ASSERT(NULL != segment);
            }
            NormDataMsg::WriteStreamPayloadMsgStart(segment, 0);
            NormDataMsg::WriteStreamPayloadLength(segment, 0);
            NormDataMsg::WriteStreamPayloadOffset(segment, write_offset);
            block->AttachSegment(write_index.segment, segment);
        }  // end if (!segment)
        
        UINT16 index = NormDataMsg::ReadStreamPayloadLength(segment);
        // If it is an application start-of-message, mark the stream header accordingly
        // (but only if it is the _first_ message start for this segment!)
        if (msg_start && (0 != len))
        {
            if (0 == NormDataMsg::ReadStreamPayloadMsgStart(segment))
                NormDataMsg::WriteStreamPayloadMsgStart(segment, index+1);
            msg_start = false;
        }
        
        UINT32 count = len - nBytes;
        UINT32 space = (UINT32)(segment_size - index);
        count = MIN(count, space);
#ifdef SIMULATE
        UINT32 simCount = index + NormDataMsg::GetStreamPayloadHeaderLength();
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
                ProtoTime currentTime;
                currentTime.GetCurrentTime();
                block->SetLastNackTime(currentTime);
                Increment(write_index.block);
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
        else if (!stream_closing)
            flush_pending = false;
		if ((0 != nBytes) || (FLUSH_NONE != flush_mode))
		{
			session.TouchSender();
		}
    }
    else
    {
        session.TouchSender();  
    }
    return nBytes;
}  // end NormStreamObject::Write()

#ifdef SIMULATE
/////////////////////////////////////////////////////////////////
//
// NormSimObject Implementation (dummy NORM_OBJECT_FILE or NORM_OBJECT_DATA)
//

NormSimObject::NormSimObject(class NormSession&       theSession,
                             class NormSenderNode*    theSender,
                             const NormObjectId&      objectId)
 : NormObject(FILE, theSession, theSender, objectId)
{
    
}

NormSimObject::~NormSimObject()
{
    
}

bool NormSimObject::Open(UINT32        objectSize,
                         const char*   infoPtr ,
                         UINT16        infoLen)
{
    return (sender ?
                true : 
                NormObject::Open(NormObjectSize(objectSize), 
                                 infoPtr, infoLen,
                                 session.SenderSegmentSize(),
                                 session.GetSenderFecId(),
                                 session.GetSenderFecFieldSize(),
                                 session.SenderBlockSize(),
                                 session.SenderNumParity()));
}  // end NormSimObject::Open()

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

char* NormSimObject::RetrieveSegment(NormBlockId   blockId,
                                     NormSegmentId segmentId)
{
    return sender ? sender->GetRetrievalSegment() : NULL;   
}  // end NormSimObject::RetrieveSegment()

#endif // SIMULATE      

/////////////////////////////////////////////////////////////////
//
// NormObjectTable Implementation
//

NormObjectTable::NormObjectTable()
#ifndef USE_PROTO_TREE
 : table((NormObject**)NULL), 
#else
 :
#endif // if/else USE_PROTO_TREE
   range_max(0), range(0),
   count(0), size(0)
{
}

NormObjectTable::~NormObjectTable()
{
    Destroy();
}

bool NormObjectTable::Init(UINT16 rangeMax, UINT16 tableSize)
{
    Destroy();
    if (!rangeMax || !tableSize) return false;
#ifndef USE_PROTO_TREE
    // Make sure (rangeMax > 0) and tableSize is greater than 0 and 2^n
    if (!rangeMax || !tableSize) return false;
    if (0 != (tableSize & 0x07)) tableSize = (tableSize >> 3) + 1;
    if (!(table = new NormObject*[tableSize]))
    {
        PLOG(PL_FATAL, "NormObjectTable::Init() table allocation error: %s\n", GetErrorString());
        return false;         
    }
    memset(table, 0, tableSize*sizeof(char*));
    hash_mask = tableSize - 1;
#endif  //  !USE_PROTO_TREE
    range_max = rangeMax;
    count = range = 0;
    size = NormObjectSize(0);
    return true;
}  // end NormObjectTable::Init()

void NormObjectTable::SetRangeMax(UINT16 rangeMax)
{
    if (rangeMax < range_max)
    {
        // Prune if necessary
        while (range > rangeMax)
        {
            NormObject* obj = Find(range_lo);
            ASSERT(NULL != obj);
            NormSenderNode* sender = obj->GetSender();
            NormSession& session = obj->GetSession();
            if (NULL == sender)
            {
                session.DeleteTxObject(obj, true);
            }
            else
            {
                if (!session.ReceiverIsSilent()) obj = Find(range_hi);
                session.Notify(NormController::RX_OBJECT_ABORTED, sender, obj);
                sender->DeleteObject(obj);
            }
        }
    }
    range_max = rangeMax;
}  // end NormObjectTable::SetRangeMax()

#ifdef USE_PROTO_TREE

NormObject* NormObjectTable::Find(const NormObjectId& objectId) const
{
    if ((0 == range) || (objectId < range_lo) || (objectId > range_hi))
        return NULL;    
    else        
        return tree.Find(objectId.GetValuePtr(), 8*sizeof(UINT16));
}  // end NormObjectTable::Find()

void NormObjectTable::Destroy()
{
    NormObject* obj;
    while(NULL != (obj = Find(range_lo)))
    {
        // TBD - should we issue PURGED/ABORTED notifications here???
        // (We haven't since this is destroyed only when session is terminated)
        // or when a NormSenderNode is deleted
        Remove(obj);
        obj->Release();
    }
    count = range = range_max = 0;
}  // end NormObjectTable::Destroy()

#else
NormObject* NormObjectTable::Find(const NormObjectId& objectId) const
{
    if (0 != range)
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

void NormObjectTable::Destroy()
{
    if (NULL != table)
    {
        NormObject* obj;
        while((obj = Find(range_lo)))
        {
            // TBD - should we issue PURGED/ABORTED notifications here???
            // (We haven't since this is destroyed only when session is terminated)
            // or when a NormSenderNode is deleted
            Remove(obj);
            obj->Release();
        }
        delete[] table;
        table = (NormObject**)NULL;
        count = range = range_max = 0;
    }  
}  // end NormObjectTable::Destroy()

#endif // if/else USE_PROTO_TREE

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
    if (0 == range)
    {
        range_lo = range_hi = objectId;
        range = 1;   
    }
    else if (objectId < range_lo)
    {
        UINT16 newRange = range_lo - objectId + range;
        if (newRange > range_max) return false;
        range_lo = objectId;
        ASSERT(range_lo <= range_hi);
        range = newRange;
    }
    else if (objectId > range_hi)
    {            
        UINT16 newRange = objectId - range_hi + range;
        if (newRange > range_max) return false;
        range_hi = objectId;
        ASSERT(range_lo <= range_hi);
        range = newRange;
    }
    
#ifdef USE_PROTO_TREE
    ASSERT(NULL == Find(theObject->GetId()));
    tree.Insert(*theObject);
#else
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
    ASSERT(((NULL != entry) ? (objectId != entry->GetId()) : true));
    theObject->next = entry;
#endif  // if/else USE_PROTO_TREE
    count++;
    size = size + theObject->GetSize();
    theObject->Retain();
    return true;
}  // end NormObjectTable::Insert()

#ifdef USE_PROTO_TREE
bool NormObjectTable::Remove(NormObject* theObject)
{
    ASSERT(NULL != theObject);
    const NormObjectId& objectId = theObject->GetId();
    if (range)
    {
        if ((objectId < range_lo) || (objectId > range_hi)) return false;
        if (range > 1)
        {
            if (objectId == range_lo)
            {
                const NormObject* next = static_cast<const NormObject*>(theObject->GetNext());
                if (NULL == next) next = static_cast<const NormObject*>(tree.GetHead());
                ASSERT(NULL != next);
                range_lo = next->GetId();
                ASSERT(range_lo <= range_hi);
                range = range_hi - range_lo + 1;
            }
            else if (objectId == range_hi)
            {
                const NormObject* prev = static_cast<const NormObject*>(theObject->GetPrev());
                if (NULL == prev) prev = static_cast<const NormObject*>(tree.GetTail());
                ASSERT(NULL != prev);
                range_hi = prev->GetId();
                ASSERT(range_lo <= range_hi);
                range = range_hi - range_lo + 1;
            }
        }
        else
        {
            range = 0;
        }
        ASSERT(NULL != tree.Find(theObject->GetId().GetValuePtr(), 8*sizeof(UINT16)));
        tree.Remove(*theObject);
        count--;
        size = size - theObject->GetSize();
        theObject->Release();
        return true;
    }
    else
    {
        return false;
    }
}  // end NormObjectTable::Remove()
#else
bool NormObjectTable::Remove(NormObject* theObject)
{
    ASSERT(NULL != theObject);
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
                NormObjectId prevId = range_lo;
                do
                {
                    --i &= hash_mask;
                    offset++;
                    if ((entry = table[i]))
                    {
                        NormObjectId id = (UINT16)objectId - offset;
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
        theObject->Release();
        return true;
    }
    else
    {
        return false;
    }
}  // end NormObjectTable::Remove()
#endif // if/else USE_PROTO_TREE

#ifdef USE_PROTO_TREE
NormObjectTable::Iterator::Iterator(NormObjectTable& objectTable)
 : table(objectTable), iterator(objectTable.tree, false, objectTable.range_lo.GetValuePtr(), 8*sizeof(UINT16))
{
    next_object = iterator.GetNextItem();
    ASSERT((NULL == next_object) || (objectTable.range_lo == next_object->GetId()))
}

void NormObjectTable::Iterator::Reset()
{
    iterator.Reset(false, table.range_lo.GetValuePtr(), 8*sizeof(UINT16));
    next_object = iterator.GetNextItem();
    ASSERT(table.IsEmpty() || (NULL != next_object));
    ASSERT((NULL == next_object) || (table.range_lo == next_object->GetId()))
}

NormObject* NormObjectTable::Iterator::GetNextObject()
{
    NormObject* nextObj = next_object;
    if (NULL != nextObj)
    {
        next_object = iterator.GetNextItem();
        if (NULL == next_object)
        {
            iterator.Reset();
            next_object = iterator.GetNextItem();
            if (next_object->GetId() <= nextObj->GetId())
                next_object = NULL;
        }
        else if (next_object->GetId() <= nextObj->GetId())
        {
            next_object = NULL;
        }
    }
    return nextObj;
}  // end NormObjectTable::Iterator::GetNextObject()

NormObject* NormObjectTable::Iterator::GetPrevObject()
{
    if (!iterator.IsReversed())
    {
        iterator.Reset(true, table.range_hi.GetValuePtr(), 8*sizeof(UINT16));
        next_object = iterator.GetPrevItem();
    }
    NormObject* prevObj = next_object;
    if (NULL != prevObj)
    {
        next_object = iterator.GetPrevItem();
        if (NULL == next_object)
        {
            iterator.Reset(true);
            next_object = iterator.GetPrevItem();
            if (next_object->GetId() >= prevObj->GetId())
                next_object = NULL;
        }
        else if (next_object->GetId() >= prevObj->GetId())
        {
            next_object = NULL;
        }
    }
    return prevObj;
}  // end NormObjectTable::Iterator::GetPrevObject()
        
        
#else
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

#endif  // if/else USE_PROTO_TREE
