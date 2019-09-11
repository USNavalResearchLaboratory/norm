#include "normSession.h"
#include <errno.h>
#include <time.h>  // for gmtime() in NormTrace()


const double NormSession::DEFAULT_TRANSMIT_RATE = 64000.0; // bits/sec
const double NormSession::DEFAULT_PROBE_MIN = 1.0;        // sec
const double NormSession::DEFAULT_PROBE_MAX = 10.0;       // sec
const double NormSession::DEFAULT_GRTT_ESTIMATE = 0.5;    // sec
const double NormSession::DEFAULT_GRTT_MAX = 10.0;        // sec
const unsigned int NormSession::DEFAULT_GRTT_DECREASE_DELAY = 3;
const double NormSession::DEFAULT_BACKOFF_FACTOR = 4.0;   
const double NormSession::DEFAULT_GSIZE_ESTIMATE = 1000.0;  

NormSession::NormSession(NormSessionMgr& sessionMgr, NormNodeId localNodeId) 
 : session_mgr(sessionMgr), notify_pending(false), local_node_id(localNodeId), 
   tx_rate(DEFAULT_TRANSMIT_RATE/8.0), backoff_factor(DEFAULT_BACKOFF_FACTOR),
   is_server(false), 
   tx_cache_count_min(1), tx_cache_count_max(256),
   tx_cache_size_max((unsigned long)20*1024*1024),
   flush_count(NORM_ROBUST_FACTOR+1),
   posted_tx_queue_empty(false),
   probe_interval(0.0), probe_interval_min(DEFAULT_PROBE_MIN),
   probe_interval_max(DEFAULT_PROBE_MAX),
   grtt_max(DEFAULT_GRTT_MAX), 
   grtt_decrease_delay_count(DEFAULT_GRTT_DECREASE_DELAY),
   grtt_response(false), grtt_current_peak(0.0),
   is_client(false),
   trace(false), tx_loss_rate(0.0), rx_loss_rate(0.0),
   next(NULL)
{
    tx_socket.Init((UdpSocketOwner*) this, 
                   (UdpSocketRecvHandler)&NormSession::TxSocketRecvHandler,
                   sessionMgr.SocketInstaller(),
                   sessionMgr.SocketInstallData());
    rx_socket.Init((UdpSocketOwner*) this, 
                   (UdpSocketRecvHandler)&NormSession::RxSocketRecvHandler,
                   sessionMgr.SocketInstaller(),
                   sessionMgr.SocketInstallData());
    
    tx_timer.Init(0.0, -1, 
                  (ProtocolTimerOwner *)this, 
                  (ProtocolTimeoutFunc)&NormSession::OnTxTimeout);
    repair_timer.Init(0.0, 1, (ProtocolTimerOwner*)this, 
                      (ProtocolTimeoutFunc)&NormSession::OnRepairTimeout);
    flush_timer.Init(0.0, 0, (ProtocolTimerOwner*)this, 
                       (ProtocolTimeoutFunc)&NormSession::OnCommandTimeout);
    probe_timer.Init(0.0, -1, (ProtocolTimerOwner*)this, 
                     (ProtocolTimeoutFunc)&NormSession::OnProbeTimeout);
    grtt_quantized = NormQuantizeRtt(DEFAULT_GRTT_ESTIMATE);
    grtt_measured = grtt_advertised = NormUnquantizeRtt(grtt_quantized);
    
    gsize_measured = DEFAULT_GSIZE_ESTIMATE;
    gsize_quantized = NormQuantizeGroupSize(DEFAULT_GSIZE_ESTIMATE);
    gsize_advertised = NormUnquantizeGroupSize(gsize_quantized);
    gsize_nack_ave = (1.2 / (2.0*DEFAULT_BACKOFF_FACTOR)) * 
                     (log(DEFAULT_GSIZE_ESTIMATE) + 1.0);
    gsize_nack_ave = exp(gsize_nack_ave);
    gsize_correction_factor = 1.0;
    gsize_nack_delta = 0.0;
    
    // This timer is for printing out occasional status reports
    // (It may be used to trigger transmission of report messages
    //  in the future for debugging, etc
    report_timer.Init(30.0, -1,  (ProtocolTimerOwner*)this, 
                     (ProtocolTimeoutFunc)&NormSession::OnReportTimeout);
}

NormSession::~NormSession()
{
    Close();
}



bool NormSession::Open()
{
    ASSERT(address.IsValid());
    if (!tx_socket.IsOpen())
    {   
        if (UDP_SOCKET_ERROR_NONE != tx_socket.Open())
        {
            DMSG(0, "NormSession::Open() tx_socket open error\n");
            return false;   
        }
    }
    if (!rx_socket.IsOpen())
    {
        if (UDP_SOCKET_ERROR_NONE != rx_socket.Open(address.Port()))
        {
            DMSG(0, "NormSession::Open() rx_socket open error\n");
            Close();
            return false;   
        }
        if (address.IsMulticast())
        {
            if (UDP_SOCKET_ERROR_NONE != rx_socket.JoinGroup(&address, ttl)) 
            {
                DMSG(0, "NormSession::Open() rx_socket join group error\n");
                Close();
                return false;
            }   
        }
    }
    
    for (unsigned int i = 0; i < DEFAULT_MESSAGE_POOL_DEPTH; i++)
    {
        NormMessage* msg = new NormMessage();
        if (msg)
        {
            message_pool.Append(msg);
        }
        else
        {
            DMSG(0, "NormSession::Open() new message error: %s\n", strerror(errno));
            Close();
            return false;
        }   
    }
    InstallTimer(&report_timer);
    
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
    if (tx_socket.IsOpen()) tx_socket.Close();
    if (rx_socket.IsOpen()) rx_socket.Close();
}  // end NormSession::Close()


bool NormSession::StartServer(unsigned long bufferSpace,
                              UINT16        segmentSize,
                              UINT16        numData, 
                              UINT16        numParity)
{
    if (!IsOpen())
    {
        if (!Open()) return false;
    }
    // (TBD) parameterize the object history depth
    if (!tx_table.Init(256))
    {
        DMSG(0, "NormSession::StartServer() tx_table.Init() error!\n");
        StopServer();
        return false;   
    }
    if (!tx_pending_mask.Init(256))
    {
        DMSG(0, "NormSession::StartServer() tx_pending_mask.Init() error!\n");
        StopServer();
        return false; 
    }
    if (!tx_repair_mask.Init(256))
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
                               numParity * (segmentSize + NormDataMsg::PayloadHeaderLen());
    
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
    
    if (!segment_pool.Init(numSegments, segmentSize + NormDataMsg::PayloadHeaderLen()))
    {
        DMSG(0, "NormSession::StartServer() segment_pool init error\n");
        StopServer();
        return false;
    }
    
    if (numParity)
    {
        if (!encoder.Init(numParity, segmentSize + NormDataMsg::PayloadHeaderLen()))
        {
            DMSG(0, "NormSession::StartServer() encoder init error\n");
            StopServer();
            return false;
        }    
    }
    
    segment_size = segmentSize;
    ndata = numData;
    nparity = numParity;
    is_server = true;
    flush_count = NORM_ROBUST_FACTOR+1;  // (TBD) parameterize robust_factor
    probe_timer.SetInterval(0.0);
    InstallTimer(&probe_timer);
    return true;
}  // end NormSession::StartServer()

void NormSession::StopServer()
{
    if (probe_timer.IsActive()) probe_timer.Deactivate();
    encoder.Destroy();
    tx_table.Destroy();
    block_pool.Destroy();
    segment_pool.Destroy();
    tx_repair_mask.Destroy();
    tx_pending_mask.Destroy();
    tx_table.Destroy();
    is_server = false;
    if (!IsClient()) Close();
}   // end NormSession::StopServer()

bool NormSession::StartClient(unsigned long bufferSize)
{
    if (!IsOpen())
    {
        if (!Open()) return false;
    }
    is_client = true;
    remote_server_buffer_size = bufferSize;
    return true;
}

void NormSession::StopClient()
{    
    server_tree.Destroy();
    is_client = false;
    if (!is_server) Close();
}


void NormSession::Serve()
{
    // Only send new data when no other messages are queued for transmission
    //if (tx_timer.IsActive()) return;
    if (!message_queue.IsEmpty()) return;
        
    
    NormObject* obj = NULL;
    // Queue next server message
    if (tx_pending_mask.IsSet())
    {
        NormObjectId objectId(tx_pending_mask.FirstSet());
        obj = tx_table.Find(objectId);
        ASSERT(obj);
    }
    else
    {
        if (!posted_tx_queue_empty)
        {
            posted_tx_queue_empty = true;
            Notify(NormController::TX_QUEUE_EMPTY,
                   (NormServerNode*)NULL,
                   (NormObject*)NULL);
            // (TBD) Was session deleted?
            Serve();
            return;
        }       
    }
    
    if (obj)
    {
        NormMessage* msg = GetMessageFromPool();
        if (msg)
        {
            if (obj->NextServerMsg(msg))
            {
                msg->generic.SetDestination(address);
                msg->object.SetGrtt(grtt_quantized);
                msg->object.SetGroupSize(gsize_quantized);
                QueueMessage(msg);
                flush_count = 0;
                if (flush_timer.IsActive()) flush_timer.Deactivate();
                if (!obj->IsPending())
                {
                    if (obj->IsStream())
                        posted_tx_queue_empty = true; // repair-delayed stream advance
                    else
                        tx_pending_mask.Unset(obj->Id());
                }
            }
            else
            {
                ReturnMessageToPool(msg);
                if (obj->IsStream())
                {
                    if (((NormStreamObject*)obj)->IsFlushPending() &&
                        (flush_count < NORM_ROBUST_FACTOR))
                    {
                        // Queue flush message
                        ServerQueueFlush();
                    }
                    if (!posted_tx_queue_empty)
                    {
                        posted_tx_queue_empty = true;
                        Notify(NormController::TX_QUEUE_EMPTY,
                               (NormServerNode*)NULL, obj);
                        // (TBD) Was session deleted?
                        Serve();
                        return;
                    }      
                }
                else
                {
                    DMSG(0, "NormSession::Serve() pending obj, no message?.\n");                
                    ASSERT(0);
                }
            }
        }
        else
        {
            DMSG(0, "NormSession::Serve() node>%lu Warning! message_pool empty.\n",
                    LocalNodeId());
        }
    }
    else if (flush_count < NORM_ROBUST_FACTOR)
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
}  // end NormSession::Serve()

void NormSession::ServerQueueFlush()
{
    // (TBD) Deal with EOT or pre-queued squelch on squelch case
    if (flush_timer.IsActive()) return;
    NormMessage* msg = GetMessageFromPool();
    if (msg)
    {
        msg->generic.SetType(NORM_MSG_CMD);
        msg->generic.SetDestination(address);
        msg->cmd.generic.SetGrtt(grtt_quantized);
        msg->cmd.generic.SetGroupSize(gsize_quantized);
        msg->cmd.generic.SetFlavor(NormCmdMsg::NORM_CMD_FLUSH);
        NormCmdFlushMsg& flush = (NormCmdFlushMsg&)msg->cmd.flush;
        flush.Reset();  // reset flags and length
        NormObject* obj = tx_table.Find(tx_table.RangeHi());
        NormObjectId objectId;
        NormBlockId blockId;
        NormSegmentId segmentId;
        if (obj)
        {
            if (obj->IsStream())
            {
                NormStreamObject* stream = (NormStreamObject*)obj;
                objectId = stream->Id();
                blockId = stream->FlushBlockId();
                segmentId = stream->FlushSegmentId();
            }
            else
            {
                objectId = obj->Id();
                blockId = obj->LastBlockId();
                segmentId = obj->LastBlockSize() - 1;
            }
        }
        else
        {
            ReturnMessageToPool(msg);
            if (ServerQueueSquelch(next_tx_object_id))
            {
                flush_count++;
                flush_timer.SetInterval(2*grtt_advertised);
                InstallTimer(&flush_timer);
            }
            DMSG(8, "NormSession::ServerQueueFlush() node>%lu squelch queued (flush_count:%u)...\n",
                    LocalNodeId(), flush_count);
            return;  
        }
        flush.SetObjectId(objectId);
        flush.SetFecBlockId(blockId);
        flush.SetFecSymbolId(segmentId);
        QueueMessage(msg);
        flush_count++;
        flush_timer.SetInterval(2*grtt_advertised);
        InstallTimer(&flush_timer);
        DMSG(8, "NormSession::ServerQueueFlush() node>%lu, flush queued (flush_count:%u)...\n",
                LocalNodeId(), flush_count);
            
    }
    else
    {
        DMSG(0, " NormSession::ServerQueueFlush() node>%lu message_pool exhausted! (couldn't flush)\n",
                LocalNodeId());  
    }
}  // end NormSession::ServerQueueFlush()

bool NormSession::OnCommandTimeout()
{
    flush_timer.Deactivate();
    Serve();
    return false;   
}  // NormSession::OnCommandTimeout()
        
void NormSession::QueueMessage(NormMessage* msg)
{

/* A little test jig
        static struct timeval lastTime = {0,0};
    struct timeval currentTime;
    GetSystemTime(&currentTime);
    if (0 != lastTime.tv_sec)
    {
        double delta = currentTime.tv_sec - lastTime.tv_sec;
        delta += (((double)currentTime.tv_usec)*1.0e-06 -  
                  ((double)lastTime.tv_usec)*1.0e-06);
        TRACE("NormSession::QueueMessage() deltaT:%lf\n", delta);
    }
    lastTime = currentTime;
*/
    if (!tx_timer.IsActive())
    {
        tx_timer.SetInterval(0.0);
        InstallTimer(&tx_timer);   
    }
    message_queue.Append(msg);
}  // end NormSesssion::QueueMessage(NormMessage& msg)



NormFileObject* NormSession::QueueTxFile(const char* path,
                                         const char* infoPtr,
                                         UINT16      infoLen)
{
    if (!IsServer())
    {
        DMSG(0, "NormSession::QueueTxFile() Error: server is closed\n");
        return NULL;
    }  
    NormFileObject* file = new NormFileObject(this, NULL, next_tx_object_id);
    if (!file)
    {
        DMSG(0, "NormSession::QueueTxFile() new file object error: %s\n",
                strerror(errno));
        return NULL; 
    }
    if (!file->Open(path, infoPtr, infoLen))
    {
       DMSG(0, "NormSession::QueueTxFile() file open error\n");
       delete file;
       return NULL; 
    }    
    if (QueueTxObject(file, false))
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

NormStreamObject* NormSession::QueueTxStream(UINT32         bufferSize, 
                                             const char*    infoPtr, 
                                             UINT16         infoLen)
{
    if (!IsServer())
    {
        DMSG(0, "NormSession::QueueTxStream() Error: server is closed\n");
        return NULL;
    }     
    NormStreamObject* stream = new NormStreamObject(this, NULL, next_tx_object_id);
    if (!stream)
    {
        DMSG(0, "NormSession::QueueTxStream() new stream object error: %s\n",
                strerror(errno));
        return NULL; 
    }
    if (!stream->Open(bufferSize, infoPtr, infoLen))
    {
        DMSG(0, "NormSession::QueueTxStream() stream open error\n");
        delete stream;
        return NULL;
    }
    if (QueueTxObject(stream, true))
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
    NormSimObject* simObject = new NormSimObject(this, NULL, next_tx_object_id);
    if (!simObject)
    {
        DMSG(0, "NormSession::QueueTxSim() new sim object error: %s\n",
                strerror(errno));
        return NULL; 
    }  
    
    if (!simObject->Open(objectSize))
    {
        DMSG(0, "NormSession::QueueTxSim() open error\n");
        delete simObject;
        return NULL;
    }
    if (QueueTxObject(simObject, false))
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

bool NormSession::QueueTxObject(NormObject* obj, bool touchServer)
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
                ((tx_table.Size() + obj->Size()) > tx_cache_size_max)))
        {
            // Remove oldest non-pending 
            NormObject* oldest = tx_table.Find(tx_table.RangeLo());
            ASSERT(!oldest->IsPending());
            if (oldest->IsRepairPending())
            {
                DMSG(0, "NormSession::QueueTxObject() all held objects repair pending\n");
                //posted_tx_queue_empty = false;
                return false;
            }
            else
            {
                tx_table.Remove(oldest);
                oldest->Close();
                delete oldest;   
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
    tx_pending_mask.Set(obj->Id());
    ASSERT(tx_pending_mask.Test(obj->Id()));
    next_tx_object_id++;
    posted_tx_queue_empty = false;
    Serve();
    return true;
}  // end NormSession::QueueTxObject()

void NormSession::DeleteTxObject(NormObject* obj)
{
    NormObjectId objectId = obj->Id();
    ASSERT(obj == tx_table.Find(objectId));
    tx_table.Remove(obj);
    obj->Close();
    tx_pending_mask.Unset(objectId);
    tx_repair_mask.Unset(objectId);
    delete obj;
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
            if (obj->Id() == objectId)
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
            if (obj->Id() < objectId) 
            {
                break;
            }
            else
            {
                if (obj->Id() > objectId)
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

bool NormSession::TxSocketRecvHandler(UdpSocket* /*theSocket*/)
{
    NormMessage msg;
    unsigned int buflen = NORM_MSG_SIZE_MAX;
    if (UDP_SOCKET_ERROR_NONE == tx_socket.RecvFrom(msg.generic.AccessBuffer(), &buflen, 
                                                    &msg.generic.AccessAddress()))
    {
        msg.generic.SetLength(buflen);
        HandleReceiveMessage(msg, true);
    }
    else
    {
        DMSG(0, "NormSession::TxSocketRecvHandler() recvfrom error");
    }
    return true;
}  // end NormSession::TxSocketRecvHandler()

bool NormSession::RxSocketRecvHandler(UdpSocket* /*theSocket*/)
{
    NormMessage msg;
    unsigned int buflen = NORM_MSG_SIZE_MAX;
    if (UDP_SOCKET_ERROR_NONE == rx_socket.RecvFrom(msg.generic.AccessBuffer(), &buflen, 
                                                    &msg.generic.AccessAddress()))
    {
        msg.generic.SetLength(buflen);
        HandleReceiveMessage(msg, false);
    }
    else
    {
        DMSG(0, "NormSession::RxSocketRecvHandler() recvfrom error");
    }
    return true;
}  // end NormSession::RxSocketRecvHandler()

#ifdef SIMULATE
bool NormSession::SimSocketRecvHandler(char* buffer, unsigned short buflen,
                                       const NetworkAddress& src, bool unicast)
{
    NormMessage msg;
    memcpy(msg.generic.AccessBuffer(), buffer, buflen);
    msg.generic.SetLength(buflen);
    msg.generic.AccessAddress() = src;
    HandleReceiveMessage(msg, unicast);
    return true;
}  // end NormSession::SimSocketRecvHandler()
#endif // SIMULATE

static double THE_TIME = 0.0;

void NormTrace(NormNodeId localId, NormMessage& msg, bool sent)
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
        "CMD_INVALID",
        "CMD_FLUSH",
        "CMD_SQUELCH",
        "CMD_ACK_REQ",
        "CMD_REPAIR_ADV",
        "CMD_CC",
        "CMD_APP"
    };        
    static const char* REQ_NAME[] = 
    {
        "INVALID",
        "WATERMARK",
        "RTT",
        "APP"
    };
    
    NormMsgType msgType = msg.generic.GetType();
    UINT16 length = msg.generic.AccessLength();
    const char* status = sent ? "dst" : "src";
    
    struct timeval currentTime;
    GetSystemTime(&currentTime);
    struct tm* ct = gmtime((time_t*)&currentTime.tv_sec);
    DMSG(0, "trace>%02d:%02d:%02d.%06lu node>%lu %s>%s ",
            ct->tm_hour, ct->tm_min, ct->tm_sec, currentTime.tv_usec,
            (UINT32)localId, status, msg.generic.AccessAddress().HostAddressString());
    
    THE_TIME = currentTime.tv_sec + 1.0e-06 * (double)currentTime.tv_usec;
    switch (msgType)
    {
        case NORM_MSG_INFO:
            DMSG(0, "INFO obj>%hu ", (UINT16)msg.object.GetObjectId());
            break;
        case NORM_MSG_DATA:
            if (msg.data.GetFecSymbolId() < msg.object.GetFecBlockLen())
                DMSG(0, "DATA ");
            else
                DMSG(0, "PRTY ");
            DMSG(0, "obj>%hu blk>%lu seg>%hu ", 
                    (UINT16)msg.object.GetObjectId(),
                    (UINT32)msg.data.GetFecBlockId(),
                    (UINT16)msg.data.GetFecSymbolId());
            break;
            
        case NORM_MSG_CMD:
        {
            NormCmdMsg::Flavor flavor = msg.cmd.generic.GetFlavor();
            DMSG(0, "%s", CMD_NAME[flavor]);
            switch (flavor)
            {
                case NormCmdMsg::NORM_CMD_ACK_REQ:
                {
                    int index = msg.cmd.ack_req.GetAckFlavor();
                    index = MIN(index, 3);
                    DMSG(0, "(%s)", REQ_NAME[index]);
                    break;
                }
                case NormCmdMsg::NORM_CMD_SQUELCH:
                    DMSG(0, " obj>%hu blk>%lu seg>%hu",
                            (UINT16)msg.cmd.squelch.GetObjectId(),
                            (UINT32)msg.cmd.squelch.GetFecBlockId(),
                            (UINT16)msg.cmd.squelch.GetFecSymbolId());
                    break;
                case NormCmdMsg::NORM_CMD_FLUSH:
                    DMSG(0, " obj>%hu blk>%lu seg>%hu",
                            (UINT16)msg.cmd.flush.GetObjectId(),
                            (UINT32)msg.cmd.flush.GetFecBlockId(),
                            (UINT16)msg.cmd.flush.GetFecSymbolId());
                    break;
                default:
                    break;
            }
            DMSG(0, " ");
            break;
        }
            
        default:
            DMSG(0, "%s ", MSG_NAME[msgType]);   
            break;
    } 
    DMSG(0, "len>%hu\n", length);
}  // end NormTrace();


void NormSession::HandleReceiveMessage(NormMessage& msg, bool wasUnicast)
{   
    
    ASSERT(this == session_mgr.top_session);
    // Drop some rx messages for testing
    if (UniformRand(100.0) < rx_loss_rate) return;
    
    if (trace) NormTrace(LocalNodeId(), msg, false);
    switch (msg.generic.GetType())
    {
        case NORM_MSG_INFO:
            //DMSG(0, "NormSession::HandleReceiveMessage(NORM_MSG_INFO)\n");
            if (IsClient()) ClientHandleObjectMessage(msg);
            break;
        case NORM_MSG_DATA:
            //DMSG(0, "NormSession::HandleReceiveMessage(NORM_MSG_DATA) ...\n");
            if (IsClient()) ClientHandleObjectMessage(msg);
            break;
        case NORM_MSG_CMD:
            if (IsClient()) ClientHandleCommand(msg);
            break;
        case NORM_MSG_NACK:
            DMSG(4, "NormSession::HandleReceiveMessage(NORM_MSG_NACK) node>%lu ...\n",
                        LocalNodeId());
            if (IsServer() && (msg.nack.GetServerId() == LocalNodeId())) 
                ServerHandleNackMessage(msg.nack);
            if (IsClient()) ClientHandleNackMessage(msg.nack);
            break;
        case NORM_MSG_ACK:
        case NORM_MSG_REPORT: 
        case NORM_MSG_INVALID:
            DMSG(0, "NormSession::HandleReceiveMessage(NORM_MSG_INVALID)\n");
            break;
    }
}  // end NormSession::HandleReceiveMessage()



void NormSession::ClientHandleObjectMessage(NormMessage& msg)
{
    NormNodeId serverId = msg.generic.GetSender();
    NormServerNode* theServer = (NormServerNode*)server_tree.FindNodeById(serverId);
    if (!theServer)
    {
        if ((theServer = new NormServerNode(this, serverId)))
        {
            server_tree.AttachNode(theServer);
            DMSG(4, "NormSession::ClientHandleObjectMessage() node>%lu new remote server:%lu ...\n",
                    LocalNodeId(), serverId);
        }
        else
        {
            DMSG(0, "NormSession::ClientHandleObjectMessage() new server node error: %s\n",
                    strerror(errno));
            // (TBD) notify application of error
            return;   
        }   
    }
    // for statistics only (TBD) #ifdef NORM_DEBUG
    theServer->IncrementRecvTotal(msg.generic.GetLength());
    theServer->SetAddress(msg.generic.GetSource());
    theServer->HandleObjectMessage(msg);
    
}  // end NormSession::ClientHandleObjectMessage()

void NormSession::ClientHandleCommand(NormMessage& msg)
{
    NormNodeId serverId = msg.generic.GetSender();
    NormServerNode* theServer = (NormServerNode*)server_tree.FindNodeById(serverId);
    if (!theServer)
    {
        //DMSG(0, "NormSession::ClientHandleCommand() node>%lu recvd command from unknown server ...\n",
        //        LocalNodeId());   
        if ((theServer = new NormServerNode(this, serverId)))
        {
            server_tree.AttachNode(theServer);
            DMSG(4, "NormSession::ClientHandleCommand() node>%lu new remote server:%lu ...\n",
                    LocalNodeId(), serverId);
        }
        else
        {
            DMSG(0, "NormSession::ClientHandleCommand() new server node error: %s\n",
                    strerror(errno));
            // (TBD) notify application of error
            return;   
        }   
    }
    // for statistics only (TBD) #ifdef NORM_DEBUG
    theServer->IncrementRecvTotal(msg.generic.GetLength());
    theServer->SetAddress(msg.generic.GetSource());
    theServer->HandleCommand(msg.cmd);
}  // end NormSession::ClientHandleCommand()


void NormSession::ServerUpdateGrttEstimate(const struct timeval& grttResponse)
{
    if (grttResponse.tv_sec || grttResponse.tv_usec)
    {
        struct timeval currentTime;
        GetSystemTime(&currentTime);
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
        if (clientRtt < 1.0e-06) clientRtt = 1.0e-06;
        grtt_response = true;
        if (clientRtt > grtt_current_peak)
        {
            // Immediately incorporate bigger RTT's
            grtt_current_peak = clientRtt;
            if (clientRtt > grtt_measured)
            {
                grtt_decrease_delay_count = DEFAULT_GRTT_DECREASE_DELAY;
                grtt_measured = 0.25 * grtt_measured + 0.75 * clientRtt; 
                if (grtt_measured > grtt_max) grtt_measured = grtt_max;
                double pktInterval = ((double)(44+segment_size))/tx_rate;
                grtt_advertised = MAX(pktInterval, grtt_measured);
                UINT8 grttQuantizedOld = grtt_quantized;
                grtt_quantized = NormQuantizeRtt(grtt_advertised);
                // Recalculate grtt_advertise since quantization rounds upward
                grtt_advertised = NormUnquantizeRtt(grtt_quantized);
                if (grttQuantizedOld != grtt_quantized)
                    DMSG(4, "NormSession::ServerUpdateGrttEstimate() node>%lu new grtt: %lf sec.\n",
                            LocalNodeId(), grtt_advertised);
            }
        } 
    }
}  // end NormSession::ServerUpdateGrttEstimate()


void NormSession::ServerHandleNackMessage(NormNackMsg& nack)
{
    // Update GRTT estimate
    struct timeval grttResponse;
    nack.GetGrttResponse(grttResponse);
    ServerUpdateGrttEstimate(grttResponse);
    gsize_nack_count++;
    
    // Parse and process NACK 
    UINT16 requestOffset = 0;
    UINT16 requestLength = 0;
    NormRepairRequest req;
    NormObject* object = NULL;
    bool freshObject = true;
    NormObjectId prevObjectId;
    NormBlock* block = NULL;
    bool freshBlock = true;
    NormBlockId prevBlockId;
    
    bool startTimer = false;
    UINT16 numErasures = 0;
    
    bool squelchQueued = false;
    
    NormObjectId txObjectIndex;
    NormBlockId txBlockIndex;
    if (tx_pending_mask.IsSet())
    {
        txObjectIndex = tx_pending_mask.FirstSet();
        NormObject* obj = tx_table.Find(txObjectIndex);
        ASSERT(obj);
        if (obj->IsPending())
        {
            if (obj->IsPendingInfo())
                txBlockIndex = 0;
            else
                txBlockIndex = obj->FirstPending() + 1;
        }
        else
        {
            txObjectIndex = txObjectIndex + 1;
            txBlockIndex = 0;   
        }        
    }
    else
    {
        txObjectIndex = next_tx_object_id;
        txBlockIndex = 0;
    }
                
    bool holdoff = (repair_timer.IsActive() && !repair_timer.RepeatCount());
    
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
        NormSegmentId nextSegmentId, lastSegmentId;
        while (iterator.NextRepairItem(&nextObjectId, &nextBlockId, &nextSegmentId))
        {
            if (NormRepairRequest::RANGES == requestForm)
            {
                if (!iterator.NextRepairItem(&lastObjectId, &lastBlockId, &lastSegmentId))
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
                        if ((OBJECT == requestLevel) ||
                            (INFO == requestLevel))
                        {
                            nextObjectId++;
                            if (nextObjectId > lastObjectId) inRange = false;
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
                }
                ASSERT(object);
                
                switch (requestLevel)
                {
                    case OBJECT:
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
                                    attemptLock = false;  // NACK arrived too late 
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
                                    inRange = false;
                                    if (!squelchQueued) 
                                    {
                                        ServerQueueSquelch(nextObjectId);
                                        squelchQueued = true;
                                    }
                                    continue;
                                } 
                            }
                            else
                            {
                                inRange = false;
                                continue;
                            }   
                        }
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
                        inRange = false;
                        break;
                    case SEGMENT:
                        if (nextBlockId != prevBlockId) freshBlock = true;
                        if (freshBlock)
                        {
                            // Is this entire block already repair pending?
                            if (object->IsRepairSet(nextBlockId)) 
                            {
                                inRange = false;
                                continue;
                            }
                            if (!(block = object->FindBlock(nextBlockId)))
                            {
                                // Try to recover block including parity calculation 
                                if (!(block = object->ServerRecoverBlock(nextBlockId)))
                                {
                                    if (NormObject::STREAM == object->GetType())
                                    {
                                        DMSG(0, "NormSession::ServerHandleNackMessage() node>%lu  "
                                                "recvd repair request for old stream block(%lu) ...\n",
                                                LocalNodeId(), (UINT32)nextBlockId);
                                        inRange = false;
                                        if (!squelchQueued) 
                                        {
                                            ServerQueueSquelch(nextObjectId);
                                            squelchQueued = true;
                                        }
                                        continue;
                                    }
                                    else
                                    {
                                        // Resource constrained, move on.
                                        DMSG(0, "NormSession::ServerHandleNackMessage() node>%lu "
                                                "Warning - server is resource contrained ...\n");
                                        inRange = false;
                                        continue; 
                                    }  
                                }
                            }  
                            freshBlock = false;
                            numErasures = 0;
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
                                            NormSegmentId firstPending = block->FirstPending();
                                            if (lastLockId <= firstPending)
                                                attemptLock = false;
                                            else if (nextSegmentId < firstPending)
                                                firstLockId = firstPending;
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
                                    inRange = false;
                                    if (!squelchQueued) 
                                    {
                                        ServerQueueSquelch(nextObjectId);
                                        squelchQueued = true;
                                    }
                                    continue;
                                }  
                            }  
                            else
                            {
                                inRange = false;
                                continue;
                            }  
                        }
                            
                        // With a series of SEGMENT repair requests for a block, "numErasures" will
                        // eventually total the number of missing segments in the block.
                        numErasures += (lastSegmentId - nextSegmentId + 1);
                        if (holdoff)
                        {
                            if (nextObjectId > txObjectIndex)
                            {
                                if (object->TxUpdateBlock(block, nextSegmentId, 
                                                          lastSegmentId, numErasures))
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
                                    object->TxUpdateBlock(block, nextSegmentId, 
                                                          lastSegmentId, numErasures);   
                                } 
                                else if (1 == (txBlockIndex - nextBlockId))
                                {
                                    NormSegmentId firstPending = block->FirstPending();
                                    if (nextSegmentId > firstPending)
                                        object->TxUpdateBlock(block, nextSegmentId, 
                                                              lastSegmentId, numErasures);
                                    else if (lastSegmentId > firstPending)
                                        object->TxUpdateBlock(block, firstPending, 
                                                              lastSegmentId, numErasures);
                                    else if (numErasures > block->ParityCount())
                                        object->TxUpdateBlock(block, firstPending, 
                                                              firstPending, numErasures);
                                }
                            }
                        }
                        else
                        {
                            block->HandleSegmentRequest(nextSegmentId, lastSegmentId, ndata, nparity, numErasures);
                            startTimer = true;
                        }
                        inRange = false;
                        break;
                        
                    case INFO:
                        nextObjectId++;
                        if (nextObjectId > lastObjectId) inRange = false;
                        break; 
                }  // end switch(requestLevel)
            }  // end while(inRange)
        }  // end while(NextRepairItem())
    }  // end while(UnpackRepairRequest())
    if (startTimer && !repair_timer.IsActive())
    {
        repair_timer.SetInterval(grtt_advertised * (backoff_factor + 1.0));  
        DMSG(4, "NormSession::ServerHandleNackMessage() node>%lu starting server "
                "NACK aggregation timer (%lf sec)...\n", LocalNodeId(), repair_timer.Interval());
        gsize_nack_count = 1;
        InstallTimer(&repair_timer); 
    }
}  // end NormSession::ServerHandleNackMessage()

void NormSession::ClientHandleNackMessage(NormNackMsg& nack)
{
    NormServerNode* theServer = (NormServerNode*)server_tree.FindNodeById(nack.GetServerId());
    if (theServer)
    {
        theServer->HandleNackMessage(nack);
    }
    else
    {
        DMSG(4, "NormSession::ClientHandleNackMessage() node>%lu heard NACK for unkown server\n",
                (UINT32)nack.GetServerId());   
    }
}  // end NormSession::ClientHandleNackMessage()


bool NormSession::ServerQueueSquelch(NormObjectId objectId)
{
    // (TBD) if a squelch is already queued, update it if (objectId < squelch->objectId)
    NormMessage* msg = GetMessageFromPool();
    if (msg)
    {
        msg->generic.SetType(NORM_MSG_CMD);
        msg->generic.SetDestination(address);
        msg->cmd.generic.SetFlavor(NormCmdMsg::NORM_CMD_SQUELCH);
        NormCmdSquelchMsg& squelch = (NormCmdSquelchMsg&)msg->cmd.squelch;
        NormObject* obj = tx_table.Find(objectId);
        NormObjectTable::Iterator iterator(tx_table);
        NormObjectId nextId;
        if (obj)
        {
            ASSERT(NormObject::STREAM == obj->GetType());
            squelch.SetObjectId(objectId);
            squelch.SetFecBlockId(((NormStreamObject*)obj)->StreamBufferLo());
            squelch.SetFecSymbolId(0);
            squelch.ResetInvalidObjectList();
            while ((obj = iterator.GetNextObject()))
                if (objectId == obj->Id()) break;
            nextId = objectId + 1;
        }
        else
        {
            obj = iterator.GetNextObject();
            if (obj)
            {
               squelch.SetObjectId(obj->Id());
               if (obj->IsStream())
                   squelch.SetFecBlockId(((NormStreamObject*)obj)->StreamBufferLo());
               else
                   squelch.SetFecBlockId(0);
               squelch.SetFecSymbolId(0); 
               nextId = obj->Id() + 1;
            }
            else
            {
                // Squelch to point to future object
                squelch.SetObjectId(next_tx_object_id);
                squelch.SetFecBlockId(0);
                squelch.SetFecSymbolId(0);
                nextId = next_tx_object_id;
            }
        }
        bool buildingList = true;
        while (buildingList && (obj = iterator.GetNextObject()))
        {
            while (nextId != obj->Id())
            {
                if (!squelch.AppendInvalidObject(nextId, segment_size))
                {
                    buildingList = false;
                    break;
                }
                nextId++;
            }
        }
        QueueMessage(msg);
        DMSG(2, "NormSession::ServerQueueSquelch() node>%lu server queued squelch ...\n",
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


void NormSession::ServerUpdateGroupSize() // (TBD) not doing anything yet
{
    // Possible approach ... use smoothed history of gsize_nack_count information
    // to adjust a group size estimator (if needed to keep the nack count average
    // with an acceptable volume) there may be some issue here since either
    // underestimating _or_ overestimating the group size may reduce
    // NACK suppression performance ... but it's somewhat safer to overestimate
    // and a check could be provided to help guess if we're over or under estimating?
    gsize_nack_ave = 0.9*gsize_nack_ave + 0.1*(double)gsize_nack_count;
    
}  // end NormSession::ServerUpdateGroupSize()

bool NormSession::OnRepairTimeout()
{
    if (repair_timer.RepeatCount())
    {
        // NACK aggregation period has ended. (incorporate accumulated repair requests)
        DMSG(4, "NormSession::OnRepairTimeout() node>%lu server NACK aggregation time ended.\n",
                LocalNodeId()); 
        
        // Update our group size estimate (we don't include nacks during repair hold-off interval)
        ServerUpdateGroupSize();  
        
        NormObjectTable::Iterator iterator(tx_table);
        NormObject* obj;
        while ((obj = iterator.GetNextObject()))
        {
            NormObjectId objectId = obj->Id();
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
        // Holdoff initiation of new repair cycle for one GRTT 
        repair_timer.SetInterval(grtt_advertised); // repair holdoff interval = 1*GRTT
    }
    else
    {
        // REPAIR holdoff interval has now ended.
        DMSG(4, "NormSession::OnRepairTimeout() node>%lu server holdoff time ended.\n",
                LocalNodeId());   
    }
    return true;
}  // end NormSession::OnRepairTimeout()



bool NormSession::OnTxTimeout()
{
    NormMessage* msg = message_queue.RemoveHead();
    if (msg)
    {
        // Fill in any last minute timestamps
        switch (msg->generic.GetType())
        {
            case NORM_MSG_CMD:
                switch (msg->cmd.generic.GetFlavor())
                {
                    case NormCmdMsg::NORM_CMD_ACK_REQ:
                        if (NormCmdAckReqMsg::RTT == msg->cmd.ack_req.GetAckFlavor())
                        {
                            struct timeval currentTime;
                            GetSystemTime(&currentTime);
                            msg->cmd.ack_req.SetRttSendTime(currentTime);   
                        }
                        break;
                    default:
                        break;
                }
                break;
                
            case NORM_MSG_NACK:
            {
                NormServerNode* theServer = 
                    (NormServerNode*)server_tree.FindNodeById(msg->nack.GetServerId());
                ASSERT(theServer);
                struct timeval grttResponse;
                theServer->CalculateGrttResponse(grttResponse);
                msg->nack.SetGrttResponse(grttResponse);
                // (TBD) fill in cc_sequence
                // (TBD) fill in loss estimate
                break;
            }
                
            case NORM_MSG_ACK:
            {
                NormServerNode* theServer = 
                    (NormServerNode*)server_tree.FindNodeById(msg->nack.GetServerId());
                ASSERT(theServer);
                struct timeval grttResponse;
                theServer->CalculateGrttResponse(grttResponse);
                //msg->ack.SetGrttResponse(grttResponse);
                // (TBD) fill in cc_sequence
                // (TBD) fill in loss estimate
                break;
            }
                
            default:
                break;
        }
        // Fill in common message fields
        msg->generic.SetVersion(1);
        msg->generic.SetSequence(tx_sequence++); 
        msg->generic.SetSender(local_node_id);
        UINT16 len = msg->generic.GetLength();
        // Drop some tx messages for testing purposes
        bool drop = (UniformRand(100.0) < tx_loss_rate);
        if (!drop)
        {
            if (UDP_SOCKET_ERROR_NONE != 
                tx_socket.SendTo(&msg->generic.GetDestination(), 
                                  msg->generic.AccessBuffer(), len))
            {
                DMSG(0, "NormSession::OnTxTimeout() sendto() error\n");
            }
            else
            {
                // Separate send/recv tracing
                if (trace) NormTrace(LocalNodeId(), *msg, true);   
            }
        }
        else
        {
            //TRACE("TX MESSAGE DROPPED! (tx_loss_rate:%lf\n", tx_loss_rate);   
        }
        tx_timer.SetInterval(((double)len) / tx_rate);
        ReturnMessageToPool(msg);
        return true;  // reinstall tx_timer
    }
    else
    {   
        if (IsServer()) Serve();
        if (message_queue.IsEmpty())
        {
            tx_timer.Deactivate();
            return false;
        }
        else
        {
            // We have a new message as a result of serving, so send it immediately
            OnTxTimeout();
            return true;
        }
    }
}  // end NormSession::OnTxTimeout()

bool NormSession::OnProbeTimeout()
{
    NormMessage* msg = GetMessageFromPool();
    if (msg)
    {
        msg->generic.SetType(NORM_MSG_CMD);
        msg->generic.SetDestination(address);
        msg->cmd.generic.SetGrtt(grtt_quantized);
        msg->cmd.generic.SetGroupSize(gsize_quantized);
        msg->cmd.generic.SetFlavor(NormCmdMsg::NORM_CMD_ACK_REQ);
        NormCmdAckReqMsg& ackReq = msg->cmd.ack_req;
        ackReq.SetAckFlavor(NormCmdAckReqMsg::RTT);
        // SetRttSendTime() when message is being sent
        ackReq.ResetAckingNodeList();
        QueueMessage(msg);
    }
    else
    {
        DMSG(0, "NormSession::OnProbeTimeout() node>%lu message_pool empty! can't probe\n",
                LocalNodeId());   
    }
        
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
            DMSG(2, "NormSession::OnProbeTimeout() node>%lu new grtt: %lf\n",
                    LocalNodeId(), grtt_advertised);
    }
    
    // Manage probe_timer interval
    if (probe_interval < probe_interval_min)
        probe_interval = probe_interval_min;
    else
        probe_interval *= 2.0;
    if (probe_interval > probe_interval_max)
        probe_interval = probe_interval_max;
    probe_timer.SetInterval(probe_interval);
    return true;
}  // end NormSession::OnProbeTimeout()

bool NormSession::OnReportTimeout()
{
    // Client reporting (just print out for now)
    struct timeval currentTime;
    GetSystemTime(&currentTime);
    struct tm* ct = gmtime((time_t*)&currentTime.tv_sec);
    DMSG(2, "Report time>%02d:%02d:%02d.%06lu node>%lu ***************************************\n", 
            ct->tm_hour, ct->tm_min, ct->tm_sec, currentTime.tv_usec, LocalNodeId());
    if (IsClient())
    {
        NormNodeTreeIterator iterator(server_tree);
        NormServerNode* next;
        while ((next = (NormServerNode*)iterator.GetNextNode()))
        {
            DMSG(2, "Remote server:%lu\n", next->Id());
            double rxRate = 8.0e-03*((double)next->RecvTotal()) / report_timer.Interval();  // kbps
            double rxGoodput = 8.0e-03*((double)next->RecvGoodput()) / report_timer.Interval();  // kbps
            next->ResetRecvStats();
            DMSG(2, "   rx_rate>%9.3lf kbps rx_goodput>%9.3lf kbps\n", rxRate, rxGoodput);
            DMSG(2, "   objects completed>%lu pending>%lu failed:%lu\n", 
                    next->CompletionCount(), next->PendingCount(), next->FailureCount());
            DMSG(2, "   buffer usage> current:%lu peak:%lu (overuns:%lu)\n", next->CurrentBufferUsage(),
                                                                             next->PeakBufferUsage(), 
                                                                             next->BufferOverunCount());
            DMSG(2, "   resyncs>%lu nacks>%lu suppressed>%lu\n", next->ResyncCount(),
                    next->NackCount(), next->SuppressCount());
                    
        }
    }  // end if (IsClient())
    DMSG(2, "***************************************************************************\n");
    return true;
}  // end NormSession::OnReportTimeout()

NormSessionMgr::NormSessionMgr()
 : socket_installer(NULL), socket_install_data(NULL), 
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
    if (NORM_NODE_ANY == localNodeId)
    {
        // Use local ip address to assign default localNodeId
        NetworkAddress localAddr;
        if (!localAddr.LookupLocalHostAddress())
        {
            DMSG(0, "NormSessionMgr::NewSession() local address lookup error\n");
            return ((NormSession*)NULL);    
        } 
        // (TBD) test IPv6 "EndIdentifier" ???
        localNodeId = localAddr.EndIdentifier();
    }
    NetworkAddress theAddress;
    if (!theAddress.LookupHostAddress(sessionAddress))
    {
        DMSG(0, "NormSessionMgr::NewSession() session address lookup error!\n");
        return ((NormSession*)NULL);   
    }
    theAddress.SetPort(sessionPort);    
    NormSession* theSession = new NormSession(*this, localNodeId);   
    if (!theSession)
    {
        DMSG(0, "NormSessionMgr::NewSession() new session error: %s\n", 
                strerror(errno)); 
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

