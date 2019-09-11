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
        if (infoLen > 0) pending_info = true;
    }
    else
    {
        segmentSize = session->ServerSegmentSize();
        numData = session->ServerBlockSize();
        numParity = session->ServerNumParity();
        if (infoPtr)
        {
            if (info) delete []info;
            if (infoLen > segment_size)
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
        last_segment_id = numData;  // assumed for STREAM
    }
    else
    {
        last_block_id = numBlocks.LSB() - 1;  
        NormObjectSize size = NormObjectSize(numBlocks.LSB()) * blockSize;
        size = objectSize - size;
        size = size / NormObjectSize(segmentSize);
        ASSERT(!size.MSB());
        ASSERT(size.LSB() < numData);
        last_segment_id = size.LSB();
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
        {
            server->PutFreeBlock(block);
        }
        else
        {
            session->ServerPutFreeBlock(block);
        }
    }
    repair_mask.Destroy();
    pending_mask.Destroy();
    block_buffer.Destroy();
}  // end NormObject::Close();


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
                    next_segment_id = ndata;
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
        repair_info = true;
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
            next_segment_id = segmentId + 1;
            break;
        case THRU_SEGMENT:
            current_block_id = blockId;
            next_segment_id = segmentId + 1;
            break;
        case THRU_BLOCK:
            current_block_id = blockId;  
            next_segment_id = ndata;
            break;
        case THRU_OBJECT:
            current_block_id = last_block_id;
            next_segment_id = ndata;
        default:
            break; 
    }
    return needRepair;
}  // end NormObject::ClientRepairCheck()

bool NormObject::IsRepairPending(bool flush)
{
    if (!flush)
    {
        // Mask repair mask for region of interest 
    }
    // Calculate repair_mask = pending_mask - repair_mask 
    repair_mask.XCopy(pending_mask);
    if (repair_mask.IsSet())
    {
        NormBlockId lastId = repair_mask.LastSet();
        NormBlockId nextId = repair_mask.FirstSet();
        while (nextId <= lastId)
        {
            NormBlock* block = block_buffer.Find(nextId);
            if (block)
            {
                if (block->IsRepairPending(ndata)) return true;
            }
            else
            {
                // We need the whole thing
                return true;
            }
            nextId++;
            nextId = repair_mask.NextSet(nextId);
        }
    }
    else
    {
        return false;   
    }
    return false;
}  // end NormObject::IsRepairPending()

void NormObject::HandleObjectMessage(NormMessage& msg)
{
    if (NORM_MSG_INFO == msg.generic.GetType())
    {
        if (pending_info)
        {
            info_len = msg.info.GetInfoLen();
            if (info_len > segment_size)
            {
                info_len = segment_size;
                DMSG(0, "NormObject::HandleObjectMessage() node:%lu server:%lu obj:%hu "
                    "Warning! info too long.\n", LocalNodeId(), server->Id(), (UINT16)id);   
            }
            memcpy(info, msg.info.GetInfo(), info_len);
            pending_info = false;
        }
        else
        {
            // (TBD) Verify info hasn't changed?   
            DMSG(0, "NormObject::HandleObjectMessage() node:%lu server:%lu obj:%hu "
                    "received duplicate info ...\n", LocalNodeId(),
                     server->Id(), (UINT16)id);
        }
    }
    else  // NORM_MSG_DATA
    {
        NormBlockId blockId = msg.data.GetFecBlockId();
        // For stream objects, a little extra mgmt is required
        if (STREAM == type)
        {
            if (!StreamUpdateStatus(blockId))
            {
                DMSG(0, "NormObject::HandleObjectMessage() node:%lu server:%lu obj:%hu "
                        "broken stream ...\n", LocalNodeId(), server->Id(), 
                        (UINT16)id);  
                // (TBD) deal with broken stream
                return;            
            }
        }
        if (pending_mask.Test(blockId))
        {
            NormBlock* block = block_buffer.Find(blockId);
            if (!block)
            {
                if (!(block = server->GetFreeBlock(id, blockId)))
                {
                    DMSG(0, "NormObject::HandleObjectMessage() node:%lu server:%lu obj:%hu "
                            "Warning! no free blocks ...\n", LocalNodeId(), server->Id(), 
                            (UINT16)id);  
                    return;
                }
                block->RxInit(blockId, ndata);
                block_buffer.Insert(block);
            }
            NormSegmentId segmentId = msg.data.GetFecSymbolId();
            if (9 == segmentId) 
            {
                return;
            }
            if (block->IsPending(segmentId))
            {
                // 1) Store data in block buffer in case its needed for decoding
                char* segment = server->GetFreeSegment(id, blockId);
                if (!segment)
                {
                    DMSG(0, "NormObject::HandleObjectMessage() node:%lu server:%lu obj:%hu "
                            "Warning! no free segments ...\n", LocalNodeId(), server->Id(), 
                            (UINT16)id);  
                    return;
                }
                
                UINT16 segmentLen = msg.data.GetDataLen();
                if (segmentLen > segment_size)
                {
                    DMSG(0, "NormObject::HandleObjectMessage() node:%lu server:%lu obj:%hu "
                            "Error! segment too large ...\n", LocalNodeId(), server->Id(), 
                            (UINT16)id);  
                    server->PutFreeSegment(segment);
                    return;  
                }
                memcpy(segment, msg.data.GetPayload(), segmentLen + NormDataMsg::PayloadHeaderLen());
                block->AttachSegment(segmentId, segment);
                block->UnsetPending(segmentId);
                // 2) Write segment to object (if it's data)
                if (segmentId < ndata) 
                    WriteSegment(blockId, segmentId, segment);
                // 3) Decode block if ready and return to pool
                if (block->ErasureCount() <= nparity)
                {
                    // Decode (if pending_mask.FirstSet() < ndata)
                    // and write any decoded data segments to object
                    DMSG(0, "NormObject::HandleObjectMessage() node:%lu server:%lu obj:%hu "
                            "decoding block ...\n", LocalNodeId(), server->Id(), 
                            (UINT16)id); 
                    UINT16 nextErasure = block->FirstPending();
                    UINT16 erasureCount = 0;
                    UINT16 blockLen = ndata + nparity;
                    while (nextErasure < blockLen)
                    {
                        server->SetErasureLoc(erasureCount++, nextErasure);
                        if (nextErasure < ndata)
                        {
                            if (!(segment = server->GetFreeSegment(id, blockId)))
                            {
                                DMSG(0, "NormObject::HandleObjectMessage() node:%lu server:%lu obj:%hu "
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
                        server->Decode(block->SegmentList(), ndata, erasureCount); 
                        for (UINT16 i = 0; i < erasureCount; i++) 
                        {
                            NormSegmentId sid = server->GetErasureLoc(i);
                            if (sid < ndata)
                                WriteSegment(blockId, sid, block->Segment(sid));
                            else
                                break;
                        }
                    }
                    // OK, we're done with this block
                    pending_mask.Unset(blockId);
                    block_buffer.Remove(block);
                    server->PutFreeBlock(block);     
                }
                
                // (TBD) Notify application if new data available
                session->Notify(NormController::RX_OBJECT_UPDATE, server, this);
            }
            else
            {
                DMSG(0, "NormObject::HandleObjectMessage() node:%lu server:%lu obj:%hu "
                    "received duplicate segment ...\n", LocalNodeId(),
                     server->Id(), (UINT16)id);
            }
        }
        else
        {
            DMSG(6, "NormObject::HandleObjectMessage() node:%lu server:%lu obj:%hu "
                    "received duplicate block message ...\n", LocalNodeId(),
                     server->Id(), (UINT16)id);
        }
    }
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
                        pending_mask.SetBits(stream_next_id, 
                                blockId - stream_next_id + 1);
                        stream_next_id = blockId + 1;
                        // Handle potential sync block id wrap
                        NormBlockId delta = stream_next_id - stream_sync_id;
                        if (delta > NormBlockId(2*pending_mask.Size()))
                            stream_sync_id = NormBlockId(pending_mask.FirstSet());
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
    }
    return true;
}  // end NormObject::StreamUpdateStatus()

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
    if (!pending_mask.IsSet()) return false;
    
    
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
    msg->object.SetSegmentSize(segment_size);
    msg->object.SetObjectSize(object_size);
    msg->object.SetFecNumParity(nparity);
    msg->object.SetFecBlockLen(ndata);
    msg->object.SetObjectId(id);
            
            
    NormBlockId blockId = pending_mask.FirstSet();
    NormBlock* block = block_buffer.Find(blockId);
    if (!block)
    {
       if (!(block = session->ServerGetFreeBlock(id, blockId)))
       {
            DMSG(0, "NormObject::NextServerMsg() Warning! server resource " 
                    "constrained (no free blocks).\n");
            return false; 
       }
       // Load block with zero initialized parity segments
       UINT16 totalBlockLen = ndata + nparity;
       for (UINT16 i = ndata; i < totalBlockLen; i++)
       {
            char* s = session->ServerGetFreeSegment(id, blockId);
            if (s)
            {
                memset(s, 0, segment_size + NormDataMsg::PayloadHeaderLen());
                block->AttachSegment(i, s); 
            }
            else
            {
                DMSG(0, "NormObject::NextServerMsg() Warning! server resource " 
                    "constrained (no free segments).\n");
                session->ServerPutFreeBlock(block);
                return false;
            }
       }      
       
       block->TxInit(blockId, ndata, session->ServerAutoParity());  
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
    if (segmentId < ndata)
    {
        // Try to read data segment 
        NormObjectSize offset;
        char* dataPtr = msg->data.AccessData();
        UINT16 length = ReadSegment(blockId, segmentId, 
                                    &offset, dataPtr, segment_size);
        if (!length)
        {
            // (TBD) deal with read error (may be ok for streams) 
            return false;  
        }
        msg->data.SetDataOffset(offset);
        msg->data.SetDataLength(length);
        
        // Perform incremental FEC encoding as needed
        if (!block->ParityReady() && nparity)
        {
            // (TBD) for non-stream objects, catch alternate "last block/segment len"
            if (length < segment_size)
                memset(dataPtr+length, 0, segment_size - length);
            session->ServerEncode(msg->data.AccessPayload(), block->SegmentList(ndata)); 
            if (segmentId == (ndata-1)) block->SetFlag(NormBlock::PARITY_READY);     
        }
    }
    else
    {
        ASSERT(block->ParityReady());
        char* segment = block->Segment(segmentId);
        ASSERT(segment);
        msg->data.SetPayload(segment, segment_size+NormDataMsg::PayloadHeaderLen());    
    }
    block->UnsetPending(segmentId);       
    msg->generic.SetType(NORM_MSG_DATA);
    if (block->InRepair()) msg->object.SetFlag(NormObjectMsg::FLAG_REPAIR);
    msg->data.SetFecBlockId(blockId);
    msg->data.SetFecSymbolId(segmentId);
    if (!block->IsPending()) pending_mask.Unset(blockId);
 
    // This lets us continue stream objects indefinitely
    if (IsStream() && !pending_mask.IsSet()) 
    {
        pending_mask.Set(block_buffer.RangeHi()+1);
    }
        
    return true;
}  // end NormObject::NextServerMsg()

/////////////////////////////////////////////////////////////////
//
// NormObjectTable Implementation
//

/////////////////////////////////////////////////////////////////
//
// NormStreamObject Implementation
//

NormStreamObject::NormStreamObject(class NormSession*       theSession, 
                                   class NormServerNode*    theServer,
                                   const NormObjectId&      objectId)
 : NormObject(STREAM, theSession, theServer, objectId)
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
    segment_pool.Destroy();
    stream_buffer.Destroy();
    block_pool.Destroy(); 
}  // end NormStreamObject::Close()

UINT16 NormStreamObject::ReadSegment(NormBlockId blockId, NormSegmentId segmentId,
                                     NormObjectSize* offset, char* buffer, UINT16 maxlen)
{
    // (TBD) compare blockId with stream_buffer.RangeLo() and stream_buffer.RangeHi()
    NormBlock* block = stream_buffer.Find(blockId);
    if (!block)
    {
        //DMSG(0, "NormStreamObject::ReadSegment() stream starved (1)\n");
        return 0;   
    }
    if (!block->IsPending(segmentId))
    {
        //DMSG(0, "NormStreamObject::ReadSegment() stream starved (2)\n");
        return 0;   
    }   
    char* segment = block->Segment(segmentId);
    block->UnsetPending(segmentId);
    ASSERT(segment);
    UINT16 length = NormDataMsg::ReadLength(segment);
    *offset = NormDataMsg::ReadOffset(segment);
    ASSERT(length <= maxlen);
    memcpy(buffer, segment+NormDataMsg::PayloadHeaderLen(), length);
    return length;
}  // end NormStreamObject::Read()

bool NormStreamObject::WriteSegment(NormBlockId   blockId, 
                                    NormSegmentId segmentId, 
                                    const char*   segment)
{
    NormBlock* block = stream_buffer.Find(blockId);
    if (!block)
    {
        bool broken = false;
        // Prune (if necessary) stream_buffer (stream is broken)
        while (!stream_buffer.CanInsert(blockId) || block_pool.IsEmpty())
        {
            block = stream_buffer.Find(stream_buffer.RangeLo());
            stream_buffer.Remove(block);
            block->EmptyToPool(segment_pool);
            block_pool.Put(block);
            broken = true;
        }
        block = block_pool.Get();
        block->SetId(blockId);
        block->ClearPending();
        bool success = stream_buffer.Insert(block);
        ASSERT(success);
        if (broken)
        {
            NormBlock* first = stream_buffer.Find(stream_buffer.RangeLo());
            read_index.block = first->Id();
            read_index.segment = 0;   
        }
    }
    ASSERT(!block->Segment(segmentId));
    char* s = segment_pool.Get();
    ASSERT(s);
    UINT16 length = NormDataMsg::ReadLength(segment);
    memcpy(s, segment, length + NormDataMsg::PayloadHeaderLen());
    block->AttachSegment(segmentId, s);
    block->SetPending(segmentId);
    return true;
}  // end NormStreamObject::WriteSegment()



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
        ASSERT((segmentOffset < read_offset) || 
               (segmentOffset == read_offset));
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
                b->EmptyToPool(segment_pool);
                segment = segment_pool.Get();
                ASSERT(segment);
            }
            NormDataMsg::WriteOffset(segment, write_offset);  
            NormDataMsg::WriteLength(segment, 0);
            block->AttachSegment(write_index.segment, segment);    
        }
        
        UINT16 index = NormDataMsg::ReadLength(segment);
        ASSERT(write_offset == (NormDataMsg::ReadOffset(segment) + 
                                NormObjectSize(index)));
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
    if (nBytes) session->TouchServer();
    return nBytes;
}  // end NormStreamObject::Write()


NormObjectTable::NormObjectTable()
 : table((NormObject**)NULL), range_max(0), range(0)
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
        //printf("NormObjectTable::Find() table[%lu] = %p\n", ((UINT16)objectId) & hash_mask, theObject);
        while (theObject && (objectId != theObject->Id())) theObject = theObject->next;
        return theObject;
    }
    else
    {
        return (NormObject*)NULL;
    }   
}  // end NormObjectTable::Find()

bool NormObjectTable::Insert(NormObject* theObject)
{
    if (range < range_max)
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
        return true;        
    }
    else
    {
        return false;
    }
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
            table[index] = (NormObject*)NULL;
        if (range > 1)
        {
            if (objectId == range_lo)
            {
                // Find next entry for range_lo
                UINT16 i = index;
                UINT16 endex;
                if (range <= hash_mask)
                    endex = (index + range) & hash_mask;
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
                            if ((entry->Id() > objectId) && (entry->Id() < nextId))
                                nextId = entry->Id();
                            entry = entry->next;
                               
                        }
                        if (entry) break;    
                    }
                } while (i != endex);
                if (entry)
                {
                    range_lo = entry->Id();
                    range = range_hi - range_lo + 1;
                }
                else if (nextId != range_hi)
                {
                    range_lo = nextId;
                    range = range_hi - range_lo + 1;   
                }
                else
                {
                    range = 0;
                }
            }
            else if (objectId == range_hi)
            {
                // Find prev entry for range_hi
                UINT16 i = index;
                UINT16 endex;
                if (range <= hash_mask)
                    endex = (index - range) & hash_mask;
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
                            if ((entry->Id() < objectId) && (entry->Id() > prevId))
                                prevId = entry->Id();
                            entry = entry->next;
                        }
                        if (entry) break;    
                    }
                } while (i != endex);
                if (entry)
                {
                    range_hi = entry->Id();
                    
                }
                else if (prevId != range_lo)
                {
                    range_hi = prevId;
                    range = range_hi - range_lo + 1;
                }
                else
                {
                    range = 0;
                }
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
            !(index < table.range_lo))
        {
            // Find next entry _after_ current "index"
            UINT16 i = index;
            UINT16 endex = table.range_hi - index;
            if (endex <= table.hash_mask)
                endex = (index + endex) & table.hash_mask;
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
                while ((NULL != entry )& (entry->Id() != id)) 
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
            (index < table.range_hi) && 
            !(index < table.range_lo))
        {
            // Find next entry _after_ current "index"
            UINT16 i = index;
            UINT16 endex = index - table.range_lo;
            if (endex <= table.hash_mask)
                endex = ((UINT16)index - endex) & table.hash_mask;
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
                while ((NULL != entry )& (entry->Id() != id)) 
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
