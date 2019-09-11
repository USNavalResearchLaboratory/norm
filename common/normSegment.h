#ifndef _NORM_SEGMENT
#define _NORM_SEGMENT

#include "normMessage.h"
#include "normBitmask.h"

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
            char** ptr = (char**)segment;
            *ptr = seg_list;
            seg_list = segment;
            seg_count++;
        }
        bool IsEmpty() {return (NULL == seg_list);}
        
    private: 
        unsigned int    seg_size;
        unsigned int    seg_count;  
        unsigned int    seg_total;
        char*           seg_list;
        
        unsigned int    peak_usage;
        unsigned int    overruns;
        bool            overrun_flag;
};  // end class NormSegmentPool

class NormBlock
{
    friend class NormBlockPool;
    friend class NormBlockBuffer;
    
    public:
        enum Flag 
        {
            PARITY_READY = 0x01,
            IN_REPAIR    = 0x02
        };
            
        NormBlock();
        ~NormBlock();
        const NormBlockId& Id() const {return id;}
        void SetId(NormBlockId& x) {id = x;}
        bool Init(UINT16 blockSize);
        void Destroy();   
        
        void SetFlag(NormBlock::Flag flag) {flags |= flag;}
        bool ParityReady() {return (0 != (flags & PARITY_READY));}
        bool InRepair() {return (0 != (flags & IN_REPAIR));}
        
        char** SegmentList(UINT16 index = 0) {return &segment_table[index];}
        char* Segment(NormSegmentId sid)
        {
            ASSERT(sid < size);
            return segment_table[sid];
        }
        void AttachSegment(NormSegmentId sid, char* segment)
        {
            ASSERT(sid < size);
            ASSERT(!segment_table[sid]);
            segment_table[sid] = segment;
            erasure_count--;
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
        
        void TxInit(NormBlockId& blockId, UINT16 ndata, UINT16 autoParity)
        {
            id = blockId;
            pending_mask.Clear();
            pending_mask.SetBits(0, ndata+autoParity);
            repair_mask.Clear();
            erasure_count = size - ndata;
            parity_count = 0;   
            flags = 0;
        }
        void RxInit(NormBlockId& blockId, UINT16 ndata)
        {
            id = blockId;
            pending_mask.Reset();
            repair_mask.Clear();
            erasure_count = size;
            parity_count = 0;   
            flags = 0;
        }
        
        NormSymbolId FirstPending() const
            {return pending_mask.FirstSet();}
        NormSymbolId FirstRepair()  const
            {return repair_mask.FirstSet();}
        bool SetPending(NormSymbolId s) 
            {return pending_mask.Set(s);}
        void UnsetPending(NormSymbolId s) 
            {pending_mask.Unset(s);}
        void ClearPending()
            {pending_mask.Clear();}
        bool SetRepair(NormSymbolId s) 
            {return repair_mask.Set(s);}
        void UnsetRepair(NormSymbolId s)
            {repair_mask.Unset(s);}
        void ClearRepairs()
            {repair_mask.Clear();}
        bool IsPending(NormSymbolId s) const
            {return pending_mask.Test(s);}
        bool IsPending() const
            {return pending_mask.IsSet();}
        bool IsRepairPending(NormSegmentId end); 
            
        NormSymbolId NextPending(NormSymbolId index) const
            {return pending_mask.NextSet(index);}
        UINT16 ErasureCount() const {return erasure_count;}
        //void DisplayPendingMask(FILE* f) {pending_mask.Display(f);}
        
        bool IsEmpty();
        void EmptyToPool(NormSegmentPool& segmentPool);
            
    private:
        NormBlockId id;
        UINT16      size;
        char**      segment_table;
        
        int         flags;
        UINT16      erasure_count;
        UINT16      parity_count;
        
        NormBitmask pending_mask;
        NormBitmask repair_mask;
        
        NormBlock*  next;
};  // end class NormBlock

class NormBlockPool
{
    public:
        NormBlockPool();
        ~NormBlockPool();
        bool Init(UINT32 numBlocks, UINT16 blockSize);
        void Destroy();
        bool IsEmpty() {return (NULL == head);}
        NormBlock* Get()
        {
            NormBlock* b = head;
            head = b ? b->next : NULL;
            return b;
        }
        void Put(NormBlock* b)
        {
            b->next = head;
            head = b;
        }
        
    private:
        NormBlock*      head;
};  // end class NormBlockPool

class NormBlockBuffer
{
    friend class NormBlockBuffer::Iterator;
    
    public:
        NormBlockBuffer();
        ~NormBlockBuffer();
        bool Init(unsigned long rangeMax, unsigned long tableSize = 256);
        void Destroy();
        
        bool Insert(NormBlock* theBlock);
        bool Remove(const NormBlock* theBlock);
        NormBlock* Find(const NormBlockId& blockId) const;
        
        NormBlockId RangeLo() {return range_lo;}
        NormBlockId RangeHi() {return range_hi;}
        bool IsEmpty() {return (0 == range);}
        bool CanInsert(NormBlockId blockId) const;
        
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
            
    private:
        static NormBlock* Next(NormBlock* b) {return b->next;}    
        
        NormBlock**     table;
        unsigned long   hash_mask;       
        unsigned long   range_max;  // max range of blocks that can be buffered
        unsigned long   range;      // zero if "block buffer" is empty
        NormBlockId     range_lo;
        NormBlockId     range_hi;
};  // end class NormBlockBuffer



#endif // _NORM_SEGMENT
