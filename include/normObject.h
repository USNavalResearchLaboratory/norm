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
        {
            notify_on_update = state;
        }
        
        // Object information
        NormObject::Type GetType() const {return type;}
        const NormObjectId& GetId() const {return transport_id;}  
        const NormObjectSize& GetSize() const {return object_size;}
        bool HaveInfo() const {return (info_len > 0);}
        bool HasInfo() const {return (NULL != info_ptr);}
        
        const char* GetInfo() const {return info_ptr;}
        UINT16 GetInfoLength() const {return info_len;}
        bool IsStream() const {return (STREAM == type);}
        
        class NormSession& GetSession() const {return session;}
        NormNodeId LocalNodeId() const;
        class NormSenderNode* GetSender() const {return sender;}
        NormNodeId GetSenderNodeId() const;
        
        // for NORM API usage only
        void SetUserData(const void* userData) 
            {user_data = userData;}
        const void* GetUserData() const
            {return user_data;}
        
        bool IsOpen() {return (0 != segment_size);}
        // Opens (inits) object for tx operation
        bool Open(const NormObjectSize& objectSize, 
                  const char*           infoPtr, 
                  UINT16                infoLen,
                  UINT16                segmentSize,
                  UINT8                 fecId,
                  UINT8                 fecM,
                  UINT16                numData,
                  UINT16                numParity);
        // Opens (inits) object for rx operation
        bool RxOpen(const NormObjectSize& objectSize, 
                    bool                  hasInfo,
                    UINT16                segmentSize,
                    UINT8                 fecId,
                    UINT8                 fecM,
                    UINT16                numData,
                    UINT16                numParity)
        {
            return Open(objectSize, (char*)NULL, hasInfo ? 1 : 0,
                        segmentSize, fecId, fecM, numData, numParity);
        }
        void Close();
        
        
        
        virtual bool WriteSegment(NormBlockId   blockId, 
                                  NormSegmentId segmentId, 
                                  const char*   buffer) = 0;
        
        virtual UINT16 ReadSegment(NormBlockId    blockId, 
                                   NormSegmentId  segmentId,
                                   char*          buffer) = 0;
        
        virtual char* RetrieveSegment(NormBlockId   blockId,
                                      NormSegmentId segmentId) = 0;
        
        NackingMode GetNackingMode() const {return nacking_mode;}
        void SetNackingMode(NackingMode nackingMode) 
        {
            nacking_mode = nackingMode;
            // (TBD) initiate an appropriate NormSenderNode::RepairCheck
            // to prompt repair process if needed
        }
        
        // These are only valid after object is open
        NormBlockId GetFinalBlockId() const {return final_block_id;}
        UINT32 GetBlockSize(NormBlockId blockId) const
        {
            return (((UINT32)blockId < large_block_count) ? large_block_size : 
                                                            small_block_size);
        }
        
        NormObjectSize GetBytesPending() const;
        
        bool IsPending(bool flush = true) const;
        bool IsRepairPending() const;
        bool IsPendingInfo() {return pending_info;}
        bool PendingMaskIsSet() const
            {return pending_mask.IsSet();}
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
                    
        // Methods available to sender for transmission
        bool NextSenderMsg(NormObjectMsg* msg);
        NormBlock* SenderRecoverBlock(NormBlockId blockId);
        bool CalculateBlockParity(NormBlock* block);
        
        /*bool IsFirstPass() {return first_pass;}
        void ClearFirstPass() {first_pass = false};*/
        
        bool TxReset(NormBlockId firstBlock = NormBlockId(0), bool requeue = false);
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
        bool HandleInfoRequest(bool holdoff);
        bool HandleBlockRequest(NormBlockId nextId, NormBlockId lastId);
        NormBlock* FindBlock(NormBlockId blockId) {return block_buffer.Find(blockId);}
        bool ActivateRepairs();
        bool IsRepairSet(NormBlockId blockId) {return repair_mask.Test(blockId);}
        bool IsPendingSet(NormBlockId blockId) {return pending_mask.Test(blockId);}
        bool AppendRepairAdv(NormCmdRepairAdvMsg& cmd);
        
        NormBlockId GetMaxPendingBlockId() const {return max_pending_block;}
        NormSegmentId GetMaxPendingSegmentId() const {return max_pending_segment;}
               
        // Used by sender for resource management scheme
        NormBlock* StealNonPendingBlock(bool excludeBlock, NormBlockId excludeId = 0);
        
        
        // Methods available to receiver for reception
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
        bool ReceiverRepairCheck(CheckLevel    level,
                               NormBlockId   blockId,
                               NormSegmentId segmentId,
                               bool          timerActive,
                               bool          holdoffPhase = false);
        bool ReceiverRewindCheck(NormBlockId    blockId,
                                 NormSegmentId  segmentId);
        bool IsRepairPending(bool flush);
        bool AppendRepairRequest(NormNackMsg& nack, bool flush);
        void SetRepairInfo() {repair_info = true;}
        bool SetRepairs(NormBlockId first, NormBlockId last)
        {
            return (first == last) ? repair_mask.Set(first) :
                                     repair_mask.SetBits(first, repair_mask.Delta(last,first)+1); 
        }
        void SetLastNackTime(const ProtoTime& theTime)
            {last_nack_time = theTime;}
        const ProtoTime& GetLastNackTime() const
            {return last_nack_time;}
        
        double GetNackAge() const
            {return ProtoTime::Delta(ProtoTime().GetCurrentTime(), last_nack_time);}
        
    protected:
        NormObject(Type                     theType, 
                   class NormSession&       theSession, 
                   class NormSenderNode*    theSender,
                   const NormObjectId&      objectId); 
    
        void Accept() {accepted = true;}
    
        NormObject::Type      type;
        class NormSession&    session;
        class NormSenderNode* sender;  // NULL value indicates local (tx) object
        unsigned int          reference_count;
        NormObjectId          transport_id;
        
        NormObjectSize        object_size;
        UINT16                segment_size;
        UINT8                 fec_id;
        UINT8                 fec_m;
        UINT16                ndata;
        UINT16                nparity;
        NormBlockBuffer       block_buffer;
        bool                  pending_info;  // set when we need to send or recv info
        ProtoSlidingMask      pending_mask;
        bool                  repair_info;   // receiver: set when
        ProtoSlidingMask      repair_mask;
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
        ProtoTime             last_nack_time;  // time of last NACK received (used for flow control)
        char*                 info_ptr;
        UINT16                info_len;
        
        // Here are some members used to let us know
        // our status with respect to the rest of the world
        bool                  first_pass;   // for sender objects
        bool                  accepted;
        bool                  notify_on_update;
        
        const void*           user_data;  // for NORM API usage only
        
        NormObject*           next; 
};  // end class NormObject


class NormFileObject : public NormObject
{
    public:
        NormFileObject(class NormSession&       theSession,
                       class NormSenderNode*    theSender,
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
        bool PadToSize()
            {return file.Pad(NormObject::GetSize().GetOffset());}
        
        virtual bool WriteSegment(NormBlockId   blockId, 
                                  NormSegmentId segmentId, 
                                  const char*   buffer);
        
        virtual UINT16 ReadSegment(NormBlockId    blockId, 
                                   NormSegmentId  segmentId,
                                   char*          buffer);
        
        virtual char* RetrieveSegment(NormBlockId   blockId,
                                      NormSegmentId segmentId);
            
    //private:
        char            path[PATH_MAX+10];
        NormFile        file;
        NormObjectSize  large_block_length;
        NormObjectSize  small_block_length;
};  // end class NormFileObject

class NormDataObject : public NormObject
{
    // (TBD) allow support of greater than 4GB size data objects
    public:
        NormDataObject(class NormSession&       theSession,
                       class NormSenderNode*    theSender,
                       const NormObjectId&      objectId);
        ~NormDataObject();
        
        bool Open(char*       dataPtr,
                  UINT32      dataLen,
                  bool        dataRelease,
                  const char* infoPtr = NULL,
                  UINT16      infoLen = 0);
        bool Accept(char* dataPtr, UINT32 dataMax, bool dataRelease);
        void Close();
        
        const char* GetData() {return data_ptr;}
        char* DetachData() 
        {
            char* dataPtr = data_ptr;
            data_ptr = NULL;
            return dataPtr;
        }
        
        virtual bool WriteSegment(NormBlockId   blockId, 
                                  NormSegmentId segmentId, 
                                  const char*   buffer);
        
        virtual UINT16 ReadSegment(NormBlockId    blockId, 
                                   NormSegmentId  segmentId,
                                   char*          buffer);
        
        virtual char* RetrieveSegment(NormBlockId   blockId,
                                      NormSegmentId segmentId);
            
    private:
        NormObjectSize  large_block_length;
        NormObjectSize  small_block_length;
        char*           data_ptr;
        UINT32          data_max;
        bool            data_released;   // when true, data_ptr is deleted 
                                         // on NormDataObject destruction
};  // end class NormDataObject


class NormStreamObject : public NormObject
{
    public:
        NormStreamObject(class NormSession&       theSession, 
                         class NormSenderNode*    theSender,
                         const NormObjectId&      objectId);
        ~NormStreamObject(); 

        bool Open(UINT32        bufferSize, 
                  bool          doubleBuffer = false,
                  const char*   infoPtr = NULL, 
                  UINT16        infoLen = 0);
        void Close(bool graceful = false);
        bool Accept(UINT32 bufferSize, bool doubleBuffer = false);
        
        enum FlushMode
        {
            FLUSH_NONE,    // no flush action taken
            FLUSH_PASSIVE, // pending queued data is transmitted, but no CMD(FLUSH) sent
            FLUSH_ACTIVE   // pending queued data is transmitted, _and_ active CMD(FLUSH)
        };
            
        void SetFlushMode(FlushMode flushMode) {flush_mode = flushMode;}
        FlushMode GetFlushMode() {return flush_mode;}
        void Flush(bool eom = false)
        {
            FlushMode oldFlushMode = flush_mode;
            SetFlushMode((FLUSH_ACTIVE == oldFlushMode) ? FLUSH_ACTIVE : FLUSH_PASSIVE);
            Write(NULL, 0, eom);
            SetFlushMode(oldFlushMode);   
        }
        void SetPushMode(bool state) {push_mode = state;}
        bool GetPushMode() const {return push_mode;}
        
        bool IsOldBlock(NormBlockId blockId) const
            {return (!stream_buffer.IsEmpty() && (blockId < stream_buffer.RangeLo()));}

        bool IsClosing() {return stream_closing;}
        bool HasVacancy() 
            {return (stream_closing ? false : write_vacancy);}
        
        NormBlock* StreamBlockLo()
            {return stream_buffer.Find(stream_buffer.RangeLo());}
        void SetLastNackTime(NormBlockId blockId, const ProtoTime& theTime)
        {
            NormBlock* block = stream_buffer.Find(blockId);
            if (NULL != block) block->SetLastNackTime(theTime);
        }
        
            
        bool Read(char* buffer, unsigned int* buflen, bool findMsgStart = false);
        UINT32 Write(const char* buffer, UINT32 len, bool eom = false);
        
        UINT32 GetCurrentReadOffset() {return read_offset;}
        
        unsigned int GetCurrentBufferUsage() const  // in segments
            {return segment_pool.CurrentUsage();}
        
        bool StreamUpdateStatus(NormBlockId blockId);
        // Note that the "pending_mask" should be cleared and the 
        // "block_buffer" emptied before "StreamResync()" is invoked
        void StreamResync(NormBlockId blockId) 
        {
            stream_sync = false;       
            StreamUpdateStatus(blockId);
        }
        bool StreamAdvance();
        
        virtual bool WriteSegment(NormBlockId   blockId, 
                                  NormSegmentId segmentId, 
                                  const char*   buffer);
        
        virtual UINT16 ReadSegment(NormBlockId    blockId, 
                                   NormSegmentId  segmentId,
                                   char*          buffer);
        
        virtual char* RetrieveSegment(NormBlockId   blockId,
                                      NormSegmentId segmentId);
        
        
        // For receive stream, we can rewind to earliest buffered offset
        void Rewind(); 
        
        bool LockBlocks(NormBlockId nextId, NormBlockId lastId, const ProtoTime& currentTime);
        void UnlockBlock(NormBlockId blockId);
        
        bool LockSegments(NormBlockId blockId, NormSegmentId firstId, NormSegmentId lastId);
        NormBlockId StreamBufferLo() const {return stream_buffer.RangeLo();} 
        void Prune(NormBlockId blockId, bool updateStatus);
        
        bool IsFlushPending() {return flush_pending;}
        NormBlockId FlushBlockId()
            {return (write_index.segment ? write_index.block : 
                     (NormBlockId((UINT32)write_index.block-1)));}
        NormSegmentId FlushSegmentId()
            {return (write_index.segment ? (write_index.segment-1) : 
                                           (ndata-1));}
        
        NormBlockId GetNextBlockId() const
            {return (sender ? read_index.block : write_index.block);}
        NormSegmentId GetNextSegmentId() const
            {return (sender ? read_index.segment : write_index.segment);}  
        
        UINT32 GetBlockPoolCount() {return block_pool.GetCount();}
        void SetBlockPoolThreshold(UINT32 value) 
            {block_pool_threshold = value;}
        
        
        unsigned long CurrentBufferUsage() const
            {return (segment_size * segment_pool.CurrentUsage());}
        unsigned long PeakBufferUsage() const
            {return (segment_size * segment_pool.PeakUsage());}
        unsigned long BufferOverunCount() const
            {return segment_pool.OverunCount() + block_pool.OverrunCount();}
        
        bool IsReadReady() const {return read_ready;}
        
        bool DetermineReadReadiness() //const
        {
            NormBlock* block = stream_buffer.Find(read_index.block);
            read_ready = ((NULL != block) && (NULL != block->GetSegment(read_index.segment))); 
            return read_ready; 
        }

        bool IsReadIndex(NormBlockId blockId, NormSegmentId segmentId) const
            {return ((blockId == read_index.block) && (segmentId == read_index.segment));}
        
        bool PassiveReadCheck(NormBlockId blockId, NormSegmentId segmentId);
         
    private:
        bool ReadPrivate(char* buffer, unsigned int* buflen, bool findMsgStart = false);
        void Terminate();
        
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
        UINT32                      write_offset;
        Index                       tx_index;
        UINT32                      tx_offset;
        bool                        write_vacancy;
        bool                        read_init;
        Index                       read_index;
        UINT32                      read_offset;
        bool                        read_ready;
        bool                        flush_pending;
        bool                        msg_start;
        FlushMode                   flush_mode;
        bool                        push_mode;
        
        bool                        stream_closing;
        
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
                      class NormSenderNode*    theSender,
                      const NormObjectId&      objectId);
        ~NormSimObject();
        
        bool Open(UINT32        objectSize,
                  const char*   infoPtr = NULL,
                  UINT16        infoLen = 0);
        bool Accept() {NormObject::Accept(); return true;}
        void Close() {NormObject::Close();}
        
        virtual bool WriteSegment(NormBlockId   blockId, 
                                  NormSegmentId segmentId, 
                                  const char*   buffer) {return true;}
        
        virtual UINT16 ReadSegment(NormBlockId    blockId, 
                                   NormSegmentId  segmentId,
                                   char*          buffer);
        
        virtual char* RetrieveSegment(NormBlockId   blockId,
                                      NormSegmentId segmentId);
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
        void SetRangeMax(UINT16 rangeMax);
        void Destroy();
        
        bool IsInited() const {return (NULL != table);}
        UINT16 GetRangeMax() const {return range_max;}
        bool CanInsert(NormObjectId objectId) const;
        bool Insert(NormObject* theObject);
        bool Remove(NormObject* theObject);
        NormObject* Find(const NormObjectId& objectId) const;
        
        NormObjectId RangeLo() const {return range_lo;}
        NormObjectId RangeHi() const {return range_hi;}
        bool IsEmpty() const {return (0 == range);}
        UINT32 GetCount() const {return count;}
        const NormObjectSize& GetSize() const {return size;}
        
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
