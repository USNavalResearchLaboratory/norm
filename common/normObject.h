#ifndef _NORM_OBJECT
#define _NORM_OBJECT

#include "normSegment.h"  // NORM segmentation classes

#include <stdio.h>

class NormObject
{
    friend class NormObjectTable;
    
    public:
        enum Type
        {
            DATA,
            FILE,
            STREAM      
        };
            
        enum CheckLevel
        {
            THRU_INFO,
            TO_BLOCK,
            THRU_SEGMENT,
            THRU_BLOCK,
            THRU_OBJECT 
        };
            
        
        virtual ~NormObject();
        NormObject::Type GetType() const {return type;}
        const NormObjectId& Id() const {return id;}  
        const NormObjectSize& Size() {return object_size;}
             
        bool IsStream() {return (STREAM == type);}
        NormNodeId LocalNodeId() const;
        
        // Opens (inits) object for tx operation
        bool Open(const NormObjectSize& objectSize, 
                  const char*           infoPtr, 
                  UINT16                infoLen);
        // Opens (inits) object for rx operation
        bool Open(const NormObjectSize& objectSize, bool hasInfo)
            {return Open(objectSize, NULL, hasInfo ? 1 : 0);}
        void Close();
        
        virtual bool WriteSegment(NormBlockId   blockId, 
                                  NormSegmentId segmentId, 
                                  const char*   buffer) = 0;
        
        virtual UINT16 ReadSegment(NormBlockId blockId, NormSegmentId segmentId,
                                   NormObjectSize* offset, char* buffer, UINT16 maxlen) = 0;
        
        // These are only valid after object is open
        NormBlockId LastBlockId();
        NormSegmentId LastSegmentId();
        NormSegmentId LastSegmentId(NormBlockId blockId);
        
        // (TBD) re-write to use current_block_id/next_segment_id when 
        //  flush == false !!!
        bool IsPending(bool flush = true) 
            {return (pending_info || pending_mask.IsSet());}
        
        // Methods available to server for transmission
        bool NextServerMsg(NormMessage* msg);
               
        // Used by session server for resource management scheme
        NormBlock* StealOldestBlock(bool excludeBlock, NormBlockId excludeId = 0);
        
        
        // Methods available to client for reception
        bool Accepted() {return accepted;}
        void HandleObjectMessage(NormMessage& msg);
        bool StreamUpdateStatus(NormBlockId blockId);
        
        // Used by remote server node for resource management scheme
        NormBlock* StealNewestBlock(bool excludeBlock, NormBlockId excludeId = 0);
        
        
        bool ClientRepairCheck(CheckLevel    level,
                               NormBlockId   blockId,
                               NormSegmentId segmentId,
                               bool          timerActive);
        bool IsRepairPending(bool flush);
        bool AppendRepairRequest(NormNackMsg& nack) {return true;}
        
    protected:
        NormObject(NormObject::Type         theType, 
                   class NormSession*       theSession, 
                   class NormServerNode*    theServer,
                   const NormObjectId&      objectId); 
    
        void Accept() {accepted = true;}
    
        NormObject::Type      type;
        class NormSession*    session;
        class NormServerNode* server;  // NULL value indicates local (tx) object
        NormObjectId          id;
        
        NormObjectSize        object_size;
        UINT16                segment_size;
        UINT16                ndata;
        UINT16                nparity;
        NormBlockBuffer       block_buffer;
        bool                  pending_info;
        NormSlidingMask       pending_mask;
        bool                  repair_info;
        NormSlidingMask       repair_mask;
        NormBlockId           current_block_id;
        NormSegmentId         next_segment_id;
        NormBlockId           last_block_id;
        NormSegmentId         last_segment_id;
        
        char*                 info;
        UINT16                info_len;
        
        bool                  accepted;
        
        // Extra state for STREAM objects
        bool                  stream_sync;
        NormBlockId           stream_sync_id;
        NormBlockId           stream_next_id;
                   
        NormObject*           next; 
};  // end class NormObject



class NormStreamObject : public NormObject
{
    public:
        NormStreamObject(class NormSession*       theSession, 
                         class NormServerNode*    theServer,
                         const NormObjectId&      objectId);
        ~NormStreamObject(); 

        bool Open(unsigned long bufferSize, 
                  const char*   infoPtr = NULL, 
                  UINT16        infoLen = 0);
        void Close();
        
        bool Accept(unsigned long bufferSize);

        virtual bool WriteSegment(NormBlockId   blockId, 
                                  NormSegmentId segmentId, 
                                  const char*   buffer);
    
        virtual UINT16 ReadSegment(NormBlockId blockId, NormSegmentId segmentId,
                                   NormObjectSize* offset, char* buffer, UINT16 maxlen);
        unsigned long Read(char* buffer, unsigned long len);
        unsigned long Write(char* buffer, unsigned long len, bool flush = false);
        
        // For receive stream, we can rewind to earliest buffered offset
        void Rewind(); 
           
    private:
        class Index
        {
            public:
                NormBlockId     block;
                NormSegmentId   segment; 
        };
        NormBlockPool               block_pool;
        NormSegmentPool             segment_pool;
        NormBlockBuffer             stream_buffer;
        Index                       write_index;
        NormObjectSize              write_offset;
        Index                       read_index;
        NormObjectSize              read_offset;
};  // end class NormStreamObject

class NormObjectTable
{
    friend class NormObjectTable::Iterator;
    
    public:
        NormObjectTable();
        ~NormObjectTable();
        bool Init(UINT16 rangeMax, UINT16 tableSize = 256);
        void Destroy();
        
        bool IsInited() {return (NULL != table);}
        
        bool Insert(NormObject* theObject);
        bool Remove(const NormObject* theObject);
        NormObject* Find(const NormObjectId& objectId) const;
        
        NormObjectId RangeLo() {return range_lo;}
        NormObjectId RangeHi() {return range_hi;}
        bool IsEmpty() {return (0 == range);}
               
        class Iterator
        {
            public:
                Iterator(const NormObjectTable& objectTable);
                NormObject* GetNextObject();
                NormObject* GetPrevObject();
                void Reset() {reset = true;}
                
            private:
                const NormObjectTable&  table;
                bool                    reset;
                NormObjectId            index;
                bool                    backwards;
        }; 
            
    private:
        NormObject* Next(NormObject* o) const {return o->next;}    
        
        NormObject**     table;
        unsigned long    hash_mask;       
        unsigned long    range_max;  // max range of objects that can be kept
        unsigned long    range;      // zero if "object table" is empty
        NormObjectId     range_lo;
        NormObjectId     range_hi;
};  // end class NormObjectTable

#endif // _NORM_OBJECT
