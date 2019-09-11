#include "normSession.h"
#include <time.h>  // for gmtime() in NormTrace()


const UINT8 NormSession::DEFAULT_TTL = 255; // bits/sec
const double NormSession::DEFAULT_TRANSMIT_RATE = 64000.0; // bits/sec
const double NormSession::DEFAULT_GRTT_INTERVAL_MIN = 1.0;        // sec
const double NormSession::DEFAULT_GRTT_INTERVAL_MAX = 30.0;       // sec
const double NormSession::DEFAULT_GRTT_ESTIMATE = 0.25;    // sec
const double NormSession::DEFAULT_GRTT_MAX = 10.0;        // sec
const unsigned int NormSession::DEFAULT_GRTT_DECREASE_DELAY = 3;
const double NormSession::DEFAULT_BACKOFF_FACTOR = 4.0;   
const double NormSession::DEFAULT_GSIZE_ESTIMATE = 1000.0; 
const UINT16 NormSession::DEFAULT_NDATA = 64; 
const UINT16 NormSession::DEFAULT_NPARITY = 32;  
const UINT16 NormSession::DEFAULT_TX_CACHE_MIN = 8;
const UINT16 NormSession::DEFAULT_TX_CACHE_MAX = 256;

const int NormSession::DEFAULT_ROBUST_FACTOR = 20;  // default robust factor

NormSession::NormSession(NormSessionMgr& sessionMgr, NormNodeId localNodeId) 
 : session_mgr(sessionMgr), notify_pending(false), tx_port(0), 
   tx_socket_actual(ProtoSocket::UDP), tx_socket(&tx_socket_actual), 
   rx_socket(ProtoSocket::UDP), local_node_id(localNodeId), 
   ttl(DEFAULT_TTL), tos(0), loopback(false), rx_port_reuse(false), rx_addr_bind(false),
   tx_rate(DEFAULT_TRANSMIT_RATE/8.0), tx_rate_min(-1.0), tx_rate_max(-1.0),
   backoff_factor(DEFAULT_BACKOFF_FACTOR), is_server(false), 
   tx_robust_factor(DEFAULT_ROBUST_FACTOR), instance_id(0),
   ndata(DEFAULT_NDATA), nparity(DEFAULT_NPARITY), auto_parity(0), extra_parity(0),
   sndr_emcon(false), encoder(NULL), 
   next_tx_object_id(0), 
   tx_cache_count_min(DEFAULT_TX_CACHE_MIN), 
   tx_cache_count_max(DEFAULT_TX_CACHE_MAX),
   tx_cache_size_max((UINT32)20*1024*1024),
   posted_tx_queue_empty(false), 
   acking_node_count(0), watermark_pending(false), tx_repair_pending(false), advertise_repairs(false),
   suppress_nonconfirmed(false), suppress_rate(-1.0), suppress_rtt(-1.0),
   probe_proactive(true), probe_pending(false), probe_reset(true), probe_data_check(false),
   grtt_interval(0.5), 
   grtt_interval_min(DEFAULT_GRTT_INTERVAL_MIN),
   grtt_interval_max(DEFAULT_GRTT_INTERVAL_MAX),
   grtt_max(DEFAULT_GRTT_MAX), 
   grtt_decrease_delay_count(DEFAULT_GRTT_DECREASE_DELAY),
   grtt_response(false), grtt_current_peak(0.0), grtt_age(0.0),
   cc_enable(false), cc_sequence(0), cc_slow_start(true), cc_active(false),
   is_client(false), rx_robust_factor(DEFAULT_ROBUST_FACTOR), unicast_nacks(false), 
   client_silent(false), rcvr_ignore_info(false), rcvr_max_delay(-1),
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
    probe_time_last.tv_sec = probe_time_last.tv_usec = 0;
    
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
            if (!tx_socket->Open(tx_port, address.GetType()))
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
        if (!rx_socket.Open(0, address.GetType(), false))
        {
            DMSG(0, "NormSession::Open() rx_socket open error\n");
            Close();
            return false;   
        }
        
        if (rx_port_reuse)
        {
            // Enable port/addr reuse and bind socket to destination address 
            if (!rx_socket.SetReuse(true))
            {
                DMSG(0, "NormSession::Open() rx_socket reuse error\n");
                Close();
                return false;   
            }
        }
        const ProtoAddress* bindAddr = NULL;
        if (rx_addr_bind)
        {
#ifndef WIN32
            if (address.IsMulticast())  // Win32 doesn't like to bind to multicast addr ???
                bindAddr = &address;
#endif // !WIN32
        }
        if(!rx_socket.Bind(address.GetPort(), bindAddr))
        {
            DMSG(0, "NormSession::Open() rx_socket bind error\n");
            Close();
            return false;
        }
    }
    if (0 != tos)
    {
        if (!tx_socket->SetTOS(tos))
        {
            DMSG(0, "NormSession::Open() warning: tx_socket set tos error\n");
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
    if (NULL != interfaceName)
    {
        bool result = true;
        if (rx_socket.IsOpen())
            result &= rx_socket.SetMulticastInterface(interfaceName);
        if (tx_socket->IsOpen())
            result &= tx_socket->SetMulticastInterface(interfaceName);
        strncpy(interface_name, interfaceName, 32);
        return result;
    }
    else
    {
        interface_name[0] = '\0';  
        return true; 
    }
}  // end NormSession::SetMulticastInterface()

void NormSession::SetTxRateInternal(double txRate)
{
    if (!is_server) 
    {
        tx_rate = txRate;
        return;
    }
    if (txRate < 0.0) 
    {
        DMSG(0, "NormSession::SetTxRateInternal() invalid transmit rate!\n");
        return;
    }
    if (tx_timer.IsActive())
    {
        if (txRate > 0.0)
        {
            double adjustInterval = (tx_rate/txRate) * tx_timer.GetTimeRemaining();
            if (adjustInterval > NORM_TICK_MIN)
            {
                tx_timer.SetInterval(adjustInterval);
                tx_timer.Reschedule();
            }
        }
        else
        {
            tx_timer.Deactivate();
        }
    }
    else if ((0.0 == tx_rate) && IsOpen())
    {
        tx_timer.SetInterval(0.0);
        if (txRate > 0.0) ActivateTimer(tx_timer);
    }
    tx_rate = txRate; 
    if (tx_rate > 0.0)
    {
        unsigned char grttQuantizedOld = grtt_quantized;
        double pktInterval = (double)(44+segment_size)/txRate;
        if (grtt_measured < pktInterval)
            grtt_quantized = NormQuantizeRtt(pktInterval);
        else
            grtt_quantized = NormQuantizeRtt(grtt_measured);
        grtt_advertised = NormUnquantizeRtt(grtt_quantized);
        
        // What do we do when "pktInterval" > "grtt_max"?
        // We will take our lumps with some extra activity timeout NACKs when they happen?
        if (grtt_advertised > grtt_max)
        {
            grtt_quantized = NormQuantizeRtt(grtt_max); 
            grtt_advertised = NormUnquantizeRtt(grtt_quantized);    
        }
        if (grttQuantizedOld != grtt_quantized)
        {
            DMSG(4, "NormSession::SetTxRateInternal() node>%lu %s to new grtt to: %lf sec\n",
                    LocalNodeId(), 
                    (grttQuantizedOld < grtt_quantized) ? "increased" : "decreased",
                    grtt_advertised);
            Notify(NormController::GRTT_UPDATED, (NormServerNode*)NULL, (NormObject*)NULL);
        }
    }
}  // end NormSession::SetTxRateInternal()

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
    else if (rateMin < 8.0)
        tx_rate_min = 1.0;          // one byte/second absolute minimum
    else
        tx_rate_min = rateMin/8.0;  // convert to bytes/second
    if (rateMax < 0.0)
        tx_rate_max = -1.0;
    else
        tx_rate_max = rateMax/8.0;  // convert to bytes/second
    if (cc_enable)
    {
        double txRate = tx_rate;
        if ((tx_rate_min > 0.0) && (txRate < tx_rate_min))
            txRate = tx_rate_min;
        if ((tx_rate_max >= 0.0) && (txRate > tx_rate_max))
            txRate = tx_rate_max;
        if (txRate != tx_rate) SetTxRateInternal(txRate);
    }
}  // end NormSession::SetTxRateBounds()
        

bool NormSession::StartServer(UINT16        instanceId,
                              UINT32        bufferSpace,
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
    if (!tx_table.Init(tx_cache_count_max)) 
    {
        DMSG(0, "NormSession::StartServer() tx_table.Init() error!\n");
        StopServer();
        return false;   
    }
    if (!tx_pending_mask.Init(tx_cache_count_max, 0x0000ffff))
    {
        DMSG(0, "NormSession::StartServer() tx_pending_mask.Init() error!\n");
        StopServer();
        return false; 
    }
    if (!tx_repair_mask.Init(tx_cache_count_max, 0x0000ffff))
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
        if (NULL != encoder) delete encoder;
        if (NULL == (encoder = new NormEncoderRS8a))
        {
            DMSG(0, "NormSession::StartServer() new NormEncoderRS8a error: %s\n", GetErrorString());
            StopServer();
            return false;
        }
        
        if (!encoder->Init(numData, numParity, segmentSize + NormDataMsg::GetStreamPayloadHeaderLength()))
        {
            DMSG(0, "NormSession::StartServer() encoder init error\n");
            StopServer();
            return false;
        }    
    }
    
    instance_id = instanceId;
    segment_size = segmentSize;
    sent_accumulator = 0;
    nominal_packet_size = (double)segmentSize;
    data_active = false;
    ndata = numData;
    nparity = numParity;
    is_server = true;
    
    flush_count = (GetTxRobustFactor() < 0) ? 0 : (GetTxRobustFactor() + 1);
    
    if (cc_enable) 
    {
        double txRate;
        if(tx_rate_min > 0.0)
        {
            txRate = tx_rate_min;
        }
        else
        {
            // Don't let txRate below MIN(one segment per grtt, one segment per seconds)
            txRate = ((double)segment_size) / grtt_measured;
            if (txRate > ((double)(segment_size)))
                txRate = (double)(segment_size);
        }
        if ((tx_rate_max >= 0.0) && (tx_rate > tx_rate_max))
            txRate = tx_rate_max;
        //tx_rate = txRate;             // keep grtt at initial
        SetTxRateInternal(txRate);  // adjusts grtt_advertised as needed
    }
    else
    {
        SetTxRateInternal(tx_rate); // takes segment size into account, etc on server start
    }
    cc_slow_start = true;
    cc_active = false;
    
    grtt_age = 0.0;
    probe_pending = false;
    probe_data_check = false;
    if (probe_reset)
    {        
        probe_reset = false;
        OnProbeTimeout(probe_timer);
        ActivateTimer(probe_timer);
    }
    
    return true;
}  // end NormSession::StartServer()


void NormSession::StopServer()
{
    if (probe_timer.IsActive()) 
    {
        probe_timer.Deactivate();
        probe_reset = true;
    }
    if (repair_timer.IsActive())
    {
        repair_timer.Deactivate();
        tx_repair_pending = false;
    }
    if (NULL != encoder)
    {
        encoder->Destroy();
        delete encoder;
        encoder = NULL;
    }
    acking_node_tree.Destroy();
    cc_node_list.Destroy();
    // Iterate tx_table and release objects
    while (!tx_table.IsEmpty())
    {
        NormObject* obj = tx_table.Find(tx_table.RangeLo());
        ASSERT(obj);
        tx_table.Remove(obj);
        obj->Close();
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
    
    // (TBD) code to support app-defined commands will go here
    /*
    if (command_pending && !command_timer.IsActive())
    {
        SenderQueueAppCommand() xxx   
    }
    
    */
    

    if (watermark_pending && !flush_timer.IsActive())
    {
        // Determine next message (objectId::blockId::segmentId) to be sent
        NormObject* nextObj;
        NormObjectId nextObjectId = next_tx_object_id;
        NormBlockId nextBlockId = 0;
        NormSegmentId nextSegmentId = 0;
        if (obj)
        {
            // Get index (objectId::blockId::segmentId) of next transmit pending segment
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
                        UINT16 nextBlockSize = nextObj->GetBlockSize(nextBlockId);
                        if (nextSegmentId >= nextBlockSize) nextSegmentId = nextBlockSize - 1;
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
                ASSERT(nextObj->IsStream());
                nextBlockId = static_cast<NormStreamObject*>(nextObj)->GetNextBlockId();
                nextSegmentId = static_cast<NormStreamObject*>(nextObj)->GetNextSegmentId();  
            }           
        }
        
        if (tx_repair_pending)
        {
            
            if ((tx_repair_object_min < nextObjectId) ||
                ((tx_repair_object_min == nextObjectId) &&
                 ((tx_repair_block_min < nextBlockId) ||
                  ((tx_repair_block_min == nextBlockId) &&
                   (tx_repair_segment_min < nextSegmentId)))))
            {
                nextObjectId = tx_repair_object_min;
                nextBlockId = tx_repair_block_min;
                nextSegmentId = tx_repair_segment_min;
                
                DMSG(8, "watermark>%hu:%lu:%hu check against repair index>%hu:%lu:%hu\n",
                       (UINT16)watermark_object_id, (UINT32)watermark_block_id, (UINT16)watermark_segment_id, 
                       (UINT16)nextObjectId, (UINT32)nextBlockId, (UINT16)nextSegmentId);
            }
        }  // end if (tx_repair_pending)
        
        if ((nextObjectId > watermark_object_id) ||
            ((nextObjectId == watermark_object_id) &&
             ((nextBlockId > watermark_block_id) ||
              ((nextBlockId == watermark_block_id) &&
                (nextSegmentId > watermark_segment_id)))))
        {
            // The sender tx position is > watermark
            if (ServerQueueWatermarkFlush()) 
            {
                watermark_active = true;
                return;
            }
            else
            {
                // (TBD) optionally return here to have ack collection temporarily 
                // suspend forward progress of data transmission
                //return;
            }
        }
        else
        {
            // The sender tx position is < watermark
            // Reset non-acked acking nodes since server has rewound 
            if (watermark_active)
            {
                watermark_active = false;
                NormNodeTreeIterator iterator(acking_node_tree);
                NormAckingNode* next;
                while ((next = static_cast<NormAckingNode*>(iterator.GetNextNode())))
                {
                    next->ResetReqCount(GetTxRobustFactor());
                }
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
                if (cc_enable && !data_active)
                {
                    data_active = true;
                    if (probe_timer.IsActive())
                    {
                        double elapsed = probe_timer.GetInterval() - probe_timer.GetTimeRemaining();
                        const NormCCNode* clr = static_cast<const NormCCNode*>(cc_node_list.Head());
                        double probeInterval = (clr && clr->IsActive()) ? 
                                                    MIN(grtt_advertised, clr->GetRtt()) : 
                                                    grtt_advertised;
                        if (elapsed > probeInterval)  
                            probe_timer.SetInterval(0.0);
                        else
                            probe_timer.SetInterval(probeInterval - elapsed);
                        probe_timer.Reschedule(); 
                    }
                }
                msg->SetDestination(address);
                msg->SetGrtt(grtt_quantized);
                msg->SetBackoffFactor((unsigned char)backoff_factor);
                msg->SetGroupSize(gsize_quantized);
                QueueMessage(msg);
                flush_count = 0;
                // (TBD) ??? should streams every allowed to be non-pending?
                //       we _could_ re-architect streams a little bit and allow
                //       for this by having NormStreamObject::Write() control
                //       stream advancement ... I think it would be cleaner.
                //       (mod NormStreamObject::StreamAdvance() to depend upon
                //        what has been written and conversely set some pending
                //        state as calls to NormStreamObject::Write() are made.
                if (!obj->IsPending() && !obj->IsStream())
                    tx_pending_mask.Unset(obj->GetId());
            }
            else
            {
                ReturnMessageToPool(msg);
                if (obj->IsStream())
                {
                    NormStreamObject* stream = static_cast<NormStreamObject*>(obj);
                    if (stream->IsFlushPending() || stream->IsClosing())
                    {
                        // Queue flush message
                        if (!flush_timer.IsActive())
                        {
                            if ((GetTxRobustFactor() < 0) || (flush_count < GetTxRobustFactor()))
                            {
                                ServerQueueFlush();
                            }
                            else if (GetTxRobustFactor() == flush_count)
                            {
                                DMSG(6, "NormSession::Serve() node>%lu server flush complete ...\n",
                                         LocalNodeId());
                                flush_count++;
                                if (stream->IsClosing())
                                {
                                    stream->Close();
                                    Notify(NormController::TX_OBJECT_PURGED, (NormServerNode*)NULL, stream);
                                    DeleteTxObject(stream);   
                                    obj = NULL;
                                }
                            }
                        }
                    }
                    //ASSERT(stream->IsPending() || stream->IsRepairPending() || stream->IsClosing());
                    if (!posted_tx_queue_empty &&  !stream->IsClosing() && stream->IsPending())
                        // post if pending || !repair_timer.IsActive() || (repair_timer.GetRepeatCount() == 0) ???
                    {
                        //data_active = false;
                        posted_tx_queue_empty = true;
                        Notify(NormController::TX_QUEUE_EMPTY, (NormServerNode*)NULL, obj);
                        // (TBD) Was session deleted?
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
            data_active = false;  // (TBD) should we wait until the flush process completes before setting false???
            posted_tx_queue_empty = true;
            Notify(NormController::TX_QUEUE_EMPTY, (NormServerNode*)NULL, (NormObject*)NULL);
            // (TBD) Was session deleted?
            return;
        }   
        if ((GetTxRobustFactor() < 0) || (flush_count < GetTxRobustFactor()))
        {
            // Queue flush message
            if (!tx_repair_pending)  // don't queue flush if repair pending
                ServerQueueFlush();
            else
                DMSG(8, "NormSession::Serve() node>%lu NORM_CMD(FLUSH) deferred by pending repairs ...\n",
                        LocalNodeId());
        }   
        else if (GetTxRobustFactor() == flush_count)
        {
            DMSG(6, "NormSession::Serve() node>%lu server flush complete ...\n",
                    LocalNodeId());
            Notify(NormController::TX_FLUSH_COMPLETED,
                   (NormServerNode*)NULL,
                   (NormObject*)NULL);
            flush_count++;   
        }
    }
}  // end NormSession::Serve()

void NormSession::ServerSetWatermark(NormObjectId  objectId,
                                     NormBlockId   blockId,
                                     NormSegmentId segmentId)       
{
    watermark_pending = true;
    watermark_active = false;
    watermark_object_id = objectId;
    watermark_block_id = blockId;
    watermark_segment_id = segmentId;
    acking_success_count = 0;
    // Reset acking_node_list
    NormNodeTreeIterator iterator(acking_node_tree);
    NormNode* next;
    int robustFactor = GetTxRobustFactor();
    while ((next = iterator.GetNextNode()))
        static_cast<NormAckingNode*>(next)->Reset(robustFactor);
    PromptServer();
}  // end Norm::ServerSetWatermark()

void NormSession::ServerCancelWatermark()
{
    watermark_pending = false;
}  // end NormSession::ServerCancelWatermark()

bool NormSession::ServerAddAckingNode(NormNodeId nodeId)
{
    NormAckingNode* theNode = static_cast<NormAckingNode*>(acking_node_tree.FindNodeById(nodeId));
    if (NULL == theNode)
    {
        theNode = new NormAckingNode(*this, nodeId);
        if (NULL != theNode)
        {
            theNode->Reset(GetTxRobustFactor());
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
    NormAckingNode* theNode = 
        static_cast<NormAckingNode*>(acking_node_tree.FindNodeById(nodeId));
    if (theNode) 
    {
        acking_node_tree.DetachNode(theNode);
        acking_node_count--;
    }
}  // end NormSession::RemoveAckingNode()

NormSession::AckingStatus NormSession::ServerGetAckingStatus(NormNodeId nodeId)
{
    if (NORM_NODE_ANY == nodeId)
    {
        // Return result based on overall success of acking process
        if (watermark_pending)
        {
            return ACK_PENDING;
        }
        else
        {
            if (acking_success_count < acking_node_count)
                return ACK_FAILURE;
            else
                return ACK_SUCCESS;
        }
    }
    else
    {
        NormAckingNode* theNode = 
            static_cast<NormAckingNode*>(acking_node_tree.FindNodeById(nodeId));
        if (theNode)
        {
            if (theNode->IsPending())
                return ACK_PENDING;
            else if (NORM_NODE_NONE == theNode->GetId())
                return ACK_SUCCESS;
            else if (theNode->AckReceived())
                return ACK_SUCCESS;
            else
                return ACK_FAILURE;
        }
        else
        {
            return ACK_INVALID;
        }              
    }    
}  // end NormSession::ServerGetAckingStatus()

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
        // _Attempt_ to set the fec_payload_id source block length field appropriately
        UINT16 blockLen;
        NormObject* obj = tx_table.Find(watermark_object_id);
        if (NULL != obj)
            blockLen = obj->GetBlockSize(watermark_block_id);
        else if (watermark_segment_id < ndata)
            blockLen = ndata;
        else
            blockLen = watermark_segment_id;
        flush->SetFecBlockLen(blockLen);
        flush->SetFecSymbolId(watermark_segment_id);
        NormNodeTreeIterator iterator(acking_node_tree);
        NormAckingNode* next;
        watermark_pending = false;
        NormAckingNode* nodeNone = NULL;
        acking_success_count = 0;
        while ((next = static_cast<NormAckingNode*>(iterator.GetNextNode())))
        {
            // Save NORM_NODE_NONE for last
            if (NORM_NODE_NONE == next->GetId()) 
            {
                if (next->IsPending()) 
                    nodeNone = next;
                else
                    acking_success_count++; // implicit success for NORM_NODE_NONE
                continue;
            }
            if (next->AckReceived())
            {
                acking_success_count++;     // ACK was received for this node
            }
            else if (next->IsPending())
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
                    nodeNone = NULL;
                    break;    
                }                
            }
        }
        if (NULL != nodeNone)
        {
            if (flush->AppendAckingNode(NORM_NODE_NONE, segment_size))
            {
                nodeNone->DecrementReqCount();
                watermark_pending = true;
            }
            else
            {
                DMSG(8, "NormSession::ServeQueueWatermarkFlush() full cmd ...\n");
            }
        }
        if (watermark_pending)
        {
            if ((GetTxRobustFactor() < 0) || (flush_count < GetTxRobustFactor()))
                flush_count++;
            QueueMessage(flush);
            DMSG(8, "NormSession::ServeQueueWatermarkFlush() node>%lu cmd queued ...\n",
                    LocalNodeId());
        }
        else if (NULL != acking_node_tree.GetRoot())
        {
            ReturnMessageToPool(flush);
            DMSG(4, "NormSession::ServeQueueWatermarkFlush() node>%lu watermark ack finished.\n");
            Notify(NormController::TX_WATERMARK_COMPLETED, (NormServerNode*)NULL, (NormObject*)NULL);
            return false; 
        }
        else
        {
            ReturnMessageToPool(flush);
            DMSG(2, "NormSession::ServeQueueWatermarkFlush() node>%lu no acking nodes specified?!\n");
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
    // (TBD) Don't enqueue a new flush if there is already one in our tx_queue!
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
            flush->SetFecBlockLen(obj->GetBlockSize(blockId));
            flush->SetFecSymbolId(segmentId);
            QueueMessage(flush);
            if ((GetTxRobustFactor() < 0) || (flush_count < GetTxRobustFactor())) 
                flush_count++;
            DMSG(4, "NormSession::ServerQueueFlush() node>%lu, flush queued (flush_count:%u)...\n",
                    LocalNodeId(), flush_count);
        }
        else
        {
            DMSG(0, "NormSession::ServerQueueFlush() node>%lu message_pool exhausted! (couldn't flush)\n",
                    LocalNodeId()); 
        } 
        
    }
    else
    {
        // Why did I do this? - Brian // Because a squelch keeps the receivers from NACKing in futility
        // (TBD) send NORM_CMD(EOT) instead? - no
        // Perhaps I should send a flush anyway w/ (next_tx_object_id - 1) and squelch accordingly?
        // This condition shouldn't occur if we have state on the most recent object ... we should
        // unless the app does bad things like "cancel" all of its tx objects ...
        // Maybe we shouldn't send anything if we have no pending tx objects? No need to flush, etc
        // if all tx object state is gone ...
        if (ServerQueueSquelch(next_tx_object_id))
        {
            if ((GetTxRobustFactor() < 0) || (flush_count < GetTxRobustFactor())) 
                flush_count++;
            DMSG(4, "NormSession::ServerQueueFlush() node>%lu squelch queued (flush_count:%u)...\n",
                 LocalNodeId(), flush_count);
        }
        else
        {
            DMSG(0, "NormSession::ServerQueueFlush() warning: node>%lu unable to queue squelch\n",
                    LocalNodeId());  
        }
    }
    flush_timer.SetInterval(2*grtt_advertised); 
    ActivateTimer(flush_timer);
}  // end NormSession::ServerQueueFlush()

bool NormSession::OnFlushTimeout(ProtoTimer& /*theTimer*/)
{
    flush_timer.Deactivate();
    PromptServer();
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
        // (TBD) if (0.0 == tx_rate), should we just dump the
        // message rather than queueing it?
    if (!tx_timer.IsActive() && (tx_rate > 0.0))
    {
        tx_timer.SetInterval(0.0);
        ActivateTimer(tx_timer);   
    }
    if (msg) 
        message_queue.Append(msg);
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
       file->Release();
       return NULL; 
    }    
    if (QueueTxObject(file))
    {
        return file;
    }
    else
    {
        file->Close();
        file->Release();
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
       obj->Release();
       return NULL; 
    }    
    if (QueueTxObject(obj))
    {
        return obj;
    }
    else
    {
        obj->Close();
        obj->Release();
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
        stream->Release();
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
        stream->Release();
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
        simObject->Release();
        return NULL;
    }
    if (QueueTxObject(simObject))
    {
        return simObject;
    }
    else
    {
        simObject->Release();
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
                ((tx_table.GetSize() + obj->GetSize()) > tx_cache_size_max)))
        {
            // Remove oldest non-pending 
            NormObject* oldest = tx_table.Find(tx_table.RangeLo());
            if (oldest->IsRepairPending() || oldest->IsPending())
            {
                DMSG(0, "NormSession::QueueTxObject() all held objects repair pending\n");
                posted_tx_queue_empty = false;
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
    // Attempt to queue the object (note it gets "retained" by the tx_table)
    if (!tx_table.Insert(obj))
    {
        DMSG(0, "NormSession::QueueTxObject() tx_table insert error\n");
        ASSERT(0);
        return false;
    }
    tx_pending_mask.Set(obj->GetId());
    ASSERT(tx_pending_mask.Test(obj->GetId()));
    next_tx_object_id++;
    TouchServer();
    return true;
}  // end NormSession::QueueTxObject()

bool NormSession::RequeueTxObject(NormObject* obj)
{
    ASSERT(obj);
    if (obj->IsStream())
    {
        // (TBD) allow buffered stream to be reset?
        DMSG(0, "NormSession::RequeueTxObject() error: can't requeue NORM_OBJECT_STREAM\n");
        return false;   
    }
    NormObjectId objectId = obj->GetId();
    if (tx_table.Find(objectId) == obj)
    {
        if (tx_pending_mask.Set(objectId))
        {
            obj->TxReset(0, true);
            return true;
        }        
        else
        {
            DMSG(0, "NormSession::RequeueTxObject() error: couldn't set object as pending\n");
            return false;
        }
    }
    else
    {
        DMSG(0, "NormSession::RequeueTxObject() error: couldn't find object\n");
        return false;
    }
}  // end NormSession::RequeueTxObject()

void NormSession::DeleteTxObject(NormObject* obj)
{
    ASSERT(obj);
    if (tx_table.Remove(obj))
    {
        NormObjectId objectId = obj->GetId();
        tx_pending_mask.Unset(objectId);
        tx_repair_mask.Unset(objectId);
    }
    obj->Close();
    obj->Release();
}  // end NormSession::DeleteTxObject()

bool NormSession::SetTxCacheBounds(NormObjectSize  sizeMax,
                                   unsigned long   countMin,
                                   unsigned long   countMax)
{
    bool result = true;
    tx_cache_size_max = sizeMax;
    tx_cache_count_min = (countMin < countMax) ? countMin : countMax;
    tx_cache_count_max = (countMax > countMin) ? countMax : countMin;
    
    if (IsServer())
    {
        // Trim/resize the tx_table and tx masks as needed
        unsigned long count = tx_table.Count();
        while ((count >= tx_cache_count_min) &&
               ((count >= tx_cache_count_max) ||
                (tx_table.GetSize() > tx_cache_size_max)))
        {
            // Remove oldest (hopefully non-pending ) object
            NormObject* oldest = tx_table.Find(tx_table.RangeLo());
            ASSERT(oldest);
            Notify(NormController::TX_OBJECT_PURGED, (NormServerNode*)NULL, oldest);
            DeleteTxObject(oldest);
            count = tx_table.Count();
        }
        if (tx_cache_count_max < DEFAULT_TX_CACHE_MAX)
            countMax = DEFAULT_TX_CACHE_MAX;
        else
            countMax = tx_cache_count_max;
        if (countMax != tx_table.GetRangeMax())
        {
            tx_table.SetRangeMax((UINT16)countMax);
            result = tx_pending_mask.Resize(countMax);
            result &= tx_repair_mask.Resize(countMax);
            if (!result)
            {
                countMax = tx_pending_mask.GetSize();
                if (tx_repair_mask.GetSize() < (INT32)countMax)
                    countMax = tx_repair_mask.GetSize(); 
                if (tx_cache_count_max > countMax)
                    tx_cache_count_max = countMax;
                if (tx_cache_count_min > tx_cache_count_max)
                    tx_cache_count_min = tx_cache_count_max;
            }
        }
    }
    return result;
}  // end NormSession::SetTxCacheBounds()

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
        if (msg.InitFromBuffer(msgLength))
        {
            HandleReceiveMessage(msg, true);
            msgLength = NormMsg::MAX_SIZE;
        }
        else
        {
            DMSG(0, "NormSession::TxSocketRecvHandler() warning: received bad message\n");   
        }
    }
}  // end NormSession::TxSocketRecvHandler()


void NormSession::RxSocketRecvHandler(ProtoSocket&       /*theSocket*/,
                                      ProtoSocket::Event /*theEvent*/)
{
    unsigned int recvCount = 0;
    NormMsg msg;
    unsigned int msgLength = NormMsg::MAX_SIZE;
    while (rx_socket.RecvFrom(msg.AccessBuffer(),
                              msgLength, 
                              msg.AccessAddress()))
    {
        if (msg.InitFromBuffer(msgLength))
        {
            HandleReceiveMessage(msg, false);
            msgLength = NormMsg::MAX_SIZE;
        }
        else
        {
            DMSG(0, "NormSession::RxSocketRecvHandler() warning: received bad message\n");   
        }
        // If our system gets very busy reading sockets, we should occasionally
        // execute any timeouts to keep protocol operation smooth
        if (++recvCount >= 100)
        {
            break;
            //session_mgr.DoSystemTimeout();
            //recvCount = 0;
        }
    }
}  // end NormSession::RxSocketRecvHandler()

void NormTrace(const struct timeval&    currentTime, 
               NormNodeId               localId, 
               const NormMsg&           msg, 
               bool                     sent)
{
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
    DMSG(0, "trace>%02d:%02d:%02d.%06lu node>%lu %s>%s ",
            ct->tm_hour, ct->tm_min, ct->tm_sec, currentTime.tv_usec,
            (UINT32)localId, status, addr.GetHostString());
    bool clrFlag = false;
    switch (msgType)
    {
        case NormMsg::INFO:
        {
            const NormInfoMsg& info = (const NormInfoMsg&)msg;
            DMSG(0, "inst>%hu seq>%hu INFO obj>%hu ", 
                    info.GetInstanceId(), seq, (UINT16)info.GetObjectId());
            break;
        }
        case NormMsg::DATA:
        {
            const NormDataMsg& data = (const NormDataMsg&)msg;
            DMSG(0, "inst>%hu seq>%hu %s obj>%hu blk>%lu seg>%hu ", 
                    data.GetInstanceId(), 
                    seq, 
                    data.IsData() ? "DATA" : "PRTY",
                    (UINT16)data.GetObjectId(),
                    (UINT32)data.GetFecBlockId(),
                    (UINT16)data.GetFecSymbolId());
            if (data.IsData() && data.IsStream())
            {
                //if (NormDataMsg::StreamPayloadFlagIsSet(data.GetPayload(), NormDataMsg::FLAG_MSG_START))
                UINT16 msgStartOffset = NormDataMsg::ReadStreamPayloadMsgStart(data.GetPayload());
                if (0 != msgStartOffset)
                {
                    DMSG(0, "start word>%hu ", msgStartOffset - 1);
                }
                //if (NormDataMsg::StreamPayloadFlagIsSet(data.GetPayload(), NormDataMsg::FLAG_STREAM_END))
                if (0 == NormDataMsg::ReadStreamPayloadLength(data.GetPayload()))
                    DMSG(0, "(stream end) ");
            }
            break;
        }
        case NormMsg::CMD:
        {
            const NormCmdMsg& cmd = static_cast<const NormCmdMsg&>(msg);
            NormCmdMsg::Flavor flavor = cmd.GetFlavor();
            DMSG(0, "inst>%hu seq>%hu %s ", cmd.GetInstanceId(), seq, CMD_NAME[flavor]);
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
                    const NormCmdSquelchMsg& squelch = 
                        static_cast<const NormCmdSquelchMsg&>(msg);
                    DMSG(0, " obj>%hu blk>%lu seg>%hu ",
                            (UINT16)squelch.GetObjectId(),
                            (UINT32)squelch.GetFecBlockId(),
                            (UINT16)squelch.GetFecSymbolId());
                    break;
                }
                case NormCmdMsg::FLUSH:
                {
                    const NormCmdFlushMsg& flush = 
                        static_cast<const NormCmdFlushMsg&>(msg);
                    DMSG(0, " obj>%hu blk>%lu seg>%hu ",
                            (UINT16)flush.GetObjectId(),
                            (UINT32)flush.GetFecBlockId(),
                            (UINT16)flush.GetFecSymbolId());
                    break;
                }
                case NormCmdMsg::CC:
                {
                    const NormCmdCCMsg& cc = static_cast<const NormCmdCCMsg&>(msg);
                    DMSG(0, " seq>%u ", cc.GetCCSequence());
                    NormHeaderExtension ext;
                    while (cc.GetNextExtension(ext))
                    {
                        if (NormHeaderExtension::CC_RATE == ext.GetType())
                        {
                            UINT16 sendRate = ((NormCCRateExtension&)ext).GetSendRate();       
                            DMSG(0, " rate>%f ", 8.0e-03 * NormUnquantizeRate(sendRate));
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
    // Ignore messages from ourself unless "loopback" is enabled
    if ((msg.GetSourceId() == LocalNodeId()) && !loopback)
        return;
    // Drop some rx messages for testing
    if (UniformRand(100.0) < rx_loss_rate) 
        return;
    
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
        if (msg.GetInstanceId() != theServer->GetInstanceId())
        {
            DMSG(2, "NormSession::ClientHandleObjectMessage() node>%lu server>%lu instanceId change - resyncing.\n",
                         LocalNodeId(), theServer->GetId());
            theServer->Close();   
            if (!theServer->Open(msg.GetInstanceId()))
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
            Notify(NormController::REMOTE_SENDER_NEW, theServer, NULL);
            if (theServer->Open(msg.GetInstanceId()))
            {
                server_tree.AttachNode(theServer);
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
    theServer->Activate(true);
    theServer->SetAddress(msg.GetSource());
    theServer->UpdateRecvRate(currentTime, msg.GetLength());
    theServer->UpdateLossEstimate(currentTime, msg.GetSequence()); 
    theServer->IncrementRecvTotal(msg.GetLength()); // for statistics only (TBD) #ifdef NORM_DEBUG
    theServer->HandleObjectMessage(msg);
    
}  // end NormSession::ClientHandleObjectMessage()

void NormSession::ClientHandleCommand(const struct timeval& currentTime,
                                      const NormCmdMsg&     cmd)
{
    // Do common updates for servers we already know.
    NormNodeId sourceId = cmd.GetSourceId();
    NormServerNode* theServer = (NormServerNode*)server_tree.FindNodeById(sourceId);
    if (theServer)
    {
        if (cmd.GetInstanceId() != theServer->GetInstanceId())
        {
            DMSG(2, "NormSession::ClientHandleCommand() node>%lu server>%lu instanceId change - resyncing.\n",
                         LocalNodeId(), theServer->GetId());
            theServer->Close();   
            if (!theServer->Open(cmd.GetInstanceId()))
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
            Notify(NormController::REMOTE_SENDER_NEW, theServer, NULL);
            if (theServer->Open(cmd.GetInstanceId()))
            {
                server_tree.AttachNode(theServer);
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
    theServer->Activate(false);
    theServer->SetAddress(cmd.GetSource());
    theServer->UpdateRecvRate(currentTime, cmd.GetLength());
    theServer->UpdateLossEstimate(currentTime, cmd.GetSequence()); 
    theServer->IncrementRecvTotal(cmd.GetLength()); // for statistics only (TBD) #ifdef NORM_DEBUG
    theServer->HandleCommand(currentTime, cmd);    
}  // end NormSession::ClientHandleCommand()


double NormSession::CalculateRtt(const struct timeval& currentTime,
                                 const struct timeval& grttResponse)
{
    if (grttResponse.tv_sec || grttResponse.tv_usec)
    {
        double rcvrRtt;
        // Calculate rtt estimate for this receiver and process the response
        if (currentTime.tv_usec < grttResponse.tv_usec)
        {
            rcvrRtt = 
                (double)(currentTime.tv_sec - grttResponse.tv_sec - 1);
            rcvrRtt += 
                ((double)(1000000 - (grttResponse.tv_usec - currentTime.tv_usec))) / 1.0e06;
        }
        else
        {
            rcvrRtt = 
                (double)(currentTime.tv_sec - grttResponse.tv_sec);
            rcvrRtt += 
                ((double)(currentTime.tv_usec - grttResponse.tv_usec)) / 1.0e06;
        } 
        // Lower limit on RTT (because of coarse timer resolution on some systems,
        // this can sometimes actually end up a negative value!)
        // (TBD) this should be system clock granularity?
        return (rcvrRtt < 1.0e-06) ? 1.0e-06 : rcvrRtt;
    }
    else
    {
        return -1.0;
    }
}  // end NormSession::CalculateRtt()

void NormSession::ServerUpdateGrttEstimate(double clientRtt)
{
    grtt_response = true;
    if ((clientRtt > grtt_measured) || !address.IsMulticast())
    {
        // Immediately incorporate bigger RTT's
        grtt_decrease_delay_count = DEFAULT_GRTT_DECREASE_DELAY;
        grtt_measured = 0.25 * grtt_measured + 0.75 * clientRtt; 
        //grtt_measured = 0.9 * grtt_measured + 0.1 * clientRtt; 
        if (grtt_measured > grtt_max) grtt_measured = grtt_max;
        UINT8 grttQuantizedOld = grtt_quantized;
        double pktInterval =  ((double)(44+segment_size))/tx_rate;
        if (grtt_measured < pktInterval)
            grtt_quantized = NormQuantizeRtt(pktInterval);
        else
            grtt_quantized = NormQuantizeRtt(grtt_measured);
        // Calculate grtt_advertised since quantization rounds upward
        grtt_advertised = NormUnquantizeRtt(grtt_quantized);
        if (grtt_advertised > grtt_max)
        {
            grtt_quantized = NormQuantizeRtt(grtt_max);
            grtt_advertised = NormUnquantizeRtt(grtt_quantized);
        }
        grtt_current_peak = grtt_measured;
        if (grttQuantizedOld != grtt_quantized)
            DMSG(4, "NormSession::ServerUpdateGrttEstimate() node>%lu increased to new grtt>%lf sec\n",
                    LocalNodeId(), grtt_advertised);
    } 
    else if (clientRtt > grtt_current_peak) 
    {
        grtt_current_peak = clientRtt;
    }
}  // end NormSession::ServerUpdateGrttEstimate()

double NormSession::CalculateRate(double size, double rtt, double loss)
{
    double denom = rtt * (sqrt((2.0/3.0)*loss) + 
                   (12.0 * sqrt((3.0/8.0)*loss) * loss *
                    (1.0 + 32.0*loss*loss)));
    return (size / denom);    
}  // end NormSession::CalculateRate()

void NormSession::ServerHandleCCFeedback(struct timeval  currentTime,      
                                         NormNodeId      nodeId,           
                                         UINT8           ccFlags,          
                                         double          ccRtt,            
                                         double          ccLoss,           
                                         double          ccRate,           
                                         UINT16          ccSequence)       
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
    
    bool ccSlowStart = (0 != (ccFlags & NormCC::START));
    
    if (!ccSlowStart)
        ccRate = CalculateRate(nominal_packet_size, ccRtt, ccLoss); 
    
    //DMSG(0, "NormSession::ServerHandleCCFeedback() node>%lu rate>%lf (rtt>%lf loss>%lf slow_start>%d)\n",
    //        nodeId, ccRate * 8.0 / 1000.0, ccRtt, ccLoss, (0 != (ccFlags & NormCC::START)));
    
    // Keep the active CLR (if there is one) at the head of the list
    NormNodeListIterator iterator(cc_node_list);
    NormCCNode* next = (NormCCNode*)iterator.GetNextNode();
    // 1) Does this response replace the active CLR?
    if (next && next->IsActive())
    {
        if ((nodeId == next->GetId()) ||
            (ccRate < next->GetRate()) ||
            ((ccRate < (next->GetRate() * 1.1)) && (ccRtt > next->GetRtt())))  // use Rtt as tie-breaker if close
        {
            NormNodeId savedId = next->GetId();
            bool savedRttStatus = next->HasRtt();
            double savedRtt = next->GetRtt();
            double savedLoss = next->GetLoss();
            double savedRate = next->GetRate();
            UINT16 savedSequence = next->GetCCSequence();
            struct timeval savedTime = next->GetFeedbackTime();
            
            next->SetId(nodeId);
            next->SetClrStatus(true);
            next->SetRttStatus(0 != (ccFlags & NormCC::RTT));
            next->SetLoss(ccLoss);
            next->SetRate(ccRate);
            next->SetCCSequence(ccSequence);
            next->SetActive(true);
            next->SetFeedbackTime(currentTime);
            cc_slow_start = ccSlowStart;  // use CLR status for our slow_start state
            if (savedId == nodeId)
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
            currentTime = savedTime;
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
        //next->SetPlrStatus(false);
        next->SetRttStatus(0 != (ccFlags & NormCC::RTT));
        next->SetRtt(ccRtt);
        next->SetLoss(ccLoss);
        next->SetRate(ccRate);
        next->SetCCSequence(ccSequence);
        next->SetActive(true);
        next->SetFeedbackTime(currentTime);
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
            //candidate->SetPlrStatus(true);  // do this only 
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
            ServerHandleCCFeedback(currentTime,
                                   ack.GetSourceId(),
                                   ext.GetCCFlags(),
                                   clientRtt >= 0.0 ?  
                                        clientRtt : NormUnquantizeRtt(ext.GetCCRtt()),
                                   NormUnquantizeLoss(ext.GetCCLoss()),
                                   NormUnquantizeRate(ext.GetCCRate()),
                                   ext.GetCCSequence());
            if (wasUnicast && probe_proactive && Address().IsMulticast()) 
            {
                // for suppression of unicast cc feedback
                advertise_repairs = true;
                QueueMessage(NULL);
            }
            break;
        }
    }
    
    switch (ack.GetAckType())
    {
        case NormAck::CC:
            // Everything is in the ACK header or extension for this one
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
                        }
                        else
                        {
                            // This can happen when new watermarks are set when an old watermark is still
                            // pending.
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
            ServerHandleCCFeedback(currentTime,
                                   nack.GetSourceId(),
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
    
    // Get the index of our next pending NORM_DATA transmission
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
        {
            requestLevel = SEGMENT;
        }
        else if (req.FlagIsSet(NormRepairRequest::BLOCK))
        {
            requestLevel = BLOCK;
        }
        else if (req.FlagIsSet(NormRepairRequest::OBJECT))
        {
            requestLevel = OBJECT;
        }
        else if (req.FlagIsSet(NormRepairRequest::INFO))
        {
            requestLevel = INFO;
        }
        else
        {
            DMSG(0, "NormSession::ServerHandleNackMessage() node>%lu recvd repair request w/ invalid repair level\n",
                    LocalNodeId());
            continue;
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
                            // Update our minimum tx repair index as needed
                            if (tx_repair_pending)
                            {
                                if (nextObjectId <= tx_repair_object_min)
                                {
                                    tx_repair_object_min = nextObjectId;
                                    tx_repair_block_min = 0;
                                    tx_repair_segment_min = 0;
                                }
                            }
                            else
                            {
                                tx_repair_pending = true;
                                tx_repair_object_min = nextObjectId;
                                tx_repair_block_min = 0;
                                tx_repair_segment_min = 0;         
                            }
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
                            // Update our minimum tx repair index as needed
                            if (tx_repair_pending)
                            {
                                if (nextObjectId <= tx_repair_object_min)
                                {
                                    tx_repair_object_min = nextObjectId;
                                    tx_repair_block_min = 0;
                                    tx_repair_segment_min = 0;
                                }
                            }
                            else
                            {
                                tx_repair_pending = true;
                                tx_repair_object_min = nextObjectId;
                                tx_repair_block_min = 0;
                                tx_repair_segment_min = 0;         
                            }
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
                            // Update our minimum tx repair index as needed
                            if (tx_repair_pending)
                            {
                                if (nextObjectId < tx_repair_object_min)
                                {
                                    tx_repair_object_min = nextObjectId;
                                    tx_repair_block_min = nextBlockId;
                                    tx_repair_segment_min = 0;
                                }
                                else if (nextObjectId == tx_repair_object_min)
                                {
                                    if (nextBlockId <= tx_repair_block_min)
                                    {
                                        tx_repair_block_min = nextBlockId;
                                        tx_repair_segment_min = 0;
                                    }
                                }
                            }
                            else
                            {
                                tx_repair_pending = true;
                                tx_repair_object_min = nextObjectId;
                                tx_repair_block_min = nextBlockId;
                                tx_repair_segment_min = 0;
                            }
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
                            // Is this entire block already repair pending?
                            if (object->IsRepairSet(nextBlockId)) 
                                continue;
                            if (NULL == (block = object->FindBlock(nextBlockId)))
                            {
                                // Is this entire block already tx pending?
                                if (object->IsPendingSet(nextBlockId))
                                {
                                    // Entire block already tx pending, don't worry about individual segments
                                    DMSG(4, "NormSession::ServerHandleNackMessage() node>%lu "
                                            "recvd SEGMENT repair request for pending block.\n");
                                    continue;   
                                }
                                else
                                {
                                    // Try to recover block including parity calculation 
                                    if (NULL == (block = object->ServerRecoverBlock(nextBlockId)))
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
                                            // Resource constrained, move on to next repair request
                                            DMSG(2, "NormSession::ServerHandleNackMessage() node>%lu "
                                                    "Warning - server is resource constrained ...\n");
                                        }  
                                        continue;
                                    }
                                }
                            }
                            freshBlock = false;
                            numErasures = extra_parity;
                            prevBlockId = nextBlockId;
                        }  // end if (freshBlock)
                        ASSERT(NULL != block);
                        // If stream && explicit data repair, lock the data for retransmission
                        // (TBD) this use of "ndata" needs to be replaced for dynamically shortened blocks
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
                            // Update our minimum tx repair index as needed
                            ASSERT(nextBlockId == block->GetId());
                            UINT16 nextBlockSize = object->GetBlockSize(nextBlockId);
                            if (tx_repair_pending)
                            {
                                if (nextObjectId < tx_repair_object_min)
                                {
                                    tx_repair_block_min = nextBlockId;
                                    tx_repair_segment_min = (nextSegmentId < nextBlockSize) ?
                                                                nextSegmentId : (nextBlockSize - 1);
                                }
                                else if (nextObjectId == tx_repair_object_min)
                                {
                                    if (nextBlockId < tx_repair_block_min)
                                    {
                                        tx_repair_block_min = nextBlockId;
                                        tx_repair_segment_min = (nextSegmentId < nextBlockSize) ?
                                                                nextSegmentId : (nextBlockSize - 1);
                                    }
                                    else if (nextBlockId == tx_repair_block_min)
                                    {
                                        if (nextSegmentId < tx_repair_segment_min)
                                            tx_repair_segment_min = nextSegmentId;
                                    }
                                }
                            }
                            else
                            {
                                tx_repair_pending = true;
                                tx_repair_object_min = nextObjectId;
                                tx_repair_block_min = nextBlockId;
                                tx_repair_segment_min = (nextSegmentId < nextBlockSize) ?
                                                                nextSegmentId : (nextBlockSize - 1);
                            }
                            block->HandleSegmentRequest(nextSegmentId, lastSegmentId, 
                                                        nextBlockSize, nparity, 
                                                        numErasures);
                            startTimer = true;
                        }  // end if/else (holdoff)
                        break;
                    case INFO:
                        // We already dealt with INFO request above with respect to initiating repair
                        nextObjectId++;
                        if (nextObjectId > lastObjectId) inRange = false;
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
    // If a squelch is already queued, update it if (objectId < squelch->objectId)
    bool doEnqueue = true;
    NormCmdSquelchMsg* squelch = NULL;
    NormMsg* msg = message_pool.GetHead();
    while (NULL != msg)
    {
        if (NormMsg::CMD == msg->GetType())
        {
            if (NormCmdMsg::SQUELCH == static_cast<NormCmdMsg*>(msg)->GetFlavor())
            {
                squelch = static_cast<NormCmdSquelchMsg*>(msg);
                break;
            }
        }
        msg = msg->GetNext();
    }
    if (NULL != squelch)
    {
        if (objectId >= squelch->GetObjectId())
            return false; // no need to update squelch
        doEnqueue = false;
    }
    else
    {
        squelch = (NormCmdSquelchMsg*)GetMessageFromPool();
    }
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
            NormBlockId blockId = static_cast<NormStreamObject*>(obj)->StreamBufferLo();
            squelch->SetFecBlockId(blockId);
            squelch->SetFecBlockLen(obj->GetBlockSize(blockId));
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
               NormBlockId blockId;
               if (obj->IsStream())
                   blockId =static_cast<NormStreamObject*>(obj)->StreamBufferLo();
               else
                   blockId = NormBlockId(0);
               squelch->SetFecBlockId(blockId);
               squelch->SetFecBlockLen(obj->GetBlockSize(blockId));
               squelch->SetFecSymbolId(0); 
               nextId = obj->GetId() + 1;
            }
            else
            {
                // Squelch to point to future object
                squelch->SetObjectId(next_tx_object_id);
                squelch->SetFecBlockId(0);
                squelch->SetFecBlockLen(0);  // (TBD) should this be "ndata" instead? but we can't be sure
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
        if (doEnqueue)
        {
            QueueMessage(squelch);
            DMSG(4, "NormSession::ServerQueueSquelch() node>%lu server queued squelch ...\n",
                    LocalNodeId());
        }
        else
        {
            DMSG(4, "NormSession::ServerQueueSquelch() node>%lu server updated squelch ...\n",
                    LocalNodeId());
        }
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
                        // (TBD) set NORM_REPAIR_ADV_LIMIT flag in this case
                        prevForm = NormRepairRequest::INVALID;
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
                if (0 == cmd.PackRepairRequest(req))
                {
                    DMSG(0, "NormSession::ServerBuildRepairAdv() warning: full msg\n");
                    // (TBD) set NORM_REPAIR_ADV_LIMIT flag in this case
                    prevForm = NormRepairRequest::INVALID;
                    break;
                }
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
        // (TBD) set NORM_REPAIR_ADV_LIMIT flag in this case
    }
    return true;
}  // end NormSession::ServerBuildRepairAdv()

bool NormSession::OnRepairTimeout(ProtoTimer& /*theTimer*/)
{
    tx_repair_pending = false;
    if (0 != repair_timer.GetRepeatCount())
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
                    DMSG(0, "NormSession::OnRepairTimeout() tx_pending_mask.Set(%hu) error (1)\n",
                            (UINT16)objectId);
            }  
            else
            {
                DMSG(6, "NormSession::OnRepairTimeout() node>%lu activating obj>%hu repairs ...\n",
                        LocalNodeId(), (UINT16)objectId);
                if (obj->ActivateRepairs()) 
                {
                    DMSG(6, "NormSession::OnRepairTimeout() node>%lu activated obj>%hu repairs ...\n",
                            LocalNodeId(), (UINT16)objectId);
                    if (!tx_pending_mask.Set(objectId))
                        DMSG(0, "NormSession::OnRepairTimeout() tx_pending_mask.Set(%hu) error (2)\n",
                                (UINT16)objectId); 
                } 
            } 
        }  // end while (iterator.GetNextObject())   
        PromptServer();
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
    
    // Note: sometimes need RepairAdv even when cc_enable is false ...                        
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
            if (tx_rate > 0.0)
                tx_timer.SetInterval(msg->GetLength() / tx_rate);
            if (advertise_repairs)
                advertise_repairs = false;
            else
                ReturnMessageToPool(msg);
            // Pre-serve to allow pre-prompt for empty tx queue
            if (message_queue.IsEmpty() && IsServer()) Serve();
        }
        else
        {
            // (TBD) should we check the type of error that occurred
            //       and take some smarter action here (e.g. re-open our sockets?)
            // Requeue the message for another try
            if (!advertise_repairs) 
                message_queue.Prepend(msg); 
            // Make sure the tx_timer interval is non-ZERO
            // (this avoids a sort of infinite loop that can occur
            //  under certain conditions)
            if (tx_rate > 0.0)
                tx_timer.SetInterval(msg->GetLength() / tx_rate);
            else if (0.0 == tx_timer.GetInterval())
                tx_timer.SetInterval(0.1);
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
    bool isClientMsg = false;
    bool isProbe = false;
    
    // Fill in any last minute timestamps
    // (TBD) fill in InstanceId fields on all messages as needed
    switch (msg.GetType())
    {
        case NormMsg::INFO:
        case NormMsg::DATA:
            ((NormObjectMsg&)msg).SetInstanceId(instance_id);
            msg.SetSequence(tx_sequence++);  // (TBD) set for session dst msgs
            break;
        case NormMsg::CMD:
            ((NormCmdMsg&)msg).SetInstanceId(instance_id);
            switch (((NormCmdMsg&)msg).GetFlavor())
            {
                case NormCmdMsg::CC:
                {
                    struct timeval currentTime;
                    ProtoSystemTime(currentTime); 
                    ((NormCmdCCMsg&)msg).SetSendTime(currentTime); 
                    isProbe = true; 
                    break;
                }
                case NormCmdMsg::SQUELCH:
                    break;
                default:
                    break;
            }
            msg.SetSequence(tx_sequence++);  // (TBD) set for session dst msgs
            break;

        case NormMsg::NACK:
        {
            isClientMsg = true;
            NormNackMsg& nack = (NormNackMsg&)msg;
            NormServerNode* theServer = 
                (NormServerNode*)server_tree.FindNodeById(nack.GetServerId());
            ASSERT(theServer);
            struct timeval currentTime;
            ProtoSystemTime(currentTime); 
            struct timeval grttResponse;
            theServer->CalculateGrttResponse(currentTime, grttResponse);
            nack.SetGrttResponse(grttResponse);
            break;
        }

        case NormMsg::ACK:
        {
            isClientMsg = true;
            NormAckMsg& ack = (NormAckMsg&)msg;
            NormServerNode* theServer = 
                (NormServerNode*)server_tree.FindNodeById(ack.GetServerId());
            ASSERT(theServer);
            struct timeval grttResponse;
            struct timeval currentTime;
            ProtoSystemTime(currentTime); 
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
    // Possibly drop some tx messages for testing purposes
    bool drop = (UniformRand(100.0) < tx_loss_rate);
    if (drop || (isClientMsg && client_silent))
    {
        //DMSG(0, "TX MESSAGE DROPPED! (tx_loss_rate:%lf\n", tx_loss_rate); 
        if (!(isClientMsg && client_silent))
        {
            // Update sent rate tracker even if dropped (for testing/debugging)
            sent_accumulator += msgSize;
            nominal_packet_size += 0.05 * (((double)msgSize) - nominal_packet_size);    
        }
    }    
    else
    {
        if (tx_socket->SendTo(msg.GetBuffer(), msgSize, msg.GetDestination()))
        {
            // Separate send/recv tracing
            if (trace) 
            {
                struct timeval currentTime;
                ProtoSystemTime(currentTime); 
                NormTrace(currentTime, LocalNodeId(), msg, true);  
            }
            // To keep track of _actual_ sent rate 
            sent_accumulator += msgSize;
            // Update nominal packet size
            nominal_packet_size += 0.05 * (((double)msgSize) - nominal_packet_size);    
        }
        else
        {
            DMSG(8, "NormSession::SendMessage() sendto() error: %s\n", GetErrorString());
            tx_sequence--;
            return false;
        }
    }
    if (isProbe)
    {
        probe_pending = false;
        probe_data_check = true;
        if (probe_reset) 
        {
            probe_reset = false;
            if (!probe_timer.IsActive())
                ActivateTimer(probe_timer);  
        }
    }
    else if (!isClientMsg)
    {
        probe_data_check = false;
        if (!probe_pending && probe_reset)
        {
            probe_reset = false;
            OnProbeTimeout(probe_timer);
            if (!probe_timer.IsActive())
                ActivateTimer(probe_timer);
        }
    }
    return true;
}  // end NormSession::SendMessage()

void NormSession::SetGrttProbingInterval(double intervalMin, double intervalMax)
{
    if ((intervalMin < 0.0) || (intervalMax < 0.0)) return;
    double temp = intervalMin;
    if (temp > intervalMax)
    {
        intervalMin = intervalMax;
        intervalMax = temp;
    }
    if (intervalMin < NORM_TICK_MIN) intervalMin = NORM_TICK_MIN;
    if (intervalMax < NORM_TICK_MIN) intervalMax = NORM_TICK_MIN;
    grtt_interval_min = intervalMin;
    grtt_interval_max = intervalMax;
    if (grtt_interval < grtt_interval_min)
        grtt_interval = grtt_interval_min;
    if (grtt_interval > grtt_interval_max)
    {
        grtt_interval = grtt_interval_max;
        if (probe_timer.IsActive() && !cc_enable)
        {
            double elapsed = probe_timer.GetInterval() - probe_timer.GetTimeRemaining();
            if (elapsed < 0.0) elapsed = 0.0;  
            if (elapsed > grtt_interval)
                probe_timer.SetInterval(0.0);
            else 
                probe_timer.SetInterval(grtt_interval - elapsed);
            probe_timer.Reschedule();             
        }           
    }    
}  // end NormSession::SetGrttProbingInterval()

void NormSession::SetGrttProbingMode(ProbingMode probingMode)
{
    if (cc_enable) return;  // can't change probing mode when cc is enabled!
                            // (cc _requires_ probing mode == PROBE_ACTIVE)
    switch (probingMode)
    {
        case PROBE_NONE:
            probe_reset = false;
            if (probe_timer.IsActive())
                probe_timer.Deactivate();
            break;
        case PROBE_PASSIVE:
            probe_proactive = false;
            if (IsServer())
            {
                if (!probe_timer.IsActive())
                {
                    probe_timer.SetInterval(0.0);
                    ActivateTimer(probe_timer);      
                }
            }
            else
            {
                probe_reset = true;
            }
            break;
        case PROBE_ACTIVE:
            probe_proactive = true;
            if (IsServer())
            {
                if (!probe_timer.IsActive())
                {
                    probe_timer.SetInterval(0.0);
                    ActivateTimer(probe_timer);      
                }
            }
            else
            {
                probe_reset = true;
            }
            break;   
    }
}  // end NormSession::SetGrttProbingMode()


bool NormSession::OnProbeTimeout(ProtoTimer& /*theTimer*/)
{
    // 1) Temporarily kill probe_timer if CMD(CC) not yet tx'd
    //    (or if data has not been sent since last probe)
    if (probe_pending || (data_active && probe_data_check))
    {
        probe_reset = true;
        if (probe_timer.IsActive())
            probe_timer.Deactivate();
        return false;
    } 
    else if (0.0 == tx_rate)
    {
        // Sender paused, so just skip probing until transmission is resumed
        return true;
    }
    
    // 2) Update grtt_estimate _if_ sufficient time elapsed.
    // This new code allows more liberal downward adjustment of
    // of grtt when congestion control is enabled. 

    // We have to keep track of the _actual_ deltaTime instead
    // of relying on the probe_timer interval because in real-
    // world operating systems, they're aren't the same and
    // sometimes not even close.
    struct timeval currentTime;
    ProtoSystemTime(currentTime);
    if ((0 == probe_time_last.tv_sec) && (0 == probe_time_last.tv_usec))
    {
        grtt_age += probe_timer.GetInterval();
    }
    else
    {
        double deltaTime = currentTime.tv_sec - probe_time_last.tv_sec;
        if (currentTime.tv_usec > probe_time_last.tv_usec)
            deltaTime += 1.0e-06*((double)(currentTime.tv_usec - probe_time_last.tv_usec));
        else
            deltaTime -= 1.0e-06*((double)(probe_time_last.tv_usec - currentTime.tv_usec));
        grtt_age += deltaTime;
    }
    probe_time_last = currentTime;

    // (TBD) We need to revisit the whole set of issues surrounding dynamic
    // estimation of grtt, particularly when congestion control is involved.
    // The main issue is when the rate increases rapidly with respect to
    // how the grtt estimate is descreasing ... this is most notable at
    // startup and thus the hack here to allow the grtt estimate to more
    // rapidly decrease during "slow start"
    double ageMax = grtt_advertised; 
    if (!cc_enable && !cc_slow_start)   
        ageMax = ageMax > grtt_interval_min ? ageMax : grtt_interval_min;
    if (grtt_age >= ageMax)
    {
        if (grtt_response)
        {
            // Update grtt estimate 
            if (grtt_current_peak < grtt_measured)
            {
                grtt_measured *= 0.9;
                if (grtt_current_peak > grtt_measured)
                    grtt_measured = grtt_current_peak;
                // (TBD) "grtt_decrease_delay_count" isn't needed any more ...
                /*if (grtt_decrease_delay_count-- == 0)
                {
                    grtt_measured = 0.5 * grtt_measured + 
                                    0.5 * grtt_current_peak;
                    grtt_current_peak = 0.0;
                    grtt_decrease_delay_count = DEFAULT_GRTT_DECREASE_DELAY;
                }*/
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
            UINT8 grttQuantizedOld = grtt_quantized;
            double pktInterval = (double)(44+segment_size)/tx_rate;
            if (grtt_measured < pktInterval)
                grtt_quantized = NormQuantizeRtt(pktInterval);
            else
                grtt_quantized = NormQuantizeRtt(grtt_measured);        
            // Recalculate grtt_advertise since quantization rounds upward
            grtt_advertised = NormUnquantizeRtt(grtt_quantized);
            if (grtt_advertised > grtt_max)
            {
                grtt_quantized = NormQuantizeRtt(grtt_max);
                grtt_advertised = NormUnquantizeRtt(grtt_quantized);
            }
            if (grttQuantizedOld != grtt_quantized)
                DMSG(4, "NormSession::OnProbeTimeout() node>%lu decreased to new grtt to: %lf sec\n",
                        LocalNodeId(), grtt_advertised);
            grtt_response = false;  // reset
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
    // defer SetSendTime() to when message is being sent (in OnTxTimeout())
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
                if (next->IsClr())
                { 
                    ccFlags |= (UINT8)NormCC::CLR;
                }
                else if (next->IsPlr())
                {
                    ccFlags |= (UINT8)NormCC::PLR;
                }
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
                //if (!next->IsClr()) next->SetActive(false);
                // "Deactivate" any nodes who have stopped providing feedback
                struct timeval feedbackTime = next->GetFeedbackTime();
                double feedbackAge = currentTime.tv_sec - feedbackTime.tv_sec;
                feedbackAge += 1.0e-06 * ((double)((currentTime.tv_usec - feedbackTime.tv_usec)));
                
                /*if (currentTime.tv_usec > feedbackTime.tv_usec)
                    feedbackAge += 1.0e-06*((double)(currentTime.tv_usec - feedbackTime.tv_usec));
                else
                    feedbackAge -= 1.0e-06*((double)(feedbackTime.tv_usec - currentTime.tv_usec));*/
                double maxFeedbackAge = 5 * MAX(grtt_advertised, next->GetRtt());
                // Safety bound to compensate for computer clock coarseness
                // and possible sluggish feedback from slower machines
                // at higher norm data rates (keeps rate from being 
                // prematurely reduced)
                if (maxFeedbackAge <(10*NORM_TICK_MIN)) maxFeedbackAge = (10*NORM_TICK_MIN); 
                unsigned int ccSeqDelta = cc_sequence - next->GetCCSequence() - 2; 
                if ((feedbackAge > maxFeedbackAge) && (ccSeqDelta > 5))
                {
                    DMSG(4, "Deactivating cc node feedbackAge:%lf sec maxAge:%lf sec ccSeqDelta:%u\n",
                            feedbackAge, maxFeedbackAge, ccSeqDelta);
                    next->SetActive(false);
                }
            }             
        }
       
        AdjustRate(false);
        
        // Determine next probe_interval
        if (data_active)
        {
            const NormCCNode* clr = static_cast<const NormCCNode*>(cc_node_list.Head());
            probeInterval = (clr && clr->IsActive()) ? MIN(grtt_advertised, clr->GetRtt()) : grtt_advertised;
        }
        else
        {
            probeInterval = grtt_interval;
        }
    }
    else
    {
        // Determine next probe_interval
        probeInterval = grtt_interval;
    }
    /*// perhaps this instead of the commented out probe_reset case???
    double nominalInterval = ((double)segment_size)/((double)tx_rate);
    if (nominalInterval > grtt_max) nominalInterval = grtt_max;
    if (nominalInterval > probeInterval) probeInterval = nominalInterval; */
        
    // Set probe_timer interval for next probe
    probe_timer.SetInterval(probeInterval);
    
    QueueMessage(cmd);  
    probe_pending = true; 
    
    return true;
}  // end NormSession::OnProbeTimeout()


void NormSession::AdjustRate(bool onResponse)
{
    const NormCCNode* clr = (const NormCCNode*)cc_node_list.Head();
    double ccRtt = clr ? clr->GetRtt() : grtt_measured;
    double ccLoss = clr ? clr->GetLoss() : 0.0;
    double txRate = tx_rate;
    if (onResponse)
    {
        if (!cc_active)
        {
            cc_active = true;
            Notify(NormController::CC_ACTIVE, NULL, NULL);
        }
        if (data_active)  // adjust only if actively transmitting
        {
            // Adjust rate based on CLR feedback and
            // adjust probe schedule
            ASSERT(clr);
            // (TBD) check feedback age
            if (cc_slow_start)
            {
                txRate = clr->GetRate();
                if (GetDebugLevel() >= 6)
                {
                    double sentRate = 8.0e-03*((double)sent_accumulator) / (report_timer.GetInterval() - report_timer.GetTimeRemaining());
                    DMSG(6, "NormSession::AdjustRate(slow start) clr>%lu newRate>%lf (oldRate>%lf sentRate>%lf clrRate>%lf\n",
                            clr->GetId(), 8.0e-03*txRate,  8.0e-03*tx_rate, sentRate, 8.0e-03*clr->GetRate());
                }                          
            }
            else
            {
                double clrRate = clr->GetRate();
                if (clrRate > txRate)
                {
                    double linRate = txRate + segment_size;
                    txRate = MIN(clrRate, linRate);
                }
                else
                {
                    txRate = clrRate;
                }  
                DMSG(6, "NormSession::AdjustRate(stdy state) clr>%lu newRate>%lf (rtt>%lf loss>%lf)\n",
                        clr->GetId(), 8.0e-03*txRate, clr->GetRtt(), clr->GetLoss());
            }
            // Adjust the probe timeout right away 
            /* double probeInterval = probe_timer.GetInterval();
            if (probeInterval > ccRtt)
            {
                double elapsed = probeInterval - probe_timer.GetTimeRemaining();
                probeInterval = (ccRtt > elapsed) ? (ccRtt - elapsed) : 0.0;
                probe_timer.SetInterval(probeInterval);
                if (probe_timer.IsActive()) probe_timer.Reschedule(); 
            } */
        }
    }
    else if (!data_active)
    {
        // reduce rate if no active data transmission
        // (TBD) Perhaps we want to be less aggressive here someday
        txRate *= 0.5;
    }
    else if (clr && clr->IsActive())
    {
        // (TBD) fix CC feedback aging ...
        /*int feedbackAge  = abs((int)cc_sequence - (int)clr->GetCCSequence());
        DMSG(0, "NormSession::AdjustRate() feedback age>%d (%d - %d\n", 
                feedbackAge, cc_sequence, clr->GetCCSequence());
        
        if (feedbackAge > 50)
        {
            double linRate = txRate - segment_size;
            linRate = MAX(linRate, 0.0);
            double expRate = txRate * 0.5;
            if (feedbackAge > 4)
                txRate = MIN(linRate, expRate);
            else
                txRate = MAX(linRate, expRate);
            
        }*/
    }
    else
    {
        // reduce rate if no active clr
        txRate *= 0.5;
    }
   
    
    // Keep "tx_rate" within default or user set rate bounds (if any)
    double minRate;
    if(tx_rate_min > 0.0)
    {
        minRate = tx_rate_min;
    }
    else
    {
        // Don't let txRate below MIN(one segment per grtt, one segment per second)
        minRate = ((double)segment_size) / grtt_measured;
        if (minRate > ((double)(segment_size)))
            minRate = (double)(segment_size);
    }
    if (txRate <= minRate) 
    {
        txRate = minRate;
        if ((NULL == clr) || (!clr->IsActive()))
        {
            // Post notification that no cc feedback is being received
            if (cc_active)
            {
                cc_active = false;
                Notify(NormController::CC_INACTIVE, NULL, NULL);
            }
        }
    }
    if ((tx_rate_max >= 0.0) && (txRate > tx_rate_max))
        txRate = tx_rate_max;
    if (txRate != tx_rate) SetTxRateInternal(txRate);
      
    struct timeval currentTime;
    ::ProtoSystemTime(currentTime);
    double theTime = (double)currentTime.tv_sec + 1.0e-06 * ((double)currentTime.tv_usec);
    DMSG(8, "ServerRateTracking time>%lf rate>%lf rtt>%lf loss>%lf\n\n", theTime, 8.0e-03*tx_rate, ccRtt, ccLoss);
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
        double sentRate = 8.0e-03*((double)sent_accumulator) / report_timer.GetInterval();  // kbps
        sent_accumulator = 0;
        DMSG(2, "   txRate>%9.3lf kbps sentRate>%9.3lf grtt>%lf\n", 
                8.0e-03*tx_rate, sentRate, grtt_advertised);
        if (cc_enable)
        {
            const NormCCNode* clr = (const NormCCNode*)cc_node_list.Head(); 
            if (clr)  
                DMSG(2, "   clr>%lu rate>%9.3lf rtt>%lf loss>%lf %s\n", clr->GetId(),
                     8.0e-03*clr->GetRate(), clr->GetRtt(), clr->GetLoss(), cc_slow_start ? "(slow_start)" : "");
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
            DMSG(2, "   fecBufferUsage> current>%lu peak>%lu (overuns>%lu)\n", next->CurrentBufferUsage(),
                                                                            next->PeakBufferUsage(), 
                                                                            next->BufferOverunCount());
            DMSG(2, "   resyncs>%lu nacks>%lu suppressed>%lu\n", 
                     next->ResyncCount() ? next->ResyncCount() - 1 : 0,  // "ResyncCount()" is reall "SyncCount()"
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
	theSession->SetTxPort(sessionPort); /* JPH 4/24/06 */
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

