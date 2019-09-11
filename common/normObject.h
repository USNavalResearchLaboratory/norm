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
            NONE,
            DATA,
            FILE,
            STREAM      
        };
            
        enum CheckLevel
        {
            TO_OBJECT,
            THRU_INFO,
            TO_BLOCK,
            THRU_SEGMENT,
            THRU_BLOCK,
            THRU_OBJECT 
        };
            
        enum NackingMode
        {
            NACK_NONE,
            NACK_INFO_ONLY,
            NACK_NORMAL
        };
            
        virtual ~NormObject();
        void Retain();
        void Release();
        unsigned int GetReferenceCount() {return reference_count;}
        
        // This must be reset after each update
        void SetNotifyOnUpdate(bool state)
            {notify_on_update = state;}
        
        // Object information
        NormObject::Type GetType() const {return type;}
        const NormObjectId& GetId() const {return transport_id;}  
        const NormObjectSize& GetSize() const {return object_size;}
        bool HaveInfo() const {return (info_len > 0);}
        const char* GetInfo() const {return info;}
        UINT16 GetInfoLength() const {return info_len;}
        bool IsStream() const {return (STREAM == type);}
        
        class NormSession& GetSession() const {return session;}
        NormNodeId LocalNodeId() const;
        class NormServerNode* GetServer() const {return server;}
        NormNodeId GetServerNodeId() const;
        
        bool IsOpen() {return (0 != segment_size);}
        // Opens (inits) object for tx operation
        bool Open(const NormObjectSize& objectSize, 
                  const char*           infoPtr, 
                  UINT16                infoLen,
                  UINT16                segmentSize,
                  UINT16                numData,
                  UINT16                numParity);
        // Opens (inits) object for rx operation
        bool Open(const NormObjectSize& objectSize, 
                  bool                  hasInfo,
                  UINT16                segmentSize,
                  UINT16                numData,
                  UINT16                numParity)
        {
            return Open(objectSize, (char*)NULL, hasInfo ? 1 : 0,
                        segmentSize, numData, numParity);
        }
        void Close();
        
        
        
        virtual bool WriteSegment(NormBlockId   blockId, 
                                  NormSegmentId segmentId, 
                                  const char*   buffer,
                                  bool          msgStart) = 0;
        virtual UINT16 ReadSegment(NormBlockId    blockId, 
                                   NormSegmentId  segmentId,
                                   char*          buffer,
                                   bool*          msgStart = NULL) = 0;
        
        virtual char* RetrieveSegment(NormBlockId   blockId,
                                      NormSegmentId segmentId) = 0;
        
        NackingMode GetNackingMode() const {return nacking_mode;}
        void SetNackingMode(NackingMode nackingMode) 
        {
            nacking_mode = nackingMode;
            // (TBD) initiate an appropriate NormServerNode::RepairCheck
            // to prompt repair process if needed
        }
        
        // These are only valid after object is open
        NormBlockId GetFinalBlockId() const {return final_block_id;}
        UINT32 GetBlockSize(NormBlockId blockId) const
        {
            return (((UINT32)blockId < large_block_count) ? large_block_size : 
                                                            small_block_size);
        }
        
        bool IsPending(bool flush = true) const;
        bool IsRepairPending() const;
        bool IsPendingInfo() {return pending_info;}
        bool GetFirstPending(NormBlockId& blockId) const 
        {
            UINT32 index = 0;
            bool result = pending_mask.GetFirstSet(index);
            blockId = NormBlockId(index);
            return result;
        }
        bool GetNextPending(NormBlockId& blockId) const
        {
            UINT32 index = (UINT32)blockId;
            bool result = pending_mask.GetNextSet(index);
            blockId = NormBlockId(index);
            return result;
        }
        bool GetLastPending(NormBlockId& blockId) const
        {
            UINT32 index = 0;
            bool result = pending_mask.GetLastSet(index);
            blockId = NormBlockId(index);
            return result;
        }
        bool GetFirstRepair(NormBlockId& blockId) const 
        {
            UINT32 index = 0;
            bool result = repair_mask.GetFirstSet(index);
            blockId = NormBlockId(index);
            return result;
        }
        
        bool GetNextRepair(NormBlockId& blockId) const
        {
            UINT32 index = (UINT32)blockId;
            bool result = repair_mask.GetNextSet(index);
            blockId = NormBlockId(index);
            return result;
        }
        bool GetLastRepair(NormBlockId& blockId) const
        {
            UINT32 index = 0;
            bool result = repair_mask.GetLastSet(index);
            blockId = NormBlockId(index);
            return result;
        }
        
        bool FindRepairIndex(NormBlockId& blockId, NormSegmentId& segmentId) const;
                    
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
            NormBlockId blockId = theBlock->GetId();
            bool result = theBlock->TxUpdate(firstSegmentId, lastSegmentId, 
                                             GetBlockSize(blockId), nparity, 
                                             numErasures);
            ASSERT(result ? pending_mask.Set(blockId) : true);
            result = result ? pending_mask.Set(blockId) : false;
            return result; 
        }  
        bool HandleInfoRequest();
        bool HandleBlockRequest(NormBlockId nextId, NormBlockId lastId);
        bool SetPending(NormBlockId blockId) {return pending_mask.Set(blockId);}
        NormBlock* FindBlock(NormBlockId blockId) {return block_buffer.Find(blockId);}
        bool ActivateRepairs();
        bool IsRepairSet(NormBlockId blockId) {return repair_mask.Test(blockId);}
        bool IsPendingSet(NormBlockId blockId) {return pending_mask.Test(blockId);}
        bool AppendRepairAdv(NormCmdRepairAdvMsg& cmd);
        
        NormBlockId GetMaxPendingBlockId() const {return max_pending_block;}
        NormSegmentId GetMaxPendingSegmentId() const {return max_pending_segment;}
               
        // Used by sender for resource management scheme
        NormBlock* StealNonPendingBlock(bool excludeBlock, NormBlockId excludeId = 0);
        
        
        // Methods available to client for reception
        bool Accepted() {return accepted;}
        void HandleObjectMessage(const NormObjectMsg& msg,
                                 NormMsg::Type        msgType,
                                 NormBlockId          blockId,
                                 NormSegmentId        segmentId);
        
        
        // Used by receiver for resource management scheme
        NormBlock* StealNewestBlock(bool excludeBlock, NormBlockId excludeId = 0);
        NormBlock* StealOldestBlock(bool excludeBlock, NormBlockId excludeId = 0);
        bool ReclaimSourceSegments(NormSegmentPool& segmentPool);
        bool PassiveRepairCheck(NormBlockId   blockId,
                                NormSegmentId segmentId);
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
        NormObject(Type                     theType, 
                   class NormSession&       theSession, 
                   class NormServerNode*    theServer,
                   const NormObjectId&      objectId); 
    
        void Accept() {accepted = true;}
    
        NormObject::Type      type;
        class NormSession&    session;
        class NormServerNode* server;  // NULL value indicates local (tx) object
        unsigned int          reference_count;
        NormObjectId          transport_id;
        
        NormObjectSize        object_size;
        UINT16                segment_size;
        UINT16                ndata;
        UINT16                nparity;
        NormBlockBuffer       block_buffer;
        bool                  pending_info;  // set when we need to send or recv info
        NormSlidingMask       pending_mask;
        bool                  repair_info;   // client: set when
        NormSlidingMask       repair_mask;
        NormBlockId           current_block_id;    // for suppression       
        NormSegmentId         next_segment_id;     // for suppression       
        NormBlockId           max_pending_block;   // for NACK construction 
        NormSegmentId         max_pending_segment; // for NACK construction
        UINT32                large_block_count;
        UINT32                large_block_size;
        UINT32                small_block_count;
        UINT32                small_block_size;
        NormBlockId           final_block_id;
        UINT16                final_segment_size;
        NackingMode           nacking_mode;
        char*                 info;
        UINT16                info_len;
        
        // Here are some members used to let us know
        // our status with respect to the rest of the world
        bool                  accepted;
        bool                  notify_on_update;
        
        NormObject*           next; 
};  // end class NormObject


class NormFileObject : public NormObject
{
    public:
        NormFileObject(class NormSession&       theSession,
                       class NormServerNode*    theServer,
                       const NormObjectId&      objectId);
        ~NormFileObject();
        
        bool Open(const char* thePath,
                  const char* infoPtr = NULL,
                  UINT16      infoLen = 0);
        bool Accept(const char* thePath);
        void Close();
        
        const char* GetPath() {return path;}
        bool Rename(const char* newPath) 
        {
            bool result = file.Rename(path, newPath);
            result ? strncpy(path, newPath, PATH_MAX) : NULL;
            return result;
        }
        
        virtual bool WriteSegment(NormBlockId   blockId, 
                                  NormSegmentId segmentId, 
                                  const char*   buffer,
                                  bool          msgStart);
        
        virtual UINT16 ReadSegment(NormBlockId    blockId, 
                                   NormSegmentId  segmentId,
                                   char*          buffer,
                                   bool*          msgStart = NULL);
        virtual char* RetrieveSegment(NormBlockId   blockId,
                                      NormSegmentId segmentId);
            
    private:
        char            path[PATH_MAX];
        NormFile        file;
        NormObjectSize  large_block_length;
        NormObjectSize  small_block_length;
};  // end class NormFileObject

class NormDataObject : public NormObject
{
    // (TBD) allow support of greater than 4GB size data objects
    public:
        NormDataObject(class NormSession&       theSession,
                       class NormServerNode*    theServer,
                       const NormObjectId&      objectId);
        ~NormDataObject();
        
        bool Open(char*       dataPtr,
                  UINT32      dataLen,
                  const char* infoPtr = NULL,
                  UINT16      infoLen = 0);
        bool Accept(char* dataPtr, UINT32 dataMax);
        void Close();
        
        const char* GetData() {return data_ptr;}
        
        virtual bool WriteSegment(NormBlockId   blockId, 
                                  NormSegmentId segmentId, 
                                  const char*   buffer,
                                  bool          msgStart);
        
        virtual UINT16 ReadSegment(NormBlockId    blockId, 
                                   NormSegmentId  segmentId,
                                   char*          buffer,
                                   bool*           msgStart = NULL);
        
        virtual char* RetrieveSegment(NormBlockId   blockId,
                                      NormSegmentId segmentId);
            
    private:
        NormObjectSize  large_block_length;
        NormObjectSize  small_block_length;
        char*           data_ptr;
        UINT32          data_max;
};  // end class NormDataObject


class NormStreamObject : public NormObject
{
    public:
        NormStreamObject(class NormSession&       theSession, 
                         class NormServerNode*    theServer,
                         const NormObjectId&      objectId);
        ~NormStreamObject(); 

        bool Open(UINT32 bufferSize, 
                  const char*   infoPtr = NULL, 
                  UINT16        infoLen = 0);
        void Close();
        bool Accept(UINT32 bufferSize);
        
        enum FlushMode
        {
            FLUSH_NONE,    // no flush action taken
            FLUSH_PASSIVE, // pending queued data is transmitted, but no CMD(FLUSH) sent
            FLUSH_ACTIVE   // pending queued data is transmitted, _and_ active CMD(FLUSH)
        };
            
        void SetFlushMode(FlushMode flushMode) {flush_mode = flushMode;}
        void Flush(bool eom = false)
        {
            FlushMode oldFlushMode = flush_mode;
            SetFlushMode((FLUSH_ACTIVE == oldFlushMode) ? FLUSH_ACTIVE : FLUSH_PASSIVE);
            Write(NULL, 0, eom);
            SetFlushMode(oldFlushMode);   
        }
        void SetPushMode(bool state) {push_mode = state;}
            
        bool Read(char* buffer, unsigned int* buflen, bool findMsgStart = false);
        UINT32 Write(const char* buffer, UINT32 len, bool eom = false);
        
        bool SyncOffsetIsValid() {return sync_offset_valid;}
        UINT32 GetSyncOffset() {return sync_offset;}
        UINT32 GetCurrentReadOffset() {return read_offset;}
        
        bool StreamUpdateStatus(NormBlockId blockId);
        void StreamResync(NormBlockId nextBlockId)
        {
            stream_next_id = nextBlockId;
        }
        void StreamAdvance();
        
        virtual bool WriteSegment(NormBlockId   blockId, 
                                  NormSegmentId segmentId, 
                                  const char*   buffer,
                                  bool          msgStart);
        virtual UINT16 ReadSegment(NormBlockId    blockId, 
                                   NormSegmentId  segmentId,
                                   char*          buffer,
                                   bool*          msgStart = NULL);
        
        virtual char* RetrieveSegment(NormBlockId   blockId,
                                      NormSegmentId segmentId);
        
        
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
        
        NormBlockId GetNextBlockId() const
            {return (server ? read_index.block : write_index.block);}
        NormSegmentId GetNextSegmentId() const
            {return (server ? read_index.segment : write_index.segment);}  
        
        UINT32 GetBlockPoolCount() {return block_pool.GetCount();}
        void SetBlockPoolThreshold(UINT32 value) 
            {block_pool_threshold = value;}
         
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
        UINT32                      sync_offset;
        bool                        sync_offset_valid;
        
        NormBlockPool               block_pool;
        NormSegmentPool             segment_pool;
        NormBlockBuffer             stream_buffer;
        Index                       write_index;
        UINT32                      write_offset;
        bool                        read_init;
        Index                       read_index;
        UINT32                      read_offset;
        bool                        flush_pending;
        bool                        msg_start;
        FlushMode                   flush_mode;
        bool                        push_mode;
        
        // For threaded API purposes
        UINT32                      block_pool_threshold;
};  // end class NormStreamObject

#ifdef SIMULATE
// This class is used to simulate file objects in the
// network simulation build of NORM
class NormSimObject : public NormObject
{
    public:
        NormSimObject(class NormSession&       theSession,
                      class NormServerNode*    theServer,
                      const NormObjectId&      objectId);
        ~NormSimObject();
        
        bool Open(UINT32 objectSize,
                  const char*   infoPtr = NULL,
                  UINT16        infoLen = 0)
        {
            return server ? true : NormObject::Open(objectSize, infoPtr, infoLen);
        }
        bool Accept() {NormObject::Accept(); return true;}
        void Close() {NormObject::Close();}
        
        virtual bool WriteSegment(NormBlockId   blockId, 
                                  NormSegmentId segmentId, 
                                  const char*   buffer,
                                  bool          msgStart) {return true;}
        
        virtual UINT16 ReadSegment(NormBlockId    blockId, 
                                   NormSegmentId  segmentId,
                                   char*          buffer,
                                   bool*          msgStart = NULL);
        
        virtual char* RetrieveSegment(NormBlockId   blockId,
                                      NormSegmentId segmentId)
        {
            return server ? server->GetRetrievalSegment() : NULL;   
        }
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
        UINT32 Count() const {return count;}
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
        
        NormObject**    table;
        UINT16          hash_mask;       
        UINT16          range_max;  // max range of objects that can be kept
        UINT16          range;      // zero if "object table" is empty
        NormObjectId    range_lo;
        NormObjectId    range_hi;
        UINT16          count;
        NormObjectSize  size;
};  // end class NormObjectTable

#endif // _NORM_OBJECT
