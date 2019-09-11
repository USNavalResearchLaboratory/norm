#ifndef _NORM_OBJECT
#define _NORM_OBJECT

#include "normSegment.h"  // NORM segmentation classes
#include "normEncoder.h"
#include "normFile.h"

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
            
        void Verify() const;
        
        virtual ~NormObject();
        NormObject::Type GetType() const {return type;}
        const NormObjectId& Id() const {return id;}  
        const NormObjectSize& Size() const {return object_size;}
        bool HaveInfo() const {return (info_len > 0);}
        const char* GetInfo() const {return info;}
        UINT16 InfoLength() const {return info_len;}
        bool IsStream() const {return (STREAM == type);}
        
        NormNodeId LocalNodeId() const;
        class NormServerNode* GetServer() {return server;}
        
        bool IsOpen() {return (0 != segment_size);}
        // Opens (inits) object for tx operation
        bool Open(const NormObjectSize& objectSize, 
                  const char*           infoPtr, 
                  UINT16                infoLen);
        // Opens (inits) object for rx operation
        bool Open(const NormObjectSize& objectSize, bool hasInfo)
            {return Open(objectSize, (char*)NULL, hasInfo ? 1 : 0);}
        void Close();
        
        virtual bool WriteSegment(NormBlockId   blockId, 
                                  NormSegmentId segmentId, 
                                  const char*   buffer) = 0;
        virtual bool ReadSegment(NormBlockId    blockId, 
                                 NormSegmentId  segmentId,
                                 char*          buffer) = 0;
        
        // These are only valid after object is open
        NormBlockId LastBlockId() const {return last_block_id;}
        NormSegmentId LastBlockSize() const {return last_block_size;}
        NormSegmentId BlockSize(NormBlockId blockId)
            {return ((blockId == last_block_id) ? last_block_size : ndata);}
        
        bool IsPending(bool flush = true) const;
        bool IsRepairPending() const;
        bool IsPendingInfo() {return pending_info;}
        NormBlockId FirstPending() {return pending_mask.FirstSet();}
        
        // Methods available to server for transmission
        bool NextServerMsg(NormObjectMsg* msg);
        NormBlock* ServerRecoverBlock(NormBlockId blockId);
        bool CalculateBlockParity(NormBlock* block);
        
        bool TxReset(NormBlockId firstBlock = NormBlockId(0));
        bool TxResetBlocks(NormBlockId nextId, NormBlockId lastId);
        bool TxUpdateBlock(NormBlock*       theBlock, 
                           NormSegmentId    firstSegmentId, 
                           NormSegmentId    lastSegmentId,
                           UINT16           numErasures)
        {
            NormBlockId blockId = theBlock->Id();
            bool result = theBlock->TxUpdate(firstSegmentId, lastSegmentId, 
                                             BlockSize(blockId), nparity, 
                                             numErasures);
            ASSERT(result ? pending_mask.Set(blockId) : true);
            result = result ? pending_mask.Set(blockId) : false;
            return result; 
        }  // end NormObject::TxUpdateBlock()
        bool HandleInfoRequest();
        bool HandleBlockRequest(NormBlockId nextId, NormBlockId lastId);
        bool SetPending(NormBlockId blockId) {return pending_mask.Set(blockId);}
        NormBlock* FindBlock(NormBlockId blockId) {return block_buffer.Find(blockId);}
        bool ActivateRepairs();
        bool IsRepairSet(NormBlockId blockId) {return repair_mask.Test(blockId);}
        bool AppendRepairAdv(NormCmdRepairAdvMsg& cmd);
               
        // Used by session server for resource management scheme
        NormBlock* StealNonPendingBlock(bool excludeBlock, NormBlockId excludeId = 0);
        
        
        // Methods available to client for reception
        bool Accepted() {return accepted;}
        void HandleObjectMessage(const NormObjectMsg& msg,
                                 NormMsg::Type        msgType,
                                 NormBlockId          blockId,
                                 NormSegmentId        segmentId);
        
        
        // Used by remote server node for resource management scheme
        NormBlock* StealNewestBlock(bool excludeBlock, NormBlockId excludeId = 0);
        NormBlock* StealOldestBlock(bool excludeBlock, NormBlockId excludeId = 0);
        
        bool ClientRepairCheck(CheckLevel    level,
                               NormBlockId   blockId,
                               NormSegmentId segmentId,
                               bool          timerActive,
                               bool          holdoffPhase = false);
        bool IsRepairPending(bool flush);
        bool AppendRepairRequest(NormNackMsg& nack, bool flush);
        void SetRepairInfo() {repair_info = true;}
        bool SetRepairs(NormBlockId first, NormBlockId last)
        {
            return (first == last) ? repair_mask.Set(first) :
                                     repair_mask.SetBits(first, last-first+1); 
        }
        
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
        NormBlockId           current_block_id;    // for suppression       
        NormSegmentId         next_segment_id;     // for suppression       
        NormBlockId           max_pending_block;   // for NACK construction 
        NormSegmentId         max_pending_segment; // for NACK construction
        NormBlockId           last_block_id;
        NormSegmentId         last_block_size;
        UINT16                last_segment_size;
        
        char*                 info;
        UINT16                info_len;
        
        bool                  accepted;
        
        
                   
        NormObject*           next; 
};  // end class NormObject


class NormFileObject : public NormObject
{
    public:
        NormFileObject(class NormSession*       theSession,
                       class NormServerNode*    theServer,
                       const NormObjectId&      objectId);
        ~NormFileObject();
        
        bool Open(const char* thePath,
                  const char* infoPtr = NULL,
                  UINT16      infoLen = 0);
        bool Accept(const char* thePath);
        void Close();
        const char* Path() {return path;}
        bool Rename(const char* newPath) 
        {
            bool result = file.Rename(path, newPath);
            result ? strncpy(path, newPath, PATH_MAX) : NULL;
            return result;
        }
        
        virtual bool WriteSegment(NormBlockId   blockId, 
                                  NormSegmentId segmentId, 
                                  const char*   buffer);
        
        virtual bool ReadSegment(NormBlockId    blockId, 
                                 NormSegmentId  segmentId,
                                 char*          buffer);
            
    private:
        char            path[PATH_MAX];
        NormFile        file;
        NormObjectSize  block_size;
};  // end class NormFileObject


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
        bool StreamUpdateStatus(NormBlockId blockId);
        void StreamResync(NormBlockId nextBlockId)
            {stream_next_id = nextBlockId;}
        void StreamAdvance();
        
        virtual bool WriteSegment(NormBlockId   blockId, 
                                  NormSegmentId segmentId, 
                                  const char*   buffer);
        virtual bool ReadSegment(NormBlockId    blockId, 
                                 NormSegmentId  segmentId,
                                 char*          buffer);
        
        bool Read(char* buffer, unsigned int* buflen, bool findMsgStart = false);
        unsigned long Write(const char* buffer, unsigned long len, bool flush, bool eom, bool push);
        
        // For receive stream, we can rewind to earliest buffered offset
        void Rewind(); 
        
        bool LockBlocks(NormBlockId nextId, NormBlockId lastId);
        bool LockSegments(NormBlockId blockId, NormSegmentId firstId,
                          NormSegmentId lastId);
        NormBlockId StreamBufferLo() {return stream_buffer.RangeLo();}
        void Prune(NormBlockId blockId);
        
        bool IsFlushPending() {return flush_pending;}
        NormBlockId FlushBlockId()
            {return (write_index.segment ? write_index.block : 
                     (NormBlockId((UINT32)write_index.block-1)));}
        NormSegmentId FlushSegmentId()
            {return (write_index.segment ? (write_index.segment-1) : 
                                           (ndata-1));}
           
    private:
        class Index
        {
            public:
                NormBlockId     block;
                NormSegmentId   segment; 
        };
        // Extra state for STREAM objects
        bool                        stream_sync;
        NormBlockId                 stream_sync_id;
        NormBlockId                 stream_next_id;
        
        NormBlockPool               block_pool;
        NormSegmentPool             segment_pool;
        NormBlockBuffer             stream_buffer;
        Index                       write_index;
        NormObjectSize              write_offset;
        Index                       read_index;
        NormObjectSize              read_offset;
        bool                        flush_pending;
        bool                        msg_start;
};  // end class NormStreamObject

#ifdef SIMULATE
// This class is used to simulate file objects in the
// network simulation build of NORM
class NormSimObject : public NormObject
{
    public:
        NormSimObject(class NormSession*       theSession,
                      class NormServerNode*    theServer,
                      const NormObjectId&      objectId);
        ~NormSimObject();
        
        bool Open(unsigned long objectSize,
                  const char*   infoPtr = NULL,
                  UINT16        infoLen = 0)
        {
            return server ? true : NormObject::Open(objectSize, infoPtr, infoLen);
        }
        bool Accept() {NormObject::Accept(); return true;}
        void Close() {NormObject::Close();}
        
        virtual bool WriteSegment(NormBlockId   blockId, 
                                  NormSegmentId segmentId, 
                                  const char*   buffer) {return true;}
        
        virtual bool ReadSegment(NormBlockId    blockId, 
                                 NormSegmentId  segmentId,
                                 char*          buffer);
};  // end class NormSimObject
#endif // SIMULATE

class NormObjectTable
{
    public:
        class Iterator;
        friend class NormObjectTable::Iterator;
        
        NormObjectTable();
        ~NormObjectTable();
        bool Init(UINT16 rangeMax, UINT16 tableSize = 256);
        void Destroy();
        
        bool IsInited() const {return (NULL != table);}
        bool CanInsert(NormObjectId objectId) const;
        bool Insert(NormObject* theObject);
        bool Remove(const NormObject* theObject);
        NormObject* Find(const NormObjectId& objectId) const;
        
        NormObjectId RangeLo() const {return range_lo;}
        NormObjectId RangeHi() const {return range_hi;}
        bool IsEmpty() const {return (0 == range);}
        unsigned long Count() const {return count;}
        const NormObjectSize& Size() const {return size;}
        
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
        unsigned long    count;
        NormObjectSize   size;
};  // end class NormObjectTable

#endif // _NORM_OBJECT
