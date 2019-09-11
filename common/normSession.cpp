#include "normSession.h"
#include <time.h>  // for gmtime() in NormTrace()


const UINT8 NormSession::DEFAULT_TTL = 255; // bits/sec
const double NormSession::DEFAULT_TRANSMIT_RATE = 64000.0; // bits/sec
const double NormSession::DEFAULT_GRTT_INTERVAL_MIN = 1.0;        // sec
const double NormSession::DEFAULT_GRTT_INTERVAL_MAX = 30.0;       // sec
const double NormSession::DEFAULT_GRTT_ESTIMATE = 0.5;    // sec
const double NormSession::DEFAULT_GRTT_MAX = 10.0;        // sec
const unsigned int NormSession::DEFAULT_GRTT_DECREASE_DELAY = 3;
const double NormSession::DEFAULT_BACKOFF_FACTOR = 4.0;   
const double NormSession::DEFAULT_GSIZE_ESTIMATE = 1000.0; 
const UINT16 NormSession::DEFAULT_NDATA = 64; 
const UINT16 NormSession::DEFAULT_NPARITY = 32;  

NormSession::NormSession(NormSessionMgr& sessionMgr, NormNodeId localNodeId) 
 : session_mgr(sessionMgr), notify_pending(false), tx_port(0), 
   tx_socket(&tx_socket_actual), tx_socket_actual(ProtoSocket::UDP), 
   rx_socket(ProtoSocket::UDP), local_node_id(localNodeId), 
   ttl(DEFAULT_TTL), loopback(false),
   tx_rate(DEFAULT_TRANSMIT_RATE/8.0), tx_rate_min(-1.0), tx_rate_max(-1.0),
   backoff_factor(DEFAULT_BACKOFF_FACTOR), is_server(false), session_id(0),
   ndata(DEFAULT_NDATA), nparity(DEFAULT_NPARITY), auto_parity(0), extra_parity(0),
   next_tx_object_id(0), tx_cache_count_min(8), tx_cache_count_max(256),
   tx_cache_size_max((UINT32)20*1024*1024),
   flush_count(NORM_ROBUST_FACTOR+1),
   posted_tx_queue_empty(false), 
   acking_node_count(0), watermark_pending(false), advertise_repairs(false),
   suppress_nonconfirmed(false), suppress_rate(-1.0), suppress_rtt(-1.0),
   probe_proactive(true), probe_pending(false), probe_reset(false),
   grtt_interval(0.5), 
   grtt_interval_min(DEFAULT_GRTT_INTERVAL_MIN),
   grtt_interval_max(DEFAULT_GRTT_INTERVAL_MAX),
   grtt_max(DEFAULT_GRTT_MAX), 
   grtt_decrease_delay_count(DEFAULT_GRTT_DECREASE_DELAY),
   grtt_response(false), grtt_current_peak(0.0), grtt_age(0.0),
   cc_enable(false), cc_sequence(0), cc_slow_start(true),
   is_client(false), unicast_nacks(false), client_silent(false),
   default_repair_boundary(NormServerNode::BLOCK_BOUNDARY),
   default_nacking_mode(NormObject::NACK_NORMAL),
   trace(false), tx_loss_rate(0.0), rx_loss_rate(0.0),
   user_data(NULL), next(NULL)
{
    interface_name[0] = '\0';
    tx_socket->SetNotifier(&sessionMgr.GetSocketNotifier());
    tx_socket->SetListener(this, &NormSession::TxSocketRecvHandler);
    
    rx_socket.SetNotifier(&sessionMgr.GetSocketNotifier());
    rx_socket.SetListener(this, &NormSession::RxSocketRecvHandler);
    
    tx_timer.SetListener(this, &NormSession::OnTxTimeout);
    tx_timer.SetInterval(0.0);
    tx_timer.SetRepeat(-1);
    
    repair_timer.SetListener(this, &NormSession::OnRepairTimeout);
    repair_timer.SetInterval(0.0);
    repair_timer.SetRepeat(1);
    
    flush_timer.SetListener(this, &NormSession::OnFlushTimeout);
    flush_timer.SetInterval(0.0);
    flush_timer.SetRepeat(0);
    
    probe_timer.SetListener(this, &NormSession::OnProbeTimeout);
    probe_timer.SetInterval(0.0);
    probe_timer.SetRepeat(-1);
    
    grtt_quantized = NormQuantizeRtt(DEFAULT_GRTT_ESTIMATE);
    grtt_measured = grtt_advertised = NormUnquantizeRtt(grtt_quantized);
    
    gsize_measured = DEFAULT_GSIZE_ESTIMATE;
    gsize_quantized = NormQuantizeGroupSize(DEFAULT_GSIZE_ESTIMATE);
    gsize_advertised = NormUnquantizeGroupSize(gsize_quantized);
    
    
    // This timer is for printing out occasional status reports
    // (It may be used to trigger transmission of report messages
    //  in the future for debugging, etc
    report_timer.SetListener(this, &NormSession::OnReportTimeout);
    report_timer.SetInterval(10.0);
    report_timer.SetRepeat(-1);
}

NormSession::~NormSession()
{
    Close();
}


bool NormSession::Open(const char* interfaceName)
{
    ASSERT(address.IsValid());
    if (!tx_socket->IsOpen())
    {   
        if (address.GetPort() != tx_port)
        {
            if (!tx_socket->Open(tx_port))
            {
                DMSG(0, "NormSession::Open() tx_socket open error\n");
                return false;   
            }
        }
        else
        {
            tx_socket = &rx_socket;   
        }
    }
    if (!rx_socket.IsOpen())
    {
        if (!rx_socket.Open(address.GetPort()))
        {
            DMSG(0, "NormSession::Open() rx_socket open error\n");
            Close();
            return false;   
        }
    }
    
    if (address.IsMulticast())
    {
        if (!tx_socket->SetTTL(ttl))
        {
            DMSG(0, "NormSession::Open() tx_socket set ttl error\n");
            Close();
            return false;
        }
        if (!tx_socket->SetLoopback(loopback))
        {
            DMSG(0, "NormSession::Open() tx_socket set loopback error\n");
            Close();
            return false;
        }
        if (interfaceName)
        {
            strncpy(interface_name, interfaceName, 31);
            interface_name[31] = '\0';
        }
        if ('\0' != interface_name[0])
        {
            bool result = rx_socket.SetMulticastInterface(interface_name);
            result &= tx_socket->SetMulticastInterface(interface_name);
            if (!result)
            {
                DMSG(0, "NormSession::Open() error setting multicast interface\n");
                Close();
                return false;
            }
            interfaceName = interface_name;
        }
        if (!rx_socket.JoinGroup(address, interfaceName)) 
        {
            DMSG(0, "NormSession::Open() rx_socket join group error\n");
            Close();
            return false;
        }   
    }
    for (unsigned int i = 0; i < DEFAULT_MESSAGE_POOL_DEPTH; i++)
    {
        NormMsg* msg = new NormMsg();
        if (msg)
        {
            message_pool.Append(msg);
        }
        else
        {
            DMSG(0, "NormSession::Open() new message error: %s\n", GetErrorString());
            Close();
            return false;
        }   
    }
    ActivateTimer(report_timer);
    
    return true;
}  // end NormSession::Open()

void NormSession::Close()
{
    if (report_timer.IsActive()) report_timer.Deactivate();
    if (is_server) StopServer();
    if (is_client) StopClient();
    if (tx_timer.IsActive()) tx_timer.Deactivate();    
    message_queue.Destroy();
    message_pool.Destroy();
    if (tx_socket->IsOpen()) tx_socket->Close();
    if (rx_socket.IsOpen()) 
    {
        if (address.IsMulticast()) 
        {
            const char* interfaceName = ('\0' != interface_name[0]) ?
                                        interface_name : NULL;
            rx_socket.LeaveGroup(address, interfaceName);
        }
        rx_socket.Close();
    }
}  // end NormSession::Close()


bool NormSession::SetMulticastInterface(const char* interfaceName)
{
    if (interfaceName)
    {
        bool result = true;
        if (rx_socket.IsOpen())
            result &= rx_socket.SetMulticastInterface(interfaceName);
        if (tx_socket->IsOpen())
            result &= tx_socket->SetMulticastInterface(interfaceName);
        return result;
    }
    else
    {
        interface_name[0] = '\0';  
        return true; 
    }
}  // end NormSession::SetMulticastInterface()

void NormSession::SetTxRate(double txRate)
{
    txRate /= 8.0; // convert to bytes/sec
    if (tx_timer.IsActive())
    {
        tx_timer.Deactivate();
        if (txRate > 0.0)
        {
            double adjustInterval = (tx_rate/txRate) * tx_timer.GetTimeRemaining();
            tx_timer.SetInterval(adjustInterval);
            ActivateTimer(tx_timer);
        }
        tx_rate = txRate;
    }
    else if ((0.0 == tx_rate) && IsOpen())
    {
        tx_rate = txRate;
        tx_timer.SetInterval(0.0);
        ActivateTimer(tx_timer);
    }
    else
    {
        tx_rate = txRate;   
    }
}  // end NormSession::SetTxRate()

void NormSession::SetTxRateBounds(double rateMin, double rateMax)
{
    // Make sure min <= max
    if ((rateMin >= 0.0) && (rateMax >= 0.0))
    {
        if (rateMin > rateMax)
        {
            double temp = rateMin;
            rateMin = rateMax;
            rateMax = temp;   
        }   
    }
    if (rateMin < 0.0)
        tx_rate_min = -1.0;
    else
        tx_rate_min = rateMin/8.0;  // convert to bytes/second
    if (rateMax < 0.0)
        tx_rate_max = -1.0;
    else
        tx_rate_max = rateMax/8.0;  // convert to bytes/second
    if (cc_enable)
    {
        if ((tx_rate_min >= 0.0) && (tx_rate < tx_rate_min))
            tx_rate = tx_rate_min;
        if ((tx_rate_max >= 0.0) && (tx_rate > tx_rate_max))
            tx_rate = tx_rate_max;
        SetTxRate(tx_rate*8.0);
    }
}  // end NormSession::SetTxRateBounds()
        

bool NormSession::StartServer(UINT32        bufferSpace,
                              UINT16        segmentSize,
                              UINT16        numData, 
                              UINT16        numParity,
                              const char*   interfaceName)
{
    if (!IsOpen())
    {
        if (!Open(interfaceName)) return false;
    }
    // (TBD) parameterize the object history depth
    if (!tx_table.Init(256))
    {
        DMSG(0, "NormSession::StartServer() tx_table.Init() error!\n");
        StopServer();
        return false;   
    }
    if (!tx_pending_mask.Init(256, 0x0000ffff))
    {
        DMSG(0, "NormSession::StartServer() tx_pending_mask.Init() error!\n");
        StopServer();
        return false; 
    }
    if (!tx_repair_mask.Init(256, 0x0000ffff))
    {
        DMSG(0, "NormSession::StartServer() tx_repair_mask.Init() error!\n");
        StopServer();
        return false; 
    }
    
    // Calculate how much memory each buffered block will require
    UINT16 blockSize = numData + numParity;
    unsigned long maskSize = blockSize >> 3;
    if (0 != (blockSize & 0x07)) maskSize++;
    unsigned long blockSpace = sizeof(NormBlock) + 
                               blockSize * sizeof(char*) + 
                               2*maskSize  +
                               numParity * (segmentSize + NormDataMsg::GetStreamPayloadHeaderLength());
    
    unsigned long numBlocks = bufferSpace / blockSpace;
    if (bufferSpace > (numBlocks*blockSpace)) numBlocks++;
    if (numBlocks < 2) numBlocks = 2;
    unsigned long numSegments = numBlocks * numParity;
    
    if (!block_pool.Init(numBlocks, blockSize))
    {
        DMSG(0, "NormSession::StartServer() block_pool init error\n");
        StopServer();
        return false;
    }
    
    if (!segment_pool.Init(numSegments, segmentSize + NormDataMsg::GetStreamPayloadHeaderLength() + 1))
    {
        DMSG(0, "NormSession::StartServer() segment_pool init error\n");
        StopServer();
        return false;
    }
    
    if (numParity)
    {
        if (!encoder.Init(numParity, segmentSize + NormDataMsg::GetStreamPayloadHeaderLength()))
        {
            DMSG(0, "NormSession::StartServer() encoder init error\n");
            StopServer();
            return false;
        }    
    }
    
    segment_size = segmentSize;
    sent_rate = 0.0;
    prev_update_time.tv_sec = prev_update_time.tv_usec = 0;
    sent_accumulator = 0;
    nominal_packet_size = (double)segmentSize;
    ndata = numData;
    nparity = numParity;
    is_server = true;
    flush_count = NORM_ROBUST_FACTOR+1;  // (TBD) parameterize robust_factor
    //probe_timer.SetInterval(0.0);
    probe_pending = probe_reset = false;
    
    if (cc_enable) 
    {
        tx_rate = segmentSize;
        if ((tx_rate_min >= 0.0) && (tx_rate < tx_rate_min))
            tx_rate = tx_rate_min;
        if ((tx_rate_max >= 0.0) && (tx_rate > tx_rate_max))
            tx_rate = tx_rate_max;
    }
            
    OnProbeTimeout(probe_timer);
    ActivateTimer(probe_timer);
    return true;
}  // end NormSession::StartServer()


void NormSession::StopServer()
{
    if (probe_timer.IsActive()) probe_timer.Deactivate();
    encoder.Destroy();
    acking_node_tree.Destroy();
    cc_node_list.Destroy();
    // Iterate tx_table and release objects
    while (!tx_table.IsEmpty())
    {
        NormObject* obj = tx_table.Find(tx_table.RangeLo());
        ASSERT(obj);
        tx_table.Remove(obj);
        obj->Release();   
    }
    // Then destroy table
    tx_table.Destroy();
    block_pool.Destroy();
    segment_pool.Destroy();
    tx_repair_mask.Destroy();
    tx_pending_mask.Destroy();
    is_server = false;
    if (!IsClient()) Close();
}   // end NormSession::StopServer()

bool NormSession::StartClient(unsigned long bufferSize, const char* interfaceName)
{
    if (!IsOpen())
    {
        if (!Open(interfaceName)) return false;
    }
    is_client = true;
    remote_server_buffer_size = bufferSize;
    return true;
}

void NormSession::StopClient()
{    
    // Iterate server_tree and close/release server nodes
    NormServerNode* serverNode = 
        static_cast<NormServerNode*>(server_tree.GetRoot());
    while (serverNode)
    {
        server_tree.DetachNode(serverNode);
        serverNode->Close();
        serverNode->Release();
        serverNode = 
            static_cast<NormServerNode*>(server_tree.GetRoot());
    }
    is_client = false;
    if (!is_server) Close();
}

void NormSession::Serve()
{
    // Only send new data when no other messages are queued for transmission
    if (!message_queue.IsEmpty()) return;
    
    // Queue next server message
    NormObjectId objectId;
    NormObject* obj = NULL;    
    if (ServerGetFirstPending(objectId))
    {
        obj = tx_table.Find(objectId);
        ASSERT(obj);
    }
    
    if (watermark_pending)
    {
        // Determine next message (objectId::blockId::segmentId) to be sent
        NormObject* nextObj;
        NormObjectId nextObjectId;
        NormBlockId nextBlockId = 0;
        NormSegmentId nextSegmentId = 0;
        if (obj)
        {
            // Use current transmit pending object
            nextObj = obj;
            nextObjectId = objectId;
            if (nextObj->IsPending())
            {
                if(nextObj->GetFirstPending(nextBlockId))
                {
                    NormBlock* block = nextObj->FindBlock(nextBlockId);
                    if (block) 
                    {
#ifdef PROTO_DEBUG
                        ASSERT(block->GetFirstPending(nextSegmentId));
#else
                        block->GetFirstPending(nextSegmentId);
#endif  // if/else PROTO_DEBUG
                        // Adjust so watermark segmentId < block length
                        if (nextSegmentId >= nextObj->GetBlockSize(nextBlockId))
                            nextSegmentId = nextObj->GetBlockSize(nextBlockId) - 1;
                        
                    }
                }
                else
                {
                    // info only pending; so blockId = segmentId = 0 (as inited)
                }
            }
            else
            {
                // Must be an active, but non-pending stream object 
                nextBlockId = static_cast<NormStreamObject*>(nextObj)->GetNextBlockId();
                nextSegmentId = static_cast<NormStreamObject*>(nextObj)->GetNextSegmentId();  
            }           
        }
        else
        {
            // Nothing transmit pending, check for repair pending object
            NormObjectTable::Iterator iterator(tx_table);
            while ((nextObj = iterator.GetNextObject()))
                if (nextObj->IsRepairPending()) break;
            if (ServerGetFirstRepairPending(nextObjectId))
            {
                if (nextObj && (nextObj->GetId() < nextObjectId))
                {
                    nextObjectId = nextObj->GetId();  
                }
                else 
                {
                    nextObj = tx_table.Find(nextObjectId);
                    ASSERT(nextObj);   
                }
            }
            if (nextObj)
            {
#ifdef PROTO_DEBUG
                ASSERT(nextObj->FindRepairIndex(nextBlockId, nextSegmentId));
#else
                nextObj->FindRepairIndex(nextBlockId, nextSegmentId);
#endif
            }
            else
            {
                nextObjectId = next_tx_object_id;
            }
        } 
        if ((nextObjectId > watermark_object_id) ||
            ((nextObjectId == watermark_object_id) &&
             ((nextBlockId > watermark_block_id) ||
              (((nextBlockId == watermark_block_id) &&
                (nextSegmentId > watermark_segment_id))))))
        {
            if (ServerQueueWatermarkFlush()) 
            {
                return;
            }
            else
            {
                // (TBD) optionally return here to have ack collection temporarily suspend data transmission
               return;
            }
        }
    }  // end if (watermark_pending)
    
    if (obj)
    {
        NormObjectMsg* msg = (NormObjectMsg*)GetMessageFromPool();
        if (msg)
        {
            if (obj->NextServerMsg(msg))
            {
                msg->SetDestination(address);
                msg->SetGrtt(grtt_quantized);
                msg->SetBackoffFactor((unsigned char)backoff_factor);
                msg->SetGroupSize(gsize_quantized);
                QueueMessage(msg);
                flush_count = 0;
                //if (flush_timer.IsActive()) flush_timer.Deactivate();
                if (!obj->IsPending())
                {
                    if (obj->IsStream())
                        posted_tx_queue_empty = true; // repair-delayed stream advance
                    else
                        tx_pending_mask.Unset(obj->GetId());
                }
                else if (obj->IsStream())
                {
                    // Is there room write to the stream
                       
                }
            }
            else
            {
                ReturnMessageToPool(msg);
                if (obj->IsStream())
                {
                    NormStreamObject* stream = static_cast<NormStreamObject*>(obj);
                    if (stream->IsFlushPending())
                    {
                        // Queue flush message
                        if (!flush_timer.IsActive())
                            if (flush_count < NORM_ROBUST_FACTOR)
                        {
                            ServerQueueFlush();
                        }
                        else if (NORM_ROBUST_FACTOR ==  flush_count)
                        {
                            DMSG(6, "NormSession::Serve() node>%lu server flush complete ...\n",
                                     LocalNodeId());
                            flush_count++;
                        }
                    }
                    if (!posted_tx_queue_empty)
                    {
                        posted_tx_queue_empty = true;
                        Notify(NormController::TX_QUEUE_EMPTY, (NormServerNode*)NULL, obj);
                        // (TBD) Was session deleted?
                        //Serve();
                        return;
                    }      
                }
                else
                {
                    DMSG(0, "NormSession::Serve() pending non-stream obj, no message?.\n");                
                    ASSERT(repair_timer.IsActive());
                }
            }
        }
        else
        {
            DMSG(0, "NormSession::Serve() node>%lu Warning! message_pool empty.\n",
                    LocalNodeId());
        }
    }
    else
    {
        // No pending objects or positive acknowledgement request
        if (!posted_tx_queue_empty)
        {
            posted_tx_queue_empty = true;
            Notify(NormController::TX_QUEUE_EMPTY,
                   (NormServerNode*)NULL,
                   (NormObject*)NULL);
            // (TBD) Was session deleted?
            //Serve();
            return;
        }     
        if (flush_count < NORM_ROBUST_FACTOR)
        {
            // Queue flush message
            ServerQueueFlush();
        }   
        else if (flush_count == NORM_ROBUST_FACTOR)
        {
            DMSG(6, "NormSession::Serve() node>%lu server flush complete ...\n",
                    LocalNodeId());
            flush_count++;   
        }
    }
}  // end NormSession::Serve()

void NormSession::ServerSetWatermark(NormObjectId  objectId,
                                     NormBlockId   blockId,
                                     NormSegmentId segmentId)       
{
    TRACE("NormSession::ServerSetWatermark(%hu:%lu:%hu) ...\n",
            (UINT16)objectId, (UINT32)blockId, (UINT16)segmentId);
    watermark_pending = true;
    watermark_object_id = objectId;
    watermark_block_id = blockId;
    watermark_segment_id = segmentId;
    acks_collected = 0;
    // Reset acking_node_list
    NormNodeTreeIterator iterator(acking_node_tree);
    NormNode* next;
    while ((next = iterator.GetNextNode()))
        static_cast<NormAckingNode*>(next)->Reset(NORM_ROBUST_FACTOR);
    PromptServer();
}  // end Norm::ServerSetWatermark()

bool NormSession::ServerAddAckingNode(NormNodeId nodeId)
{
    NormAckingNode* theNode = static_cast<NormAckingNode*>(acking_node_tree.FindNodeById(nodeId));
    if (NULL == theNode)
    {
        theNode = new NormAckingNode(*this, nodeId);
        if (NULL != theNode)
        {
            theNode->Reset(NORM_ROBUST_FACTOR);
            acking_node_tree.AttachNode(theNode);
            acking_node_count++;
            return true;
        }
        else
        {
            DMSG(0, "NormSession::AddAckingNode() new NormAckingNode error: %s\n", GetErrorString());
        }                
    }
    else
    {
        DMSG(0, "NormSession::AddAckingNode() warning: node already in list!?\n");
    }
    return true;
}  // end NormSession::AddAckingNode(NormNodeId nodeId)

void NormSession::ServerRemoveAckingNode(NormNodeId nodeId)
{
    NormAckingNode* theNode = static_cast<NormAckingNode*>(acking_node_tree.FindNodeById(nodeId));
    if (theNode) 
    {
        if (watermark_pending && theNode->AckReceived())
            acks_collected--;
        acking_node_tree.DetachNode(theNode);
        delete theNode;
        acking_node_count--;
    }
}  // end NormSession::RemoveAckingNode()

bool NormSession::ServerQueueWatermarkFlush()
{
    if (flush_timer.IsActive()) return false;
    NormCmdFlushMsg* flush = static_cast<NormCmdFlushMsg*>(GetMessageFromPool());
    if (flush)
    {
        flush->Init();
        flush->SetDestination(address);
        flush->SetGrtt(grtt_quantized);
        flush->SetBackoffFactor((unsigned char)backoff_factor);
        flush->SetGroupSize(gsize_quantized);
        flush->SetObjectId(watermark_object_id);
        flush->SetFecBlockId(watermark_block_id);
        flush->SetFecSymbolId(watermark_segment_id);
        NormNodeTreeIterator iterator(acking_node_tree);
        NormAckingNode* next;
        watermark_pending = false;
        while ((next = static_cast<NormAckingNode*>(iterator.GetNextNode())))
        {
            if (next->IsPending())
            {
                // Add node to list     
                if (flush->AppendAckingNode(next->GetId(), segment_size))
                {
                    next->DecrementReqCount();
                    watermark_pending = true;
                }
                else
                {
                    DMSG(8, "NormSession::ServeQueueWatermarkFlush() full cmd ...\n");
                    break;    
                }                
            }
        }
        if (watermark_pending)
        {
            flush_count++;
            QueueMessage(flush);
            DMSG(8, "NormSession::ServeQueueWatermarkFlush() node>%lu cmd queued ...\n",
                    LocalNodeId());
        }
        else
        {
            DMSG(4, "NormSession::ServeQueueWatermarkFlush() node>%lu watermark ack finished incomplete\n");
            // (TBD) notify app
            return false; 
        }
    }
    else
    {
        DMSG(0, "NormSession::ServerQueueWatermarkRequest() node>%lu message_pool exhausted! (couldn't req)\n",
                LocalNodeId());
    }        
    flush_timer.SetInterval(2*grtt_advertised);
    ActivateTimer(flush_timer);
    return true;
}  // end NormSession::ServerQueueWatermarkFlush()
        
void NormSession::ServerQueueFlush()
{
    // (TBD) Deal with EOT or pre-queued squelch on squelch case
    if (flush_timer.IsActive()) return;
    NormObject* obj = tx_table.Find(tx_table.RangeHi());
    NormObjectId objectId;
    NormBlockId blockId;
    NormSegmentId segmentId;
    if (obj)
    {
        if (obj->IsStream())
        {
            NormStreamObject* stream = (NormStreamObject*)obj;
            objectId = stream->GetId();
            blockId = stream->FlushBlockId();
            segmentId = stream->FlushSegmentId();
        }
        else
        {
            objectId = obj->GetId();
            blockId = obj->GetFinalBlockId();
            segmentId = obj->GetBlockSize(blockId) - 1;
        }
    }
    else
    {
        // Why did I do this? - Brian
        // (TBD) send NORM_CMD(EOT) instead? - no
        if (ServerQueueSquelch(next_tx_object_id))
        {
            flush_count++;
            flush_timer.SetInterval(2*grtt_advertised);
            ActivateTimer(flush_timer);
        }
        DMSG(8, "NormSession::ServerQueueFlush() node>%lu squelch queued (flush_count:%u)...\n",
                LocalNodeId(), flush_count);
        return;  
    }
    NormCmdFlushMsg* flush = (NormCmdFlushMsg*)GetMessageFromPool();
    if (flush)
    {
        flush->Init();
        flush->SetDestination(address);
        flush->SetGrtt(grtt_quantized);
        flush->SetBackoffFactor((unsigned char)backoff_factor);
        flush->SetGroupSize(gsize_quantized);
        flush->SetObjectId(objectId);
        flush->SetFecBlockId(blockId);
        flush->SetFecSymbolId(segmentId);
        QueueMessage(flush);
        flush_count++;
        DMSG(0, "NormSession::ServerQueueFlush() node>%lu, flush queued (flush_count:%u)...\n",
                LocalNodeId(), flush_count);
    }
    else
    {
        DMSG(0, "NormSession::ServerQueueFlush() node>%lu message_pool exhausted! (couldn't flush)\n",
                LocalNodeId());  
    }   
    flush_timer.SetInterval(2*grtt_advertised); 
    ActivateTimer(flush_timer);
}  // end NormSession::ServerQueueFlush()

bool NormSession::OnFlushTimeout(ProtoTimer& /*theTimer*/)
{
    flush_timer.Deactivate();
    PromptServer();//Serve();  // (TBD) Change this to PromptServer() ??
    return false;   
}  // NormSession::OnFlushTimeout()
        
void NormSession::QueueMessage(NormMsg* msg)
{

/* A little test jig
        static struct timeval lastTime = {0,0};
    struct timeval currentTime;
    ProtoSystemTime(currentTime);
    if (0 != lastTime.tv_sec)
    {
        double delta = currentTime.tv_sec - lastTime.tv_sec;
        delta += (((double)currentTime.tv_usec)*1.0e-06 -  
                  ((double)lastTime.tv_usec)*1.0e-06);
        DMSG(0, "NormSession::QueueMessage() deltaT:%lf\n", delta);
    }
    lastTime = currentTime;
*/
    if (!tx_timer.IsActive() && (tx_rate > 0.0))
    {
        tx_timer.SetInterval(0.0);
        ActivateTimer(tx_timer);   
    }
    if (msg) message_queue.Append(msg);
}  // end NormSesssion::QueueMessage(NormMsg& msg)



NormFileObject* NormSession::QueueTxFile(const char* path,
                                         const char* infoPtr,
                                         UINT16      infoLen)
{
    if (!IsServer())
    {
        DMSG(0, "NormSession::QueueTxFile() Error: server is closed\n");
        return NULL;
    }  
    NormFileObject* file = new NormFileObject(*this, (NormServerNode*)NULL, next_tx_object_id);
    if (!file)
    {
        DMSG(0, "NormSession::QueueTxFile() new file object error: %s\n",
                GetErrorString());
        return NULL; 
    }
    if (!file->Open(path, infoPtr, infoLen))
    {
       DMSG(0, "NormSession::QueueTxFile() file open error\n");
       delete file;
       return NULL; 
    }    
    if (QueueTxObject(file))
    {
        return file;
    }
    else
    {
        file->Close();
        delete file;
        return NULL;
    }
}  // end NormSession::QueueTxFile()

NormDataObject* NormSession::QueueTxData(const char* dataPtr,
                                         UINT32      dataLen,
                                         const char* infoPtr,
                                         UINT16      infoLen)
{
    if (!IsServer())
    {
        DMSG(0, "NormSession::QueueTxData() Error: server is closed\n");
        return NULL;
    }  
    NormDataObject* obj = new NormDataObject(*this, (NormServerNode*)NULL, next_tx_object_id);
    if (!obj)
    {
        DMSG(0, "NormSession::QueueTxData() new data object error: %s\n",
                GetErrorString());
        return NULL; 
    }
    if (!obj->Open((char*)dataPtr, dataLen, false, infoPtr, infoLen))
    {
       DMSG(0, "NormSession::QueueTxData() object open error\n");
       delete obj;
       return NULL; 
    }    
    if (QueueTxObject(obj))
    {
        return obj;
    }
    else
    {
        obj->Close();
        delete obj;
        return NULL;
    }
}  // end NormSession::QueueTxData()


NormStreamObject* NormSession::QueueTxStream(UINT32         bufferSize, 
                                             bool           doubleBuffer,
                                             const char*    infoPtr, 
                                             UINT16         infoLen)
{
    if (!IsServer())
    {
        DMSG(0, "NormSession::QueueTxStream() Error: server is closed\n");
        return NULL;
    }     
    NormStreamObject* stream = new NormStreamObject(*this, (NormServerNode*)NULL, next_tx_object_id);
    if (!stream)
    {
        DMSG(0, "NormSession::QueueTxStream() new stream object error: %s\n",
                GetErrorString());
        return NULL; 
    }
    if (!stream->Open(bufferSize, doubleBuffer, infoPtr, infoLen))
    {
        DMSG(0, "NormSession::QueueTxStream() stream open error\n");
        delete stream;
        return NULL;
    }
    if (QueueTxObject(stream))
    {
        // (???: stream has nothing pending until user writes to it???)
        //stream->Reset();
        return stream;
    }
    else
    {
        stream->Close();
        delete stream;
        return NULL;
    }
}  // end NormSession::QueueTxStream()

#ifdef SIMULATE
NormSimObject* NormSession::QueueTxSim(unsigned long objectSize)
{
    if (!IsServer())
    {
        DMSG(0, "NormSession::QueueTxSim() Error: server is closed\n");
        return NULL;
    }
    NormSimObject* simObject = new NormSimObject(*this, NULL, next_tx_object_id);
    if (!simObject)
    {
        DMSG(0, "NormSession::QueueTxSim() new sim object error: %s\n",
                GetErrorString());
        return NULL; 
    }  
    
    if (!simObject->Open(objectSize))
    {
        DMSG(0, "NormSession::QueueTxSim() open error\n");
        delete simObject;
        return NULL;
    }
    if (QueueTxObject(simObject))
    {
        return simObject;
    }
    else
    {
        delete simObject;
        return NULL;
    }
}  // end NormSession::QueueTxSim()
#endif // SIMULATE

bool NormSession::QueueTxObject(NormObject* obj)
{
    if (!IsServer())
    {
        DMSG(0, "NormSession::QueueTxObject() non-server session error!\n");
        return false;
    }
    
    // Manage tx_table min/max count and max size bounds
    if (tx_table.Count() >= tx_cache_count_min)
    {
        unsigned long count = tx_table.Count();
        while ((count >= tx_cache_count_min) &&
               ((count >= tx_cache_count_max) ||
                ((tx_table.Size() + obj->GetSize()) > tx_cache_size_max)))
        {
            // Remove oldest non-pending 
            NormObject* oldest = tx_table.Find(tx_table.RangeLo());
            if (oldest->IsRepairPending() || oldest->IsPending())
            {
                DMSG(0, "NormSession::QueueTxObject() all held objects repair pending\n");
                //posted_tx_queue_empty = false;
                return false;
            }
            else
            {
                Notify(NormController::TX_OBJECT_PURGED, (NormServerNode*)NULL, oldest);
                DeleteTxObject(oldest);
            }   
            count = tx_table.Count();           
        } 
    }
    // Attempt to queue the object
    if (!tx_table.Insert(obj))
    {
        DMSG(0, "NormSession::QueueTxObject() tx_table insert error\n");
        ASSERT(0);
        return false;
    }
    obj->Retain();
    tx_pending_mask.Set(obj->GetId());
    ASSERT(tx_pending_mask.Test(obj->GetId()));
    next_tx_object_id++;
    TouchServer();
    return true;
}  // end NormSession::QueueTxObject()

void NormSession::DeleteTxObject(NormObject* obj)
{
    if (tx_table.Remove(obj))
    {
        NormObjectId objectId = obj->GetId();
        tx_pending_mask.Unset(objectId);
        tx_repair_mask.Unset(objectId);
    }
    obj->Close();
    obj->Release();
}  // end NormSession::DeleteTxObject()

NormBlock* NormSession::ServerGetFreeBlock(NormObjectId objectId, 
                                           NormBlockId  blockId)
{
    // First, try to get one from our block pool
    NormBlock* b = block_pool.Get();
    // Second, try to steal oldest non-pending block
    if (!b)
    {
        NormObjectTable::Iterator iterator(tx_table);
        NormObject* obj;
        while ((obj = iterator.GetNextObject()))
        {           
            if (obj->GetId() == objectId)
                b = obj->StealNonPendingBlock(true, blockId);
            else
                b = obj->StealNonPendingBlock(false);
            if (b) 
            {
                b->EmptyToPool(segment_pool);
                break;
            }
        }
    }
    // Finally, try to steal newer pending block
    if (!b)
    {
        // reverse iteration to find newest object with resources
        NormObjectTable::Iterator iterator(tx_table);
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
    return b;
}  // end NormSession::ServerGetFreeBlock()

char* NormSession::ServerGetFreeSegment(NormObjectId objectId, 
                                        NormBlockId  blockId)
{
    while (segment_pool.IsEmpty())
    {     
        NormBlock* b = ServerGetFreeBlock(objectId, blockId);
        if (b)
            block_pool.Put(b);
        else
            return NULL;
    }
    return segment_pool.Get();
}  // end NormSession::ServerGetFreeSegment()

void NormSession::TxSocketRecvHandler(ProtoSocket&       /*theSocket*/,
                                      ProtoSocket::Event /*theEvent*/)
{
    NormMsg msg;
    unsigned int msgLength = NormMsg::MAX_SIZE;
    while (tx_socket->RecvFrom(msg.AccessBuffer(),
                              msgLength, 
                              msg.AccessAddress()))
    {
        msg.InitFromBuffer(msgLength);
        HandleReceiveMessage(msg, true);
        msgLength = NormMsg::MAX_SIZE;
    }
}  // end NormSession::TxSocketRecvHandler()

void NormSession::RxSocketRecvHandler(ProtoSocket&       /*theSocket*/,
                                      ProtoSocket::Event /*theEvent*/)
{
    NormMsg msg;
    unsigned int msgLength = NormMsg::MAX_SIZE;
    while (rx_socket.RecvFrom(msg.AccessBuffer(),
                              msgLength, 
                              msg.AccessAddress()))
    {
        msg.InitFromBuffer(msgLength);
        HandleReceiveMessage(msg, false);
        msgLength = NormMsg::MAX_SIZE;
    }
}  // end NormSession::RxSocketRecvHandler()

void NormTrace(const struct timeval&    currentTime, 
               NormNodeId               localId, 
               const NormMsg&           msg, 
               bool                     sent)
{
    //if (DebugLevel() < 8) return;  // (TBD) provide per-session trace on/off switch
    static const char* MSG_NAME[] =
    {
        "INVALID",
        "INFO", 
        "DATA", 
        "CMD",
        "NACK",
        "ACK",
        "REPORT"
    };       
    static const char* CMD_NAME[] =
    {
        "CMD(INVALID)",
        "CMD(FLUSH)",
        "CMD(EOT)",
        "CMD(SQUELCH)",
        "CMD(CC)",
        "CMD(REPAIR_ADV)",
        "CMD(ACK_REQ)",
        "CMD(APP)"
    };      
       
                
                  
    static const char* REQ_NAME[] = 
    {
        "INVALID",
        "WATERMARK",
        "RTT",
        "APP"
    };
    
    NormMsg::Type msgType = msg.GetType();
    UINT16 length = msg.GetLength();
    const char* status = sent ? "dst" : "src";
    const ProtoAddress& addr = sent ? msg.GetDestination() : msg.GetSource();
    
    UINT16 seq = msg.GetSequence();

#ifdef _WIN32_WCE
    struct tm timeStruct;
    timeStruct.tm_hour = currentTime.tv_sec / 3600;
    unsigned long hourSecs = 3600 * timeStruct.tm_hour;
    timeStruct.tm_min = (currentTime.tv_sec - (hourSecs)) / 60;
    timeStruct.tm_sec = currentTime.tv_sec - (hourSecs) - (60*timeStruct.tm_min);
    timeStruct.tm_hour = timeStruct.tm_hour % 24;
    struct tm* ct = &timeStruct;
#else            
    struct tm* ct = gmtime((time_t*)&currentTime.tv_sec);
#endif // if/else _WIN32_WCE
    DMSG(0, "trace>%02d:%02d:%02d.%06lu node>%lu %s>%s seq>%hu ",
            ct->tm_hour, ct->tm_min, ct->tm_sec, currentTime.tv_usec,
            (UINT32)localId, status, addr.GetHostString(), seq);
    bool clrFlag = false;
    switch (msgType)
    {
        case NormMsg::INFO:
        {
            const NormInfoMsg& info = (const NormInfoMsg&)msg;
            DMSG(0, "INFO obj>%hu ", (UINT16)info.GetObjectId());
            break;
        }
        case NormMsg::DATA:
        {
            const NormDataMsg& data = (const NormDataMsg&)msg;
            if (data.IsData())
                DMSG(0, "DATA ");
            else
                DMSG(0, "PRTY ");
            DMSG(0, "obj>%hu blk>%lu seg>%hu ", 
                    (UINT16)data.GetObjectId(),
                    (UINT32)data.GetFecBlockId(),
                    (UINT16)data.GetFecSymbolId());
            if (data.IsData())
            {
                UINT16 x;
                memcpy(&x, data.GetPayloadData(), 2);
                if (data.FlagIsSet(NormObjectMsg::FLAG_MSG_START))
                    DMSG(0, "start byte>%hu ", ntohs(x));   
            }
            break;
        }
        case NormMsg::CMD:
        {
            NormCmdMsg::Flavor flavor = ((const NormCmdMsg&)msg).GetFlavor();
            DMSG(0, "%s ", CMD_NAME[flavor]);
            switch (flavor)
            {
                case NormCmdMsg::ACK_REQ:
                {
                    int index = ((const NormCmdAckReqMsg&)msg).GetAckType();
                    index = MIN(index, 3);
                    DMSG(0, "(%s) ", REQ_NAME[index]);
                    break;
                }
                case NormCmdMsg::SQUELCH:
                {
                    const NormCmdSquelchMsg& squelch = (const NormCmdSquelchMsg&)msg;
                    DMSG(0, " obj>%hu blk>%lu seg>%hu ",
                            (UINT16)squelch.GetObjectId(),
                            (UINT32)squelch.GetFecBlockId(),
                            (UINT16)squelch.GetFecSymbolId());
                    break;
                }
                case NormCmdMsg::FLUSH:
                {
                    const NormCmdFlushMsg& flush = (const NormCmdFlushMsg&)msg;
                    DMSG(0, " obj>%hu blk>%lu seg>%hu ",
                            (UINT16)flush.GetObjectId(),
                            (UINT32)flush.GetFecBlockId(),
                            (UINT16)flush.GetFecSymbolId());
                    break;
                }
                case NormCmdMsg::CC:
                {
                    const NormCmdCCMsg& cc = (const NormCmdCCMsg&)msg;
                    DMSG(0, " seq>%u ", cc.GetCCSequence());
                    NormHeaderExtension ext;
                    while (cc.GetNextExtension(ext))
                    {
                        if (NormHeaderExtension::CC_RATE == ext.GetType())
                        {
                            UINT16 sendRate = ((NormCCRateExtension&)ext).GetSendRate();       
                            DMSG(0, " rate>%f ", (8.0/1000.0) * NormUnquantizeRate(sendRate));
                            break;
                        }
                    }
                    break;
                }
                default:
                    break;
            }
            break;
        }
        
        case NormMsg::ACK:
        case NormMsg::NACK:
        {
            // look for NormCCFeedback extension
            NormHeaderExtension ext;
            while (msg.GetNextExtension(ext))
            {
                if (NormHeaderExtension::CC_FEEDBACK == ext.GetType())
                {
                    clrFlag = ((NormCCFeedbackExtension&)ext).CCFlagIsSet(NormCC::CLR);
                    break;
                }
            }
            DMSG(0, "%s ", MSG_NAME[msgType]);   
            break;
        }
            
        default:
            DMSG(0, "%s ", MSG_NAME[msgType]);   
            break;
    } 
    DMSG(0, "len>%hu %s\n", length, clrFlag ? "(CLR)" : "");
}  // end NormTrace();


void NormSession::HandleReceiveMessage(NormMsg& msg, bool wasUnicast)
{   
    // Drop some rx messages for testing
    if (UniformRand(100.0) < rx_loss_rate) 
    {
        return;
    }
    
    struct timeval currentTime;
    ::ProtoSystemTime(currentTime);
    
    if (trace) NormTrace(currentTime, LocalNodeId(), msg, false);
    
    switch (msg.GetType())
    {
        case NormMsg::INFO:
            //DMSG(0, "NormSession::HandleReceiveMessage(NormMsg::INFO)\n");
            if (IsClient()) ClientHandleObjectMessage(currentTime, (NormObjectMsg&)msg);
            break;
        case NormMsg::DATA:
            //DMSG(0, "NormSession::HandleReceiveMessage(NormMsg::DATA) ...\n");
            if (IsClient()) ClientHandleObjectMessage(currentTime, (NormObjectMsg&)msg);
            break;
        case NormMsg::CMD:
            //DMSG(0, "NormSession::HandleReceiveMessage(NormMsg::CMD) ...\n");
            if (IsClient()) ClientHandleCommand(currentTime, (NormCmdMsg&)msg);
            break;
        case NormMsg::NACK:
            DMSG(4, "NormSession::HandleReceiveMessage(NormMsg::NACK) node>%lu ...\n",  LocalNodeId());
            if (IsServer() && (((NormNackMsg&)msg).GetServerId() == LocalNodeId()))
            { 
                ServerHandleNackMessage(currentTime, (NormNackMsg&)msg);
                if (wasUnicast && (backoff_factor > 0.5) && Address().IsMulticast()) 
                {
                    // for suppression of unicast nack feedback
                    advertise_repairs = true;
                    QueueMessage(NULL); // to prompt transmit timeout
                }
            }
            if (IsClient()) ClientHandleNackMessage((NormNackMsg&)msg);
            break;
        case NormMsg::ACK:
            if (IsServer() && (((NormAckMsg&)msg).GetServerId() == LocalNodeId())) 
                ServerHandleAckMessage(currentTime, (NormAckMsg&)msg, wasUnicast);
            if (IsClient()) ClientHandleAckMessage((NormAckMsg&)msg);
            break;
            
        case NormMsg::REPORT: 
        case NormMsg::INVALID:
            DMSG(0, "NormSession::HandleReceiveMessage(NormMsg::INVALID)\n");
            break;
    }
}  // end NormSession::HandleReceiveMessage()



void NormSession::ClientHandleObjectMessage(const struct timeval&   currentTime, 
                                            const NormObjectMsg&    msg)
{
    // Do common updates for servers we already know.
    NormNodeId sourceId = msg.GetSourceId();
    NormServerNode* theServer = (NormServerNode*)server_tree.FindNodeById(sourceId);
    if (theServer)
    {
        if (msg.GetSessionId() != theServer->GetSessionId())
        {
            DMSG(2, "NormSession::ClientHandleObjectMessage() node>%lu server>%lu sessionId change - resyncing.\n",
                         LocalNodeId(), theServer->GetId());
            theServer->Close();   
            if (!theServer->Open(msg.GetSessionId()))
            {
                DMSG(0, "NormSession::ClientHandleObjectMessage() node>%lu error re-opening NormServerNode\n");
                // (TBD) notify application of error
                return;  
            }   
        }
    }
    else
    {
        if ((theServer = new NormServerNode(*this, msg.GetSourceId())))
        {
            Notify(NormController::REMOTE_SERVER_NEW, theServer, NULL);
            if (theServer->Open(msg.GetSessionId()))
            {
                server_tree.AttachNode(theServer);
                theServer->Retain();
                DMSG(4, "NormSession::ClientHandleObjectMessage() node>%lu new remote server:%lu ...\n",
                        LocalNodeId(), msg.GetSourceId());
            }
            else
            {
                DMSG(0, "NormSession::ClientHandleObjectMessage() node>%lu error opening NormServerNode\n");
                // (TBD) notify application of error
                return;    
            }            
        }
        else
        {
            DMSG(0, "NormSession::ClientHandleObjectMessage() new NormServerNode error: %s\n",
                    GetErrorString());
            // (TBD) notify application of error
            return;   
        }   
    }
    theServer->Activate();
    theServer->UpdateLossEstimate(currentTime, msg.GetSequence()); 
    theServer->SetAddress(msg.GetSource());
    theServer->IncrementRecvTotal(msg.GetLength()); // for statistics only (TBD) #ifdef NORM_DEBUG
    theServer->HandleObjectMessage(msg);
    theServer->UpdateRecvRate(currentTime, msg.GetLength());
}  // end NormSession::ClientHandleObjectMessage()

void NormSession::ClientHandleCommand(const struct timeval& currentTime,
                                      const NormCmdMsg&     cmd)
{
    // Do common updates for servers we already know.
    NormNodeId sourceId = cmd.GetSourceId();
    NormServerNode* theServer = (NormServerNode*)server_tree.FindNodeById(sourceId);
    if (theServer)
    {
        if (cmd.GetSessionId() != theServer->GetSessionId())
        {
            DMSG(2, "NormSession::ClientHandleCommand() node>%lu server>%lu sessionId change - resyncing.\n",
                         LocalNodeId(), theServer->GetId());
            theServer->Close();   
            if (!theServer->Open(cmd.GetSessionId()))
            {
                DMSG(0, "NormSession::ClientHandleCommand() node>%lu error re-opening NormServerNode\n");
                // (TBD) notify application of error
                return;  
            }   
        }
    }
    else
    {
        //DMSG(0, "NormSession::ClientHandleCommand() node>%lu recvd command from unknown server ...\n",
        //        LocalNodeId());   
        if ((theServer = new NormServerNode(*this, cmd.GetSourceId())))
        {
            if (theServer->Open(cmd.GetSessionId()))
            {
                server_tree.AttachNode(theServer);
                theServer->Retain();
                DMSG(4, "NormSession::ClientHandleCommand() node>%lu new remote server:%lu ...\n",
                        LocalNodeId(), cmd.GetSourceId());
            }
            else
            {
                DMSG(0, "NormSession::ClientHandleCommand() node>%lu error opening NormServerNode\n");
                // (TBD) notify application of error
                return;
            }
        }
        else
        {
            DMSG(0, "NormSession::ClientHandleCommand() new NormServerNode error: %s\n",
                    GetErrorString());
            // (TBD) notify application of error
            return;   
        }   
    }
    theServer->Activate();
    theServer->UpdateLossEstimate(currentTime, cmd.GetSequence()); 
    theServer->SetAddress(cmd.GetSource());
    theServer->IncrementRecvTotal(cmd.GetLength()); // for statistics only (TBD) #ifdef NORM_DEBUG
    theServer->HandleCommand(currentTime, cmd);
    theServer->UpdateRecvRate(currentTime, cmd.GetLength());
}  // end NormSession::ClientHandleCommand()


double NormSession::CalculateRtt(const struct timeval& currentTime,
                                 const struct timeval& grttResponse)
{
    if (grttResponse.tv_sec || grttResponse.tv_usec)
    {
        double clientRtt;
        // Calculate rtt estimate for this client and process the response
        if (currentTime.tv_usec < grttResponse.tv_usec)
        {
            clientRtt = 
                (double)(currentTime.tv_sec - grttResponse.tv_sec - 1);
            clientRtt += 
                ((double)(1000000 - (grttResponse.tv_usec - currentTime.tv_usec))) / 1.0e06;
        }
        else
        {
            clientRtt = 
                (double)(currentTime.tv_sec - grttResponse.tv_sec);
            clientRtt += 
                ((double)(currentTime.tv_usec - grttResponse.tv_usec)) / 1.0e06;
        } 
        // Lower limit on RTT (because of coarse timer resolution on some systems,
        // this can sometimes actually end up a negative value!)
        // (TBD) this should be system clock granularity?
        return (clientRtt < 1.0e-06) ? 1.0e-06 : clientRtt;
    }
    else
    {
        return -1.0;
    }
}  // end NormSession::CalculateRtt()

void NormSession::ServerUpdateGrttEstimate(double clientRtt)
{
    grtt_response = true;
    //if ((clientRtt > grtt_current_peak) || !address.IsMulticast()) 
    if ((clientRtt > grtt_measured) || !address.IsMulticast())
    {
        // Immediately incorporate bigger RTT's
        grtt_current_peak = clientRtt;
        //if ((clientRtt > grtt_measured) || !address.IsMulticast()) 
        {
            grtt_decrease_delay_count = DEFAULT_GRTT_DECREASE_DELAY;
            //grtt_measured = 0.25 * grtt_measured + 0.75 * clientRtt; 
            grtt_measured = 0.9 * grtt_measured + 0.1 * clientRtt; 
            if (grtt_measured > grtt_max) grtt_measured = grtt_max;
            double pktInterval =  ((double)(44+segment_size))/tx_rate;
            UINT8 grttQuantizedOld = grtt_quantized;
            grtt_quantized = NormQuantizeRtt(MAX(pktInterval, grtt_measured));
            // Calculate grtt_advertised since quantization rounds upward
            grtt_advertised = NormUnquantizeRtt(grtt_quantized);
            
            if (grttQuantizedOld != grtt_quantized)
                DMSG(4, "NormSession::ServerUpdateGrttEstimate() node>%lu new grtt>%lf sec\n",
                        LocalNodeId(), grtt_advertised);
        }
    } 
}  // end NormSession::ServerUpdateGrttEstimate()

double NormSession::CalculateRate(double size, double rtt, double loss)
{
    double denom = rtt * (sqrt((2.0/3.0)*loss) + 
                   (12.0 * sqrt((3.0/8.0)*loss) * loss *
                    (1.0 + 32.0*loss*loss)));
    return (size / denom);    
}  // end NormSession::CalculateRate()

void NormSession::ServerHandleCCFeedback(NormNodeId nodeId,
                                         UINT8      ccFlags,
                                         double     ccRtt,
                                         double     ccLoss,
                                         double     ccRate,
                                         UINT16     ccSequence)
{
    // Keep track of current suppressing feedback
    // (non-CLR, lowest rate, unconfirmed RTT)
    if (0 == (ccFlags & NormCC::CLR))
    {
        if (suppress_rate < 0.0)
        {
            suppress_rate = ccRate;
            suppress_rtt = ccRtt;
            suppress_nonconfirmed = (0 == (ccFlags & NormCC::RTT));
        }
        else
        {
            if (ccRate < suppress_rate) suppress_rate = ccRate;
            if (ccRtt > suppress_rtt) suppress_rtt = ccRtt;
            if (0 == (ccFlags & NormCC::RTT)) suppress_nonconfirmed = true;    
        }          
    }
    if (!cc_enable) return;
    
    // Adjust ccRtt if we already have state on this nodeId
    NormCCNode* node = (NormCCNode*)cc_node_list.FindNodeById(nodeId);
    if (node) ccRtt = node->UpdateRtt(ccRtt);
    
    if (0 == (ccFlags & NormCC::START))
    {
        // slow start has ended
        cc_slow_start = false;  
        // adjust rate using current rtt for node
        ccRate = CalculateRate(nominal_packet_size, ccRtt, ccLoss);
    }
    //DMSG(0, "NormSession::ServerHandleCCFeedback() node>%lu rate>%lf (rtt>%lf loss>%lf slow_start>%d)\n",
    //        nodeId, ccRate * 8.0 / 1000.0, ccRtt, ccLoss, (0 != (ccFlags & NormCC::START)));
    
    // Keep the active CLR (if there is one) at the head of the list
    NormNodeListIterator iterator(cc_node_list);
    NormCCNode* next = (NormCCNode*)iterator.GetNextNode();
    // 1) Does this response replace the active CLR?
    if (next && next->IsActive())
    {
        if (ccRate < next->GetRate() || (nodeId == next->GetId()))
        {
            NormNodeId savedId = next->GetId();
            bool savedRttStatus = next->HasRtt();
            double savedRtt = next->GetRtt();
            double savedLoss = next->GetLoss();
            double savedRate = next->GetRate();
            UINT16 savedSequence = next->GetCCSequence();
            
            next->SetId(nodeId);
            next->SetClrStatus(true);
            next->SetRttStatus(0 != (ccFlags & NormCC::RTT));
            next->SetLoss(ccLoss);
            next->SetRate(ccRate);
            next->SetCCSequence(ccSequence);
            next->SetActive(true);
            if (next->GetId() == nodeId)
            {
                // This was feedback from the current CLR  
                AdjustRate(true);
                return;
            }
            else
            {
                next->SetRtt(ccRtt);
                AdjustRate(true);
            }
            ccFlags = 0;
            nodeId = savedId;
            if (savedRttStatus)
                ccFlags = NormCC::RTT;
            ccRtt = savedRtt;
            ccLoss = savedLoss;
            ccRate = savedRate,
            ccSequence = savedSequence;
        }
    }
    else 
    {
        // There was no active CLR
        if (!next)
        {
            if ((next  = new NormCCNode(*this, nodeId)))
            {
                cc_node_list.Append(next);
            }
            else
            {
                 DMSG(0, "NormSession::ServerHandleCCFeedback() memory allocation error: %s\n",
                            GetErrorString());  
                 return;
            } 
        }  
        next->SetId(nodeId);
        next->SetClrStatus(true);
        next->SetRttStatus(0 != (ccFlags & NormCC::RTT));
        next->SetRtt(ccRtt);
        next->SetLoss(ccLoss);
        next->SetRate(ccRate);
        next->SetCCSequence(ccSequence);
        next->SetActive(true);
        AdjustRate(true);
        return;
    }
    
    // 2) Go through cc_node_list and find lowest priority candidate
    NormCCNode* candidate = NULL;
    if (cc_node_list.GetCount() < 5)
    {
        if ((candidate = new NormCCNode(*this, nodeId)))
        {
            cc_node_list.Append(candidate);
        }   
        else
        {
            DMSG(0, "NormSession::ServerHandleCCFeedback() memory allocation error: %s\n",
                            GetErrorString()); 
        }
    }
    else
    {
        while ((next = (NormCCNode*)iterator.GetNextNode()))
        {
            if (next->GetId() == nodeId)
            {
                candidate = next;
                break;
            }
            else if (candidate)
            {
                if (candidate->IsActive() && !next->IsActive())
                {
                    candidate = next;
                    continue;   
                }
                if (!next->HasRtt() && candidate->HasRtt()) 
                    continue;
                else if (!candidate->HasRtt() && next->HasRtt())
                    candidate = next;
                else if (candidate->GetRate() < next->GetRate())
                    candidate = next;
            }
            else
            {
                candidate = next;
                continue;
            }
        }
    }
    
    // 3) Replace candidate if this response is higher precedence
    if (candidate)
    {
        bool haveRtt = (0 != (ccFlags && NormCC::RTT));
        bool replace;
        if (candidate->GetId() == nodeId)
            replace = true;
        else if (!candidate->IsActive())
            replace = true;
        else if (!haveRtt && candidate->HasRtt())
            replace = true;
        else if (haveRtt && !candidate->HasRtt())
            replace = false;
        else if (ccRate < candidate->GetRate())
            replace = true;
        else
            replace = false;
        if (replace)
        {
            candidate->SetId(nodeId);
            candidate->SetClrStatus(false);
            candidate->SetRttStatus(0 != (ccFlags & NormCC::RTT));
            candidate->SetRtt(ccRtt);
            candidate->SetLoss(ccLoss);
            candidate->SetRate(ccRate);
            candidate->SetCCSequence(ccSequence);
            candidate->SetActive(true);
        }
    }
}  // end NormSession::ServerHandleCCFeedback()
                                         

void NormSession::ServerHandleAckMessage(const struct timeval& currentTime, const NormAckMsg& ack, bool wasUnicast)
{
    // Update GRTT estimate
    struct timeval grttResponse;
    ack.GetGrttResponse(grttResponse);
    double clientRtt = CalculateRtt(currentTime, grttResponse);
    if (clientRtt >= 0.0) ServerUpdateGrttEstimate(clientRtt);
    
    // Look for NORM-CC Feedback header extension
    NormCCFeedbackExtension ext;
    while (ack.GetNextExtension(ext))
    {
        if (NormHeaderExtension::CC_FEEDBACK == ext.GetType())
        {
            ServerHandleCCFeedback(ack.GetSourceId(),
                                   ext.GetCCFlags(),
                                   clientRtt >= 0.0 ?  
                                        clientRtt : NormUnquantizeRtt(ext.GetCCRtt()),
                                   NormUnquantizeLoss(ext.GetCCLoss()),
                                   NormUnquantizeRate(ext.GetCCRate()),
                                   ext.GetCCSequence());
        }
        break;
    }
    
    if (wasUnicast && probe_proactive && Address().IsMulticast()) 
    {
        // for suppression of unicast feedback
        advertise_repairs = true;
        QueueMessage(NULL);
    }
    
    switch (ack.GetAckType())
    {
        case NormAck::CC:
            // Everything is in the ACK header for this one
            break;
            
        case NormAck::FLUSH:
            if (watermark_pending)
            {
                NormAckingNode* acker = 
                    static_cast<NormAckingNode*>(acking_node_tree.FindNodeById(ack.GetSourceId()));
                if (acker)
                {
                    if (!acker->AckReceived())
                    {
                        const NormAckFlushMsg& flushAck = static_cast<const NormAckFlushMsg&>(ack);
                        if ((watermark_object_id == flushAck.GetObjectId()) &&
                            (watermark_block_id  == flushAck.GetFecBlockId()) &&
                            (watermark_segment_id  == flushAck.GetFecSymbolId()))
                        {
                            acker->MarkAckReceived(); 
                            acks_collected++; 
                            if (acks_collected >= acking_node_count)
                            {
                                watermark_pending = false;
                                DMSG(4, "NormSession::ServerHandleAckMessage() watermark acknowledgement complete\n");
                                // (TBD) notify app   
                            }
                        }
                        else
                        {
                            DMSG(0, "NormSession::ServerHandleAckMessage() received wrong watermark ACK?!\n");    
                        }
                    }
                    else
                    {
                        DMSG(0, "NormSession::ServerHandleAckMessage() received redundant watermark ACK?!\n");
                    }
                }
                else
                {
                    DMSG(0, "NormSession::ServerHandleAckMessage() received watermark ACK from unknown acker?!\n");
                }
            }
            else
            {
                DMSG(0, "NormSession::ServerHandleAckMessage() received unsolicited watermark ACK?!\n");
            }
            break;
            
        // (TBD) Handle other acknowledgement types
        default: 
            DMSG(0, "NormSession::ServerHandleAckMessage() node>%lu received "
                    "unsupported ack type:%d\n", LocalNodeId(), ack.GetAckType());
    }
}  // end ServerHandleAckMessage()

void NormSession::ServerHandleNackMessage(const struct timeval& currentTime, NormNackMsg& nack)
{
    // (TBD) maintain average of "numErasures" for SEGMENT repair requests
    //       to use as input to a an automatic "auto parity" adjustor
    // Update GRTT estimate
    struct timeval grttResponse;
    nack.GetGrttResponse(grttResponse);
    double clientRtt = CalculateRtt(currentTime, grttResponse);
    if (clientRtt >= 0.0) ServerUpdateGrttEstimate(clientRtt);
    
    // Look for NORM-CC Feedback header extension
    NormCCFeedbackExtension ext;
    while (nack.GetNextExtension(ext))
    {
        if (NormHeaderExtension::CC_FEEDBACK == ext.GetType())
        {
            ServerHandleCCFeedback(nack.GetSourceId(),
                                   ext.GetCCFlags(),
                                   clientRtt >= 0.0 ?  
                                       clientRtt : NormUnquantizeRtt(ext.GetCCRtt()),
                                   NormUnquantizeLoss(ext.GetCCLoss()),
                                   NormUnquantizeRate(ext.GetCCRate()),
                                   ext.GetCCSequence());
        }
        break;
    }
    
    // Parse and process NACK 
    UINT16 requestOffset = 0;
    UINT16 requestLength = 0;
    NormRepairRequest req;
    NormObject* object = NULL;
    bool freshObject = true;
    NormObjectId prevObjectId = 0;
    NormBlock* block = NULL;
    bool freshBlock = true;
    NormBlockId prevBlockId = 0;
    
    bool startTimer = false;
    UINT16 numErasures = extra_parity;
    
    bool squelchQueued = false;
    
    NormObjectId txObjectIndex;
    NormBlockId txBlockIndex;
    if (ServerGetFirstPending(txObjectIndex))
    {
        NormObject* obj = tx_table.Find(txObjectIndex);
        ASSERT(obj);
        if (obj->IsPendingInfo())
        {
            txBlockIndex = 0;
        }
        else if (obj->GetFirstPending(txBlockIndex))
        {
            txBlockIndex++;
        }
        else
        {
            txObjectIndex = next_tx_object_id;
            txBlockIndex = 0;
        }
    }
    else
    {
        txObjectIndex = next_tx_object_id;
        txBlockIndex = 0;
    }
                
    bool holdoff = (repair_timer.IsActive() && !repair_timer.GetRepeatCount());    
    enum NormRequestLevel {SEGMENT, BLOCK, INFO, OBJECT};
    while ((requestLength = nack.UnpackRepairRequest(req, requestOffset)))
    {
        NormRepairRequest::Form requestForm = req.GetForm();
        requestOffset += requestLength;
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
                    DMSG(0, "NormSession::ServerHandleNackMessage() node>%lu recvd incomplete RANGE request!\n",
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
            
            bool inRange = true;
            while (inRange)
            {
                if (nextObjectId != prevObjectId) freshObject = true;
                if (freshObject)
                {
                    freshBlock = true;
                    if (!(object = tx_table.Find(nextObjectId)))
                    {
                        DMSG(4, "NormSession::ServerHandleNackMessage() node>%lu recvd repair request "
                                "for unknown object ...\n", LocalNodeId());
                        if (!squelchQueued) 
                        {
                            ServerQueueSquelch(nextObjectId);
                            squelchQueued = true;
                        }
                        if ((OBJECT == requestLevel) || (INFO == requestLevel))
                        {
                            nextObjectId++;
                            if (nextObjectId > lastObjectId) 
                                inRange = false;
                        }
                        else
                        {
                            inRange = false;
                        }
                        continue;
                    } 
                    prevObjectId = nextObjectId;
                    freshObject = false;
                    // Deal with INFO request if applicable
                    if (req.FlagIsSet(NormRepairRequest::INFO)) 
                    {
                        if (holdoff)
                        {
                            if (nextObjectId > txObjectIndex)
                                object->HandleInfoRequest();
                        }
                        else
                        {
                            object->HandleInfoRequest();
                            startTimer = true;
                        }
                    }
                }  // end if (freshObject)
                ASSERT(object);
                
                switch (requestLevel)
                {
                    case OBJECT:
                        DMSG(8, "NormSession::ServerHandleNackMessage(OBJECT) objs>%hu:%hu\n", 
                                (UINT16)nextObjectId, (UINT16)lastObjectId);
                        if (holdoff)
                        {
                            if (nextObjectId > txObjectIndex)
                            {
                                if (object->IsStream())
                                    object->TxReset(((NormStreamObject*)object)->StreamBufferLo());
                                else
                                    object->TxReset();
                                if (!tx_pending_mask.Set(nextObjectId))
                                    DMSG(0, "NormSession::ServerHandleNackMessage() tx_pending_mask.Set(%hu) error (1)\n",
                                            (UINT16)nextObjectId); 
                            }
                        }
                        else
                        {
                            tx_repair_mask.Set(nextObjectId);
                            startTimer = true;   
                        }
                        nextObjectId++;
                        if (nextObjectId > lastObjectId) inRange = false;
                        break;
                    case BLOCK:
                        DMSG(8, "NormSession::ServerHandleNackMessage(BLOCK) obj>%hu blks>%lu:%lu\n", 
                                (UINT16)nextObjectId, (UINT32)nextBlockId, (UINT32)lastBlockId);
                        inRange = false; // BLOCK requests are processed in one pass
                        // (TBD) if entire object is TxReset(), continue
                        if (object->IsStream())
                        {
                            bool attemptLock = true;
                            NormBlockId firstLockId = nextBlockId;
                            if (holdoff)
                            {
                                // Only lock blocks for which we're going to accept the repair request
                                if (nextObjectId == txObjectIndex)
                                {
                                    if (lastBlockId < txBlockIndex)
                                        attemptLock = false;
                                    else if (nextBlockId < txBlockIndex)
                                        firstLockId = txBlockIndex;
                                }
                                else if (nextObjectId < txObjectIndex)
                                {
                                    attemptLock = false;  // NACK arrived too late to be useful
                                }
                            }
                            
                            // Make sure the stream' pending_mask can be set as needed
                            // (TBD)
                            
                            // Lock stream_buffer pending for block data retransmissions
                            if (attemptLock)
                            {
                                if (!((NormStreamObject*)object)->LockBlocks(firstLockId, lastBlockId))
                                {
                                    DMSG(4, "NormSession::ServerHandleNackMessage() node>%lu LockBlocks() failure\n",
                                            LocalNodeId());
                                    if (!squelchQueued) 
                                    {
                                        ServerQueueSquelch(nextObjectId);
                                        squelchQueued = true;
                                    }
                                    break;
                                } 
                            }
                            else
                            {
                                break;   // ignore late arriving NACK
                            }   
                        }  // end if (object->IsStream()
                        if (holdoff)
                        {
                            if (nextObjectId == txObjectIndex)
                            {
                                if (nextBlockId >= txBlockIndex)
                                    object->TxResetBlocks(nextBlockId, lastBlockId);
                                else if (lastBlockId >= txBlockIndex)
                                    object->TxResetBlocks(txBlockIndex, lastBlockId);
                            }
                            else if (nextObjectId > txObjectIndex)
                            {
                                if (object->TxResetBlocks(nextBlockId, lastBlockId))
                                {
                                    if (!tx_pending_mask.Set(nextObjectId))
                                        DMSG(0, "NormSession::ServerHandleNackMessage() tx_pending_mask.Set(%hu) error (2)\n",
                                                (UINT16)nextObjectId);
                                }
                            }
                        }
                        else
                        {
                            object->HandleBlockRequest(nextBlockId, lastBlockId);
                            startTimer = true;
                        }
                        break;
                    case SEGMENT:
                        DMSG(8, "NormSession::ServerHandleNackMessage(SEGMENT) obj>%hu blk>%lu segs>%hu:%hu\n", 
                                (UINT16)nextObjectId, (UINT32)nextBlockId, 
                                (UINT32)nextSegmentId, (UINT32)lastSegmentId);
                        inRange = false;  // SEGMENT repairs are also handled in one pass
                        if (nextBlockId != prevBlockId) freshBlock = true;
                        if (freshBlock)
                        {
                            freshBlock = false;
                            // Is this entire block already repair pending?
                            if (object->IsRepairSet(nextBlockId)) 
                                break;
                            if (!(block = object->FindBlock(nextBlockId)))
                            {
                                // Is this entire block already tx pending?
                                if (!object->IsPendingSet(nextBlockId))
                                {
                                    // Try to recover block including parity calculation 
                                    if (!(block = object->ServerRecoverBlock(nextBlockId)))
                                    {
                                        if (NormObject::STREAM == object->GetType())
                                        {
                                            DMSG(4, "NormSession::ServerHandleNackMessage() node>%lu "
                                                    "recvd repair request for old stream block(%lu) ...\n",
                                                    LocalNodeId(), (UINT32)nextBlockId);
                                            if (!squelchQueued) 
                                            {
                                                ServerQueueSquelch(nextObjectId);
                                                squelchQueued = true;
                                            }
                                        }
                                        else
                                        {
                                            // Resource constrained, move on.
                                            DMSG(2, "NormSession::ServerHandleNackMessage() node>%lu "
                                                    "Warning - server is resource contrained ...\n");
                                        }  
                                        break;
                                    }
                                }
                                else
                                {
                                    // Entire block already tx pending, don't recover
                                    DMSG(0, "NormSession::ServerHandleNackMessage() node>%lu "
                                            "recvd SEGMENT repair request for pending block.\n");
                                    break;   
                                }
                            }
                            numErasures = extra_parity;
                            prevBlockId = nextBlockId;
                        }
                        // If stream && explicit data repair, lock the data for retransmission
                        
                        if (object->IsStream() && (nextSegmentId < ndata))
                        {
                            bool attemptLock = true;
                            NormSegmentId firstLockId = nextSegmentId;
                            NormSegmentId lastLockId = ndata - 1;
                            lastLockId = MIN(lastLockId, lastSegmentId);
                            if (holdoff)
                            {
                                if (nextObjectId == txObjectIndex)
                                {
                                    if (nextBlockId < txBlockIndex)
                                    {
                                        if (1 == (txBlockIndex - nextBlockId))
                                        {
                                            // We're currently sending this block
                                            if (block->IsPending())
                                            {
                                                NormSegmentId firstPending = 0;
                                                block->GetFirstPending(firstPending);
                                                if (lastLockId <= firstPending)
                                                    attemptLock = false;
                                                else if (nextSegmentId < firstPending)
                                                    firstLockId = firstPending;
                                            }
                                            else
                                            {
                                                // block was just recovered   
                                            }
                                        }
                                        else
                                        {
                                            attemptLock = false;  // NACK arrived way too late    
                                        }                                           
                                    }
                                }
                                else if (nextObjectId < txObjectIndex)
                                {
                                    attemptLock = false;  // NACK arrived too late   
                                }
                            }
                            if (attemptLock)
                            {
                                if (!((NormStreamObject*)object)->LockSegments(nextBlockId, firstLockId, lastLockId))
                                {
                                    DMSG(0, "NormSession::ServerHandleNackMessage() node>%lu "
                                            "LockSegments() failure\n", LocalNodeId());
                                    if (!squelchQueued) 
                                    {
                                        ServerQueueSquelch(nextObjectId);
                                        squelchQueued = true;
                                    }
                                    break;
                                }  
                            }  
                            else
                            {
                                break;  // ignore late arriving NACK
                            }  
                        }  // end if (object->IsStream() && (nextSegmentId < ndata))
                            
                        // With a series of SEGMENT repair requests for a block, "numErasures" will
                        // eventually total the number of missing segments in the block.
                        numErasures += (lastSegmentId - nextSegmentId + 1);
                        if (holdoff)
                        {
                            if (nextObjectId > txObjectIndex)
                            {
                                if (object->TxUpdateBlock(block, nextSegmentId, lastSegmentId, numErasures))
                                {
                                    if (!tx_pending_mask.Set(nextObjectId))
                                        DMSG(0, "NormSession::ServerHandleNackMessage() tx_pending_mask.Set(%hu) error (3)\n",
                                                (UINT16)nextObjectId);       
                                }
                            }
                            else if (nextObjectId == txObjectIndex)
                            {
                                if (nextBlockId >= txBlockIndex)
                                {
                                    object->TxUpdateBlock(block, nextSegmentId, lastSegmentId, numErasures);   
                                } 
                                else if (1 == (txBlockIndex - nextBlockId))
                                {
                                    NormSegmentId firstPending = 0;
                                    if (block->GetFirstPending(firstPending))
                                    {
                                        if (nextSegmentId > firstPending)
                                            object->TxUpdateBlock(block, nextSegmentId, lastSegmentId, numErasures);
                                        else if (lastSegmentId > firstPending)
                                            object->TxUpdateBlock(block, firstPending, lastSegmentId, numErasures);
                                        else if (numErasures > block->ParityCount())
                                            object->TxUpdateBlock(block, firstPending, firstPending, numErasures);
                                    }
                                    else
                                    {
                                        // This block was just recovered, so do full update
                                        object->TxUpdateBlock(block, nextSegmentId, lastSegmentId, numErasures);
                                    }
                                }
                            }
                        }
                        else
                        {
                            block->HandleSegmentRequest(nextSegmentId, lastSegmentId, 
                                                        object->GetBlockSize(block->GetId()), 
                                                        nparity, numErasures);
                            startTimer = true;
                        }  // end if/else (holdoff)
                        break;
                    case INFO:
                        nextObjectId++;
                        if (nextObjectId > lastObjectId) 
                            inRange = false;
                        break; 
                }  // end switch(requestLevel)
            }  // end while(inRange)
        }  // end while(NextRepairItem())
    }  // end while(UnpackRepairRequest())
    if (startTimer && !repair_timer.IsActive())
    {
        // BACKOFF related code
        double aggregateInterval = address.IsMulticast() ? 
                                    grtt_advertised * (backoff_factor + 1.0) : 0.0;
        // backoff == 0.0 is a special case
        //aggregateInterval = (backoff_factor > 0.0) ? aggregateInterval : 0.0;
        
        if (tx_timer.IsActive())
        {
            double txTimeout = tx_timer.GetTimeRemaining() - 1.0e-06;
            aggregateInterval = MAX(txTimeout, aggregateInterval);   
        }
          
        repair_timer.SetInterval(aggregateInterval);  
        DMSG(4, "NormSession::ServerHandleNackMessage() node>%lu starting server "
                "NACK aggregation timer (%lf sec)...\n", LocalNodeId(), aggregateInterval);
        ActivateTimer(repair_timer); 
    }
}  // end NormSession::ServerHandleNackMessage()


void NormSession::ClientHandleAckMessage(const NormAckMsg& ack)
{
    NormServerNode* theServer = (NormServerNode*)server_tree.FindNodeById(ack.GetServerId());
    if (theServer)
    {
        theServer->HandleAckMessage(ack); 
    }
    else
    {
        DMSG(4, "NormSession::ClientHandleAckMessage() node>%lu heard ACK for unknown server.\n",
                LocalNodeId()); 
    }
}  // end NormSession::ClientHandleAckMessage()

void NormSession::ClientHandleNackMessage(const NormNackMsg& nack)
{
    NormServerNode* theServer = (NormServerNode*)server_tree.FindNodeById(nack.GetServerId());
    if (theServer)
    {
        theServer->HandleNackMessage(nack);
    }
    else
    {
        DMSG(4, "NormSession::ClientHandleNackMessage() node>%lu heard NACK for unknown server\n",
                LocalNodeId());   
    }
}  // end NormSession::ClientHandleNackMessage()


bool NormSession::ServerQueueSquelch(NormObjectId objectId)
{
    // (TBD) if a squelch is already queued, update it if (objectId < squelch->objectId)
    NormCmdSquelchMsg* squelch = (NormCmdSquelchMsg*)GetMessageFromPool();
    if (squelch)
    {
        squelch->Init();
        squelch->SetDestination(address);
        squelch->SetGrtt(grtt_quantized);
        squelch->SetBackoffFactor((unsigned char)backoff_factor);
        squelch->SetGroupSize(gsize_quantized);
        NormObject* obj = tx_table.Find(objectId);
        NormObjectTable::Iterator iterator(tx_table);
        NormObjectId nextId;
        if (obj)
        {
            ASSERT(NormObject::STREAM == obj->GetType());
            squelch->SetObjectId(objectId);
            squelch->SetFecBlockId(((NormStreamObject*)obj)->StreamBufferLo());
            squelch->SetFecSymbolId(0);
            squelch->ResetInvalidObjectList();
            while ((obj = iterator.GetNextObject()))
                if (objectId == obj->GetId()) break;
            nextId = objectId + 1;
        }
        else
        {
            obj = iterator.GetNextObject();
            if (obj)
            {
               squelch->SetObjectId(obj->GetId());
               if (obj->IsStream())
                   squelch->SetFecBlockId(((NormStreamObject*)obj)->StreamBufferLo());
               else
                   squelch->SetFecBlockId(0);
               squelch->SetFecSymbolId(0); 
               nextId = obj->GetId() + 1;
            }
            else
            {
                // Squelch to point to future object
                squelch->SetObjectId(next_tx_object_id);
                squelch->SetFecBlockId(0);
                squelch->SetFecSymbolId(0);
                nextId = next_tx_object_id;
            }
        }
        bool buildingList = true;
        while (buildingList && (obj = iterator.GetNextObject()))
        {
            while (nextId != obj->GetId())
            {
                if (!squelch->AppendInvalidObject(nextId, segment_size))
                {
                    buildingList = false;
                    break;
                }
                nextId++;
            }
        }
        QueueMessage(squelch);
        DMSG(4, "NormSession::ServerQueueSquelch() node>%lu server queued squelch ...\n",
                LocalNodeId());
        return true;
    }
    else
    {
        DMSG(0, " NormSession::ServerQueueSquelch() node>%lu message_pool exhausted! (couldn't squelch)\n",
                LocalNodeId());  
        return false;
    }
}  // end NormSession::ServerQueueSquelch()


bool NormSession::ServerBuildRepairAdv(NormCmdRepairAdvMsg& cmd)
{
    // Build a NORM_CMD(REPAIR_ADV) message with current pending repair state.
    NormRepairRequest req;
    req.SetFlag(NormRepairRequest::OBJECT);
    NormRepairRequest::Form prevForm = NormRepairRequest::INVALID;
    NormObjectId firstId;
    UINT16 objectCount = 0;
    NormObjectTable::Iterator iterator(tx_table);
    NormObject* nextObject = iterator.GetNextObject();
    while (nextObject)
    {
        NormObject* currentObject = nextObject;
        nextObject = iterator.GetNextObject();
        NormObjectId currentId = currentObject->GetId();
        bool repairEntireObject = tx_repair_mask.Test(currentId);
        if (repairEntireObject)
        {
            if (!objectCount) firstId = currentId;  // set first OBJECT level repair id
            objectCount++;  // increment consecutive OBJECT level repair count.
        }
        
        // Check for non-OBJECT level request or end
        if (objectCount && (!repairEntireObject || !nextObject))
        {
            NormRepairRequest::Form form;
            switch (objectCount)
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
                        DMSG(0, "NormSession::ServerBuildRepairAdv() warning: full msg\n");
                        break;
                    }    
                }
                req.SetForm(form);
                cmd.AttachRepairRequest(req, segment_size);
                prevForm = form;
            }
            switch (form)
            {
                case 0:
                    ASSERT(0);  // can't happen
                    break;
                case 1:
                case 2:
                    req.SetForm(NormRepairRequest::ITEMS);
                    req.AppendRepairItem(firstId, 0, ndata, 0);       // (TBD) error check
                    if (2 == objectCount) 
                        req.AppendRepairItem(currentId, 0, ndata, 0); // (TBD) error check
                    break;
                default:
                    req.SetForm(NormRepairRequest::RANGES);
                    req.AppendRepairRange(firstId, 0, ndata, 0,       // (TBD) error check
                                          currentId, 0, ndata, 0); 
                    break;
            }   
            cmd.PackRepairRequest(req);
            objectCount = 0;
        }
        if (!repairEntireObject)
        {           
            if ((NormRepairRequest::INVALID != prevForm) && currentObject->IsRepairPending())
            {
                cmd.PackRepairRequest(req);  // (TBD) error check;
                prevForm = NormRepairRequest::INVALID;
                currentObject->AppendRepairAdv(cmd);
            }
            else
            {
                currentObject->AppendRepairAdv(cmd);
            }
            objectCount = 0;
        }
    }  // end while (nextObject)
    if (NormRepairRequest::INVALID != prevForm)
    {
        if (0 == cmd.PackRepairRequest(req))
            DMSG(0, "NormSession::ServerBuildRepairAdv() warning: full msg\n");
    }
    return true;
}  // end NormSession::ServerBuildRepairAdv()

bool NormSession::OnRepairTimeout(ProtoTimer& /*theTimer*/)
{
    if (repair_timer.GetRepeatCount())
    {
        // NACK aggregation period has ended. (incorporate accumulated repair requests)
        DMSG(4, "NormSession::OnRepairTimeout() node>%lu server NACK aggregation time ended.\n",
                LocalNodeId()); 
        
        NormObjectTable::Iterator iterator(tx_table);
        NormObject* obj;
        while ((obj = iterator.GetNextObject()))
        {
            NormObjectId objectId = obj->GetId();
            if (tx_repair_mask.Test(objectId))
            {
                DMSG(6, "NormSession::OnRepairTimeout() node>%lu tx reset obj>%hu ...\n",
                        LocalNodeId(), (UINT16)objectId);
                if (obj->IsStream())
                    obj->TxReset(((NormStreamObject*)obj)->StreamBufferLo());
                else
                    obj->TxReset();
                tx_repair_mask.Unset(objectId);
                if (!tx_pending_mask.Set(objectId))
                    DMSG(0, "NormSession::OnRepairTimeout() rx_pending_mask.Set(%hu) error (1)\n",
                            (UINT16)objectId);
            }  
            else
            {
                //DMSG(6, "NormSession::OnRepairTimeout() node>%lu activating obj>%hu repairs ...\n",
                //        LocalNodeId(), (UINT16)objectId);
                if (obj->ActivateRepairs()) 
                {
                    DMSG(6, "NormSession::OnRepairTimeout() node>%lu activated obj>%hu repairs ...\n",
                            LocalNodeId(), (UINT16)objectId);
                    if (!tx_pending_mask.Set(objectId))
                        DMSG(0, "NormSession::OnRepairTimeout() rx_pending_mask.Set(%hu) error (2)\n",
                                (UINT16)objectId); 
                }  
            } 
        }  // end while (iterator.GetNextObject())                
        TouchServer(); 
        // BACKOFF related code
        // Holdoff initiation of new repair cycle for one GRTT 
        // (TBD) for unicast sessions, use CLR RTT ???
        //double holdoffInterval = backoff_factor > 0.0 ? grtt_advertised : 0.0;     
        double holdoffInterval = grtt_advertised;   
        repair_timer.SetInterval(holdoffInterval); // repair holdoff interval = 1*GRTT
        DMSG(4, "NormSession::OnRepairTimeout() node>%lu starting server "
                "NACK holdoff timer (%lf sec)...\n", LocalNodeId(), holdoffInterval);
        
    }
    else
    {
        // REPAIR holdoff interval has now ended.
        DMSG(4, "NormSession::OnRepairTimeout() node>%lu server holdoff time ended.\n",
                LocalNodeId());   
    }
    return true;
}  // end NormSession::OnRepairTimeout()

// (TBD) Should pass current system time to ProtoTimer timeout handlers
//       for more efficiency ...
bool NormSession::OnTxTimeout(ProtoTimer& /*theTimer*/)
{
    NormMsg* msg;  
    
    // (TBD) sometimes need RepairAdv even when cc_enable is false ...                        
    NormCmdRepairAdvMsg adv;
        
    if (advertise_repairs && (probe_proactive || (repair_timer.IsActive() && 
                                                  repair_timer.GetRepeatCount())))
    {
        // Build a NORM_CMD(NACK_ADV) in response to 
        // receipt of unicast NACK or CC update  
        adv.Init();
        adv.SetGrtt(grtt_quantized);
        adv.SetBackoffFactor((unsigned char)backoff_factor);
        adv.SetGroupSize(gsize_quantized);
        adv.SetDestination(address);
        
        // Fill in congestion control header extension
        NormCCFeedbackExtension ext;
        adv.AttachExtension(ext);
        
        if (suppress_rate < 0.0)
        {
            ext.SetCCFlag(NormCC::RTT);
            ext.SetCCRtt(grtt_quantized);
            ext.SetCCRate(NormQuantizeRate(tx_rate));
        }
        else
        {
            if (!suppress_nonconfirmed) ext.SetCCFlag(NormCC::RTT);
            ext.SetCCRtt(NormQuantizeRtt(suppress_rtt));
            ext.SetCCRate(NormQuantizeRate(suppress_rate));
        }
        
        ServerBuildRepairAdv(adv);
        
        msg = (NormMsg*)&adv;
    }
    else
    {
        msg = message_queue.RemoveHead();
        advertise_repairs = false;
    }
    
    suppress_rate = -1.0;  // reset cc feedback suppression rate
    
    if (msg)
    {
        if (SendMessage(*msg))
        {
            if (advertise_repairs)
                advertise_repairs = false;
            else
                ReturnMessageToPool(msg);
            // Pre-serve to allow pre-prompt for empty tx queue
            if (message_queue.IsEmpty() && IsServer()) Serve();
        }
        else
        {
            // Requeue the message for another try
            if (!advertise_repairs) 
                message_queue.Prepend(msg);   
        }
        return true;  // reinstall tx_timer
    }
    else
    {   
        // 1) Prompt for next server message
        if (IsServer()) Serve();
        
        if (message_queue.IsEmpty())
        {
            tx_timer.Deactivate();
            // Check that any possible notifications posted in
            // the previous call to Serve() may have caused a
            // change in server state making it ready to send
            //if (IsServer()) Serve();
            return false;
        }
        else
        {
            // We have a new message as a result of serving, so send it immediately
            return OnTxTimeout(tx_timer);
        }
    }
}  // end NormSession::OnTxTimeout()


bool NormSession::SendMessage(NormMsg& msg)
{   
    struct timeval currentTime;
    ProtoSystemTime(currentTime); 
    
    bool clientMsg = false;
    bool isProbe = false;
    
    // Fill in any last minute timestamps
    // (TBD) fill in SessionId fields on all messages as needed
    switch (msg.GetType())
    {
        case NormMsg::INFO:
        case NormMsg::DATA:
            ((NormObjectMsg&)msg).SetSessionId(session_id);
            msg.SetSequence(tx_sequence++);  // (TBD) set for session dst msgs
            break;
        case NormMsg::CMD:
            ((NormCmdMsg&)msg).SetSessionId(session_id);
            switch (((NormCmdMsg&)msg).GetFlavor())
            {
                case NormCmdMsg::CC:
                    ((NormCmdCCMsg&)msg).SetSendTime(currentTime); 
                    isProbe = true; 
                    break;
                default:
                    break;
            }
            msg.SetSequence(tx_sequence++);  // (TBD) set for session dst msgs
            break;

        case NormMsg::NACK:
        {
            clientMsg = true;
            NormNackMsg& nack = (NormNackMsg&)msg;
            NormServerNode* theServer = 
                (NormServerNode*)server_tree.FindNodeById(nack.GetServerId());
            ASSERT(theServer);
            struct timeval grttResponse;
            theServer->CalculateGrttResponse(currentTime, grttResponse);
            nack.SetGrttResponse(grttResponse);
            break;
        }

        case NormMsg::ACK:
        {
            clientMsg = true;
            NormAckMsg& ack = (NormAckMsg&)msg;
            NormServerNode* theServer = 
                (NormServerNode*)server_tree.FindNodeById(ack.GetServerId());
            ASSERT(theServer);
            struct timeval grttResponse;
            theServer->CalculateGrttResponse(currentTime, grttResponse);
            ack.SetGrttResponse(grttResponse);
            break;
        }

        default:
            break;
    }
    // Fill in common message fields
     
    msg.SetSourceId(local_node_id);
    UINT16 msgSize = msg.GetLength();
    bool result = true;
    // Possibly drop some tx messages for testing purposes
    bool drop = (UniformRand(100.0) < tx_loss_rate);
    if (drop || (clientMsg && client_silent))
    {
        //DMSG(0, "TX MESSAGE DROPPED! (tx_loss_rate:%lf\n", tx_loss_rate); 
    }    
    else
    {
        if (tx_socket->SendTo(msg.GetBuffer(), msgSize,
                              msg.GetDestination()))
        {
            // Separate send/recv tracing
            if (trace) NormTrace(currentTime, LocalNodeId(), msg, true);  
            
            // Keep track of _actual_ sent rate 
            // (TBD) move "sent_rate" tracking to ""OnReportTimeout()
            // since it is no longer critical to protocol operation
            // but just kept for information purposes ...
            if (prev_update_time.tv_sec || prev_update_time.tv_usec)
            {
                double interval = (double)(currentTime.tv_sec - prev_update_time.tv_sec);
                if (currentTime.tv_usec > prev_update_time.tv_sec)
                    interval += 1.0e-06*(double)(currentTime.tv_usec - prev_update_time.tv_usec);
                else
                    interval -= 1.0e-06*(double)(prev_update_time.tv_usec - currentTime.tv_usec);                
                if (interval < 1.0) //grtt_advertised)
                {
                    sent_accumulator += msgSize;
                }
                else
                {
                     sent_rate = ((double)(sent_accumulator)) / interval;
                     prev_update_time = currentTime;
                     sent_accumulator = msgSize;
                }
            }
            else
            {
                sent_rate = ((double)msgSize) / grtt_advertised;
                prev_update_time = currentTime; 
                sent_accumulator = msgSize;    
            }
            // Update nominal packet size
            nominal_packet_size += 0.05 * (((double)msgSize) - nominal_packet_size);    
        }
        else
        {
            DMSG(8, "NormSession::SendMessage() sendto() error\n");
            result = false;
        }
    }
    if (result && isProbe)
    {
        probe_pending = false;
        if (probe_reset) 
        {
            probe_reset = false;
            OnProbeTimeout(probe_timer);
            ActivateTimer(probe_timer);  
        }
    }
    tx_timer.SetInterval(((double)msgSize) / tx_rate);
    return result;
}  // end NormSession::SendMessage()

bool NormSession::OnProbeTimeout(ProtoTimer& /*theTimer*/)
{
    // 1) Temporarily kill probe_timer if CMD(CC) not yet tx'd
    if (probe_pending)
    {
        probe_reset = true;
        probe_timer.Deactivate();
        return false;
    } 
    else if (0.0 == tx_rate)
    {
        // Sender paused, just idle probing until transmission is resumed
        return true;
    }
    
    // 2) Update grtt_estimate _if_ sufficient time elapsed.
    // This new code allows more liberal downward adjustment of
    // of grtt when congestion control is enabled. 
    grtt_age += probe_timer.GetInterval();
    double ageMax = 3 * grtt_advertised;
    ageMax = ageMax > grtt_interval_min ? ageMax : grtt_interval_min;
    if (grtt_age >= ageMax)//grtt_interval)
    {
        if (grtt_response)
        {
            // Update grtt estimate 
            if (grtt_current_peak < grtt_measured)
            {
                if (grtt_decrease_delay_count-- == 0)
                {
                    grtt_measured = 0.5 * grtt_measured + 
                                    0.5 * grtt_current_peak;
                    grtt_current_peak = 0.0;
                    grtt_decrease_delay_count = DEFAULT_GRTT_DECREASE_DELAY;
                }
            }
            else
            {
                // Increase already incorporated
                grtt_current_peak = 0.0;
                grtt_decrease_delay_count = DEFAULT_GRTT_DECREASE_DELAY;   
            }
            if (grtt_measured < NORM_GRTT_MIN)
                grtt_measured = NORM_GRTT_MIN;
            else if (grtt_measured > grtt_max)
                grtt_measured = grtt_max;
            double pktInterval = (double)(44+segment_size)/tx_rate;
            grtt_advertised = MAX(pktInterval, grtt_measured);
            double grttQuantizedOld = grtt_quantized;
            grtt_quantized = NormQuantizeRtt(grtt_advertised);
            // Recalculate grtt_advertise since quantization rounds upward
            grtt_advertised = NormUnquantizeRtt(grtt_quantized);
            grtt_response = false;
            if (grttQuantizedOld != grtt_quantized)
                DMSG(4, "NormSession::OnProbeTimeout() node>%lu new grtt: %lf\n",
                        LocalNodeId(), grtt_advertised);
        }
        grtt_age = 0.0;
    }
    
    if (grtt_interval < grtt_interval_min)
        grtt_interval = grtt_interval_min;
    else
        grtt_interval *= 1.5;
    if (grtt_interval > grtt_interval_max)
        grtt_interval = grtt_interval_max;    
    
    // 3) Build a NORM_CMD(CC) message
    NormCmdCCMsg* cmd = (NormCmdCCMsg*)GetMessageFromPool();
    if (!cmd)
    {
        DMSG(0, "NormSession::OnProbeTimeout() node>%lu message_pool empty! can't probe\n",
                LocalNodeId());   
        return true;
    } 
    cmd->Init();
    cmd->SetDestination(address);
    cmd->SetGrtt(grtt_quantized);
    
    cmd->SetBackoffFactor((unsigned char)backoff_factor);
    cmd->SetGroupSize(gsize_quantized);  
    // SetSendTime() when message is being sent (in OnTxTimeout())
    cmd->SetCCSequence(cc_sequence++);
    
    if (probe_proactive)
    {
        NormCCRateExtension ext;
        cmd->AttachExtension(ext);
        ext.SetSendRate(NormQuantizeRate(tx_rate));
    }
        
    double probeInterval;
    if (cc_enable)
    {
        // Iterate over cc_node_list and append cc_nodes ...
        NormNodeListIterator iterator(cc_node_list);
        NormCCNode* next;
        while ((next = (NormCCNode*)iterator.GetNextNode()))
        {
            if (next->IsActive())
            {
                UINT8 ccFlags = 0;
                if (next->IsClr()) ccFlags |= (UINT8)NormCC::CLR;
                ccFlags |= (UINT8)NormCC::RTT;
                UINT8 rttQuantized = NormQuantizeRtt(next->GetRtt());
                if (cc_slow_start) ccFlags |= (UINT8)NormCC::START;
                UINT16 rateQuantized = NormQuantizeRate(next->GetRate());
                // (TBD) check result
                cmd->AppendCCNode(segment_size, 
                                  next->GetId(), 
                                  ccFlags, 
                                  rttQuantized,
                                  rateQuantized);
                if (!next->IsClr()) next->SetActive(false);
            }             
        }
       
        AdjustRate(false);
        
        // Determine next probe_interval
        const NormCCNode* clr = (const NormCCNode*)cc_node_list.Head();
        probeInterval = clr ? MIN(grtt_advertised, clr->GetRtt()) : grtt_advertised;
        //double nominalRate = ((double)segment_size)/((double)tx_rate);
        //probeInterval = MAX(probeInterval, nominalRate);
    }
    else
    {
        // Determine next probe_interval
        probeInterval = grtt_interval;
    }
       
    QueueMessage(cmd);  
    probe_pending = true;  
    
    // 3) Set probe_timer interval
    probe_timer.SetInterval(probeInterval);
    
    return true;
}  // end NormSession::OnProbeTimeout()


void NormSession::AdjustRate(bool onResponse)
{
    const NormCCNode* clr = (const NormCCNode*)cc_node_list.Head();
    double ccRtt = clr ? clr->GetRtt() : grtt_measured;
    double ccLoss = clr ? clr->GetLoss() : 0.0;
    double oldRate = tx_rate;
    if (onResponse)
    {
        // Adjust rate based on CLR feedback and
        // adjust probe schedule
        ASSERT(clr);
        // (TBD) check feedback age
        if (cc_slow_start)
        {
            tx_rate = clr->GetRate();
            DMSG(6, "NormSession::AdjustRate(slow start) clr>%lu newRate>%lf (oldRate>%lf sentRate>%lf clrRate>%lf\n",
                    clr->GetId(), tx_rate*8.0/1000.0,  oldRate*8.0/1000.0, sent_rate*8.0/1000.0, 
                    clr->GetRate()*8.0/1000.0);          
        }
        else
        {
            double clrRate = clr->GetRate();
            if (clrRate > tx_rate)
            {
                double linRate = tx_rate + segment_size;
                tx_rate = MIN(clrRate, linRate);
            }
            else
            {
                tx_rate = clrRate;
            }  
            DMSG(6, "NormSession::AdjustRate(stdy state) clr>%lu newRate>%lf (rtt>%lf loss>%lf)\n",
                    clr->GetId(), tx_rate*8.0/1000.0, clr->GetRtt(), clr->GetLoss());
        }
    }
    else if (clr)
    {
        // (TBD) fix CC feedback aging ...
        /*int feedbackAge  = abs((int)cc_sequence - (int)clr->GetCCSequence());
        DMSG(0, "NormSession::AdjustRate() feedback age>%d (%d - %d\n", 
                feedbackAge, cc_sequence, clr->GetCCSequence());
        
        if (feedbackAge > 50)
        {
            double linRate = tx_rate - segment_size;
            linRate = MAX(linRate, 0.0);
            double expRate = tx_rate * 0.5;
            if (feedbackAge > 4)
                tx_rate = MIN(linRate, expRate);
            else
                tx_rate = MAX(linRate, expRate);
            
        }*/
    }
    
    // Don't let tx_rate below MIN(one segment per grtt, one segment per second)
    double minRate = ((double)segment_size) / grtt_measured;
    minRate = MIN((double)segment_size, minRate);   
    tx_rate = MAX(tx_rate, minRate);
    
    // Keep "tx_rate" within user set rate bounds (if any)
    if ((tx_rate_min >= 0.0) && (tx_rate < tx_rate_min))
        tx_rate = tx_rate_min;
    if ((tx_rate_max >= 0.0) && (tx_rate > tx_rate_max))
        tx_rate = tx_rate_max;
    
    struct timeval currentTime;
    ::ProtoSystemTime(currentTime);
    double theTime = (double)currentTime.tv_sec + 1.0e-06 * ((double)currentTime.tv_usec);
    
    if (tx_rate != oldRate)
    {
        if (tx_timer.IsActive())
        {
            //double ratio = tx_rate / oldRate;
            double txInterval = tx_timer.GetInterval() * oldRate / tx_rate;
            double timeElapsed = tx_timer.GetInterval() - tx_timer.GetTimeRemaining();
            txInterval = timeElapsed < txInterval ?  (txInterval - timeElapsed) : 0.0;
            tx_timer.SetInterval(txInterval);
            tx_timer.Reschedule();
        }
    }
    
    DMSG(8, "ServerRateTracking time>%lf rate>%lf rtt>%lf loss>%lf\n\n", theTime, tx_rate*(8.0/1000.0), ccRtt, ccLoss);
}  // end NormSession::AdjustRate()

bool NormSession::OnReportTimeout(ProtoTimer& /*theTimer*/)
{
    // Client reporting (just print out for now)
    struct timeval currentTime;
    ProtoSystemTime(currentTime);
#ifdef _WIN32_WCE
    struct tm timeStruct;
    timeStruct.tm_hour = currentTime.tv_sec / 3600;
    unsigned long hourSecs = 3600 * timeStruct.tm_hour;
    timeStruct.tm_min = (currentTime.tv_sec - (hourSecs)) / 60;
    timeStruct.tm_sec = currentTime.tv_sec - (hourSecs) - (60*timeStruct.tm_min);
    timeStruct.tm_hour = timeStruct.tm_hour % 24;
    struct tm* ct = &timeStruct;
#else            
    struct tm* ct = gmtime((time_t*)&currentTime.tv_sec);
#endif // if/else _WIN32_WCE
    DMSG(2, "REPORT time>%02d:%02d:%02d.%06lu node>%lu ***************************************\n", 
            ct->tm_hour, ct->tm_min, ct->tm_sec, currentTime.tv_usec, LocalNodeId());
    if (IsServer())
    {
        DMSG(2, "Local status:\n");
        DMSG(2, "   txRate>%9.3lf kbps sentRate>%9.3lf grtt>%lf\n", 
                 ((double)tx_rate)*8.0/1000.0, sent_rate*8.0/1000.0, grtt_advertised);
        if (cc_enable)
        {
            const NormCCNode* clr = (const NormCCNode*)cc_node_list.Head(); 
            if (clr)  
                DMSG(2, "   clr>%lu rate>%9.3lf rtt>%lf loss>%lf\n", clr->GetId(),
                     clr->GetRate()*8.0/1000.0, clr->GetRtt(), clr->GetLoss());
        }
            
    }
    if (IsClient())
    {
        NormNodeTreeIterator iterator(server_tree);
        NormServerNode* next;
        while ((next = (NormServerNode*)iterator.GetNextNode()))
        {
            DMSG(2, "Remote sender>%lu\n", next->GetId());
            double rxRate = 8.0e-03*((double)next->RecvTotal()) / report_timer.GetInterval();  // kbps
            double rxGoodput = 8.0e-03*((double)next->RecvGoodput()) / report_timer.GetInterval();  // kbps
            next->ResetRecvStats();
            DMSG(2, "   rxRate>%9.3lf kbps rx_goodput>%9.3lf kbps\n", rxRate, rxGoodput);
            DMSG(2, "   rxObjects> completed>%lu pending>%lu failed:%lu\n", 
                    next->CompletionCount(), next->PendingCount(), next->FailureCount());
            DMSG(2, "   bufferUsage> current>%lu peak>%lu (overuns>%lu)\n", next->CurrentBufferUsage(),
                                                                            next->PeakBufferUsage(), 
                                                                            next->BufferOverunCount());
            DMSG(2, "   resyncs>%lu nacks>%lu suppressed>%lu\n", next->ResyncCount(),
                                                                 next->NackCount(), 
                                                                 next->SuppressCount());
                    
        }
    }  // end if (IsClient())
    DMSG(2, "***************************************************************************\n");
    return true;
}  // end NormSession::OnReportTimeout()

NormSessionMgr::NormSessionMgr(ProtoTimerMgr&           timerMgr, 
                               ProtoSocket::Notifier&   socketNotifier)
 : timer_mgr(timerMgr), socket_notifier(socketNotifier),
   controller(NULL), top_session(NULL)
{
}

NormSessionMgr::~NormSessionMgr()
{
    Destroy();
}

void NormSessionMgr::Destroy()
{
    NormSession* next;
    while ((next = top_session))
    {
        top_session = next->next;
        delete next;
    }
}  // end NormSessionMgr::Destroy()

NormSession* NormSessionMgr::NewSession(const char* sessionAddress,
                                        UINT16      sessionPort,
                                        NormNodeId  localNodeId)
{
    if ((NORM_NODE_ANY == localNodeId) || (NORM_NODE_NONE == localNodeId))
    {
        // Use local ip address to assign default localNodeId
        ProtoAddress localAddr;
        if (!localAddr.ResolveLocalAddress())
        {
            DMSG(0, "NormSessionMgr::NewSession() local address lookup error\n");
            return ((NormSession*)NULL);    
        } 
        // (TBD) test IPv6 "EndIdentifier" ???
        localNodeId = localAddr.EndIdentifier();
    }
    ProtoAddress theAddress;
    if (!theAddress.ResolveFromString(sessionAddress))
    {
        DMSG(0, "NormSessionMgr::NewSession() session address lookup error!\n");
        return ((NormSession*)NULL);   
    }
    theAddress.SetPort(sessionPort);    
    NormSession* theSession = new NormSession(*this, localNodeId);   
    if (!theSession)
    {
        DMSG(0, "NormSessionMgr::NewSession() new session error: %s\n", 
                GetErrorString()); 
        return ((NormSession*)NULL);  
    }     
    theSession->SetAddress(theAddress);    
    // Add new session to our session list
    theSession->next = top_session;
    top_session = theSession;
    return theSession;
}  // end NormSessionMgr::NewSession();

void NormSessionMgr::DeleteSession(class NormSession* theSession)
{
    NormSession* prev = NULL;
    NormSession* next = top_session;
    while (next && (next != theSession))
    {
        prev = next;
        next = next->next;   
    }
    if (next)
    {
        if (prev)
            prev->next = theSession->next;
        else
            top_session = theSession->next;   
        delete theSession;
    }
}  // end NormSessionMgr::DeleteSession()

