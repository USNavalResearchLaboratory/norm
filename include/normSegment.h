#ifndef _NORM_SEGMENT
#define _NORM_SEGMENT

#include "normMessage.h"
#include "protoBitmask.h"

#define USE_PROTO_TREE 1  // for more better performing NormBlockBuffer?


// Norm uses preallocated (or dynamically allocated) pools of 
// segments (vectors) for different buffering purposes

class NormSegmentPool
{
    public:
        NormSegmentPool();
        ~NormSegmentPool();
        
        bool Init(unsigned int count, unsigned int size);
        void Destroy();        
        char* Get();
        void Put(char* segment)
        {
            ASSERT(seg_count < seg_total);
            *((char**)((void*)segment)) = seg_list;  // this might make a warning on Solaris
            seg_list = segment;
            seg_count++;
        }
        bool IsEmpty() const {return (NULL == seg_list);}
        
        unsigned int CurrentUsage() const 
            {return (seg_total - seg_count);}
        unsigned long PeakUsage() const {return peak_usage;}
        unsigned long OverunCount() const {return overruns;}
        unsigned int GetSegmentSize() {return seg_size;}
        
    private: 
        unsigned int    seg_size;
        unsigned int    seg_count;  
        unsigned int    seg_total;
        char*           seg_list;
		char**          seg_pool;
        
        unsigned long   peak_usage;
        unsigned long   overruns;
        bool            overrun_flag;
};  // end class NormSegmentPool

#ifdef USE_PROTO_TREE
class NormBlock : public ProtoSortedTree::Item
#else
class NormBlock
#endif // if/else USE_PROTO_TREE
{
    friend class NormBlockPool;
    friend class NormBlockBuffer;
    
    public:
        enum Flag 
        {
            IN_REPAIR    = 0x01
        };
            
        NormBlock();
        ~NormBlock();
        const NormBlockId& GetId() const {return blk_id;}
        void SetId(NormBlockId& x) {blk_id = x;}
        bool Init(UINT16 totalSize);
        void Destroy();   
        
        void SetFlag(NormBlock::Flag flag) {flags |= flag;}
        void ClearFlag(NormBlock::Flag flag) {flags &= ~flag;}
        bool InRepair() {return (0 != (flags & IN_REPAIR));}
        bool ParityReady(UINT16 ndata) {return (erasure_count == ndata);}
        UINT16 ParityReadiness() {return erasure_count;}
        void IncreaseParityReadiness() {erasure_count++;}
        void SetParityReadiness(UINT16 ndata) {erasure_count = ndata;}
        
        char** SegmentList(UINT16 index = 0) {return &segment_table[index];}
        char* GetSegment(NormSegmentId sid)
        {
            ASSERT(sid < size);
            return segment_table[sid];
        }
        void AttachSegment(NormSegmentId sid, char* segment)
        {
            ASSERT(sid < size);
            ASSERT(!segment_table[sid]);
            segment_table[sid] = segment;
        }    
        char* DetachSegment(NormSegmentId sid)
        {
            ASSERT(sid < size);
            char* segment = segment_table[sid];
            segment_table[sid] = (char*)NULL;
            return segment;
        }
        void SetSegment(NormSegmentId sid, char* segment)
        {
            ASSERT(sid < size);
            ASSERT(!segment_table[sid]);
            segment_table[sid] = segment;
        }    
        
        // Sender routines
        void TxInit(NormBlockId& blockId, UINT16 ndata, UINT16 autoParity)
        {
            blk_id = blockId;
            pending_mask.Clear();
            pending_mask.SetBits(0, ndata+autoParity);
            repair_mask.Clear();
            erasure_count = 0;
            parity_count = 0; 
            parity_offset = autoParity;  
            flags = 0;
            seg_size_max = 0;
            last_nack_time.GetCurrentTime();
        }
        void TxRecover(NormBlockId& blockId, UINT16 ndata, UINT16 nparity)
        {
            blk_id = blockId;
            pending_mask.Clear();
            repair_mask.Clear();
            erasure_count = 0;
            parity_count = nparity;  // force recovered blocks to 
            parity_offset = nparity; // explicit repair mode ???  
            flags = IN_REPAIR;
            seg_size_max = 0;
        }
        bool TxReset(UINT16 ndata, UINT16 nparity, UINT16 autoParity, 
                     UINT16 segmentSize);
        bool TxUpdate(NormSegmentId nextId, NormSegmentId lastId,
                      UINT16 ndata, UINT16 nparity, UINT16 erasureCount);
        void UpdateSegSizeMax(UINT16 segSize)
            {seg_size_max = (segSize > seg_size_max) ? segSize : seg_size_max;}
        UINT16 GetSegSizeMax() {return seg_size_max;}
        
        bool HandleSegmentRequest(NormSegmentId nextId, NormSegmentId lastId,
                                  UINT16 ndata, UINT16 nparity, 
                                  UINT16 erasureCount);
        bool ActivateRepairs(UINT16 nparity);
        void ResetParityCount(UINT16 nparity) 
        {
            parity_offset += parity_count;
            parity_offset = MIN(parity_offset, nparity);
            parity_count = 0;
        }
        bool AppendRepairAdv(NormCmdRepairAdvMsg& cmd, 
                             NormObjectId         objectId,
                             bool                 repairInfo,
                             UINT8                fecId,
                             UINT8                fecM,
                             UINT16               numData,
                             UINT16               payloadMax);
        
        // Receiver routines
        void RxInit(NormBlockId& blockId, UINT16 ndata, UINT16 nparity)
        {
            blk_id = blockId;
            pending_mask.Clear();
            pending_mask.SetBits(0, ndata+nparity);
            repair_mask.Clear();
            erasure_count = ndata;
            parity_count = 0;
            parity_offset = 0;
            flags = 0;
        }
        // Note: This invalidates the repair_mask state.
        bool IsRepairPending(UINT16 ndata, UINT16 nparity); 
        void DecrementErasureCount() {erasure_count--;}
        void IncrementErasureCount() {erasure_count++;}
        UINT16 ErasureCount() const {return erasure_count;}
        void IncrementParityCount() {parity_count++;}
        UINT16 ParityCount() const {return parity_count;}
        
        bool GetFirstPending(NormSymbolId& symbolId) const
        {
            UINT32 index;
            bool result = pending_mask.GetFirstSet(index);
            symbolId = (UINT16)index;
            return result;
        }
        bool GetNextPending(NormSymbolId& symbolId) const
        {
            UINT32 index = (UINT32)symbolId;
            bool result = pending_mask.GetNextSet(index);
            symbolId = (UINT16)index;
            return result;
        }
        NormSymbolId GetFirstRepair(NormSymbolId& symbolId)  const
        {
            UINT32 index;
            bool result = repair_mask.GetFirstSet(index);
            symbolId = (UINT16)index;
            return result;
        }
        
        bool GetNextRepair(NormSymbolId& symbolId) const
        {
            UINT32 index = (UINT32)symbolId;
            bool result = repair_mask.GetNextSet(index);
            symbolId = (UINT16)index;
            return result;
        }
        
        bool SetPending(NormSymbolId s) 
            {return pending_mask.Set(s);}
        bool SetPending(NormSymbolId firstId, UINT16 count)
            {return pending_mask.SetBits(firstId, count);}
        void UnsetPending(NormSymbolId s) 
            {pending_mask.Unset(s);}
        void ClearPending()
            {pending_mask.Clear();}
        bool SetRepair(NormSymbolId s) 
            {return repair_mask.Set(s);}
        bool SetRepairs(NormSymbolId first, NormSymbolId last)
        {
            if (first == last)
                return repair_mask.Set(first);
            else
                return (repair_mask.SetBits(first, last-first+1));   
        }
        void UnsetRepair(NormSymbolId s)
            {repair_mask.Unset(s);}
        void ClearRepairs()
            {repair_mask.Clear();}
        bool IsPending(NormSymbolId s) const
            {return pending_mask.Test(s);}
        bool IsPending() const
            {return pending_mask.IsSet();}   
        bool IsRepairPending() const
            {return repair_mask.IsSet();}
        bool IsTransmitPending() const
            {return (pending_mask.IsSet() || repair_mask.IsSet());}
        
        NormObjectSize GetBytesPending(UINT16      numData,
                                       UINT16      segmentSize,
                                       NormBlockId finalBlockId,
                                       UINT16      finalSegmentSize) const;
        
        bool AppendRepairRequest(NormNackMsg&   nack, 
                                 UINT8          fecId,
                                 UINT8          fecM,
                                 UINT16         numData, 
                                 UINT16         numParity,
                                 NormObjectId   objectId,
                                 bool           pendingInfo,
                                 UINT16         payloadMax);
        
        
        void SetLastNackTime(const ProtoTime& theTime)
            {last_nack_time = theTime;}
        const ProtoTime& GetLastNackTime() const
            {return last_nack_time;}
        double GetNackAge() const
            {return ProtoTime::Delta(ProtoTime().GetCurrentTime(), last_nack_time);}
        
        //void DisplayPendingMask(FILE* f) {pending_mask.Display(f);}
        
        //bool IsEmpty() const;
        void EmptyToPool(NormSegmentPool& segmentPool);
            
    private:
#ifdef USE_PROTO_TREE
        const char* GetKey() const
            {return blk_id.GetValuePtr();}
        unsigned int GetKeysize() const
            {return (8*sizeof(UINT32));} 
        ProtoTree::Endian GetEndian() const
            {return ProtoTree::GetNativeEndian();}    
#endif  // USE_PROTO_TREE
            
        NormBlockId  blk_id;
        UINT16       size;
        char**       segment_table;
        
        int          flags;
        UINT16       erasure_count;
        UINT16       parity_count;  // how many fresh parity we are currently planning to send
        UINT16       parity_offset; // offset from where our fresh parity will be sent
        UINT16       seg_size_max;
        
        ProtoBitmask pending_mask;
        ProtoBitmask repair_mask;
        ProtoTime    last_nack_time;  // for stream flow control
        NormBlock*   next;            // used for NormBlockPool
};  // end class NormBlock

class NormBlockPool
{
    public:
        NormBlockPool();
        ~NormBlockPool();
        bool Init(UINT32 numBlocks, UINT16 totalSize);
        void Destroy();
        bool IsEmpty() const {return (NULL == head);}
        NormBlock* Get()
        {
            NormBlock* b = head;
            head = b ? b->next : NULL;
            if (b) 
            {
                blk_count--;
                overrun_flag = false;
            }
            else if (!overrun_flag)
            {
                PLOG(PL_DEBUG, "NormBlockPool::Get() warning: operating with constrained buffering resources\n");
                overruns++;
                overrun_flag = true;   
            }
            return b;
        }
        void Put(NormBlock* b)
        {
            b->next = head;
            head = b;
            blk_count++;
        }
        unsigned long OverrunCount() const {return overruns;}
        UINT32 GetCount() {return blk_count;}
        UINT32 GetTotal() {return blk_total;}
        
    private:
        NormBlock*      head;
        UINT32          blk_total;
        UINT32          blk_count;
        unsigned long   overruns;
        bool            overrun_flag;
};  // end class NormBlockPool

#ifdef USE_PROTO_TREE
class NormBlockTree : public ProtoSortedTreeTemplate<NormBlock> {};
#endif // USE_PROTO_TREE

class NormBlockBuffer
{
    public:
        class Iterator;
        friend class NormBlockBuffer::Iterator;
            
        NormBlockBuffer();
        ~NormBlockBuffer();
        bool Init(unsigned long rangeMax, unsigned long tableSize, UINT32 fecBlockMask);
        void Destroy();
        
        bool Insert(NormBlock* theBlock);
        bool Remove(NormBlock* theBlock);
        NormBlock* Find(const NormBlockId& blockId) const;
        
        NormBlockId RangeLo() const {return range_lo;}
        NormBlockId RangeHi() const {return range_hi;}
        NormBlockId RangeMin() const;
        bool IsEmpty() const {return (0 == range);}
        bool CanInsert(NormBlockId blockId) const;

#ifdef USE_PROTO_TREE
        class Iterator
        {
            public:
                Iterator(NormBlockBuffer& blockBuffer);
                NormBlock* GetNextBlock();
                void Reset();
                
            private:
                NormBlockBuffer&           buffer;
                NormBlockTree::Iterator    iterator;
                NormBlock*                 next_block;
        }; 
        
#else        
        class Iterator
        {
            public:
                Iterator(const NormBlockBuffer& blockBuffer);
                NormBlock* GetNextBlock();
                void Reset() {reset = true;}
                
            private:
                const NormBlockBuffer&  buffer;
                bool                    reset;
                NormBlockId             index;
        }; 
#endif // if/else USE_PROTO_TREE            
    private:
        int Compare(NormBlockId a, NormBlockId b) const
            {return NormBlockId::Compare(a, b, fec_block_mask);}
        INT32 Difference(NormBlockId a, NormBlockId b) const
            {return NormBlockId::Difference(a, b, fec_block_mask);}
        void Increment(NormBlockId& b, UINT32 i = 1) const
            {b.Increment(i, fec_block_mask);}
        void Decrement(NormBlockId& b, UINT32 i = 1) const
            {b.Decrement(i, fec_block_mask);}

#ifdef USE_PROTO_TREE
        NormBlockTree   tree;
#else    
        static NormBlock* Next(NormBlock* b) {return b->next;}    
        NormBlock**     table;
        unsigned long   hash_mask; 
#endif // if/else USE_PROTO_TREE      
        unsigned long   range_max;  // max range of blocks that can be buffered
        unsigned long   range;      // zero if "block buffer" is empty
        UINT32          fec_block_mask;
        NormBlockId     range_lo;
        NormBlockId     range_hi;
};  // end class NormBlockBuffer

#endif // _NORM_SEGMENT
