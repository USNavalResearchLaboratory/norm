
#include "normNode.h"
#include "normSession.h"

#include <errno.h>

NormNode::NormNode(class NormSession& theSession, NormNodeId nodeId)
 : session(theSession), id(nodeId), reference_count(0),
   parent(NULL), right(NULL), left(NULL)
{
    
}

NormNode::~NormNode()
{
}

void NormNode::Retain()
{
    reference_count++;
}  // end NormNode::Retain()

void NormNode::Release()
{
    if (reference_count)
        reference_count--;
    else
        DMSG(0, "NormNode::Release() releasing non-retained node?!\n");
    if (0 == reference_count) delete this;   
}  // end NormNode::Release()


const NormNodeId& NormNode::LocalNodeId() const {return session.LocalNodeId();}


NormCCNode::NormCCNode(class NormSession& theSession, NormNodeId nodeId)
 : NormNode(theSession, nodeId)
{
    
}

NormCCNode::~NormCCNode()
{
}


NormServerNode::NormServerNode(class NormSession& theSession, NormNodeId nodeId)
 : NormNode(theSession, nodeId), session_id(0), synchronized(false), sync_id(0),
   max_pending_range(256), is_open(false), segment_size(0), ndata(0), nparity(0), 
   repair_boundary(BLOCK_BOUNDARY), erasure_loc(NULL),
   retrieval_loc(NULL), retrieval_pool(NULL),
   cc_sequence(0), cc_enable(false), cc_rate(0.0), 
   rtt_confirmed(false), is_clr(false), is_plr(false),
   slow_start(true), send_rate(0.0), recv_rate(0.0), recv_accumulator(0),
   recv_total(0), recv_goodput(0), resync_count(0),
   nack_count(0), suppress_count(0), completion_count(0), failure_count(0)
{
    
    
    repair_boundary = session.ClientGetDefaultRepairBoundary();
    default_nacking_mode = session.ClientGetDefaultNackingMode();
    unicast_nacks = session.ClientGetUnicastNacks();
    // (TBD) get "max_pending_range" value from NormSession parameter
            
    repair_timer.SetListener(this, &NormServerNode::OnRepairTimeout);
    repair_timer.SetInterval(0.0);
    repair_timer.SetRepeat(1);
    
    activity_timer.SetListener(this, &NormServerNode::OnActivityTimeout);
    activity_timer.SetInterval(NormSession::DEFAULT_GRTT_ESTIMATE*NORM_ROBUST_FACTOR);
    activity_timer.SetRepeat(NORM_ROBUST_FACTOR);
    
    cc_timer.SetListener(this, &NormServerNode::OnCCTimeout);
    cc_timer.SetInterval(0.0);
    cc_timer.SetRepeat(1);
    
    ack_timer.SetListener(this, &NormServerNode::OnAckTimeout);
    ack_timer.SetInterval(0.0);
    ack_timer.SetRepeat(0);
    
    grtt_send_time.tv_sec = 0;
    grtt_send_time.tv_usec = 0;
    grtt_quantized = NormQuantizeRtt(NormSession::DEFAULT_GRTT_ESTIMATE);
    grtt_estimate = NormUnquantizeRtt(grtt_quantized);
    gsize_quantized = NormQuantizeGroupSize(NormSession::DEFAULT_GSIZE_ESTIMATE);
    gsize_estimate = NormUnquantizeGroupSize(gsize_quantized);
    
    backoff_factor = NormSession::DEFAULT_BACKOFF_FACTOR;
    
    rtt_quantized = NormQuantizeRtt(NormSession::DEFAULT_GRTT_ESTIMATE);
    rtt_estimate = NormUnquantizeRtt(rtt_quantized);
    
    loss_estimator.SetLossEventWindow(NormSession::DEFAULT_GRTT_ESTIMATE);
    
    prev_update_time.tv_sec = 0;
    prev_update_time.tv_usec = 0;
    
}

NormServerNode::~NormServerNode()
{
    Close();
}

bool NormServerNode::Open(UINT16 sessionId)
{
    session_id = sessionId;
    if (!rx_table.Init(max_pending_range))
    {
        DMSG(0, "NormServerNode::Open() rx_table init error\n");
        Close();
        return false;
    }
    if (!rx_pending_mask.Init(max_pending_range, 0x0000ffff))
    {
        DMSG(0, "NormServerNode::Open() rx_pending_mask init error\n");
        Close();
        return false;
    }
    if (!rx_repair_mask.Init(max_pending_range, 0x0000ffff))
    {
        DMSG(0, "NormServerNode::Open() rx_repair_mask init error\n");
        Close();
        return false;
    }    
    is_open = true;
    synchronized = false;
    return true;
}  // end NormServerNode::Open()

void NormServerNode::Close()
{
    if (activity_timer.IsActive()) activity_timer.Deactivate();
    if (repair_timer.IsActive()) repair_timer.Deactivate();
    if (cc_timer.IsActive()) cc_timer.Deactivate();   
    FreeBuffers(); 
    rx_repair_mask.Destroy();
    rx_pending_mask.Destroy();
    rx_table.Destroy();
    synchronized = false;
    is_open = false;
}  // end NormServerNode::Close()

bool NormServerNode::AllocateBuffers(UINT16 segmentSize, 
                                     UINT16 numData, 
                                     UINT16 numParity)
{    
    ASSERT(IsOpen());
    // Calculate how much memory each buffered block will require
    UINT16 blockSize = numData + numParity;
    unsigned long maskSize = blockSize >> 3;
    if (0 != (blockSize & 0x07)) maskSize++;
    unsigned long blockStateSpace = sizeof(NormBlock) +  blockSize * sizeof(char*) + 2*maskSize;
    unsigned long bufferSpace = session.RemoteServerBufferSize();

    // The "bufferFactor" weight determines the ratio of segment buffers (blockSegmentSpace) to
    // allocated NormBlock (blockStateSpace).  
    // If "bufferFactor = 1.0", this is equivalent to the old scheme, where every allocated
    // block can be fully buffered (numData segs) for decoding (no seeking required).  If 
    // "bufferFactor = 0.0", only a guarantee of at least "numParity" segments per block is 
    // enforced.  Note that "bufferFactor" values > 0.0 help reduce "seeking" for decoding, 
    // but reduce the number of blocks for which NORM can keep state.  Note this only comes 
    // into play when NORM would be "buffer constrained"
    // (TBD) perhaps we should keep more "block state" than we can even buffer parity for ???
    //       (this would reduce requests for full block retransmissions when resource contrained)
    double bufferFactor = 0.0;
    unsigned long segPerBlock = 
        (unsigned long) ((bufferFactor * (double)numData) +
                         ((1.0 - bufferFactor) * (double)numParity) + 0.5);
    // If there's no parity, no segment buffering for decoding is required at all!
    // (Thus, the full rxbuffer space can be used for block state)
    if (0 == numParity) segPerBlock = 0;
    unsigned long blockSegmentSpace = segPerBlock * (segmentSize + NormDataMsg::GetStreamPayloadHeaderLength());    
    unsigned long blockSpace = blockStateSpace+blockSegmentSpace;
    unsigned long numBlocks = bufferSpace / blockSpace;
    if (bufferSpace > (numBlocks*blockSpace)) numBlocks++;
    if (numBlocks < 2) numBlocks = 2;
    unsigned long numSegments = numBlocks * segPerBlock;

    
    if (!block_pool.Init(numBlocks, blockSize))
    {
        DMSG(0, "NormServerNode::Open() block_pool init error\n");
        Close();
        return false;
    }
    
    // The extra byte of segments is used for marking segments  (not any more!! TBD remove)
    // which are "start segments" for messages encapsulated in 
    // a NormStreamObject
    if (!segment_pool.Init(numSegments, segmentSize+NormDataMsg::GetStreamPayloadHeaderLength()+1))
    {
        DMSG(0, "NormServerNode::Open() segment_pool init error\n");
        Close();
        return false;
    }
    
    // The "retrieval_pool" is used for FEC block decoding
    // These segments are temporarily used for "retrieved" source symbol segments
    // needed for block decoding  (new rx buffer mgmt scheme)
    if (!(retrieval_pool = new char*[numData]))
    {
        DMSG(0, "NormServerNode::Open() new retrieval_pool error: %s\n", GetErrorString());
        Close();
        return false;          
    }
    for (UINT16 i = 0; i < numData; i++)
    {
        char* s = new char[segmentSize+NormDataMsg::GetStreamPayloadHeaderLength()];
        if (NULL == s)
        {
            DMSG(0, "NormServerNode::Open() new retrieval segment error: %s\n", GetErrorString());
            Close();
            return false;
        }   
        retrieval_pool[i] = s;
    }
    retrieval_index = 0;
    
    if (!(retrieval_loc = new UINT16[numData]))
    {
        DMSG(0, "NormServerNode::Open() retrieval_loc allocation error: %s\n", GetErrorString());
        Close();
        return false;   
    }
    
    if (!decoder.Init(numParity, segmentSize+NormDataMsg::GetStreamPayloadHeaderLength()))
    {
        DMSG(0, "NormServerNode::Open() decoder init error\n");
        Close();
        return false; 
    }
    if (!(erasure_loc = new UINT16[numParity]))
    {
        DMSG(0, "NormServerNode::Open() erasure_loc allocation error: %s\n",  GetErrorString());
        Close();
        return false;   
    }
    segment_size = segmentSize;
    nominal_packet_size = (double)segmentSize;
    ndata = numData;
    nparity = numParity;
    return true;
}  // end NormServerNode::AllocateBuffers()

void NormServerNode::FreeBuffers()
{
    if (erasure_loc)
    {
        delete[] erasure_loc;
        erasure_loc = NULL;
    }
    decoder.Destroy();
    if (retrieval_loc)
    {
        delete[] retrieval_loc;
        retrieval_loc = NULL;
    }
    if (retrieval_pool)
    {
        for (UINT16 i = 0; i < ndata; i++)
        {
            if (retrieval_pool[i]) 
            {
                delete[] retrieval_pool[i];   
                retrieval_pool[i] = NULL;
            }
        }   
        delete[] retrieval_pool;
        retrieval_pool = NULL;
    }
    
    NormObject* obj;
    while ((obj = rx_table.Find(rx_table.RangeLo()))) 
    {
        session.Notify(NormController::RX_OBJECT_ABORTED, this, obj);
        UINT16 objectId = obj->GetId();
        DeleteObject(obj);
        // We do the following to remember which objects were pending
        rx_pending_mask.Set(objectId);
    }
    
    segment_pool.Destroy();
    block_pool.Destroy();
    segment_size = ndata = nparity = 0; 
}  // end NormServerNode::FreeBuffers()


void NormServerNode::HandleCommand(const struct timeval& currentTime, 
                                   const NormCmdMsg&     cmd)
{
    UINT8 grttQuantized = cmd.GetGrtt();
    if (grttQuantized != grtt_quantized)
    {
        grtt_quantized = grttQuantized;
        grtt_estimate = NormUnquantizeRtt(grttQuantized);
        DMSG(4, "NormServerNode::HandleCommand() node>%lu server>%lu new grtt: %lf sec\n",
                LocalNodeId(), GetId(), grtt_estimate);
        activity_timer.SetInterval(grtt_estimate*NORM_ROBUST_FACTOR);
        if (activity_timer.IsActive()) activity_timer.Reschedule();
    }
    UINT8 gsizeQuantized = cmd.GetGroupSize();
    if (gsizeQuantized != gsize_quantized)
    {
        gsize_quantized = gsizeQuantized;
        gsize_estimate = NormUnquantizeGroupSize(gsizeQuantized);
        DMSG(4, "NormServerNode::HandleCommand() node>%lu server>%lu new group size:%lf\n",
                LocalNodeId(), GetId(), gsize_estimate);
    }
    backoff_factor = (double)cmd.GetBackoffFactor();
        
    NormCmdMsg::Flavor flavor = cmd.GetFlavor();
    switch (flavor)
    {
        case NormCmdMsg::SQUELCH:
        {
            const NormCmdSquelchMsg& squelch = (const NormCmdSquelchMsg&)cmd;
            // 1) Sync to squelch
            NormObjectId objectId = squelch.GetObjectId();
            Sync(objectId);
            // 2) Prune stream object if applicable
            NormObject* obj = rx_table.Find(objectId);
            if (obj && (NormObject::STREAM == obj->GetType()))
            {
                NormBlockId blockId = squelch.GetFecBlockId();
                ((NormStreamObject*)obj)->Prune(blockId);   
            }
            // 3) (TBD) Discard any invalidated objects
            break;
        }
            
        case NormCmdMsg::ACK_REQ:
            // (TBD) handle ack requests
            break;
            
        case NormCmdMsg::CC:
        {
            const NormCmdCCMsg& cc = (const NormCmdCCMsg&)cmd;
            grtt_recv_time = currentTime;
            cc.GetSendTime(grtt_send_time);
            cc_sequence = cc.GetCCSequence();
            NormCCRateExtension ext;
            while (cc.GetNextExtension(ext))
            {
                if (NormHeaderExtension::CC_RATE == ext.GetType())
                {
                    cc_enable = true;
                    send_rate = NormUnquantizeRate(ext.GetSendRate());
                    // Are we in the cc_node_list?
                    UINT8 flags, rtt;
                    UINT16 loss;
                    if (cc.GetCCNode(LocalNodeId(), flags, rtt, loss))
                    {
                        if (rtt != rtt_quantized)
                        {
                            rtt_quantized = rtt;
                            rtt_estimate = NormUnquantizeRtt(rtt);
                            loss_estimator.SetLossEventWindow(rtt_estimate);
                        }
                        rtt_confirmed = true;
                        if (0 != (flags & NormCC::CLR))
                        {
                            is_clr = true;
                            is_plr = false;
                        }
                        else if (0 != (flags & NormCC::PLR))
                        {
                            is_clr = false;
                            is_plr = true;   
                        }
                        else
                        {
                            is_clr = is_plr = false;   
                        }
                    }
                    else
                    {
                        is_clr = is_plr = false;
                    }
                    double maxBackoff;
                    if (is_clr || is_plr || !session.Address().IsMulticast())
                    {
                        // Respond immediately
                        maxBackoff = 0.0;
                        if (cc_timer.IsActive()) cc_timer.Deactivate();
                    }
                    else
                    {
                        if (cc_timer.IsActive()) break;
                        double backoffFactor = backoff_factor;
                        backoffFactor = MAX(backoffFactor, 4.0);
                        maxBackoff = grtt_estimate*backoffFactor;

                    }
                    double backoffTime = (maxBackoff > 0.0) ?
                                            ExponentialRand(maxBackoff, gsize_estimate) : 
                                            0.0;
                    // Bias backoff timeout based on our rate 
                    double r;
                    if (slow_start)
                    {
                        r = recv_rate / send_rate;
                        cc_rate = 2.0 * recv_rate;
                    }
                    else
                    {
                        cc_rate = NormSession::CalculateRate(nominal_packet_size,
                                                             rtt_estimate,
                                                             LossEstimate());
                        r = cc_rate / send_rate;
                        r = MIN(r, 0.9);
                        r = MAX(r, 0.5);
                        r = (r - 0.5) / 0.4;
                    }
                    //DMSG(0, "NormServerNode::HandleCommand(CC) node>%lu bias:%lf recv_rate:%lf send_rate:%lf "
                    //      "grtt:%lf gsize:%lf\n",
                    //        LocalNodeId(), r, recv_rate*(8.0/1000.0), send_rate*(8.0/1000.0),
                    // 

                    backoffTime = 0.25 * r * maxBackoff + 0.75 * backoffTime;
                    cc_timer.SetInterval(backoffTime);
                    DMSG(6, "NormServerNode::HandleCommand() node>%lu begin CC back-off: %lf sec)...\n",
                            LocalNodeId(), backoffTime);
                    session.ActivateTimer(cc_timer);
                }  // end if (CC_RATE == ext.GetType())
            }  // end while (GetNextExtension())
            break;
        }    
        case NormCmdMsg::FLUSH:
        {
            // (TBD) should we force synchronize if we're expected
            // to positively acknowledge the FLUSH
            const NormCmdFlushMsg& flush = (const NormCmdFlushMsg&)cmd;
            bool doAck = false;
            UINT16 nodeCount = flush.GetAckingNodeCount();
            NormNodeId localId = LocalNodeId();
            for (UINT16 i = 0; i < nodeCount; i++)
            {
                if (flush.GetAckingNodeId(i) == localId)
                {
                    doAck = true;
                    break;   
                }
            } 
            if (!synchronized)
            {
                if (doAck)
                {
                    // Force sync since we're expected to ACK 
                    Sync(flush.GetObjectId());   
                }
                else
                {
                    // (TBD) optionally sync on any flush ?
                }            
            }
            if (0 != nodeCount) // this was a watermark flush
            {
                if (!PassiveRepairCheck(flush.GetObjectId(), 
                                        flush.GetFecBlockId(), 
                                        flush.GetFecSymbolId()))
                {
                    if (doAck)
                    {
                       watermark_object_id = flush.GetObjectId();
                       watermark_block_id = flush.GetFecBlockId();
                       watermark_segment_id = flush.GetFecSymbolId();
                       if (!ack_timer.IsActive())
                       {
                            double ackBackoff = UniformRand(grtt_estimate);  
                            ack_timer.SetInterval(ackBackoff);
                            session.ActivateTimer(ack_timer); 
                       }
                    }
                    break;  // no pending repairs, skip regular "RepairCheck"   
                }
            }
            if (synchronized) 
            {
                const NormCmdFlushMsg& flush = (const NormCmdFlushMsg&)cmd;
                UpdateSyncStatus(flush.GetObjectId());
                RepairCheck(NormObject::THRU_SEGMENT, 
                            flush.GetObjectId(), 
                            flush.GetFecBlockId(), 
                            flush.GetFecSymbolId());
            }
            break;
        }   
        case NormCmdMsg::REPAIR_ADV:
        {
            const NormCmdRepairAdvMsg& repairAdv = (const NormCmdRepairAdvMsg&)cmd;
            // Does the CC feedback of this ACK suppress our CC feedback
            if (!is_clr && !is_plr && cc_timer.IsActive() && 
                cc_timer.GetRepeatCount())
            {
                NormCCFeedbackExtension ext;
                while (repairAdv.GetNextExtension(ext))
                {
                    if (NormHeaderExtension::CC_FEEDBACK == ext.GetType())
                    {
                        HandleCCFeedback(ext.GetCCFlags(), NormUnquantizeRate(ext.GetCCRate()));
                        break;
                    }
                }
            }   
            if (repair_timer.IsActive() && repair_timer.GetRepeatCount())
            {
                HandleRepairContent(repairAdv.GetRepairContent(), 
                                    repairAdv.GetRepairContentLength());
            }
            break;
        }
            
        default:
            DMSG(0, "NormServerNode::HandleCommand() recv'd unimplemented command!\n");
            break;
    }  // end switch(flavor)
    
}  // end NormServerNode::HandleCommand()

void NormServerNode::HandleCCFeedback(UINT8 ccFlags, double ccRate)
{
    ASSERT(cc_timer.IsActive() && cc_timer.GetRepeatCount());
    if (0 == (ccFlags & NormCC::CLR))
    {
        // We're suppressed by non-CLR receivers with no RTT confirmed
        // and/or lower rate
        double localRate = slow_start ? 
                            (2.0*recv_rate) :
                            NormSession::CalculateRate(nominal_packet_size,
                                                       rtt_estimate,
                                                       LossEstimate());
        localRate = MAX(localRate, cc_rate);
        
        bool hasRtt = (0 != (ccFlags & NormCC::RTT));
        bool suppressed;
        
        if (rtt_confirmed)
        {
            // If we have confirmed our own RTT we
            // are suppressed by _any_ receivers with
            // lower rate than our own
            if (localRate > (0.9 * ccRate))
                suppressed = true;
            else
                suppressed = false;
        }
        else
        {
            // If we haven't confirmed our own RTT we
            // are suppressed by only by other
            // non-confirmed receivers
            if (hasRtt)
                suppressed = false;
            else if (localRate > (0.9 * ccRate))
                suppressed = true;
            else
                suppressed = false;
        }
        if (suppressed)
        {
            if (cc_timer.IsActive()) cc_timer.Deactivate();
            cc_timer.SetInterval(grtt_estimate*backoff_factor);  // (TBD) ???
            session.ActivateTimer(cc_timer);
            cc_timer.DecrementRepeatCount();
        }
    }
}  // end NormServerNode::HandleCCFeedback()

void NormServerNode::HandleAckMessage(const NormAckMsg& ack)
{
    // Does the CC feedback of this ACK suppress our CC feedback
    if (!is_clr && !is_plr && cc_timer.IsActive() && cc_timer.GetRepeatCount())
    {
        NormCCFeedbackExtension ext;
        while (ack.GetNextExtension(ext))
        {
            if (NormHeaderExtension::CC_FEEDBACK == ext.GetType())
            {
                HandleCCFeedback(ext.GetCCFlags(), NormUnquantizeRate(ext.GetCCRate()));
                break;
            }
        }
    }    
}  // end NormServerNode::HandleAckMessage()

void NormServerNode::HandleNackMessage(const NormNackMsg& nack)
{
    // Does the CC feedback of this NACK suppress our CC feedback
    if (!is_clr && !is_plr && cc_timer.IsActive() && cc_timer.GetRepeatCount())
    {
        NormCCFeedbackExtension ext;
        while (nack.GetNextExtension(ext))
        {
            if (NormHeaderExtension::CC_FEEDBACK == ext.GetType())
            {
                HandleCCFeedback(ext.GetCCFlags(), NormUnquantizeRate(ext.GetCCRate()));
                break;
            }
        }
    }
    // Clients also care about recvd NACKS for NACK suppression
    if (repair_timer.IsActive() && repair_timer.GetRepeatCount())
        HandleRepairContent(nack.GetRepairContent(), nack.GetRepairContentLength());
}  // end NormServerNode::HandleNackMessage()

// Clients use this method to process NACK content overheard from other 
// clients or via NORM_CMD(REPAIR_ADV) messages received from the server.  
// Such content can "suppress" pending NACKs
void NormServerNode::HandleRepairContent(const UINT32* buffer, UINT16 bufferLen)
{
    // Parse NACK and incorporate into repair state masks
    NormRepairRequest req;
    UINT16 requestLength = 0;
    bool freshObject = true;
    NormObjectId prevObjectId;
    NormObject* object = NULL;
    bool freshBlock = true;
    NormBlockId prevBlockId = 0;
    NormBlock* block = NULL;
    while ((requestLength = req.Unpack(buffer, bufferLen)))
    {      
        // Point "buffer" to next request and adjust "bufferLen"
        buffer += (requestLength/4); 
        bufferLen -= requestLength;
        // Process request
        enum NormRequestLevel {SEGMENT, BLOCK, INFO, OBJECT};
        NormRepairRequest::Form requestForm = req.GetForm();
        NormRequestLevel requestLevel;
        if (req.FlagIsSet(NormRepairRequest::SEGMENT))
            requestLevel = SEGMENT;
        else if (req.FlagIsSet(NormRepairRequest::BLOCK))
            requestLevel = BLOCK;
        else if (req.FlagIsSet(NormRepairRequest::OBJECT))
            requestLevel = OBJECT;
        else
        {
            requestLevel = INFO;
            ASSERT(req.FlagIsSet(NormRepairRequest::INFO));
        }
        bool repairInfo = req.FlagIsSet(NormRepairRequest::INFO);

        NormRepairRequest::Iterator iterator(req);
        NormObjectId nextObjectId, lastObjectId;
        NormBlockId nextBlockId, lastBlockId;
        UINT16 nextBlockLen, lastBlockLen;
        NormSegmentId nextSegmentId, lastSegmentId;
        while (iterator.NextRepairItem(&nextObjectId, &nextBlockId, 
                                       &nextBlockLen, &nextSegmentId))
        {
            if (NormRepairRequest::RANGES == requestForm)
            {
                if (!iterator.NextRepairItem(&lastObjectId, &lastBlockId, 
                                             &lastBlockLen, &lastSegmentId))
                {
                    DMSG(0, "NormServerNode::HandleRepairContent() node>%lu recvd incomplete RANGE request!\n",
                            LocalNodeId());
                    continue;  // (TBD) break/return instead???  
                }  
                // (TBD) test for valid range form/level
            }
            else
            {
                lastObjectId = nextObjectId;
                lastBlockId = nextBlockId;
                lastBlockLen = nextBlockLen;
                lastSegmentId = nextSegmentId;
            }   
            
            switch(requestLevel)
            {
                case INFO:
                {
                    while (nextObjectId <= lastObjectId)
                    {
                        NormObject* obj = rx_table.Find(nextObjectId);
                        if (obj) obj->SetRepairInfo();  
                        nextObjectId++;
                    }
                    break;
                }
                case OBJECT:
                    rx_repair_mask.SetBits(nextObjectId, lastObjectId - nextObjectId + 1);
                    break;
                case BLOCK:
                {
                    if (nextObjectId != prevObjectId) freshObject = true;
                    if (freshObject) 
                    {
                        object = rx_table.Find(nextObjectId);
                        prevObjectId = nextObjectId;
                    }
                    if (object) 
                    {
                        if (repairInfo) object->SetRepairInfo();
                        object->SetRepairs(nextBlockId, lastBlockId); 
                    } 
                    break;
                }
                case SEGMENT:
                {
                    if (nextObjectId != prevObjectId) freshObject = true;
                    if (freshObject) 
                    {
                        object = rx_table.Find(nextObjectId);
                        prevObjectId = nextObjectId;
                    }
                    if (object)
                    {
                        if (repairInfo) object->SetRepairInfo(); 
                        if (nextBlockId != prevBlockId) freshBlock = true;
                        if (freshBlock)
                        {
                            block = object->FindBlock(nextBlockId);
                            prevBlockId = nextBlockId;
                        }
                        if (block) 
                            block->SetRepairs(nextSegmentId,lastSegmentId);
                    }
                    break;
                }
            }  // end switch(requestLevel)
        }  // end while (iterator.NextRepairItem())
    }  // end while (nack.UnpackRepairRequest())
}  // end NormServerNode::HandleRepairContent()


void NormServerNode::CalculateGrttResponse(const struct timeval&    currentTime,
                                           struct timeval&          grttResponse) const
{
    grttResponse.tv_sec = grttResponse.tv_usec = 0;
    if (grtt_send_time.tv_sec || grtt_send_time.tv_usec)
    {
        // 1st - Get current time
        grttResponse = currentTime;    
        // 2nd - Calculate hold_time (current_time - recv_time)
        if (grttResponse.tv_usec < grtt_recv_time.tv_usec)
        {
            grttResponse.tv_sec = grttResponse.tv_sec - grtt_recv_time.tv_sec - 1;
            grttResponse.tv_usec = 1000000 - (grtt_recv_time.tv_usec - 
                                              grttResponse.tv_usec);
        }
        else
        {
            grttResponse.tv_sec = grttResponse.tv_sec - grtt_recv_time.tv_sec;
            grttResponse.tv_usec = grttResponse.tv_usec - grtt_recv_time.tv_usec;
        }
        // 3rd - Calculate adjusted grtt_send_time (hold_time + send_time)
        grttResponse.tv_sec += grtt_send_time.tv_sec;
        grttResponse.tv_usec += grtt_send_time.tv_usec;
        if (grttResponse.tv_usec > 1000000)
        {
            grttResponse.tv_usec -= 1000000;
            grttResponse.tv_sec += 1;
        }    
    }
}  // end NormServerNode::CalculateGrttResponse()

void NormServerNode::DeleteObject(NormObject* obj)
{
    if (rx_table.Remove(obj))
        rx_pending_mask.Unset(obj->GetId());
    obj->Close();
    obj->Release();
}  // end NormServerNode::DeleteObject()

NormBlock* NormServerNode::GetFreeBlock(NormObjectId objectId, NormBlockId blockId)
{
    NormBlock* b = block_pool.Get();
    if (!b)
    {
        if (session.ClientIsSilent())
        {
            // forward iteration to find oldest older object with resources
            NormObjectTable::Iterator iterator(rx_table);
            NormObject* obj;
            while ((obj = iterator.GetNextObject()))
            {
                if (obj->GetId() > objectId)
                {
                    break;   
                }
                else
                {
                    if (obj->GetId() < objectId)
                        b = obj->StealOldestBlock(false);
                    else
                        b = obj->StealOldestBlock(true, blockId); 
                    if (b) 
                    {
                        b->EmptyToPool(segment_pool);
                        break;
                    }
                }
            }            
        }
        else
        {
            // reverse iteration to find newest newer object with resources
            NormObjectTable::Iterator iterator(rx_table);
            NormObject* obj;
            while ((obj = iterator.GetPrevObject()))
            {
                if (obj->GetId() < objectId) 
                {
                    break;
                }
                else
                {
                    if (obj->GetId() > objectId)
                        b = obj->StealNewestBlock(false); 
                    else 
                        b = obj->StealNewestBlock(true, blockId); 
                    if (b) 
                    {
                        b->EmptyToPool(segment_pool);
                        break;
                    }
                }
            } 
        }
    }  
    return b;
}  // end NormServerNode::GetFreeBlock()

char* NormServerNode::GetFreeSegment(NormObjectId objectId, NormBlockId blockId)
{
    if (segment_pool.IsEmpty())
    {
        // First, try to steal (retrievable) buffered source symbol segments
        NormObjectTable::Iterator iterator(rx_table);
        NormObject* obj;
        while ((obj = iterator.GetNextObject()))
        {
            // This takes source segments only from the "oldest" obj/blk
            // (TBD) Should these be from the "newest" obj/blk instead?
            if (obj->ReclaimSourceSegments(segment_pool))
                break;     
        }  
        // Second, if necessary, steal an ordinally "newer" block
        // (TBD) we might try to keep the block state, and only
        //       steal the segment needed?
        while (segment_pool.IsEmpty())
        {
            NormBlock* b = GetFreeBlock(objectId, blockId);
            if (b)
                block_pool.Put(b);
            else
                break;
        }
    }
    return segment_pool.Get();
}  // end NormServerNode::GetFreeSegment()

void NormServerNode::HandleObjectMessage(const NormObjectMsg& msg)
{
    UINT8 grttQuantized = msg.GetGrtt();
    if (grttQuantized != grtt_quantized)
    {
        grtt_quantized = grttQuantized;
        grtt_estimate = NormUnquantizeRtt(grttQuantized);
        DMSG(4, "NormServerNode::HandleObjectMessage() node>%lu server>%lu new grtt: %lf sec\n",
                LocalNodeId(), GetId(), grtt_estimate);
        activity_timer.SetInterval(grtt_estimate*NORM_ROBUST_FACTOR);
        if (activity_timer.IsActive()) activity_timer.Reschedule();
    }
    UINT8 gsizeQuantized = msg.GetGroupSize();
    if (gsizeQuantized != gsize_quantized)
    {
        gsize_quantized = gsizeQuantized;
        gsize_estimate = NormUnquantizeGroupSize(gsizeQuantized);
        DMSG(4, "NormServerNode::HandleObjectMessage() node>%lu server>%lu new group size: %lf\n",
                LocalNodeId(), GetId(), gsize_estimate);
    }
    backoff_factor = (double)msg.GetBackoffFactor();
    
    NormMsg::Type msgType = msg.GetType();
    NormObjectId objectId = msg.GetObjectId();
    NormBlockId blockId;
    NormSegmentId segmentId;
    if (NormMsg::INFO == msgType)
    {
        blockId = 0;
        segmentId = 0;
    }
    else
    {
        const NormDataMsg& data = (const NormDataMsg&)msg;
        // (TBD) verify source block length per new spec
        blockId = data.GetFecBlockId();
        segmentId = data.GetFecSymbolId();  
        
        // The current NORM implementation assumes senders maintain a fixed, common
        // set of FEC coding parameters for its transmissions.  The buffers (on a
        // "per-remote-server basis") for receiver FEC processing are allocated here
        //  when:
        //    1) A NORM_DATA message is received and the buffers have not
        //       been previously allocated, or
        //    2) When the FEC parameters have changed (TBD)
        //
        if (!BuffersAllocated())
        {
            DMSG(4, "NormServerNode::HandleObjectMessage() node>%lu allocating server>%lu buffers ...\n",
                    LocalNodeId(), GetId());
            // Currently,, our implementation requires the FEC Object Transmission Information
            // to properly allocate resources
            NormFtiExtension fti;
            while (msg.GetNextExtension(fti))
            {
                if (NormHeaderExtension::FTI == fti.GetType())
                {
                    // (TBD) pass "fec_id" to Open() method too
                    if (!AllocateBuffers(fti.GetSegmentSize(), 
                                         fti.GetFecMaxBlockLen(),
                                         fti.GetFecNumParity()))
                    {
                        DMSG(0, "NormServerNode::HandleObjectMessage() node>%lu server>%lu buffer allocation error\n",
                                LocalNodeId(), GetId());
                        // (TBD) notify app of error ??
                        return;
                    }  
                    break;
                }
            }
            if (!BuffersAllocated())
            {
                DMSG(0, "NormServerNode::HandleObjectMessage() node>%lu server>%lu - no FTI provided!\n",
                         LocalNodeId(), GetId());
                // (TBD) notify app of error ??
                return;  
            }             
        }
        else
        {
            // (TBD) make sure FEC parameters are still the same.    
        }        
    }
    
    ObjectStatus status;
    if (synchronized)
    {
        status = UpdateSyncStatus(objectId);
    }
    else
    {      
        // Does this object message meet our sync policy?
        if (SyncTest(msg))
        {
            Sync(objectId);
            SetPending(objectId); 
            status = OBJ_NEW;
        }
        else
        {
            // The hacky use of "sync_id" here keeps the debug message from
            // printing too often while "waiting to sync" ...
            if (0 == sync_id)
            {
                DMSG(0, "NormServerNode::HandleObjectMessage() waiting to sync ...\n");
                sync_id = 100;
            }
            else
            {
                sync_id--;
            }
            return;   
        }
    }    
       
    NormObject* obj = NULL;
    switch (status)
    {
        case OBJ_PENDING:
            if ((obj = rx_table.Find(objectId))) 
                break;
        case OBJ_NEW:
        {
            if (msg.FlagIsSet(NormObjectMsg::FLAG_STREAM))
            {
                if (!(obj = new NormStreamObject(session, this, objectId)))
                {
                    DMSG(0, "NormServerNode::HandleObjectMessage() new NORM_OBJECT_STREAM error: %s\n",
                            strerror(errno));
                }
            }
            else if (msg.FlagIsSet(NormObjectMsg::FLAG_FILE))
            {
#ifdef SIMULATE
                if (!(obj = new NormSimObject(session, this, objectId)))
#else
                if (!(obj = new NormFileObject(session, this, objectId)))
#endif
                {
                    DMSG(0, "NormServerNode::HandleObjectMessage() new NORM_OBJECT_FILE error: %s\n",
                            strerror(errno));
                }
            }
            else
            {
                if (!(obj = new NormDataObject(session, this, objectId)))
                {
                    DMSG(0, "NormServerNode::HandleObjectMessage() new NORM_OBJECT_DATA error: %s\n",
                            strerror(errno));
                }
            }
            
            if (obj)
            { 
                NormFtiExtension fti;
                while (msg.GetNextExtension(fti))
                {
                    if (NormHeaderExtension::FTI == fti.GetType())
                    {
                        // Pre-open receive object and notify app for accept.
                        if (obj->Open(fti.GetObjectSize(), 
                                      msg.FlagIsSet(NormObjectMsg::FLAG_INFO),
                                      fti.GetSegmentSize(), 
                                      fti.GetFecMaxBlockLen(),
                                      fti.GetFecNumParity()))
                        {
                            session.Notify(NormController::RX_OBJECT_NEW, this, obj);
                            if (obj->Accepted())
                            {
                                rx_table.Insert(obj);
                                obj->Retain();
                                // (TBD) Do I _need_ to call "StreamUpdateStatus()" here?
                                if (obj->IsStream()) 
                                    (static_cast<NormStreamObject*>(obj))->StreamUpdateStatus(blockId);
                                DMSG(8, "NormServerNode::HandleObjectMessage() node>%lu server>%lu new obj>%hu\n", 
                                    LocalNodeId(), GetId(), (UINT16)objectId);
                            }
                            else
                            {
                                DeleteObject(obj);
                                obj = NULL;    
                            }
                        }
                        else        
                        {
                            DeleteObject(obj);
                            obj = NULL;   
                        }
                        break;
                    }
                }
                if (obj && !obj->IsOpen())
                {
                    DMSG(0, "NormServerNode::HandleObjectMessage() node>%lu server>%lu "
                            "new obj>%hu - no FTI provided!\n", LocalNodeId(), GetId(), (UINT16)objectId);
                    DeleteObject(obj);
                    obj = NULL;
                }
            }
            break;
        }
        case OBJ_COMPLETE:
            obj = NULL;
            break;
        default:
            ASSERT(0);
            break;
    }  // end switch(status)   
    
    if (obj)
    {
        obj->HandleObjectMessage(msg, msgType, blockId, segmentId);
        if (!obj->IsPending())
        {
            // Reliable reception of this object has completed
            if (NormObject::FILE == obj->GetType()) 
#ifdef SIMULATE
                static_cast<NormSimObject*>(obj)->Close();           
#else
                static_cast<NormFileObject*>(obj)->Close();
#endif // !SIMULATE
            if (NormObject::STREAM != obj->GetType())
            {
                // Streams never complete
                session.Notify(NormController::RX_OBJECT_COMPLETED, this, obj);
                DeleteObject(obj);
                completion_count++;
            }
        } 
    }  
    
    switch (repair_boundary)
    {
        case BLOCK_BOUNDARY:
            // Normal FEC "block boundary" repair check
            // (checks for repair needs for objects/blocks _prior_ to current objectId::blockId)
            RepairCheck(NormObject::TO_BLOCK, objectId, blockId, segmentId);
            break;
        case OBJECT_BOUNDARY:
            // Optional "object boundary repair check (non-streams only!)
            // (checks for repair needs for objects _prior_ to current objectId)
            // (also requests "info" for current objectId)
            if (obj && (NormObject::STREAM == obj->GetType()))
                RepairCheck(NormObject::TO_BLOCK, objectId, blockId, segmentId);
            else
                RepairCheck(NormObject::THRU_INFO, objectId, blockId, segmentId);
            break;
    }
}  // end NormServerNode::HandleObjectMessage()

bool NormServerNode::SyncTest(const NormObjectMsg& msg) const
{
    // Allow sync on stream at any time
    bool result = msg.FlagIsSet(NormObjectMsg::FLAG_STREAM);
                
    // Allow sync on INFO or block zero DATA message
    result = result || (NormMsg::INFO == msg.GetType()) ? 
                            true : (0 == ((const NormDataMsg&)msg).GetFecBlockId());
    
    // Never sync on repair messages
    result = result && !msg.FlagIsSet(NormObjectMsg::FLAG_REPAIR);
    
    return result;
    
}  // end NormServerNode::SyncTest()


// This method establishes the sync point "sync_id"
// objectId.  The sync point is the first ordinal
// object id for which the receiver is maintaining
// reliability.  Objects prior to the "sync point"
// are ignored.
// The related member variables and their purpose:
// "sync_id" - sync point object id, gets rolled upward
//             in NormServerNode::SetPending() to deal with wrap
//
// "next_id" - id of next expected pending object
//             (set in NormServerNode::SetPending())
//
// "max_pending_object" - max object id heard from sender
//             (inited in NormServerNode::Sync() on 
//              initial sync, update in NormServerNode::RepairCheck()
//
void NormServerNode::Sync(NormObjectId objectId)
{
    if (synchronized)
    {
        NormObjectId firstPending;
        if (GetFirstPending(firstPending))
        {
            NormObjectId lastPending;
            GetLastPending(lastPending);
            if ((objectId > lastPending) || ((next_id - objectId) > max_pending_range))
            {
                NormObject* obj;
                while ((obj = rx_table.Find(rx_table.RangeLo()))) 
                {
                    session.Notify(NormController::RX_OBJECT_ABORTED, this, obj);
                    DeleteObject(obj);
                    failure_count++;
                }
                rx_pending_mask.Clear(); 
            }
            else if (objectId > firstPending)
            {
               NormObject* obj;
               while ((obj = rx_table.Find(rx_table.RangeLo())) &&
                      (obj->GetId() < objectId)) 
               {
                   session.Notify(NormController::RX_OBJECT_ABORTED, this, obj);
                   DeleteObject(obj);
                   failure_count++;
               }
               unsigned long numBits = (UINT16)(objectId - firstPending) + 1;
               rx_pending_mask.UnsetBits(firstPending, numBits); 
            }
        }  
        if ((next_id < objectId) || ((next_id - objectId) > max_pending_range))
        {
            max_pending_object = next_id = objectId;
        }
        sync_id = objectId;
        ASSERT(OBJ_INVALID != GetObjectStatus(objectId));
    }
    else
    {
        ASSERT(!rx_pending_mask.IsSet());
        sync_id = next_id = max_pending_object = objectId;
        synchronized = true;
    }
}  // end NormServerNode::Sync()

NormServerNode::ObjectStatus NormServerNode::UpdateSyncStatus(const NormObjectId& objectId)
{
    ASSERT(synchronized);
    ObjectStatus status = GetObjectStatus(objectId);
    switch (status)
    {
        case OBJ_INVALID:
            // (TBD) We may want to control re-sync policy options
            //       or revert to fresh sync if sync is totally lost,
            //       otherwise SQUELCH process will get things in order
            DMSG(2, "NormServerNode::UpdateSyncStatus() node>%lu re-syncing to server>%lu...\n",
                    LocalNodeId(), GetId());
            Sync(objectId);
            resync_count++;
            status = OBJ_NEW;
        case OBJ_NEW:
            SetPending(objectId);
            break;
        default:
            break;
    }
    return status;
}  // end NormServerNode::UpdateSyncStatus()

void NormServerNode::SetPending(NormObjectId objectId)
{
    ASSERT(synchronized);
    ASSERT(OBJ_NEW == GetObjectStatus(objectId));
    if (objectId < next_id)
    {
        rx_pending_mask.Set(objectId);
    }
    else
    {
        rx_pending_mask.SetBits(next_id, objectId - next_id + 1);
        next_id = objectId + 1; 
        // This prevents the "sync_id" from getting stale
        GetFirstPending(sync_id);
    }
}  // end NormServerNode::SetPending()


NormServerNode::ObjectStatus NormServerNode::GetObjectStatus(const NormObjectId& objectId) const
{
   if (synchronized)
   {
       if (objectId < sync_id) 
       {
           if ((sync_id - objectId) > max_pending_range)
           {
                return OBJ_INVALID;
           }
           else
           {
                return OBJ_COMPLETE;  
           }
       }
       else
       {
            if (objectId < next_id)
            {
                if (rx_pending_mask.Test(objectId))
                {
                    return OBJ_PENDING;
                }
                else
                {
                    return OBJ_COMPLETE;
                }
            }
            else
            {
                if (rx_pending_mask.IsSet())
                {
                    if (rx_pending_mask.CanSet(objectId))
                    {
                        return OBJ_NEW;
                    }
                    else
                    {
                        return OBJ_INVALID;
                    }
                }
                else
                {
                    NormObjectId delta = objectId - next_id + 1;
                    if (delta > NormObjectId((UINT16)rx_pending_mask.Size()))
                    {
                        return OBJ_INVALID;
                    }
                    else
                    {
                        return OBJ_NEW;
                    }
                }
            }  
        }
   }
   else
   {
        return OBJ_NEW;   
   } 
}  // end NormServerNode::ObjectStatus()

// This is a "passive" THRU_SEGMENT repair check
// (used to for watermark ack check)
bool NormServerNode::PassiveRepairCheck(NormObjectId           objectId,  
                                        NormBlockId            blockId,
                                        NormSegmentId          segmentId)
{
    NormObjectId nextId;
    if (GetFirstPending(nextId))
    {
        if (nextId < objectId)
        {
            return true;
        }
        else if (nextId == objectId)
        {
            NormObject* obj = rx_table.Find(nextId);
            if (obj)
                return obj->PassiveRepairCheck(blockId, segmentId);
            else
                return true;  // entire object pending
        }
    }
    return false;  
}  // end NormServerNode::PassiveRepairCheck()

// This is the "active" repair check, which may activate NACKing
void NormServerNode::RepairCheck(NormObject::CheckLevel checkLevel,
                                 NormObjectId           objectId,  
                                 NormBlockId            blockId,
                                 NormSegmentId          segmentId)
{    
    ASSERT(synchronized);
    if (objectId > max_pending_object) max_pending_object = objectId;
    if (!repair_timer.IsActive())
    {
        // repair timer inactive
        bool startTimer = false;
        NormObjectId nextId;
        if (GetFirstPending(nextId))
        {
            if (rx_repair_mask.IsSet()) rx_repair_mask.Clear();
            do
            {
                if (nextId > objectId) break;
                NormObject* obj = rx_table.Find(nextId);
                if (obj)
                {
                    NormObject::CheckLevel level;
                    if (nextId < objectId)
                    {
                        level = NormObject::THRU_OBJECT;
                    }
                    else
                    {
                        level = checkLevel;
                    }
                    startTimer |= 
                        obj->ClientRepairCheck(level, blockId, segmentId, false);
                }
                else
                {
                    startTimer = true;
                }
                nextId++;
            } while (GetNextPending(nextId));
            current_object_id = objectId;
            if (startTimer)
            {
                // BACKOFF related code
                double backoffInterval = 
                    (session.Address().IsMulticast() && (backoff_factor > 0.0)) ?
                        ExponentialRand(grtt_estimate*backoff_factor, gsize_estimate) : 
                        0.0;
                repair_timer.SetInterval(backoffInterval);
                DMSG(4, "NormServerNode::RepairCheck() node>%lu begin NACK backoff: %lf sec)...\n",
                        LocalNodeId(), backoffInterval);
                session.ActivateTimer(repair_timer);  
            }
        }
    }
    else if (repair_timer.GetRepeatCount())
    {
        // Repair timer in backoff phase
        // Trim server current transmit position reference
        NormObject* obj = rx_table.Find(objectId);
        if (obj) obj->ClientRepairCheck(checkLevel, blockId, segmentId, true);
        if (objectId < current_object_id) current_object_id = objectId;
    }
    else
    {
        // Repair timer in holdoff phase   
        bool rewindDetected = objectId < current_object_id;
        if (!rewindDetected)
        {
            NormObject* obj = rx_table.Find(objectId);
            if (obj) 
                rewindDetected = obj->ClientRepairCheck(checkLevel, blockId, segmentId, true, true);
        }
        if (rewindDetected)
        {
            repair_timer.Deactivate();
            DMSG(4, "NormServerNode::RepairCheck() node>%lu server rewind detected, ending NACK holdoff ...\n",
                    LocalNodeId());
            
            RepairCheck(checkLevel, objectId, blockId, segmentId);
        }
    }
}  // end NormServerNode::RepairCheck()

// When repair timer fires, possibly build a NACK
// and queue for transmission to this server node
bool NormServerNode::OnRepairTimeout(ProtoTimer& /*theTimer*/)
{
    switch(repair_timer.GetRepeatCount())
    {
        case 0:  // hold-off time complete
            DMSG(4, "NormServerNode::OnRepairTimeout() node>%lu end NACK hold-off ...\n",
                    LocalNodeId());
            break;
            
        case 1:  // back-off timeout complete
        {
            DMSG(4, "NormServerNode::OnRepairTimeout() node>%lu end NACK back-off ...\n",
                    LocalNodeId());
            // 1) Were we suppressed? 
            NormObjectId nextId;
            if (GetFirstPending(nextId))
            {
                // This loop checks to see if we have any repair pending objects
                // (If we don't have any, that means we were suppressed)
                bool repairPending = false;
                do
                {
                    if (nextId > current_object_id) break;
                    if (!rx_repair_mask.Test(nextId)) 
                    {
                        NormObject* obj = rx_table.Find(nextId);
                        if (!obj || obj->IsRepairPending(nextId != current_object_id))
                        {
                            repairPending = true;
                            break;
                        }                        
                    }
                    nextId++;
                } while (GetNextPending(nextId));
                
                if (repairPending)
                {
                    // We weren't completely suppressed, so build NACK
                    NormNackMsg* nack = (NormNackMsg*)session.GetMessageFromPool();
                    if (!nack)
                    {
                        DMSG(3, "NormServerNode::OnRepairTimeout() node>%lu Warning! "
                                "message pool empty ...\n", LocalNodeId());
                        repair_timer.Deactivate();
                        return false;   
                    }
                    nack->Init();
                    bool nackAppended = false;
                    
                    if (cc_enable)
                    {
                        NormCCFeedbackExtension ext;
                        nack->AttachExtension(ext);
                        if (is_clr) 
                            ext.SetCCFlag(NormCC::CLR);
                        else if (is_plr)
                            ext.SetCCFlag(NormCC::CLR);
                        if (rtt_confirmed)
                            ext.SetCCFlag(NormCC::RTT);
                        ext.SetCCRtt(rtt_quantized);
                        double ccLoss = LossEstimate();
                        UINT16 lossQuantized = NormQuantizeLoss(ccLoss);
                        ext.SetCCLoss(lossQuantized);
                        if (slow_start)
                        {
                            ext.SetCCFlag(NormCC::START);
                            ext.SetCCRate(NormQuantizeRate(2.0 * recv_rate));
                        }
                        else
                        {
                            double ccRate = NormSession::CalculateRate(nominal_packet_size,
                                                                       rtt_estimate,
                                                                       ccLoss);
                            ext.SetCCRate(NormQuantizeRate(ccRate));
                        }
                        DMSG(6, "NormServerNode::OnRepairTimeout() node>%lu sending NACK rate:%lf kbps (rtt:%lf loss:%lf s:%hu) slow_start:%d\n",
                                 LocalNodeId(), NormUnquantizeRate(ext.GetCCRate()) * (8.0/1000.0), 
                                 rtt_estimate, ccLoss, nominal_packet_size, slow_start);
                        ext.SetCCSequence(cc_sequence);
                        // Cancel potential pending NORM_ACK(RTT) 
                        if (cc_timer.IsActive())
                        {
                            cc_timer.Deactivate();
                            cc_timer.SetInterval(grtt_estimate*backoff_factor);
                            session.ActivateTimer(cc_timer);
                            cc_timer.DecrementRepeatCount();   
                        }
                    }  // end if (cc_enable)
                    
                    // Iterate through rx pending object list, 
                    // appending repair requests as needed
                    NormRepairRequest req;
                    NormRepairRequest::Form prevForm = NormRepairRequest::INVALID;
                    bool iterating = GetFirstPending(nextId);
                    iterating = iterating && (nextId <= max_pending_object);
                    NormObjectId prevId = nextId;
                    UINT16 consecutiveCount = 0;
                    while (iterating || (0 != consecutiveCount))
                    {
                        bool appendRequest = false;
                        NormObject* obj = iterating ? rx_table.Find(nextId) : NULL;
                        if (obj)
                            appendRequest = true;
                        else if (iterating && ((nextId - prevId) == consecutiveCount))
                            consecutiveCount++;  // consecutive series of missing objs starts/continues 
                        else
                            appendRequest = true;  // consecutive series broken or finished
                        
                        if (appendRequest)
                        {
                            NormRepairRequest::Form nextForm;
                            switch (consecutiveCount)
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
                            }  
                            if (prevForm != nextForm)
                            {
                                if ((NormRepairRequest::INVALID != prevForm) &&
                                    (NormObject::NACK_NONE != default_nacking_mode))
                                {
                                    if (0 == nack->PackRepairRequest(req))
                                    {
                                        DMSG(3, "NormServerNode::OnRepairTimeout() warning: full NACK msg\n");
                                        break;
                                    } 
                                    nackAppended = true;
                                }
                                if (NormRepairRequest::INVALID != nextForm)
                                {
                                    nack->AttachRepairRequest(req, segment_size); // (TBD) error check
                                    req.SetForm(nextForm);
                                    req.ResetFlags();
                                    // Set flags for missing objects according to
                                    // default "nacking mode"
                                    if (NormObject::NACK_INFO_ONLY == default_nacking_mode)
                                        req.SetFlag(NormRepairRequest::INFO);
                                    else
                                        req.SetFlag(NormRepairRequest::OBJECT);
                                }
                                prevForm = nextForm;
                            }
                            switch (nextForm)
                            {
                                case NormRepairRequest::ITEMS:
                                    req.AppendRepairItem(prevId, 0, ndata, 0);
                                    if (2 == consecutiveCount)
                                        req.AppendRepairItem(prevId+1, 0, ndata, 0);
                                    break;
                                case NormRepairRequest::RANGES:
                                    req.AppendRepairRange(prevId, 0, ndata, 0,
                                                          prevId+consecutiveCount-1, 0, ndata, 0);
                                    break;
                                default:
                                    break;  
                            }
                            
                            if (obj)
                            {
                                if (obj->IsPending(nextId != max_pending_object))
                                {
                                    if ((NormRepairRequest::INVALID != prevForm) &&
                                        (NormObject::NACK_NONE != default_nacking_mode))
                                    {
                                        if (0 == nack->PackRepairRequest(req))
                                        {
                                            DMSG(3, "NormServerNode::OnRepairTimeout() warning: full NACK msg\n");
                                            break;
                                        } 
                                        nackAppended = true;
                                    }
                                    prevForm = NormRepairRequest::INVALID; 
                                    bool flush = (nextId != max_pending_object);
                                    nackAppended |= obj->AppendRepairRequest(*nack, flush); 
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
                        nextId++;
                        iterating = GetNextPending(nextId);
                    } // end while (iterating || (0 != consecutiveCount))
                    // Pack in repair request "req" if it's outstanding
                    if ((NormRepairRequest::INVALID != prevForm) &&
                        (NormObject::NACK_NONE != default_nacking_mode))
                    {
                        if (0 != nack->PackRepairRequest(req))
                            nackAppended = true;
                        else
                            DMSG(3, "NormServerNode::OnRepairTimeout() warning: full NACK msg\n");
                    }
                    // Queue NACK for transmission
                    nack->SetServerId(GetId());
                    nack->SetSessionId(session_id);
                    // GRTT response is deferred until transmit time
                    
                    if (unicast_nacks)
                        nack->SetDestination(GetAddress());
                    else
                        nack->SetDestination(session.Address());
                    // Debug check to make sure NACK has content
                    if (nackAppended)
                    {
                        ASSERT(nack->GetRepairContentLength() > 0);
                        //session.QueueMessage(nack);
                        session.SendMessage(*nack);
                        session.ReturnMessageToPool(nack);
                        nack_count++;
                    }
                    else
                    {
                        // The nack had no repair request content,
                        // perhaps because of our "nacking mode"
                        // even though there were pending objects
                        DMSG(4, "NormServerNode::OnRepairTimeout() node>%lu zero content nack ...\n",
                            LocalNodeId());
                        session.ReturnMessageToPool(nack);
                    }
                }
                else
                {
                    suppress_count++;
                    DMSG(4, "NormServerNode::OnRepairTimeout() node>%lu NACK SUPPRESSED ...\n",
                            LocalNodeId());
                }  // end if/else(repairPending)
                // BACKOFF related code
                double holdoffInterval = 
                    session.Address().IsMulticast() ? grtt_estimate*(backoff_factor + 2.0) : 
                                                       grtt_estimate;
                // backoff == 0.0 is a special case
                //holdoffInterval = (backoff_factor > 0.0) ? holdoffInterval : 1.0*grtt_estimate;
                
                repair_timer.SetInterval(holdoffInterval);
                DMSG(4, "NormServerNode::OnRepairTimeout() node>%lu begin NACK hold-off: %lf sec ...\n",
                         LocalNodeId(), holdoffInterval);
                
            }
            else
            {
                DMSG(4, "NormServerNode::OnRepairTimeout() node>%lu nothing pending ...\n",
                        LocalNodeId());
                // (TBD) cancel hold-off timeout ???  
            }  // end if/else (repair_mask.IsSet())       
        }
        break;
        
        default: // should never occur
            ASSERT(0);
            break;
    }
    return true;
}  // end NormServerNode::OnRepairTimeout()

void NormServerNode::UpdateRecvRate(const struct timeval& currentTime, unsigned short msgSize)
{
    if (prev_update_time.tv_sec || prev_update_time.tv_usec)
    {
        double interval = (double)(currentTime.tv_sec - prev_update_time.tv_sec);
        if (currentTime.tv_usec > prev_update_time.tv_sec)
            interval += 1.0e-06*(double)(currentTime.tv_usec - prev_update_time.tv_usec);
        else
            interval -= 1.0e-06*(double)(prev_update_time.tv_usec - currentTime.tv_usec);            
        double rttEstimate = rtt_confirmed ? rtt_estimate : grtt_estimate;
        // We put a 0.100 sec lower bound on our rttEstimate for the recv_rate measurement
        // interval because of the typical limited granularity of our system clock
        rttEstimate = rttEstimate < 0.1 ? 0.1 : rttEstimate;
        recv_accumulator += msgSize;
        if (interval >= rttEstimate)
        {
            recv_rate = ((double)(recv_accumulator)) / interval;
            prev_update_time = currentTime;
            recv_accumulator = 0;
        }
    }
    else
    {
        if (send_rate > 0.0)
            recv_rate = send_rate;
        else
            recv_rate = ((double)msgSize) / grtt_estimate;
        prev_update_time = currentTime;
        recv_accumulator = 0;
    }
    nominal_packet_size += 0.05 * (((double)msgSize) - nominal_packet_size);
}  // end NormServerNode::UpdateRecvRate()

void NormServerNode::Activate()
{
    if (!activity_timer.IsActive())
    {
        activity_timer.SetInterval(grtt_estimate*NORM_ROBUST_FACTOR);
        session.ActivateTimer(activity_timer);
        server_active = false;
        session.Notify(NormController::REMOTE_SERVER_ACTIVE, this, NULL);
    }
    else
    {
        server_active = true;
    }
}  // end NormServerNode::Activate()

bool NormServerNode::OnActivityTimeout(ProtoTimer& /*theTimer*/)
{
    if (server_active)
    {
        activity_timer.ResetRepeat();
    }
    else if (0 == activity_timer.GetRepeatCount())
    {
        // Serve completely inactive?
        DMSG(0, "NormServerNode::OnActivityTimeout() node>%lu server>%lu gone inactive?\n",
                LocalNodeId(), GetId());
        FreeBuffers();
        session.Notify(NormController::REMOTE_SERVER_INACTIVE, this, NULL);
    }
    else
    {
        DMSG(4, "NormServerNode::OnActivityTimeout() node>%lu for server>%lu\n",
            LocalNodeId(), GetId());
        struct timeval currentTime;
        ::ProtoSystemTime(currentTime);
        UpdateRecvRate(currentTime, 0);
        if (synchronized)
        {
            NormObject* objMax = rx_table.Find(max_pending_object);
            if (NULL != objMax)
            {
                NormSegmentId segMax = objMax->GetMaxPendingSegmentId();
                if (0 != segMax)
                    RepairCheck(NormObject::THRU_SEGMENT, 
                                max_pending_object,
                                objMax->GetMaxPendingBlockId(), 
                                objMax->GetMaxPendingSegmentId() - 1);
                else
                    RepairCheck(NormObject::TO_BLOCK, 
                                max_pending_object,
                                objMax->GetMaxPendingBlockId(), 
                                0);
            }
            else
                RepairCheck(NormObject::TO_BLOCK,   // (TBD) thru object???
                            max_pending_object, 0, 0);
        }
    }
    server_active = false;
    return true;     
}  // end NormServerNode::OnActivityTimeout()
  
bool NormServerNode::UpdateLossEstimate(const struct timeval& currentTime, 
                                        unsigned short        seq, 
                                        bool                  ecn)
{
    bool result = loss_estimator.Update(currentTime, seq, ecn);
    if (result && slow_start)
    {
        double lossInit = (recv_rate * rtt_estimate) /
                          (segment_size*sqrt(3.0/2.0));
        lossInit = (lossInit*lossInit);
        double currentInterval = (double)loss_estimator.LastLossInterval();
        lossInit = 1.0 / MAX(lossInit, currentInterval);
        loss_estimator.SetInitialLoss(lossInit);
        slow_start = false;
    }
    return result;
}

void NormServerNode::AttachCCFeedback(NormAckMsg& ack)
{
    // GRTT response is deferred until transmit time
    NormCCFeedbackExtension ext;
    ack.AttachExtension(ext);
    if (is_clr) 
    {
        ext.SetCCFlag(NormCC::CLR);
    }
    else if (is_plr)
        ext.SetCCFlag(NormCC::PLR);
    if (rtt_confirmed)
        ext.SetCCFlag(NormCC::RTT);
    ext.SetCCRtt(rtt_quantized);
    double ccLoss = LossEstimate();
    UINT16 lossQuantized = NormQuantizeLoss(ccLoss);
    ext.SetCCLoss(lossQuantized);
    if (slow_start)
    {
        ext.SetCCFlag(NormCC::START);
        ext.SetCCRate(NormQuantizeRate(2.0 * recv_rate));
    }
    else
    {
        double ccRate = NormSession::CalculateRate(nominal_packet_size, 
                                                   rtt_estimate, ccLoss);
        ext.SetCCRate(NormQuantizeRate(ccRate));
    }
    //DMSG(0, "NormServerNode::OnCCTimeout() node>%lu sending ACK rate:%lf kbps (rtt:%lf loss:%lf s:%lf recvRate:%lf) slow_start:%d\n",
    //               LocalNodeId(), NormUnquantizeRate(ext.GetCCRate()) * (8.0/1000.0), 
    //               rtt_estimate, ccLoss, nominal_packet_size, recv_rate*(8.0/1000.), slow_start);
    ext.SetCCSequence(cc_sequence);
}  // end 

bool NormServerNode::OnCCTimeout(ProtoTimer& /*theTimer*/)
{
    // Build and queue ACK()   
    switch (cc_timer.GetRepeatCount())
    {
        case 0:
            // "hold-off" time has ended
            break;
            
        case 1:
        {
            // We weren't suppressed, so build an ACK(RTT) and send
            NormAckMsg* ack = (NormAckMsg*)session.GetMessageFromPool();
            if (!ack)
            {
                DMSG(3, "NormServerNode::OnCCTimeout() node>%lu warning: "
                        "message pool empty ...\n", LocalNodeId());
                if (cc_timer.IsActive()) cc_timer.Deactivate();
                return false;   
            }
            ack->Init();
            ack->SetServerId(GetId());
            ack->SetSessionId(session_id);
            ack->SetAckType(NormAck::CC);
            ack->SetAckId(0);
            
            AttachCCFeedback(*ack);  // cc feedback extension
            
            if (unicast_nacks)
                ack->SetDestination(GetAddress());
            else
                ack->SetDestination(session.Address());
            //if (is_clr || is_plr)
            {
                // Don't rate-limit feedback messages.
                session.SendMessage(*ack);
                session.ReturnMessageToPool(ack);
            }
            //else
            //{
            //    session.QueueMessage(ack);
            //}
            
            // Begin cc_timer "holdoff" phase
            cc_timer.SetInterval(grtt_estimate*backoff_factor);
            return true;
        }
        
        default:
            // Should never occur
            ASSERT(0);
            break;
    }
    return true;
}  // end NormServerNode::OnCCTimeout()

bool NormServerNode::OnAckTimeout(ProtoTimer& /*theTimer*/)
{
    NormAckFlushMsg* ack = (NormAckFlushMsg*)session.GetMessageFromPool();
    if (ack)
    {
        ack->Init();
        ack->SetServerId(GetId());
        ack->SetSessionId(session_id);
        ack->SetAckId(0);
        AttachCCFeedback(*ack);
        ack->SetObjectId(watermark_object_id);
        ack->SetFecBlockId(watermark_block_id);
        ack->SetFecBlockLen(ndata); // yuk
        ack->SetFecSymbolId(watermark_segment_id);
        if (unicast_nacks)
            ack->SetDestination(GetAddress());
        else
            ack->SetDestination(session.Address());
        
        // Don't rate limit feedback messages
        session.SendMessage(*ack);
        session.ReturnMessageToPool(ack);
        if (!is_clr && !is_plr)
        {
            // Install cc feedback holdoff
            if (cc_timer.IsActive()) cc_timer.Deactivate();
            cc_timer.SetInterval(grtt_estimate*backoff_factor);
            session.ActivateTimer(cc_timer);
            cc_timer.DecrementRepeatCount();
        } 
    }
    else
    {
        DMSG(3, "NormServerNode::OnAckTimeout() warning: message pool exhausted!\n");
    }
    return true;
}  // end NormServerNode::OnAckTimeout()


NormAckingNode::NormAckingNode(class NormSession& theSession, NormNodeId nodeId)
 : NormNode(theSession, nodeId), ack_received(false), req_count(NORM_ROBUST_FACTOR)
{
    
}

NormAckingNode::~NormAckingNode()
{
}

NormNodeTree::NormNodeTree()
 : root(NULL)
{

}

NormNodeTree::~NormNodeTree()
{
    Destroy();
}


NormNode *NormNodeTree::FindNodeById(NormNodeId nodeId) const
{
    NormNode* x = root;
    while(x && (x->id != nodeId))
    {
		if (nodeId < x->id)
            x = x->left;
        else
            x = x->right;
    }
    return x;   
}  // end NormNodeTree::FindNodeById() 



void NormNodeTree::AttachNode(NormNode *node)
{
    ASSERT(node);
    node->left = NULL;
    node->right = NULL;
    NormNode *x = root;
    while (x)
    {
        if (node->id < x->id)
        {
            if (!x->left)
            {
                x->left = node;
                node->parent = x;
                return;
            }
            else
            {
                x = x->left;
            }
        }
        else
        {
           if (!x->right)
           {
               x->right = node;
               node->parent = x;
               return;
           }
           else
           {
               x = x->right;
           }   
        }
    }
    root = node;  // root _was_ NULL
}  // end NormNodeTree::AttachNode()


void NormNodeTree::DetachNode(NormNode* node)
{
    ASSERT(node);
    NormNode* x;
    NormNode* y;
    if (!node->left || !node->right)
    {
        y = node;
    }
    else
    {
        if (node->right)
        {
            y = node->right;
            while (y->left) y = y->left;
        }
        else
        {
            x = node;
            y = node->parent;
            while(y && (y->right == x))
            {
                x = y;
                y = y->parent;
            }
        }
    }
    if (y->left)
        x = y->left;
    else
        x = y->right;
    if (x) x->parent = y->parent;
    if (!y->parent)
        root = x;
    else if (y == y->parent->left)
        y->parent->left = x;
    else
        y->parent->right = x;
    
    if (node != y)
    {
        if ((y->parent = node->parent))
        {
            if (y->id < y->parent->id)
                y->parent->left = y;
            else
                y->parent->right = y;
        }
        else
        {
            root = y;
        }
        if ((y->left = node->left)) y->left->parent = y;
        if ((y->right = node->right)) y->right->parent = y;
    }        
}  // end NormNodeTree::DetachNode()


void NormNodeTree::Destroy()
{
    NormNode* n;
    while ((n = root)) 
    {
        DetachNode(n);
        delete n;
    }
}  // end NormNodeTree::Destroy()

NormNodeTreeIterator::NormNodeTreeIterator(const NormNodeTree& t)
 : tree(t)
{
    NormNode* x = t.root;
    if (x)
    {
        while (x->left) x = x->left;
        next = x;
    }
    else
    {
        next = NULL;    
    }    
}  

void NormNodeTreeIterator::Reset()
{
    NormNode* x = tree.root;
    if (x)
    {
        while (x->left) x = x->left;
        next = x;
    }
    else
    {
        next = NULL;   
    }
}  // end NormNodeTreeIterator::Reset()

NormNode* NormNodeTreeIterator::GetNextNode()
{
    NormNode* n = next;
    if (n)
    {
        if (next->right)
        {
            NormNode* y = n->right;
            while (y->left) y = y->left;
            next = y;
        }
        else
        {
            NormNode* x = n;
            NormNode* y = n->parent;
            while(y && (y->right == x))
            {
                x = y;
                y = y->parent;
            }
            next = y;
        }
    }
    return n;
}  // end NormNodeTreeIterator::GetNextNode()

NormNodeList::NormNodeList()
    : head(NULL), tail(NULL), count(0)
{
}

NormNodeList::~NormNodeList()
{
    Destroy();
}

NormNode* NormNodeList::FindNodeById(NormNodeId nodeId) const
{
    NormNode *next = head;
    while (next)
    {
        if (nodeId == next->id)
            return next;
        else
            next = next->right;
    }
    return NULL;
}  // NormNodeList::Find()

void NormNodeList::Append(NormNode *theNode)
{
    ASSERT(theNode);
    theNode->left = tail;
    if (tail)
        tail->right = theNode;
    else
        head = theNode;
    tail = theNode;
    theNode->right = NULL;
    count++;
}  // end NormNodeList::Append()

void NormNodeList::Remove(NormNode *theNode)
{
    ASSERT(theNode);
    if (theNode->right)
        theNode->right->left = theNode->left;
    else
        tail = theNode->left;
    if (theNode->left)
        theNode->left->right = theNode->right;
    else
        head = theNode->right;
    count--;
}  // end NormNodeList::Remove()

void NormNodeList::Destroy()
{
    NormNode* n;
    while ((n = head))
    {
        Remove(n);
        delete n;
    }   
}  // end NormNodeList::Destroy()


//////////////////////////////////////////////////////////
//
// NormLossEstimator implementation
//

NormLossEstimator::NormLossEstimator()
 : synchronized(false), seeking_loss_event(true), event_window(0.0)
{
    memset(history, 0, (DEPTH+1)*sizeof(unsigned int));
}

const double NormLossEstimator::weight[DEPTH] = 
{
    1.0, 1.0, 1.0, 1.0,
    0.8, 0.6, 0.4, 0.2  
};

int NormLossEstimator::SequenceDelta(unsigned short a, unsigned short b)
{
    int delta = a - b;
    if (delta < -0x8000)
        return (delta + 0x10000);
    else if (delta < 0x8000)
        return delta;
    else 
        return delta - 0x10000; 
}  // end NormLossEstimator::SequenceDelta()

// Returns true when a loss event has occurred
bool NormLossEstimator::Update(const struct timeval&    currentTime,
                               unsigned short           seq, 
                               bool                     ecn)
{
    if (!synchronized)
    {   
        Sync(seq);
        return false;
    }
    bool outage = false;
    int delta = SequenceDelta(seq, index_seq);
    if (abs(delta) > MAX_OUTAGE)    // out-of-range packet
    {
        index_seq = seq;
        return false;
    }    
    else if (delta > 0)             // new packet arrival
    {
        if (ecn || (delta > 1)) outage = true;
        index_seq = seq;
    }
    else // (delta <= 0)            // old misordered or duplicate packet
    {
        return false;               
    }        
    if (outage)
    {
        if (!seeking_loss_event)
        {
            double deltaTime = (double)(currentTime.tv_sec - event_time.tv_sec);
            if (currentTime.tv_usec > event_time.tv_usec)
                deltaTime += (double)(currentTime.tv_usec - event_time.tv_usec) * 1.0e-06;
            else
                deltaTime -= (double)(event_time.tv_usec - currentTime.tv_usec) * 1.0e-06;
            if (deltaTime > event_window) seeking_loss_event = true;
        }
        if (seeking_loss_event)
        {
            memmove(history+1, history, DEPTH*sizeof(unsigned int));
            history[0] = 1;
            seeking_loss_event = false; 
            event_time = currentTime;  
            return true;
        }
        else
        {
            // Only count one loss per loss event
            history[0] = 1;
            return false;
        } 
    }
    else
    {
        history[0]++;
        return false;           
    }    
}  // end NormLossEstimator::Update()

double NormLossEstimator::LossFraction()
{
    if (0 == history[1]) return 0.0;
    double weightSum = 0.0;
    double s0 = 0.0;
    const double* wptr = weight;
    const unsigned int* h = history;
    unsigned int i;
    for (i = 0; i < DEPTH; i++)
    {
        if (0 == *h) break;
        s0 += *wptr * *h++;
        weightSum += *wptr++;
    }
    s0 /= weightSum;
    
    weightSum = 0.0;
    double s1 = 0.0;
    wptr = weight;
    h = history + 1;
    for (i = 0; i < DEPTH; i++)
    {
        if (0 == *h) break;
        s1 += *wptr * *h++;  // ave loss interval w/out current interval
        weightSum += *wptr++; // (TBD) this could be pre-computed
    }
    s1 /= weightSum;
    return (1.0 / (MAX(s0,s1)));
}  // end NormLossEstimator::LossFraction()



NormLossEstimator2::NormLossEstimator2()
    : lag_mask(0xffffffff), lag_depth(0), lag_test_bit(0x01),
      event_window(0), event_index(0), 
      event_window_time(0.0), event_index_time(0.0), 
      seeking_loss_event(true),
      no_loss(true), initial_loss(0.0), loss_interval(0.0),
      current_discount(1.0)
{
    memset(history, 0, 9*sizeof(unsigned long));
    discount[0] = 1.0;
}

const double NormLossEstimator2::weight[8] = 
{
    1.0, 1.0, 1.0, 1.0,
    0.8, 0.6, 0.4, 0.2  
};
        
int NormLossEstimator2::SequenceDelta(unsigned short a, unsigned short b)
{
    int delta = a - b;
    if (delta < -0x8000)
        return (delta + 0x10000);
    else if (delta < 0x8000)
        return delta;
    else 
        return delta - 0x10000; 
}  // end NormLossEstimator2::SequenceDelta()


bool NormLossEstimator2::Update(const struct timeval&   currentTime,
                               unsigned short           theSequence, 
                               bool                     ecnStatus)
{
    if (!init) 
    {
        Init(theSequence); 
        return false;
    } 
    unsigned int outageDepth = 0;
    // Process packet through lag filter and check for loss
    int delta = SequenceDelta(theSequence, lag_index);  
    if (delta > 100)        // Very new packet arrived
    {
        Sync(theSequence);  // resync
        return false;   
    }
    else if (delta > 0)     // New packet arrived
    {
        if (lag_depth)
        {
            unsigned int outage = 0;
            for (int i = 0; i < delta; i++)
            {
                if (i <= (int)lag_depth)
                {
                    outage++;
                    if (lag_mask & lag_test_bit)
                    {
                        if (outage > 1) 
                            outageDepth = MAX(outage, outageDepth);
                        outage = 0;  
                    }
                    else
                    {
                        lag_mask |= lag_test_bit;
                    }
                    lag_mask <<= 1;
                } 
                else
                {
                    outage += delta - lag_depth - 1; 
                    break;
                }
            }
            outageDepth = MAX(outage, outageDepth);
            lag_mask |= 0x01;
        }
        else
        {
            if (delta > 1) outageDepth = delta - 1;
        }
        lag_index = theSequence;
    }
    else if (delta < -100)              // Very old packet arrived
    {
         Sync(theSequence); // resync
         return false; 
    }
    else if (delta < -((int)lag_depth)) // Old packet arrived
    {
        ChangeLagDepth(-delta);
    }
    else if (delta < 0)                 // Lagging packet arrived
    {                                   // (duplicates have no effect)
        lag_mask |= (0x01 << (-delta));
        return false;
    }
    else // (delta == 0)
    {
        return false;                    // Duplicate packet arrived, ignore
    }        
    
    if (ecnStatus) outageDepth += 1;
    
    bool newLossEvent = false;
    
    if (!seeking_loss_event)
    {
        double theTime = (((double)currentTime.tv_sec) + 
                          (((double)currentTime.tv_usec)/1.0e06));
        if (theTime > event_index_time) seeking_loss_event = true;
        
        // (TBD) Should we reset our history on
        //  outages within the event_window???
    }
    
    if (seeking_loss_event)
    {
        double scale;
        if (history[0] > loss_interval)
            scale = 0.125 / (1.0 + log((double)(event_window ? event_window : 1)));
        else
            scale = 0.125;
        if (outageDepth)  // non-zero outageDepth means pkt loss(es)
        {
            if (no_loss)  // first loss
            {
                //fprintf(stderr, "First Loss: seq:%u init:%f history:%lu adjusted:",
                //                 theSequence, initial_loss, history[0]);
                if (initial_loss != 0.0)
                {
                    unsigned long initialHistory = (unsigned long) ((1.0 / initial_loss) + 0.5);
                    history[0] = MAX(initialHistory, history[0]/2);
                }
                //fprintf(stderr, "%lu\n", history[0]);
                no_loss = false;
            }
            
            // Old method
            if (loss_interval > 0.0)
                loss_interval += scale*(((double)history[0]) - loss_interval);
            else
                loss_interval = (double) history[0]; 
            
            // New method
            // New loss event, shift loss interval history & discounts
            memmove(&history[1], &history[0], 8*sizeof(unsigned long));
            history[0] = 0;
            memmove(&discount[1], &discount[0], 8*sizeof(double));
            discount[0] = 1.0;
            current_discount = 1.0;
            
            event_index = theSequence;
            //if (event_window) 
                seeking_loss_event = false;
            newLossEvent = true;
            no_loss = false;
            // (TBD) use fixed pt. math here ...
            event_index_time = (((double)currentTime.tv_sec) + 
                                (((double)currentTime.tv_usec)/1.0e06));
            event_index_time += event_window_time;
        }
        else 
        {
            //if (no_loss) fprintf(stderr, "No loss (seq:%u) ...\n", theSequence);
            if (loss_interval > 0.0)
            {
                double diff = ((double)history[0]) - loss_interval;
                if (diff >= 1.0)
                {
                    //scale *= (diff * diff) / (loss_interval * loss_interval);
                    loss_interval += scale*log(diff);
                }
            }
        }    
    }  
    else
    {
        if (outageDepth) history[0] = 0;
    }  // end if/else (seeking_loss_event)
    
    if (history[0] < 100000) history[0]++;
    
    return newLossEvent;
}  // end NormLossEstimator2::ProcessRecvPacket()

double NormLossEstimator2::LossFraction()
{
#if defined0
    if (use_ewma_loss_estimate)
        return MdpLossFraction();   // MDP EWMA approach
    else
#endif // SIMULATOR
    return (TfrcLossFraction());    // ACIRI TFRC approach
}  // end NormLossEstimator2::LossFraction()



// TFRC Loss interval averaging with discounted, weighted averaging
double NormLossEstimator2::TfrcLossFraction()
{
    if (!history[1]) return 0.0;   
    // Compute older weighted average s1->s8 for discount determination  
    double average = 0.0;
    double scaling = 0.0;
	unsigned int i;
    for (i = 1; i < 9; i++)
    {
        if (history[i])
        {
            average += history[i] * weight[i-1] * discount[i];
            scaling += discount[i] * weight[i-1];
        }
        else
        {
            break;
        }
    }
    double s1 = average / scaling;

    // Compute discount if applicable  
     if (history[0] > (2.0*s1))
    {
        current_discount = (2.0*s1) / (double) history[0];
        current_discount = MAX (current_discount, 0.5);
    }
    
    // Re-compute older weighted average s1->s8 with discounting
	if (current_discount < 1.0)
    { 
        average = 0.0;
        scaling = 0.0;
        for (i = 1; i < 9; i++)
        {
            if (history[i])
            {
                average += current_discount * history[i] * weight[i-1] * discount[i];
                scaling += current_discount * discount[i] * weight[i-1];
            }
            else
            {
                break;
            }
        }
        s1 = average / scaling;
    }
    
    // Compute newer weighted average s0->s7 with discounting
    average = 0.0;
    scaling = 0.0;
	for (i = 0; i < 8; i++)
    {
        if (history[i])
        {
            double d = (i > 0) ? current_discount : 1.0;
            average += d * history[i] * weight[i] * discount[i];
            scaling += d * discount[i] * weight[i];
        }
        else
        {
            break;
        }
    }
    double s0 = average / scaling;   
    // Use max of old/new averages
    return (1.0 /  MAX(s0, s1));
}  // end NormLossEstimator2::LossFraction()
