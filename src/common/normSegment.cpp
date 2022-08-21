#include "normSegment.h"

NormSegmentPool::NormSegmentPool()
 : seg_size(0), seg_count(0), seg_total(0), seg_list(NULL), seg_pool(NULL),
   peak_usage(0), overruns(0), overrun_flag(false)
{
}

NormSegmentPool::~NormSegmentPool()
{
    Destroy();
}

bool NormSegmentPool::Init(unsigned int count, unsigned int size)
{
    if (seg_pool) Destroy();
    peak_usage = 0;
    overruns = 0;        
#ifdef SIMULATE
    // In simulations, don't really need big vectors for data
    // since we don't actually read/write real data (for the most part)
    size = MIN(size, SIM_PAYLOAD_MAX);
#endif  // SIMULATE
    // This makes sure we get appropriate alignment
    unsigned int allocSize = size / sizeof(char*);
    if ((allocSize*sizeof(char*)) < size) allocSize++;
    seg_size = allocSize * sizeof(char*);
	seg_pool = new char*[allocSize * count];
	if (seg_pool)
	{
		char** ptr = seg_pool;
		for (unsigned int i = 0; i < count; i++)
		{
			*ptr = seg_list;
			seg_list = (char*)ptr;
			ptr += allocSize;
		}
	}
	else
	{
		PLOG(PL_FATAL, "NormSegmentPool::Init() memory allocation error: %s\n",
				GetErrorString());
		Destroy();
		return false;
	}
	seg_total = seg_count = count;
	return true;
}  // end NormSegmentPool::Init()

void NormSegmentPool::Destroy()
{
    ASSERT(seg_count == seg_total);
	if (NULL != seg_pool)
        delete[] seg_pool;
	seg_pool = NULL;
	seg_list = NULL;
	seg_count = 0;
	seg_total = 0;
	seg_size = 0;
}  // end NormSegmentPool::Destroy()

char* NormSegmentPool::Get()
{
    char* ptr = seg_list;
    if (ptr)
    {
        //memcpy(&seg_list, ptr, sizeof(char*));
        seg_list = *((char**)((void*)ptr));
        seg_count--;
//#ifdef NORM_DEBUG
        overrun_flag = false;
        unsigned int usage = seg_total - seg_count;
        if (usage > peak_usage) peak_usage = usage;
    }
    else
    {
        if (!overrun_flag)
        {
            PLOG(PL_WARN, "NormSegmentPool::Get() warning: operating with constrained buffering resources\n");
            overruns++; 
            overrun_flag = true;
        } 
//#endif // NORM_DEBUG 
    }
    return ptr;
}  // end NormSegmentPool::GetSegment()


////////////////////////////////////////////////////////////
// NormBlock Implementation

NormBlock::NormBlock()
 : size(0), segment_table(NULL), erasure_count(0), parity_count(0), next(NULL)
{
}     

NormBlock::~NormBlock()
{
    Destroy();
}

bool NormBlock::Init(UINT16 totalSize)
{
    if (segment_table) Destroy();
    if (!(segment_table = new char*[totalSize]))
    {
        PLOG(PL_FATAL, "NormBlock::Init() segment_table allocation error: %s\n", GetErrorString());
        return false;   
    }
    memset(segment_table, 0, totalSize*sizeof(char*));
    if (!pending_mask.Init(totalSize))
    {
        PLOG(PL_FATAL, "NormBlock::Init() pending_mask allocation error: %s\n", GetErrorString());
        Destroy();
        return false;   
    }
    if (!repair_mask.Init(totalSize))
    {
        PLOG(PL_FATAL, "NormBlock::Init() repair_mask allocation error: %s\n", GetErrorString());
        Destroy();
        return false;   
    }
    size = totalSize;
    erasure_count = 0;
    parity_count = 0;
    parity_offset = 0;
    seg_size_max = 0;
    return true;
}  // end NormBlock::Init()

void NormBlock::Destroy()
{
    repair_mask.Destroy();
    pending_mask.Destroy();
    // (TBD) Option to return segments to pool from which they came
    if (segment_table)
    {
        for (unsigned int i = 0; i < size; i++)
        {
            ASSERT(!segment_table[i]);
            if (segment_table[i]) delete []segment_table[i];
        }
        delete []segment_table;
        segment_table = (char**)NULL;
    }
    erasure_count = parity_count = size = 0;
}  // end NormBlock::Destroy()

void NormBlock::EmptyToPool(NormSegmentPool& segmentPool)
{
    ASSERT(NULL != segment_table);
    for (unsigned int i = 0; i < size; i++)
    {
        if (NULL != segment_table[i]) 
        {
            segmentPool.Put(segment_table[i]);
            segment_table[i] = (char*)NULL;
        }
    }
}  // end NormBlock::EmptyToPool()

/*
bool NormBlock::IsEmpty() const
{
    ASSERT(segment_table);
    for (unsigned int i = 0; i < size; i++)
        if (NULL != segment_table[i]) return false;
    return true;
}  // end NormBlock::IsEmpty()
*/
        
// Used by receiver side to determine if NACK should be sent
// Note: This invalidates the block's "repair_mask" state
bool NormBlock::IsRepairPending(UINT16 numData, UINT16 numParity)
{
    // Receivers ask for a block of parity to fulfill their
    // repair needs (erasure_count), but if there isn't 
    // enough parity, they ask for some data segments, too
    
    // This first section of code presets bits in the repair_mask
    // for those segments we don't care about.  We care about the
    // parity we need (erasure_count) and any explicit segments
    // required when our erasure_count exceeds numParity
    // The XCopy() below then determines if there is any residual
    // repair need (remember repair_mask has overheard repair
    // state already set)
    if (erasure_count > numParity)
    {
        if (numParity)
        {
            UINT16 i = numParity;
            NormSegmentId nextId = 0;
            GetFirstPending(nextId);
            while (i--)
            {
                // (TBD) for more NACK suppression, we could skip ahead
                // if this bit is already set in repair_mask?
                repair_mask.Set(nextId);  // set bit a parity can fill
                nextId++;
                GetNextPending(nextId);  
            } 
        }
        else if (size > numData)
        {
            repair_mask.SetBits(numData, size-numData);   
        }  
    }
    else
    {
        repair_mask.SetBits(0, numData);
        repair_mask.SetBits(numData+erasure_count, numParity-erasure_count);
    }
    // Calculate repair_mask = pending_mask - repair_mask 
    repair_mask.XCopy(pending_mask);
    return (repair_mask.IsSet());
}  // end NormBlock::IsRepairPending()

// Called by sender
bool NormBlock::TxReset(UINT16 numData, 
                        UINT16 numParity, 
                        UINT16 autoParity, 
                        UINT16 segmentSize)
{
    bool increasedRepair = false;
    repair_mask.SetBits(0, numData+autoParity);
    repair_mask.UnsetBits(numData+autoParity, numParity-autoParity);
    repair_mask.Xor(pending_mask);
    if (repair_mask.IsSet()) 
    {
        increasedRepair = true;
        repair_mask.Clear();
        pending_mask.SetBits(0, numData+autoParity);
        pending_mask.UnsetBits(numData+autoParity, numParity-autoParity);
        parity_offset = autoParity;  // reset parity since we're resending this one
        parity_count = numParity;    // no parity repair this repair cycle
        SetFlag(IN_REPAIR);
        if (!ParityReady(numData))  // (TBD) only when incrementalParity == true
        {
            // Clear _any_ existing incremental parity state
            char** ptr = segment_table+numData;
            while (numParity--)
            {
                if (*ptr) 
                {
                    UINT16 payloadMax = segmentSize + 
                                        NormDataMsg::GetStreamPayloadHeaderLength();
#ifdef SIMULATE
                    payloadMax = MIN(payloadMax, SIM_PAYLOAD_MAX);
#endif // SIMULATE
                    memset(*ptr, 0, payloadMax);
                }
                ptr++;
            }
            erasure_count = 0;
            seg_size_max = 0;
        }
    }
    return increasedRepair;
}  // end NormBlock::TxReset()

bool NormBlock::ActivateRepairs(UINT16 numParity)
{
    if (repair_mask.IsSet())
    {
        pending_mask.Add(repair_mask);
        ASSERT(pending_mask.IsSet());
        repair_mask.Clear(); 
        SetFlag(IN_REPAIR);
        return true;
    }
    else
    {
        return false;
    }
}  // end NormBlock::ActivateRepairs()

// For NACKs arriving during sender repair_timer "holdoff" time
// (we directly update the "pending_mask" for blocks/segments
//  greater than our current transmit index)
bool NormBlock::TxUpdate(NormSegmentId nextId, NormSegmentId lastId,
                         UINT16 numData, UINT16 numParity, UINT16 erasureCount)
{
    bool increasedRepair = false;
    if (nextId < numData)
    {
        // Explicit data repair request
        parity_offset = parity_count = numParity;
        while (nextId <= lastId)
        {
            if (!pending_mask.Test(nextId))
            {
                pending_mask.Set(nextId);
                increasedRepair = true;
            }
            nextId++;      
        }
    }
    else
    {
        // parity repair request
        UINT16 parityAvailable = numParity - parity_offset;
        if (erasureCount <= parityAvailable)
        {
           // Use fresh parity for repair
           if (erasureCount > parity_count)
           {
               pending_mask.SetBits(numData+parity_offset+parity_count, 
                                    erasureCount - parity_count);
               parity_count = erasureCount;
               increasedRepair = true; 
           }
        }
        else
        {
            // TBD - double-check this ... not sure this is exactly right
            //       (may need to always do explicit repair here?)
            // Use any remaining fresh parity ...
            if (parity_count < parityAvailable)
            {
                UINT16 count = parityAvailable - parity_count;
                pending_mask.SetBits(numData+parity_offset+parity_count, count); 
                parity_count = parityAvailable;  
                nextId += parityAvailable;
                increasedRepair = true;
            }
            // and explicit repair for the rest
            while (nextId <= lastId)
            {
                if (!pending_mask.Test(nextId))
                {
                    pending_mask.Set(nextId);
                    increasedRepair = true;
                }
                nextId++; 
            } 
        }   
    }
    return increasedRepair;
}  // end NormBlock::TxUpdate()

bool NormBlock::HandleSegmentRequest(NormSegmentId nextId, NormSegmentId lastId,
                                     UINT16 numData, UINT16 numParity, UINT16 erasureCount)
{
    PLOG(PL_TRACE, "NormBlock::HandleSegmentRequest() blk>%lu seg>%hu:%hu erasures:%hu\n",
                   (unsigned long)blk_id.GetValue(), (UINT16)nextId, (UINT16)lastId, erasureCount);
    bool increasedRepair = false;
    if (nextId < numData)
    {
        // Explicit data repair request
        parity_count = parity_offset = numParity;
        while (nextId <= lastId)
        {
            if (!repair_mask.Test(nextId))
            {
                repair_mask.Set(nextId);
                increasedRepair = true;
            }
            nextId++; 
        }   
    }
    else
    {
        // parity repair request
        UINT16 parityAvailable = numParity - parity_offset;
        if (erasureCount <= parityAvailable)
        {
           // Use fresh parity for repair
           if (erasureCount > parity_count)
           {
               repair_mask.SetBits(numData+parity_offset+parity_count, 
                                   erasureCount - parity_count);
               parity_count = erasureCount;
               increasedRepair = true; 
           }
        }
        else
        {
            // TBD - double-check this. It may not be exactly right
            //     (may need to alwayds do explicitr repair here)
            // Use any remaining fresh parity ...
            if (parity_count < parityAvailable)
            {
                UINT16 count = parityAvailable - parity_count;
                repair_mask.SetBits(numData+parity_offset+parity_count, count); 
                parity_count = parityAvailable;  
                nextId += parityAvailable;
                increasedRepair = true;
            }
            // and explicit repair for the rest
            while (nextId <= lastId)
            {
                if (!repair_mask.Test(nextId))
                {
                    repair_mask.Set(nextId);
                    increasedRepair = true;
                }
                nextId++; 
            } 
        }   
    }
    return increasedRepair;
}  // end NormBlock::HandleSegmentRequest()

// (TBD) this should return true if something is appended, false otherwise
bool NormBlock::AppendRepairAdv(NormCmdRepairAdvMsg& cmd, 
                                NormObjectId         objectId,
                                bool                 repairInfo,
                                UINT8                fecId,
                                UINT8                fecM,
                                UINT16               numData,
                                UINT16               payloadMax)
{
    bool requestAppended = false;
    NormRepairRequest req;
    req.SetFlag(NormRepairRequest::SEGMENT);
    if (repairInfo) req.SetFlag(NormRepairRequest::INFO);
    NormSymbolId nextId = 0;
    if (GetFirstRepair(nextId))
    {
        UINT16 totalSize = size;
        NormRepairRequest::Form prevForm = NormRepairRequest::INVALID;
        UINT16 segmentCount = 0;
        UINT16 firstId = 0;
        while (nextId < totalSize)
        {
            UINT16 currentId = nextId;
            if (!GetNextRepair(++nextId)) nextId = totalSize;
            if (!segmentCount) firstId = currentId;
            segmentCount++;
            // Check for break in consecutive series or end
            if (((nextId  - currentId) > 1) || (nextId >= totalSize))
            {
                NormRepairRequest::Form form;
                switch (segmentCount)
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
                            prevForm = NormRepairRequest::INVALID;
                            PLOG(PL_WARN, "NormBlock::AppendRepairAdv() warning: full msg\n");
                            break;
                        }
                        requestAppended = true;
                    }
                    req.SetForm(form);
                    cmd.AttachRepairRequest(req, payloadMax); // (TBD) error check
                    prevForm = form;
                }            
                switch(form)
                {
                    case NormRepairRequest::INVALID:
                        ASSERT(0);  // can't happen
                        break;
                    case NormRepairRequest::ITEMS:
                        req.AppendRepairItem(fecId, fecM, objectId, blk_id, numData, firstId);
                        if (2 == segmentCount) 
                            req.AppendRepairItem(fecId, fecM, objectId, blk_id, numData, currentId);
                        break;
                    case NormRepairRequest::RANGES:
                        req.AppendRepairRange(fecId, fecM, objectId, blk_id, numData, firstId,
                                              objectId, blk_id, numData, currentId);
                        break;
                    case NormRepairRequest::ERASURES:
                        // erasure counts not used
                        break;
                } 
                segmentCount = 0;  
            }
        }  // end while (nextId < totalSize)
        if (NormRepairRequest::INVALID != prevForm) 
        {
            if (0 == cmd.PackRepairRequest(req))
                PLOG(PL_WARN, "NormBlock::AppendRepairAdv() warning: full msg\n");
            else
                requestAppended = true;
        }
    }
    return requestAppended;
}  // end NormBlock::AppendRepairAdv()

NormObjectSize NormBlock::GetBytesPending(UINT16      numData,
                                          UINT16      segmentSize,
                                          NormBlockId finalBlockId,
                                          UINT16      finalSegmentSize) const
{
    NormObjectSize pendingBytes(0);
    NormSegmentId nextId;
    if (GetFirstPending(nextId))
    {
        do
        {
            if (nextId < numData)
                pendingBytes += NormObjectSize(segmentSize);
            else
                break;
            nextId++;
        } while (GetNextPending(nextId));        
    }
    // Correct for final_segment_size, if applicable
    if ((blk_id == finalBlockId) && IsPending(numData - 1))
    {
        pendingBytes -= NormObjectSize(segmentSize);
        pendingBytes += NormObjectSize(finalSegmentSize);  
    }
    return pendingBytes;
}  // end NormBlock::GetBytesPending()

// Called by receiver
// (TBD) this should return true iff something appended, false otherwise
bool NormBlock::AppendRepairRequest(NormNackMsg&    nack, 
                                    UINT8           fecId,
                                    UINT8           fecM,
                                    UINT16          numData, 
                                    UINT16          numParity,
                                    NormObjectId    objectId,
                                    bool            pendingInfo,
                                    UINT16          payloadMax)
{
    bool requestAppended = false;
    NormSegmentId nextId = 0;
    NormSegmentId endId;
    if (erasure_count > numParity)
    {
        // Request explicit repair 
        GetFirstPending(nextId);
        UINT16 i = numParity;
        // Skip numParity missing data segments
        while (i--)
        {
            nextId++;
            GetNextPending(nextId);
        }
        endId = numData + numParity;
    }
    else
    {
        nextId = numData;
        GetNextPending(nextId);
        endId = numData + erasure_count;   
    }
    NormRepairRequest req;
    req.SetFlag(NormRepairRequest::SEGMENT);                  
    if (pendingInfo) req.SetFlag(NormRepairRequest::INFO);  
    NormRepairRequest::Form prevForm = NormRepairRequest::INVALID;
    UINT16 segmentCount = 0;
    // new code begins here  
    UINT16 firstId = 0;
    while (nextId < endId)
    {
        UINT16 currentId = nextId;
        if (!GetNextPending(++nextId)) nextId = endId;
        if (0 == segmentCount) firstId = currentId;
        segmentCount++;
        // Check for break in consecutive series or end
        if (((nextId  - currentId) > 1) || (nextId >= endId))
        {
            NormRepairRequest::Form form;
            switch (segmentCount)
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
                    if (0 == nack.PackRepairRequest(req))
                    {
                        prevForm = NormRepairRequest::INVALID;  // so we don't re-attempt pack
                        PLOG(PL_WARN, "NormBlock::AppendRepairRequest() warning: full NACK msg\n");
                        break;   
                    }
                    requestAppended = true;
                }
                nack.AttachRepairRequest(req, payloadMax);  // (TBD) error check
                req.SetForm(form);
                prevForm = form;
            }
            switch (form)
            {
                case NormRepairRequest::INVALID:
                    ASSERT(0);
                    break;
                case NormRepairRequest::ITEMS:
                    req.AppendRepairItem(fecId, fecM, objectId, blk_id, numData, firstId);       // (TBD) error check
                    if (2 == segmentCount)
                        req.AppendRepairItem(fecId, fecM, objectId, blk_id, numData, currentId); // (TBD) error check
                    break;
                case NormRepairRequest::RANGES:
                    req.AppendRepairRange(fecId, fecM, 
                                          objectId, blk_id, numData, firstId,       // (TBD) error check
                                          objectId, blk_id, numData, currentId);    // (TBD) error check
                    break;
                case NormRepairRequest::ERASURES:
                    // erasure counts not used
                    break;
            }  // end switch(form)
            segmentCount = 0;
        }       
    }  // end while (nextId < lastId)
    if (NormRepairRequest::INVALID != prevForm) 
    {
        if (0 == nack.PackRepairRequest(req))
            PLOG(PL_WARN, "NormBlock::AppendRepairRequest() warning: full NACK msg\n");
        else
            requestAppended = true;
    }
    return requestAppended;
}  // end NormBlock::AppendRepairRequest()
         
NormBlockPool::NormBlockPool()
 : head((NormBlock*)NULL), blk_total(0), blk_count(0), overruns(0), overrun_flag(false)
{
}

NormBlockPool::~NormBlockPool()
{
    Destroy();
}

bool NormBlockPool::Init(UINT32 numBlocks, UINT16 segsPerBlock)
{
    if (head) Destroy();
    for (UINT32 i = 0; i < numBlocks; i++)
    {
        NormBlock* b = new NormBlock();
        if (b)
        {
            if (!b->Init(segsPerBlock))
            {
                PLOG(PL_FATAL, "NormBlockPool::Init() block init error\n");
                delete b;
                Destroy();
                return false;   
            }  
            b->next = head;
            head = b;
            blk_count++;
            blk_total++;
        }
        else
        {
            PLOG(PL_FATAL, "NormBlockPool::Init() new block error\n");
            Destroy();
            return false; 
        } 
    }
    return true;
}  // end NormBlockPool::Init()

void NormBlockPool::Destroy()
{
    ASSERT(blk_total == blk_count);
    NormBlock* next;
    while ((next = head))
    {
        head = next->next;
        delete next;   
    }
    blk_count = blk_total = 0;
}  // end NormBlockPool::Destroy()

NormBlockBuffer::NormBlockBuffer()
#ifdef USE_PROTO_TREE
 :
#else
 : table((NormBlock**)NULL), 
#endif  // if/else USE_PROTO_TREE
   range_max(0), range(0), fec_block_mask(0)
{
}

NormBlockBuffer::~NormBlockBuffer()
{
    Destroy();
}

bool NormBlockBuffer::Init(unsigned long rangeMax, unsigned long tableSize, UINT32 fecBlockMask)
{
    Destroy();
    // Make sure tableSize is greater than 0 and 2^n
    //if (!rangeMax || !tableSize) 
    if (0 == tableSize)
    {
        PLOG(PL_FATAL, "NormBlockBuffer::Init() bad range(%lu) or tableSize(%lu)\n",
                        rangeMax, tableSize);
        return false;
    }
#ifndef USE_PROTO_TREE
    if (0 != (tableSize & 0x07)) tableSize = (tableSize >> 3) + 1;
    if (!(table = new NormBlock*[tableSize]))
    {
        PLOG(PL_FATAL, "NormBlockBuffer::Init() buffer allocation error: %s\n", GetErrorString());
        return false;         
    }
    memset(table, 0, tableSize*sizeof(char*));
    hash_mask = tableSize - 1;
#endif // !USE_PROTO_TREE
    range_max = rangeMax;
    range = 0;
    fec_block_mask = fecBlockMask;
    return true;
}  // end NormBlockBuffer::Init()

#ifdef USE_PROTO_TREE
void NormBlockBuffer::Destroy()
{
    NormBlock* block;
    while((block = Find(range_lo)))
    {
        PLOG(PL_ERROR, "NormBlockBuffer::Destroy() buffer not empty!?\n");
        Remove(block);
        delete block;   
    }
    range_max = range = 0;
}  // end NormBlockBuffer::Destroy()

NormBlock* NormBlockBuffer::Find(const NormBlockId& blockId) const
{
    if ((0 == range) || (Compare(blockId, range_lo) < 0) || (Compare(blockId, range_hi) > 0))
        return NULL;
    else
        return tree.Find(blockId.GetValuePtr(), 8*sizeof(UINT32));
}  // end NormBlockBuffer::Find()

#else

void NormBlockBuffer::Destroy()
{
    if (table)
    {
        NormBlock* block;
        while((block = Find(range_lo)))
        {
            PLOG(PL_ERROR, "NormBlockBuffer::Destroy() buffer not empty!?\n");
            Remove(block);
            delete block;   
        }
        delete []table;
        table = (NormBlock**)NULL;
    }  
    range_max = range = 0;  
}  // end NormBlockBuffer::Destroy()

NormBlock* NormBlockBuffer::Find(const NormBlockId& blockId) const
{
    if (range)
    {
        //if ((blockId < range_lo)  || (blockId > range_hi)) 
        if ((Compare(blockId, range_lo) < 0) || (Compare(blockId, range_hi) > 0))
            return (NormBlock*)NULL;
        NormBlock* theBlock = table[(blockId.GetValue()) & hash_mask];
        while ((NULL != theBlock) && (blockId != theBlock->GetId())) 
            theBlock = theBlock->next;
        return theBlock;
    }
    else
    {
        return (NormBlock*)NULL;
    }   
}  // end NormBlockBuffer::Find()

#endif // if/else USE_PROTO_TREE

NormBlockId NormBlockBuffer::RangeMin() const
{
    if (range_max > 1)
    {
        NormBlockId rangeMin = range_hi;
        Decrement(rangeMin, (UINT32)range_max - 1);
        return rangeMin;
    }
    else
    {
        return range_lo;
    }
}  // end NormBlockBuffer::RangeMin()

bool NormBlockBuffer::CanInsert(NormBlockId blockId) const
{
    if (0 != range)
    {
        // if (blockId < range_lo)
        if (Compare(blockId, range_lo) < 0)
        {
            if (((UINT32)Difference(range_lo, blockId) + range) > range_max)
                return false;
            else
                return true;
        }
        // else if (blockId > range_hi)
        else if (Compare(blockId, range_hi) > 0)
        {
            if (((UINT32)Difference(blockId, range_hi) + range) > range_max)
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
}  // end NormBlockBuffer::CanInsert()


bool NormBlockBuffer::Insert(NormBlock* theBlock)
{
    const NormBlockId& blockId = theBlock->GetId();
    if (0 == range)
    {
        range_lo = range_hi = blockId;
        range = 1;   
    }
    // else if (blockId < range_lo)
    else if (Compare(blockId, range_lo) < 0)
    {
        UINT32 newRange = (UINT32)Difference(range_lo, blockId) + (UINT32)range;
        if (newRange > range_max) return false;
        range_lo = blockId;
        range = newRange;
    }
    // else if (blockId > range_hi)
    else if (Compare(blockId, range_hi) > 0)
    {            
        UINT32 newRange = (UINT32)Difference(blockId, range_hi) + (UINT32)range;
        if (newRange > range_max) return false;
        range_hi = blockId;
        range = newRange;
    }
    ASSERT(Compare(range_hi, range_lo) >= 0);
    // else unchanged range
#ifdef USE_PROTO_TREE
    ASSERT(NULL == Find(theBlock->GetId()));
    tree.Insert(*theBlock);
#else
    UINT32 index = blockId.GetValue() & hash_mask;
    NormBlock* prev = NULL;
    NormBlock* entry = table[index];
    // while (entry && (entry->GetId() < blockId)) 
    while ((NULL != entry) && (Compare(entry->GetId(), blockId) < 0))
    {
        prev = entry;
        entry = entry->next;
    }  
    if (prev)
        prev->next = theBlock;
    else
        table[index] = theBlock;
    ASSERT((entry ? (blockId != entry->GetId()) : true));
    theBlock->next = entry;
#endif // if/else USE_PROTO_TREE
    return true;
}  // end NormBlockBuffer::Insert()

#ifdef USE_PROTO_TREE

bool NormBlockBuffer::Remove(NormBlock* theBlock)
{
    ASSERT(NULL != theBlock);
    const NormBlockId& blockId = theBlock->GetId();
    switch (range)
    {
        case 0:
            return false;  // empty NormBlockBuffer
        case 1:
            if (blockId != range_lo)
                return false;  // out-of-range
            range = 0;
            break;
        default:
            if ((Compare(blockId, range_lo) < 0) || (Compare(blockId, range_hi) > 0)) 
                return false;  // out-of-range
            if (blockId == range_lo)
            {
                const NormBlock* next = static_cast<const NormBlock*>(theBlock->GetNext());
                if (NULL == next) next = static_cast<const NormBlock*>(tree.GetHead());
                ASSERT(NULL != next);
                range_lo = next->GetId();
                range = Difference(range_hi, range_lo) + 1;
            }
            else if (blockId == range_hi)
            {
                const NormBlock* prev = static_cast<const NormBlock*>(theBlock->GetPrev());
                if (NULL == prev) prev = static_cast<const NormBlock*>(tree.GetTail());
                ASSERT(NULL != prev);
                range_hi = prev->GetId();
                range = Difference(range_hi, range_lo) + 1;
            }
            // else range unchanged
            break;
    }
    ASSERT(NULL != tree.Find(theBlock->GetId().GetValuePtr(), 8*sizeof(UINT32)));
    tree.Remove(*theBlock);
    return true;
}  // end NormBlockBuffer::Remove()

#else

bool NormBlockBuffer::Remove(NormBlock* theBlock)
{
    ASSERT(NULL != theBlock);
    if (range)
    {
        const NormBlockId& blockId = theBlock->GetId();
        // if ((blockId < range_lo) || (blockId > range_hi)) 
        if ((Compare(blockId, range_lo) < 0) || (Compare(blockId, range_hi) > 0))
            return false;
        UINT32 index = blockId.GetValue() & hash_mask;
        NormBlock* prev = NULL;
        NormBlock* entry = table[index];
        while (entry && (entry->GetId() != blockId))
        {
            prev = entry;
            entry = entry->next;
        }
        if (NULL == entry) return false;
        if (NULL != prev)
            prev->next = entry->next;
        else
            table[index] = entry->next;
        
        if (range > 1)
        {
            if (blockId == range_lo)
            {
                // Find next entry for new range_lo
                UINT32 i = index;
                UINT32 endex;
                if (range <= hash_mask)
                    endex = (index + range - 1) & hash_mask;
                else
                    endex = index;
                entry = NULL;
                UINT32 offset = 0;
                NormBlockId nextId = range_hi;
                do
                {
                    ++i &= hash_mask;
                    offset++;
                    if (NULL != (entry = table[i]))
                    {
                        // NormBlockId id = blockId + offset;
                        NormBlockId id = blockId;
                        Increment(id, offset);
                        while(entry && (entry->GetId() != id)) 
                        {
                            // if ((entry->GetId() > blockId) && (entry->GetId() < nextId)
                            if ((Compare(entry->GetId(), blockId) > 0) &&
                                (Compare(entry->GetId(), nextId) < 0))
                            { 
                                nextId = entry->GetId();
                            }
                            entry = entry->next;
                        }
                        if (NULL != entry) break;    
                    }
                } while (i != endex);
                if (NULL != entry)
                    range_lo = entry->GetId();
                else
                    range_lo = nextId;
                range = (UINT32)Difference(range_hi, range_lo) + 1; 
            }
            else if (blockId == range_hi)
            {
                // Find prev entry for new range_hi
                UINT32 i = index;
                UINT32 endex;
                if (range <= hash_mask)
                    endex = (index - range + 1) & hash_mask;
                else
                    endex = index;
                entry = NULL;
                UINT32 offset = 0;
                NormBlockId prevId = range_lo;
                do
                {
                    --i &= hash_mask;
                    offset++;
                    if ((entry = table[i]))
                    {
                        // NormBlockId id = blockId - offset;
                        NormBlockId id = blockId;
                        Decrement(id, offset);
                        while(entry && (entry->GetId() != id)) 
                        {
                            // if ((entry->GetId() < blockId) && (entry->GetId() > prevId)) 
                            if ((Compare(entry->GetId(), blockId) < 0) && 
                                (Compare(entry->GetId(), prevId) > 0))
                            {  
                                prevId = entry->GetId();
                            }
                            entry = entry->next;
                        }
                        if (NULL != entry) break;    
                    }
                } while (i != endex);
                if (NULL != entry)
                    range_hi = entry->GetId();
                else 
                    range_hi = prevId;
                range = (UINT32)Difference(range_hi, range_lo) + 1;
            } 
        }
        else
        {
            range = 0;
        }  
        return true;
    }
    else
    {
        return false;
    }
}  // end NormBlockBuffer::Remove()

#endif // if/else USE_PROTO_TREE


#ifdef USE_PROTO_TREE
NormBlockBuffer::Iterator::Iterator(NormBlockBuffer& blockBuffer)
 : buffer(blockBuffer), iterator(blockBuffer.tree, false, blockBuffer.range_lo.GetValuePtr(), 8*sizeof(UINT32))
{
    next_block = iterator.GetNextItem();
    ASSERT((NULL == next_block) || (blockBuffer.range_lo == next_block->GetId()))
}

void NormBlockBuffer::Iterator::Reset()
{
    iterator.Reset(false, buffer.range_lo.GetValuePtr(), 8*sizeof(UINT32));
    next_block = iterator.GetNextItem();
    ASSERT(buffer.IsEmpty() || (NULL != next_block));
    ASSERT((NULL == next_block) || (buffer.range_lo == next_block->GetId()))
}  // end NormBlockBuffer::Iterator::Reset()

NormBlock* NormBlockBuffer::Iterator::GetNextBlock()
{
    NormBlock* nextBlock = next_block;
    if (NULL != nextBlock)
    {
        next_block = iterator.GetNextItem();
        if (NULL == next_block)
        {
            iterator.Reset();
            next_block = iterator.GetNextItem();
            if (buffer.Compare(next_block->GetId(), nextBlock->GetId()) <= 0)
                next_block = NULL;
        }
        else if (buffer.Compare(next_block->GetId(), nextBlock->GetId()) <= 0)
        {
            next_block = NULL;
        }
    }
    return nextBlock;
}  // end NormBlockBuffer::Iterator::GetNextBlock()
    
#else
    
NormBlockBuffer::Iterator::Iterator(const NormBlockBuffer& blockBuffer)
 : buffer(blockBuffer), reset(true)
{
}

NormBlock* NormBlockBuffer::Iterator::GetNextBlock()
{
    if (reset)
    {
        if (buffer.range)
        {
            reset = false;
            index = buffer.range_lo;
            return buffer.Find(index);
        }
        else
        {
            return (NormBlock*)NULL;
        }
    }
    else
    {
        // if (buffer.range && (index < buffer.range_hi) &&  (index >= buffer.range_lo))
        if ((0 != buffer.range) &&
            (buffer.Compare(index, buffer.range_hi) < 0) &&
            (buffer.Compare(index, buffer.range_lo) >= 0))
        {
            // Find next entry _after_ current "index"
            UINT32 i = index.GetValue();;
            UINT32 endex;
            // if ((UINT32)(buffer.range_hi - index) <= buffer.hash_mask)
            if ((UINT32)buffer.Difference(buffer.range_hi, index) <= buffer.hash_mask)
                endex = buffer.range_hi.GetValue() & buffer.hash_mask;
            else
                endex = index.GetValue();
            UINT32 offset = 0;
            NormBlockId nextId = buffer.range_hi;
            do
            {
                ++i &= buffer.hash_mask;
                offset++;
                // NormBlockId id = (UINT32)index + offset;
                NormBlockId id = index;
                buffer.Increment(id, offset);
                ASSERT(i < 256);
                NormBlock* entry = buffer.table[i];
                while ((NULL != entry ) && (entry->GetId() != id)) 
                {
                    // if ((entry->GetId() > index) && (entry->GetId() < nextId))
                    if ((buffer.Compare(entry->GetId(), index) > 0) && 
                        (buffer.Compare(entry->GetId(), nextId) < 0))
                    {
                        nextId = entry->GetId();
                    }
                    entry = NormBlockBuffer::Next(entry);
                }
                if (entry)
                {
                    index = entry->GetId();
                    return entry;   
                } 
            } while (i != endex);
            // If we get here, use nextId value
            index = nextId;
            return buffer.Find(nextId);
        }
        else
        {
            return (NormBlock*)NULL;
        }
    }   
}  // end NormBlockBuffer::Iterator::GetNextBlock()

#endif  // if/else USE_PROTO_TREE
