#include "normSimAgent.h"
#include <errno.h>

// This NORM simulation agent includes support for providing transport
// of a message stream from the MGEN simulation agent with restrictions.  The
// current restriction is the MGEN simulation agent

NormSimAgent::NormSimAgent(ProtoTimerMgr&         timerMgr,
                           ProtoSocket::Notifier& socketNotifier)
  : msg_sink(NULL), session_mgr(timerMgr, socketNotifier), session(NULL),
    address(NULL), port(0), ttl(3), 
    tx_rate(NormSession::DEFAULT_TRANSMIT_RATE), probe_count(1),
    cc_enable(false), ecn_enable(false), cc_mode(NORM_FIXED),
    unicast_nacks(false), silent_receiver(false),
    backoff_factor(NormSession::DEFAULT_BACKOFF_FACTOR),
    segment_size(1024), ndata(32), nparity(16), auto_parity(0), extra_parity(0),
    group_size(NormSession::DEFAULT_GSIZE_ESTIMATE),
    grtt_estimate(NormSession::DEFAULT_GRTT_ESTIMATE),
    tx_buffer_size(1024*1024), 
    tx_cache_min(NormSession::DEFAULT_TX_CACHE_MIN), 
    tx_cache_max(NormSession::DEFAULT_TX_CACHE_MAX), 
    tx_cache_size(NormSession::DEFAULT_TX_CACHE_SIZE), 
    rx_buffer_size(1024*1024), rx_cache_max(NormSession::DEFAULT_RX_CACHE_MAX),
    tx_object_size(0), tx_object_interval(0.0), 
    tx_object_size_min(0), tx_object_size_max(0), 
    tx_repeat_count(0), tx_repeat_interval(0.0),
    tx_requeue(0), tx_requeue_count(0),
    stream(NULL), auto_stream(false), 
    stream_buffer_max(0), stream_buffer_count(0), 
    stream_bytes_remain(0), watermark_pending(false),
    flow_control(false), push_mode(false), flush_mode(NormStreamObject::FLUSH_PASSIVE),
    msg_sync(false), mgen_bytes(0), mgen_pending_bytes(0),
    tracing(false), log_file_ptr(NULL), tx_loss(0.0), rx_loss(0.0)
{
    // Bind NormSessionMgr to this agent and simulation environment
    session_mgr.SetController(static_cast<NormController*>(this));
    
    interval_timer.SetListener(this, &NormSimAgent::OnIntervalTimeout);
    interval_timer.SetInterval(0.0);
    interval_timer.SetRepeat(0);
    
    memset(mgen_buffer, 0, 64);
}

NormSimAgent::~NormSimAgent()
{
    if (address) delete address;
}


const char* const NormSimAgent::cmd_list[] = 
{
    "+debug",        // debug level
    "+log",          // log file name
    "-trace",        // message tracing
    "+flog",         // set "file" send/recv log file name
    "+txloss",       // tx packet loss percent
    "+rxloss",       // rx packet loss percent
    "+address",      // session dest address
    "+ttl",          // multicast ttl
    "+rate",         // tx rate
    "+cc",           // congestion control on/off (sets NORM-CC operation, or fixed rate if off)
    "+ecn",          // ecn support on/off
    "-cce",          // sets NORM-CCE operation
    "-ccl",          // sets NORM-CCL operation
    "+probe",        // set number of cc probes per rtt
    "+flowControl",  // flow control on/off
    "+backoff",      // backoff factor 'k' (maxBackoff = k * GRTT)
    "+input",        // stream input
    "+output",       // stream output
    "+interval",     // delay between tx objects
    "+repeat",       // number of times to repeat tx object set
    "+rinterval",    // repeat interval
    "+segment",      // sender segment size
    "+block",        // sender blocking size
    "+parity",       // sender parity segments calculated per block
    "+auto",         // sender auto parity count
    "+extra",        // sender extra parity count
    "+gsize",        // group size estimate
    "+grtt",         // grtt estimate
    "+txbuffer",     // tx buffer size (bytes)
    "+txcachebounds",// <countMin:countMax:sizeMax> limits on sender tx object caching
    "+rxbuffer",     // rx buffer size (bytes)
    "+rxcachemax",   // sets rcvr "max_pending_range" for NormObjectIds
    "+start",        // open session and begin activity
    "-stop",         // cease activity and close session
    "+sendFile",     // queue a "sim" file of <size> bytes for transmission
    "+sendRandomFile",  // queue random-size file size range <sizeMin>:<sizeMax>
    "+requeue",      // requeue (i.e. retransmit) each file sent <count> times
    "+sendStream",   // send a simulated NORM stream
    "+openStream",   // open a stream object for messaging (sending)
    "+push",         // "on" means real-time push stream advancement (non-blocking)
    "+flush",        // stream flush mode
    "-doFlush",      // invoke flushing of stream
    "+ackingNode",   // <nodeId>; add <nodeId> to acking node list   
    "+unicastNacks", // receivers will unicast feedback
    "+silentReceiver", // receivers will not transmit
    NULL         
};
    
bool NormSimAgent::ProcessCommand(const char* cmd, const char* val)
{
    CmdType type = CommandType(cmd);
    ASSERT(CMD_INVALID != type);
    unsigned int len = strlen(cmd);
    if ((CMD_ARG == type) && !val)
    {
        PLOG(PL_FATAL, "NormSimAgent::ProcessCommand(%s) missing argument\n", cmd);
        return false;        
    }
    
    if (!strncmp("debug", cmd, len))
    {
        
        int debugLevel = atoi(val);
        if ((debugLevel < 0) || (debugLevel > 12))
        {
            PLOG(PL_FATAL, "NormSimAgent::ProcessCommand(segment) invalid debug level!\n");   
            return false;
        }
        SetDebugLevel(debugLevel);
    }
    else if (!strncmp("log", cmd, len))
    {
        OpenDebugLog(val);
    }
    else if (!strncmp("flog", cmd, len))
    {
        // open a file to log "file" enqueue/completion events in ".pdrc" format
        if (NULL == (log_file_ptr = fopen(val, "w")))
        {
            perror("NormSimAgent::ProcessCommand(flog) fopen() error:");
            return false;
        }
    }
    else if (!strncmp("trace", cmd, len))
    {
        tracing = true;
        if (session) session->SetTrace(true);
    }
    else if (!strncmp("txloss", cmd, len))
    {
        double txLoss = atof(val);
        if (txLoss < 0)
        {
            PLOG(PL_FATAL, "NormSimAgent::ProcessCommand(txloss) invalid txRate!\n");   
            return false;
        }
        tx_loss = txLoss;
        if (session) session->SetTxLoss(txLoss);
    }
    else if (!strncmp("rxloss", cmd, len))
    {
        double rxLoss = atof(val);
        if (rxLoss < 0)
        {
            PLOG(PL_FATAL, "NormSimAgent::ProcessCommand(rxloss) invalid txRate!\n");   
            return false;
        }
        rx_loss = rxLoss;
        if (session) session->SetRxLoss(rxLoss);
    }
    else if (!strncmp("address", cmd, len))
    {
        unsigned int len = strlen(val);
        if (address) delete address;
        if (!(address = new char[len+1]))
        {
            PLOG(PL_FATAL, "NormSimAgent::ProcessCommand(address) allocation error:%s\n",
                strerror(errno)); 
            return false;
        }
        strcpy(address, val);
        char* ptr = strchr(address, '/');
        if (!ptr)
        {
            delete address;
            address = NULL;
            PLOG(PL_FATAL, "NormSimAgent::ProcessCommand(address) missing port number!\n");   
            return false;
        }
        *ptr++ = '\0';
        int portNum = atoi(ptr);
        if ((portNum < 1) || (portNum > 65535))
        {
            delete address;
            address = NULL;
            PLOG(PL_FATAL, "NormSimAgent::ProcessCommand(address) invalid port number!\n");   
            return false;
        }
        port = portNum;
    }
    else if (!strncmp("ttl", cmd, len))
    {
        int ttlTemp = atoi(val);
        if ((ttlTemp < 1) || (ttlTemp > 255))
        {
            PLOG(PL_FATAL, "NormSimAgent::ProcessCommand(ttl) invalid value!\n");   
            return false;
        }
        ttl = ttlTemp;
    }
    else if (!strncmp("rate", cmd, len))
    {
        double txRate = atof(val);
        if (txRate < 0)
        {
            PLOG(PL_FATAL, "NormSimAgent::ProcessCommand(rate) invalid txRate!\n");   
            return false;
        }
        tx_rate = txRate;
        if (session) session->SetTxRate(txRate);
    }
    else if (!strncmp("probe", cmd, len))
    {
        unsigned int probeCount = (unsigned int)atoi(val);
        if (0 == probeCount)
        {
            PLOG(PL_FATAL, "NormSimAgent::ProcessCommand(probe) invalid probeCount!\n");   
            return false;
        }
        probe_count = probeCount;
        if (session) session->SetProbeCount(probeCount);
    }
    else if (!strncmp("cc", cmd, len))
    {
        bool ccEnable;
        if (!strcmp(val, "on"))
        {
            ccEnable = true;
        }
        else if (!strcmp(val, "off"))
        {
            ccEnable = false;
        }
        else
        {
            PLOG(PL_FATAL, "NormSimAgent::ProcessCommand(cc) invalid argument!\n");   
            return false;
        }    
        if (ccEnable) 
            SetCCMode(NORM_CC);
        else
            SetCCMode(NORM_FIXED);
        return true;
    }
    else if (!strncmp("ecn", cmd, len))
    {
        if (!strcmp(val, "on"))
        {
            ecn_enable = true;
        }
        else if (!strcmp(val, "off"))
        {
            ecn_enable = false;
        }
        else
        {
            PLOG(PL_FATAL, "NormSimAgent::ProcessCommand(ecn) invalid argument!\n");   
            return false;
        }        
        if (session) 
        {
            bool ignoreLoss = (NORM_CCE == cc_mode);
            bool tolerateLoss = (NORM_CCL == cc_mode);
            session->SetEcnSupport(ecn_enable, ignoreLoss, tolerateLoss);
        }
        return true;
    }
    else if (!strncmp("cce", cmd, len))
    {
        // This sets up NORM-CCE congestion control
        // (i.e. ECN-enabled and "ignoreLoss)
        SetCCMode(NORM_CCE);
        return true;
    }
    else if (!strncmp("ccl", cmd, len))
    {
        // This sets up NORM-CCE congestion control
        // (i.e. ECN-enabled and "ignoreLoss)
        SetCCMode(NORM_CCL);
        return true;
    }
    else if (!strncmp("backoff", cmd, len))
    {
        double backoffFactor = atof(val);
        if (backoffFactor < 0)
        {
            PLOG(PL_FATAL, "NormSimAgent::ProcessCommand(backoff) invalid txRate!\n");   
            return false;
        }
        backoff_factor = backoffFactor;
        if (session) session->SetBackoffFactor(backoffFactor);
    }
    else if (!strncmp("interval", cmd, len))
    {
        if (1 != sscanf(val, "%lf", &tx_object_interval)) 
            tx_object_interval = -1.0;
        if (tx_object_interval < 0.0)
        {
            PLOG(PL_FATAL, "NormSimAgent::ProcessCommand(interval) Invalid tx object interval: %s\n",
                     val);
            tx_object_interval = 0.0;
            return false;
        }
    }    
    else if (!strncmp("repeat", cmd, len))
    {
        tx_repeat_count = atoi(val);  
    }
    else if (!strncmp("segment", cmd, len))
    {
        int segmentSize = atoi(val);
        if ((segmentSize < 0) || (segmentSize > 8300))
        {
            PLOG(PL_FATAL, "NormSimAgent::ProcessCommand(segment) invalid segment size!\n");   
            return false;
        }
        segment_size = segmentSize;
    }
    else if (!strncmp("block", cmd, len))
    {
        int blockSize = atoi(val);
        if ((blockSize < 1) || (blockSize > 255))
        {
            PLOG(PL_FATAL, "NormSimAgent::ProcessCommand(block) invalid block size!\n");   
            return false;
        }
        ndata = blockSize;
    }
    else if (!strncmp("parity", cmd, len))
    {
        int numParity = atoi(val);
        if ((numParity < 0) || (numParity > 254))
        {
            PLOG(PL_FATAL, "NormSimAgent::ProcessCommand(parity) invalid value!\n");   
            return false;
        }
        nparity = numParity;
    }
    else if (!strncmp("auto", cmd, len))
    {
        int autoParity = atoi(val);
        if ((autoParity < 0) || (autoParity > 254))
        {
            PLOG(PL_FATAL, "NormSimAgent::ProcessCommand(auto) invalid value!\n");   
            return false;
        }
        auto_parity = autoParity;
        if (session) session->SenderSetAutoParity(autoParity);
    }
    else if (!strncmp("extra", cmd, len))
    {
        int extraParity = atoi(val);
        if ((extraParity < 0) || (extraParity > 254))
        {
            PLOG(PL_FATAL, "NormSimAgent::ProcessCommand(extra) invalid value!\n");   
            return false;
        }
        extra_parity = extraParity;
        if (session) session->SenderSetExtraParity(extraParity);
    }
    else if (!strncmp("gsize", cmd, len))
    {
        if (1 != sscanf(val, "%lf", &group_size))
        {
            PLOG(PL_FATAL, "NormSimAgent::ProcessCommand(gize) invalid value!\n");   
            return false;
        }
        if (session) session->SenderSetGroupSize(group_size);
    }
    else if (!strncmp("grtt", cmd, len))
    {
        if (1 != sscanf(val, "%lf", &grtt_estimate))
        {
            PLOG(PL_FATAL, "NormSimAgent::ProcessCommand(gize) invalid value!\n");   
            return false;
        }
        if (session) session->SenderSetGroupSize(grtt_estimate);
    }
    else if (!strncmp("txbuffer", cmd, len))
    {
        if (1 != sscanf(val, "%lu", &tx_buffer_size))
        {
            PLOG(PL_FATAL, "NormSimAgent::ProcessCommand(txbuffer) invalid value!\n");   
            return false;
        }
    }
    else if (!strncmp("txcachebounds", cmd, len))
    {
        unsigned long countMin, countMax, sizeMax;
        if (3 != sscanf(val, "%lu:%lu:%lu\n", &countMin, &countMax, &sizeMax))
        {
            PLOG(PL_FATAL, "NormSimAgent::ProcessCommand(txcachebounds) invalid value!\n");   
            return false;
        }
        tx_cache_min = countMin;
        tx_cache_max = countMax;
        tx_cache_size = sizeMax;
        if (session) session->SetTxCacheBounds(tx_cache_size, tx_cache_min, tx_cache_max);
    }
    else if (!strncmp("rxbuffer", cmd, len))
    {
        if (1 != sscanf(val, "%lu", &rx_buffer_size))
        {
            PLOG(PL_FATAL, "NormSimAgent::ProcessCommand(rxbuffer) invalid value!\n");   
            return false;
        }
    }
    else if (!strncmp("rxcachemax", cmd, len))
    {
        if (1 != sscanf(val, "%lu", &rx_cache_max))
        {
            PLOG(PL_FATAL, "NormSimAgent::ProcessCommand(rxcachemax) invalid value!\n");   
            return false;
        }
        if (session) session->SetRxCacheMax(rx_cache_max);
    }
    else if (!strncmp("start", cmd, len))
    {
        if (!strcmp("sender", val))
        {
            return StartSender();
        }
        else if (!strcmp("receiver", val))
        {
           return StartReceiver(); 
        }
        else
        {
            PLOG(PL_FATAL, "NormSimAgent::ProcessCommand(start) invalid value!\n");   
            return false;    
        }           
    }
    else if (!strncmp("stop", cmd, len))
    {
        Stop();   
    }
    else if (!strncmp("sendFile", cmd, len))
    {
        if (1 != sscanf(val, "%lu", &tx_object_size))
        {
            PLOG(PL_FATAL, "NormSimAgent::ProcessCommand(sendFile) invalid size!\n");   
            return false;
        }
        if (session)
        {
            if (tx_repeat_count >= 0) tx_repeat_count++;
            OnIntervalTimeout(interval_timer);
        }
        else
        {
            PLOG(PL_FATAL, "NormSimAgent::ProcessCommand(sendFile) no session started!\n");
            return false;
        }   
    }  
    else if (!strncmp("sendRandomFile", cmd, len))
    {
        if (2 != sscanf(val, "%lu:%lu", &tx_object_size_min, &tx_object_size_max))
        {
            PLOG(PL_FATAL, "NormSimAgent::ProcessCommand(sendRandomFile) invalid size!\n");   
            return false;
        }
        if (tx_object_size_min > tx_object_size_max)
        {
            unsigned int tempSize = tx_object_size_min;
            tx_object_size_min = tx_object_size_max;
            tx_object_size_max = tempSize;
        }
        tx_object_size = tx_object_size_max - tx_object_size_min;
        tx_object_size = (unsigned long)(tx_object_size * UniformRand(1.0));
        tx_object_size += tx_object_size_min;
        if (session)
        {
            if (tx_repeat_count >= 0) tx_repeat_count++;
            OnIntervalTimeout(interval_timer);
        }
        else
        {
            PLOG(PL_FATAL, "NormSimAgent::ProcessCommand(sendRandomFile) no session started!\n");
            return false;
        }  
    }    
    else if (!strncmp("requeue", cmd, len))
    {
        if (1 != sscanf(val, "%d", &tx_requeue))
        {
            PLOG(PL_FATAL, "NormSimAgent::ProcessCommand(requeue) invalid count!\n");   
            return false;
        }
        tx_requeue_count = tx_requeue;
    }
    else if (!strncmp("sendStream", cmd, len))
    {
        if (session)
        {
            if (stream)
            {
                PLOG(PL_FATAL, "NormSimAgent::ProcessCommand(sendStream) stream already open!\n");   
                return false;
            }
            if (1 != sscanf(val, "%lu", &tx_object_size))
            {
                PLOG(PL_FATAL, "NormSimAgent::ProcessCommand(sendStream) invalid buffer size!\n");   
                return false;
            }
            if (!(stream = session->QueueTxStream(tx_object_size)))
            {
                PLOG(PL_FATAL, "NormSimAgent::ProcessCommand(sendStream) error opening stream!\n");
                return false;
            }
            stream->SetFlushMode(flush_mode);
            stream->SetPushMode(push_mode);
            auto_stream = true;
        }
        else
        {
            PLOG(PL_FATAL, "NormSimAgent::ProcessCommand(sendStream) session not started!\n");
            return false;
        }   
    }  
    else if (!strncmp("openStream", cmd, len))
    {
        if (session)
        {
            if (NULL != stream)
            {
                PLOG(PL_FATAL, "NormSimAgent::ProcessCommand(openStream) stream already open!\n");   
                return false;
            }
            if (1 != sscanf(val, "%lu", &tx_object_size))
            {
                PLOG(PL_FATAL, "NormSimAgent::ProcessCommand(openStream) invalid buffer size!\n");   
                return false;
            }
            if(!(tx_msg_buffer = new char[65536]))
            {
                PLOG(PL_FATAL, "NormSimAgent::ProcessCommand(openStream) error allocating tx_msg_buffer: %s\n",
                        strerror(errno));
                return false;
            } 
            if (!(stream = session->QueueTxStream(tx_object_size)))
            {
                PLOG(PL_FATAL, "NormSimAgent::ProcessCommand(openStream) error opening stream!\n");
                return false;
            }
            stream->SetFlushMode(flush_mode);
            stream->SetPushMode(push_mode);
            auto_stream = false;  
            tx_msg_len = tx_msg_index = 0;
            if (flow_control)
            {
                // ACK-based flow control has been enabled, so initialize
                stream_buffer_max = ComputeStreamBufferSegmentCount(tx_object_size, segment_size, ndata);
                stream_buffer_max -= ndata; // a little safety margin
                stream_buffer_count = stream_bytes_remain = 0;
                watermark_pending = false;
            }
        }
        else
        {
            PLOG(PL_FATAL, "NormSimAgent::ProcessCommand(openStream) session not started!\n");
            return false;
        }   
    }  
    else if (!strncmp("flush", cmd, len))
    {
        int valLen = strlen(val);
        if (!strncmp("none", val, valLen))
            flush_mode = NormStreamObject::FLUSH_NONE;
        else if (!strncmp("passive", val, valLen))
            flush_mode = NormStreamObject::FLUSH_PASSIVE;
        else if (!strncmp("active", val, valLen))
            flush_mode = NormStreamObject::FLUSH_ACTIVE;
        else
        {
            PLOG(PL_FATAL, "NormApp::OnCommand(flush) invalid msg flush mode!\n");   
            return false;
        }
    }
    else if (!strncmp("doFlush", cmd, len))
    {
        if (session)
        {
            return FlushStream(true);  // TBD - provide control over EOM marking here?
        }
        else
        {
            PLOG(PL_FATAL, "NormSimAgent::ProcessCommand(doFlush) session not started!\n");
            return false;
        }          
    }
    else if (!strncmp("push", cmd, len))
    {
        if (!strcmp(val, "on"))
            push_mode = true;
        else if (!strcmp(val, "off"))
            push_mode = false;
        else
        {
            PLOG(PL_FATAL, "NormSimAgent::ProcessCommand(push) invalid argument!\n");   
            return false;
        }        
        return true;
    }
    else if (!strncmp("ackingNode", cmd, len))
    {
        return AddAckingNode(atoi(val));
    }
    else if (!strncmp("unicastNacks", cmd, len))
    {
        if (!strcmp(val, "on"))
            unicast_nacks = true;
        else if (!strcmp(val, "off"))
            unicast_nacks = false;
        else
        {
            PLOG(PL_FATAL, "NormSimAgent::ProcessCommand(unicastNacks) invalid argument!\n");   
            return false;
        }        
        if (session) session->ReceiverSetUnicastNacks(unicast_nacks);
        return true;
    }
    else if (!strncmp("silentReceiver", cmd, len))
    {
        if (!strcmp(val, "on"))
            silent_receiver = true;
        else if (!strcmp(val, "off"))
            silent_receiver = false;
        else
        {
            PLOG(PL_FATAL, "NormSimAgent::ProcessCommand(silentReceiver) invalid argument!\n");   
            return false;
        }        
        if (session) session->ReceiverSetSilent(silent_receiver);
        return true;
    }
    return true;
}  // end NormSimAgent::ProcessCommand()

unsigned int NormSimAgent::ComputeStreamBufferSegmentCount(unsigned int bufferBytes, UINT16 segmentSize, UINT16 blockSize)
{
    // This same computation is performed in NormStreamObject::Open() in "normObject.cpp"
    unsigned int numBlocks = bufferBytes / (blockSize * segmentSize);
    if (numBlocks < 2) numBlocks = 2; // NORM enforces a 2-block minimum buffer size
    return (numBlocks * blockSize);
}  // end NormSimAgent::ComputeStreamBufferSegmentCount()

unsigned int NormSimAgent::WriteToStream(const char*        buffer,
                                         unsigned int       numBytes)
{
    ASSERT(NULL != stream);
    if (!flow_control)
    {
        return stream->Write(buffer, numBytes, false);
    }
    else if (stream_buffer_count < stream_buffer_max)
    {
        // This method uses stream->Write(), but limits writes by explicit ACK-based flow control status
    // 1) How many buffer bytes are available?
        unsigned int bytesAvailable = segment_size * (stream_buffer_max - stream_buffer_count);
        bytesAvailable -= stream_bytes_remain;  // unflushed segment portiomn
        if (numBytes <= bytesAvailable) 
        {
            unsigned int totalBytes = numBytes + stream_bytes_remain;
            unsigned int numSegments = totalBytes / segment_size;
            stream_bytes_remain = totalBytes % segment_size;
            stream_buffer_count += numSegments;
        }
        else
        {
            numBytes = bytesAvailable;
            stream_buffer_count = stream_buffer_max;        
        }
        // 2) Write to the stream
        unsigned int bytesWritten = stream->Write(buffer, numBytes, false);
        
        ASSERT(bytesWritten == numBytes);  // this could fail if timer-based flow control is left enabled
        // We had a "stream is closing" case here somehow?  Need to make sure we don't try
        // to write to norm stream
        
        // 3) Do we need to issue a watermark ACK request?
        if (!watermark_pending && (stream_buffer_count >= (stream_buffer_max >> 1)))
        {
            //TRACE("NormSimAgent::WriteToStream() initiating watermark ACK request (buffer count:%lu max:%lu usage:%u)...\n",
            //            stream_buffer_count, stream_buffer_max, NormStreamGetBufferUsage(tx_stream));
            session->SenderSetWatermark(stream->GetId(), 
                                        stream->FlushBlockId(),
                                        stream->FlushSegmentId(),
                                        false);
            watermark_pending = true;
        }
        return bytesWritten;
    }
    else
    {
        PLOG(PL_DETAIL, "NormSimAgent::WriteToStream() is blocked pending acknowledgment from receiver\n");
        return 0;
    }
}  // end NormSimAgent::WriteToStream()


void NormSimAgent::SetCCMode(NormCC ccMode)
{
    cc_mode = ccMode;
    if (NULL != session)
    {
        switch (ccMode)
        {
            case NORM_FIXED:
                cc_enable = false;
                session->SetCongestionControl(false);
                session->SetEcnSupport(ecn_enable, false, false);
                break;
            case NORM_CC:
                cc_enable = true;
                session->SetCongestionControl(true);
                session->SetEcnSupport(ecn_enable, false, false);
                break;
            case NORM_CCE:
                cc_enable = true;
                session->SetCongestionControl(true);
                session->SetEcnSupport(true, true, false);
                break;
            case NORM_CCL:
                cc_enable = true;
                session->SetCongestionControl(true);
                session->SetEcnSupport(ecn_enable, false, true);
                break;
        }
    }
}  // end NormSimAgent::SetCCMode()


NormSimAgent::CmdType NormSimAgent::CommandType(const char* cmd)
{
    if (!cmd) return CMD_INVALID;
    unsigned int len = strlen(cmd);
    CmdType type = CMD_INVALID;
    const char* const* nextCmd = cmd_list;
    int matchCount = 0;
    while (NULL != *nextCmd)
    {
        if (!strncmp(cmd, *nextCmd+1, len))
        {
            if (len == strlen(*nextCmd+1))
            {
                // exact match
                return (('+' == *nextCmd[0]) ? CMD_ARG : CMD_NOARG);
            }
            matchCount++;   
            if ('+' == *nextCmd[0])
                type = CMD_ARG;
            else
                type = CMD_NOARG;
        }
        nextCmd++;
    }
    switch (matchCount)
    {
        case 0:
        case 1:
            return type;
        default:
            return CMD_INVALID;  // it was ambiguous
    }
}  // end NormSimAgent::CommandType()

bool NormSimAgent::SendMessage(unsigned int len, const char* txBuffer)
{
    if (session)
    {
        if (NULL == stream)
        {
            if(NULL == (tx_msg_buffer = new char[65536]))
            {
                PLOG(PL_FATAL, "NormSimAgent::SendMessage() error allocating tx_msg_buffer: %s\n",
                        strerror(errno));
                return false;
            } 
            if (NULL == (stream = session->QueueTxStream(tx_object_size)))
            {
                PLOG(PL_FATAL, "NormSimAgent::SendMessage() error opening stream!\n");
                return false;
            }
            auto_stream = false;  
            tx_msg_len = tx_msg_index = 0;
        }
        
        if (0 == tx_msg_len)
        {
            memcpy(tx_msg_buffer, txBuffer, len);
            tx_msg_len = len;
            OnInputReady();
            return true;
        }
        else
        {
            // Message still pending, can't send yet
            PLOG(PL_DEBUG, "NormSimAgent::SendMessage() warning: input overflow!\n");
            //ASSERT(0);
            return false;
        }
    }
    else
    {
        PLOG(PL_FATAL, "NormSimAgent::SendMessage() session not started!\n");
        return false;
    }

    return true;
}  // end NormSimAgent::SendMessage()

void NormSimAgent::OnInputReady()
{
    if (auto_stream)
    {
        // sending a dummy byte stream (fill the stream buffer up)
        char buffer[NormMsg::MAX_SIZE];
        bool inputReady = true;
        while (inputReady)
        {
            unsigned int numBytes = WriteToStream(buffer, segment_size);
            inputReady = (numBytes == segment_size);
        }
    }
    else if (tx_msg_index < tx_msg_len)
    {
        unsigned int bytesWrote;
        if (flow_control)
        {
            bytesWrote = WriteToStream(tx_msg_buffer+tx_msg_index,
                                       tx_msg_len - tx_msg_index);
        }
        else
        {
            bytesWrote = WriteToStream(tx_msg_buffer+tx_msg_index,
                                       tx_msg_len - tx_msg_index);
        }
        tx_msg_index += bytesWrote;
        if (tx_msg_index == tx_msg_len)
        {
            // Mark EOM _and_ flush (using preset flush mode)
            FlushStream(true);  
            tx_msg_index = tx_msg_len = 0;
        }   
    }
}  // end NormSimAgent::OnInputReady()

// Do an _active_ flush of the stream (for use at end-of-transmission)
bool NormSimAgent::FlushStream(bool eom)
{
    if (stream && session && session->IsSender())
    {
        stream->Flush(eom);
        if (flow_control && (NormStreamObject::FLUSH_ACTIVE == flush_mode))
        {
            if (0 != stream_bytes_remain)
            {
                stream_buffer_count++;
                stream_bytes_remain = 0;
                if (!watermark_pending && (stream_buffer_count >= (stream_buffer_max >> 1)))
                {
                    //TRACE("NormSimAgent::FlushStream() initiating watermark ACK request (buffer count:%lu max:%lu usage:%u)...\n",
                    //            stream_buffer_count, stream_buffer_max, NormStreamGetBufferUsage(tx_stream));
                    session->SenderSetWatermark(stream->GetId(), 
                                                stream->FlushBlockId(),
                                                stream->FlushSegmentId(),
                                                false);
                    watermark_pending = true;
                }
            }
        }
        return true;
    }
    else
    {
        PLOG(PL_FATAL, "NormSimAgent::FlushStream() no output stream to flush\n");
        return false;
    }   
}  // end NormSimAgent::FlushStream()
    

void NormSimAgent::Notify(NormController::Event event,
                          class NormSessionMgr* sessionMgr,
                          class NormSession*    session,
                          class NormSenderNode* sender,
                          class NormObject*     object)
{
    switch (event)
    {
       case TX_QUEUE_VACANCY:
       case TX_QUEUE_EMPTY:
          // Can queue a new object or write to stream for transmission  
          if ((NULL != object) && (object == stream))
          {
              // Stream starved, get input  
              OnInputReady();
          }
          else
          {            
              // Schedule or queue next "sim object" transmission
              if (interval_timer.GetInterval() > 0.0)
                ActivateTimer(interval_timer);            
              else
                OnIntervalTimeout(interval_timer);
          } 
          break;
          
       case TX_WATERMARK_COMPLETED:
           if (flow_control)
           {
                if (NormSession::ACK_SUCCESS == session->SenderGetAckingStatus(NORM_NODE_ANY))
                {
                    // if we've been blocking normal input, unblock it and prompt for more
                    // (we could post a vacancy notification here?
                    watermark_pending = false;
                    bool wasBlocked = stream_buffer_count >= stream_buffer_max;
                    stream_buffer_count -= (stream_buffer_max >> 1);
                    if (wasBlocked) OnInputReady();
                }
                else
                {
                    // reset watermark request
                    session->SenderResetWatermark();
                }   
           }
           break;          
          
       case TX_OBJECT_SENT:
          if ((0 != tx_requeue) && (object != stream))
          {
            if (0 != tx_requeue_count)
            {
                if (session->RequeueTxObject(object))
                {
                    if (tx_requeue_count > 0) tx_requeue_count--;
                }
                else
                {
                    // unable to requeue
                    PLOG(PL_ERROR, "NormSimAgent::Notify(TX_OBJECT_SENT) warning!: requeue attempt exceeded configured tx cache bounds!\n");
                    // reset "tx_requeue_count" for next tx object
                    tx_requeue_count = tx_requeue;
                }
            }
            else
            {
                // reset "tx_requeue_count" for next tx object
                tx_requeue_count = tx_requeue;
            }
          }
          break;
      
    case RX_OBJECT_NEW:
      {
          // It's up to the app to "accept" the object
          switch (object->GetType())
          {
              case NormObject::STREAM:
                {
                    NormObjectSize size;
                    if (silent_receiver)
                      size = NormObjectSize(rx_buffer_size);
                    else
                      size = object->GetSize();
                    if (!((NormStreamObject*)object)->Accept(size.LSB()))
                    {
                        PLOG(PL_ERROR, "NormSimAgent::Notify(RX_OBJECT_NEW) stream object accept error!\n");
                    }
                    if (!stream)
                      stream = (NormStreamObject*)object;
                    else
                      PLOG(PL_ERROR, "NormSimAgent::Notify(RX_OBJECT_NEW) warning! one stream already accepted.\n");
                }
                break;   
              case NormObject::FILE:
                {
                    if (!((NormSimObject*)object)->Accept())
                    {
                        PLOG(PL_ERROR, "NormSimAgent::Notify(RX_OBJECT_NEW) sim object accept error!\n");
                    }
                    if (NULL != log_file_ptr)
                    {
                        // Log start of NEW receive object
                        unsigned long objSize = object->GetSize().LSB();
                        ProtoTime theTime;
                        theTime.GetCurrentTime();
                        double rxTime = theTime.GetValue();
                        double sec = fmod(rxTime, 60.0);
                        int min = (int)fmod(rxTime/60.0,60.0);
                        //int hr = (int)fmod(rxTime/3600.0, 24.0);
                        int hr = (int)(rxTime/3600.0);
                        fprintf(log_file_ptr, "%02d:%02d:%lf NEW size>%lu seq>%hu\n",
                                hr, min, sec, objSize, (UINT16)object->GetId());
                    }
                }
                break;
              case NormObject::DATA: 
                PLOG(PL_ERROR, "NormSimAgent::Notify() DATA objects not _yet_ supported...\n");      
                break;
              default:
                PLOG(PL_ERROR, "NormSimAgent::Notify() INVALID object type!\n");      
                ASSERT(0);
                break;    
          }   
          break;
      }
      
    case RX_OBJECT_INFO:
      switch(object->GetType())
      {
          case NormObject::FILE:
          case NormObject::DATA:
          case NormObject::STREAM:
          default:
            break;
      }  // end switch(object->GetType())
      break;
      
    case RX_OBJECT_UPDATED:
      switch (object->GetType())
      {
          case NormObject::FILE:
            // (TBD) update progress
            break;

          case NormObject::STREAM:
            {
                // Read the stream when it's updated  
                if (msg_sink)
                {      
                    bool dataReady = true;
                    while (dataReady)  
                    {       
                        if (0 == mgen_pending_bytes)
                        {
                            // Reading (at least part of) 2 byte MGEN msg len header
                            unsigned int want = 2 - mgen_bytes;
                            unsigned int got = want;
                            bool findMsgSync = msg_sync ? false : true;
                            if ((static_cast<NormStreamObject*>(object))->Read(mgen_buffer + mgen_bytes, 
                                                                               &got, findMsgSync))
                            {
                                mgen_bytes += got;
                                msg_sync = true;
                                if (got != want) dataReady = false;
                            }
                            else
                            {
                                if (msg_sync)
                                    {
                                    PLOG(PL_WARN, "NormSimAgent::Notify(1) detected stream break\n");
                                    //ASSERT(0);
                                    mgen_bytes = mgen_pending_bytes = 0;
                                    msg_sync = false;
                                    continue;
                                }
                                else
                                {
                                    break;
                                }
                            }
                            if (2 == mgen_bytes)
                            {
                                UINT16 msgSize;
                                memcpy(&msgSize, mgen_buffer, sizeof(UINT16));
                                mgen_pending_bytes = ntohs(msgSize) - 2;  
                            }
                        }
                        if (mgen_pending_bytes)
                        {
                            // Save the first part for MGEN logging
                            if (mgen_bytes < 64)
                            {
                                unsigned int want = MIN(mgen_pending_bytes, 62);
                                unsigned int got = want;
                                if ((static_cast<NormStreamObject*>(object))->Read(mgen_buffer+mgen_bytes, 
                                                                                   &got))
                                {
                                    mgen_pending_bytes -= got;
                                    mgen_bytes += got;
                                    if (got != want) dataReady = false;
                                }
                                else
                                {
                                    PLOG(PL_WARN, "NormSimAgent::Notify(2) detected stream break\n");
                                    mgen_bytes = mgen_pending_bytes = 0;
                                    msg_sync = false;
                                    continue;
                                }
                            }
                            while (dataReady && mgen_pending_bytes)
                            {
                                char buffer[256];
                                unsigned int want = MIN(256, mgen_pending_bytes); 
                                unsigned int got = want;
                                if ((static_cast<NormStreamObject*>(object))->Read(buffer, &got))
                                {
                                    mgen_pending_bytes -= got;
                                    mgen_bytes += got;
                                    if (got != want) dataReady = false;
                                }
                                else
                                {
                                    PLOG(PL_WARN, "NormSimAgent::Notify(3) detected stream break\n");
                                    mgen_bytes = mgen_pending_bytes = 0;
                                    msg_sync = false;
                                    break;
                                }
                            }
                            if (msg_sync && (0 == mgen_pending_bytes))
                            {
                                ProtoAddress srcAddr;
                                srcAddr.ResolveFromString(sender->GetAddress().GetHostString());
                                srcAddr.SetPort(sender->GetAddress().GetPort());
                                msg_sink->HandleMessage(mgen_buffer,mgen_bytes,srcAddr);
                                mgen_bytes = 0;   
                            }
                        }  // end if (mgen_pending_bytes)
                    }  // end while(dataReady)
                }
                else
                {
                    // Simply read and discard the byte stream for sim purposes
                    char buffer[1024];
                    unsigned int want = 1024;
                    unsigned int got = want;
                    while (1)
                    {
                        if ((static_cast<NormStreamObject*>(object))->Read(buffer, &got))
                        {
                            // Break when data is no longer available
                            if (got != want) break;
                        }
                        else
                        {
                            PLOG(PL_WARN, "NormSimAgent::Notify() detected stream break\n");
                        }
                        got = want = 1024;
                    }
                }
                break;
            }

          case NormObject::DATA: 
            PLOG(PL_FATAL, "NormSimAgent::Notify() DATA objects not supported...\n");      
            ASSERT(0);
            break;

          default:
            // should never occur
            break;
      }  // end switch (object->GetType())
      break;
    case RX_OBJECT_COMPLETED:
      {
          switch(object->GetType())
          {
              case NormObject::FILE:
                //DMSG(0, "norm: Completed rx file: %s\n", ((NormFileObject*)object)->Path());
                if (NULL != log_file_ptr)
                {
                    unsigned long objSize = object->GetSize().LSB();
                    ProtoTime theTime;
                    theTime.GetCurrentTime();
                    double rxTime = theTime.GetValue();
                    double sec = fmod(rxTime, 60.0);
                    int min = (int)fmod(rxTime/60.0,60.0);
                    int hr = (int)fmod(rxTime/3600.0, 24.0);
                    fprintf(log_file_ptr, "%02d:%02d:%lf RECV size>%lu seq>%hu\n",
                            hr, min, sec, objSize, (UINT16)object->GetId());
                }
                break;
              case NormObject::STREAM:
                //DMSG(0, "norm: Completed rx stream ...\n");
                break;
              case NormObject::DATA:
                ASSERT(0);
                break;
              default:
                break;
          }
          break;
      }
      case RX_OBJECT_ABORTED:
          switch(object->GetType())
          {
              case NormObject::FILE:
                //DMSG(0, "norm: Aborted rx file: %s\n", ((NormFileObject*)object)->Path());
                if (NULL != log_file_ptr)
                {
                    unsigned long objSize = object->GetSize().LSB();
                    ProtoTime theTime;
                    theTime.GetCurrentTime();
                    double rxTime = theTime.GetValue();
                    double sec = fmod(rxTime, 60.0);
                    int min = (int)fmod(rxTime/60.0,60.0);
                    int hr = (int)fmod(rxTime/3600.0, 24.0);
                    fprintf(log_file_ptr, "%02d:%02d:%lf ABORT size>%lu seq>%hu\n",
                            hr, min, sec, objSize, (UINT16)object->GetId());
                }
                break;
              case NormObject::STREAM:
                //DMSG(0, "norm: Aborted rx stream ...\n");
                break;
              case NormObject::DATA:
                ASSERT(0);
                break;
              default:
                break;
          }
          break;
          
      default:
          PLOG(PL_DEBUG, "NormSimAgent::Notify() unhandled NormEvent type\n");
        break;
    }  // end switch(event)
}  // end NormSimAgent::Notify()


bool NormSimAgent::OnIntervalTimeout(ProtoTimer& theTimer)
{
    if (tx_repeat_count)
    {
        if (stream)
        {
            // (TBD)
        }
        else
        {
            // Queue a NORM_OBJECT_SIM as long as there are repeats
            if (tx_object_size_min > 0)
            {
                tx_object_size = tx_object_size_max - tx_object_size_min;
                tx_object_size = (unsigned long)(tx_object_size * UniformRand(1.0));
                tx_object_size += tx_object_size_min;
            }
            NormObject* object = session->QueueTxSim(tx_object_size);
            if (NULL != object)
            {
                if (tx_repeat_count > 0) tx_repeat_count--;
                PLOG(PL_DEBUG, "NormSimAgent::OnIntervalTimeout(() Queued file size: %lu bytes\n", tx_object_size);  
                if (NULL != log_file_ptr)
                {
                    ProtoTime theTime;
                    theTime.GetCurrentTime();
                    double rxTime = theTime.GetValue();
                    double sec = fmod(rxTime, 60.0);
                    int min = (int)fmod(rxTime/60.0,60.0);
                    int hr = (int)fmod(rxTime/3600.0, 24.0);
                    fprintf(log_file_ptr, "%02d:%02d:%lf SEND size>%lu seq>%hu\n",
                            hr, min, sec, tx_object_size, (UINT16)object->GetId());
                } 
            }             
            else
            {
                // Note, probably was flow controlled, so we don't decrement the repeat count here and the
                // next QUEUE_VACANCY event will trigger another attempt to enqueue
                TRACE("NormSimAgent::OnIntervalTimeout() warning: unable to enqueue tx object. (flow control?)\n");
                PLOG(PL_WARN, "NormSimAgent::OnIntervalTimeout() warning: unable to enqueue tx object. (flow control?)\n");
            }   
        }
        interval_timer.SetInterval(tx_object_interval);
        return true;
    }
    else
    {
        // Done
        if (interval_timer.IsActive()) interval_timer.Deactivate();
        return false;
    }
}  // end NormSimAgent::OnIntervalTimeout()

bool NormSimAgent::AddAckingNode(NormNodeId nodeId)
{
    if (NULL == session)
    {
        PLOG(PL_FATAL, "NormSimAgent::AddAckingNode() error: sender not started!\n");
        return false;
    }
    flow_control = true;  // assume ACK flow control is desired
    return session->SenderAddAckingNode(nodeId);
}  // end NormSimAgent::AddAckingNode()
    
bool NormSimAgent::StartSender()
{
    if (NULL != session)
    {
        PLOG(PL_FATAL, "NormSimAgent::StartSender() Error! sender or receiver already started!\n");
        return false;
    }
    // Validate our session settings
    if (NULL == address)
    {
        PLOG(PL_FATAL, "NormSimAgent::StartSender() Error! no session address given.");
        return false;
    }
    
    // Create a new session on multicast group/port
    session = session_mgr.NewSession(address, port, GetAgentId());
    if (NULL != session)
    {
        // Common session parameters
        session->SetTxRate(tx_rate);
        SetCCMode(cc_mode); // calls NormSession::SetCongestionControl() and NormSession::SetEcnSupport() as needed
        session->SetProbeCount(probe_count);
        session->SetBackoffFactor(backoff_factor);
        session->SetTrace(tracing);
        session->SetTxLoss(tx_loss);
        session->SetRxLoss(rx_loss);
        session->SenderSetGroupSize(group_size);
        session->SenderSetGrtt(grtt_estimate);        
        session->SetTxCacheBounds(tx_cache_size, tx_cache_min, tx_cache_max);
        UINT16 instanceId = (rand() * 65535) / RAND_MAX;
        if (!session->StartSender(instanceId, tx_buffer_size, segment_size, ndata, nparity))
        {
            PLOG(PL_FATAL, "NormSimAgent::OnStartup() start sender error!\n");
            session_mgr.Destroy();
            return false;
        }        
        session->SenderSetAutoParity(auto_parity);
        session->SenderSetExtraParity(extra_parity);
        return true;
    }
    else
    {
        PLOG(PL_FATAL, "NormSimAgent::StartSender() new session error!\n");
        return false;
    }
}  // end NormSimAgent::StartSender()


bool NormSimAgent::StartReceiver()
{
    
    if (NULL != session)
    {
        PLOG(PL_FATAL, "NormSimAgent::StartReceiver() Error! sender or receiver already started!\n");
        return false;
    }
    // Validate our session settings
    if (NULL == address)
    {
        PLOG(PL_FATAL, "NormSimAgent::StartReceiver() Error! no session address given.");
        return false;
    }
    
    // Create a new session on multicast group/port
    session = session_mgr.NewSession(address, port, GetAgentId());
    if (session)
    {
        // Common session parameters
        session->SetTxRate(tx_rate);
        session->SetBackoffFactor(backoff_factor);
        session->SetTrace(tracing);
        session->SetTxLoss(tx_loss);
        session->SetRxLoss(rx_loss);
        SetCCMode(cc_mode); // calls NormSession::SetCongestionControl() and NormSession::SetEcnSupport() as needed
        session->ReceiverSetUnicastNacks(unicast_nacks);
        session->ReceiverSetSilent(silent_receiver);
        session->SetRxCacheMax(rx_cache_max);
        // StartReceiver(bufferSize)
        if (!session->StartReceiver(rx_buffer_size))
        {
            PLOG(PL_FATAL, "NormSimAgent::StartReceiver() start receiver error!\n");
            session_mgr.Destroy();
            return false;
        }
        return true;
    }
    else
    {
        PLOG(PL_FATAL, "NormSimAgent::StartReceiver() new session error!\n");
        return false;
    }
}  // end NormSimAgent::StartSender()

void NormSimAgent::Stop()
{
    if (session) 
    {
        if (session->IsSender()) session->StopSender();
        if (session->IsReceiver()) session->StopReceiver();
        session_mgr.DeleteSession(session);
        session = NULL;
        stream = NULL;
    }   
    if (NULL != log_file_ptr)
    {
        fclose(log_file_ptr);
        log_file_ptr = NULL;
    }
}  // end NormSimAgent::StopSender()



