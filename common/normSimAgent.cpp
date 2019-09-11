#include "normSimAgent.h"

#include <errno.h>

// This NORM simulation agent includes support for providing transport
// of a message stream from the MGEN simulation agent with restrictions.  The
// current restriction is the MGEN simulation agent

NormSimAgent::NormSimAgent(ProtoTimerMgr&         timerMgr,
                           ProtoSocket::Notifier& socketNotifier)
 : session_mgr(timerMgr, socketNotifier), session(NULL),
   address(NULL), port(0), ttl(3), 
   tx_rate(NormSession::DEFAULT_TRANSMIT_RATE), 
   cc_enable(false), unicast_nacks(false), silent_client(false),
   backoff_factor(NormSession::DEFAULT_BACKOFF_FACTOR),
   segment_size(1024), ndata(32), nparity(16), auto_parity(0), extra_parity(0),
   group_size(NormSession::DEFAULT_GSIZE_ESTIMATE),
   tx_buffer_size(1024*1024), rx_buffer_size(1024*1024),
   tx_object_size(0), tx_object_interval(0.0), 
   tx_repeat_count(0), tx_repeat_interval(0.0),
   stream(NULL), auto_stream(false), push_stream(false),
   mgen(NULL), msg_sync(false), mgen_bytes(0), mgen_pending_bytes(0),
   tracing(false), tx_loss(0.0), rx_loss(0.0)
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
    "+txloss",       // tx packet loss percent
    "+rxloss",       // rx packet loss percent
    "+address",      // session dest addres
    "+ttl",          // multicast ttl
    "+rate",         // tx rate
    "+cc",           // congestion control on/off
    "+backoff",      // backoff factor 'k' (maxBackoff = k * GRTT)
    "+input",        // stream input
    "+output",       // stream output
    "+interval",     // delay between tx objects
    "+repeat",       // number of times to repeat tx object set
    "+rinterval",    // repeat interval
    "+segment",      // server segment size
    "+block",        // server blocking size
    "+parity",       // server parity segments calculated per block
    "+auto",         // server auto parity count
    "+extra",        // server extra parity count
    "+gsize",        // group size estimate
    "+txbuffer",     // tx buffer size (bytes)
    "+rxbuffer",     // rx buffer size (bytes)
    "+start",        // open session and beging activity
    "-stop",         // cease activity and close session
    "+sendFile",     // queue a "sim" file for transmission
    "+sendStream",   // send a simulated NORM stream
    "+openStream",   // open a stream object for messaging
    "+push",         // "on" means real-time push stream advancement (non-blocking)
    "-flushStream",  // flush output stream
    "+unicastNacks", // clients will unicast feedback
    "+silentClient", // clients will not transmit
    NULL         
};
    
bool NormSimAgent::ProcessCommand(const char* cmd, const char* val)
{
    CmdType type = CommandType(cmd);
    ASSERT(CMD_INVALID != type);
    unsigned int len = strlen(cmd);
    if ((CMD_ARG == type) && !val)
    {
        DMSG(0, "NormSimAgent::ProcessCommand(%s) missing argument\n", cmd);
        return false;        
    }
    
    if (!strncmp("debug", cmd, len))
    {
        
        int debugLevel = atoi(val);
        if ((debugLevel < 0) || (debugLevel > 12))
        {
            DMSG(0, "NormSimAgent::ProcessCommand(segment) invalid debug level!\n");   
            return false;
        }
        SetDebugLevel(debugLevel);
    }
    else if (!strncmp("log", cmd, len))
    {
        OpenDebugLog(val);
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
            DMSG(0, "NormSimAgent::ProcessCommand(txloss) invalid txRate!\n");   
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
            DMSG(0, "NormSimAgent::ProcessCommand(rxloss) invalid txRate!\n");   
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
            DMSG(0, "NormSimAgent::ProcessCommand(address) allocation error:%s\n",
                strerror(errno)); 
            return false;
        }
        strcpy(address, val);
        char* ptr = strchr(address, '/');
        if (!ptr)
        {
            delete address;
            address = NULL;
            DMSG(0, "NormSimAgent::ProcessCommand(address) missing port number!\n");   
            return false;
        }
        *ptr++ = '\0';
        int portNum = atoi(ptr);
        if ((portNum < 1) || (portNum > 65535))
        {
            delete address;
            address = NULL;
            DMSG(0, "NormSimAgent::ProcessCommand(address) invalid port number!\n");   
            return false;
        }
        port = portNum;
    }
    else if (!strncmp("ttl", cmd, len))
    {
        int ttlTemp = atoi(val);
        if ((ttlTemp < 1) || (ttlTemp > 255))
        {
            DMSG(0, "NormSimAgent::ProcessCommand(ttl) invalid value!\n");   
            return false;
        }
        ttl = ttlTemp;
    }
    else if (!strncmp("rate", cmd, len))
    {
        double txRate = atof(val);
        if (txRate < 0)
        {
            DMSG(0, "NormSimAgent::ProcessCommand(rate) invalid txRate!\n");   
            return false;
        }
        tx_rate = txRate;
        if (session) session->SetTxRate(txRate);
    }
    else if (!strncmp("cc", cmd, len))
    {
        bool ccEnable;
        if (!strcmp(val, "on"))
            ccEnable = true;
        else if (!strcmp(val, "off"))
            ccEnable = false;
        else
        {
            DMSG(0, "NormSimAgent::ProcessCommand(cc) invalid argument!\n");   
            return false;
        }        
        cc_enable = ccEnable;
        if (session) session->SetCongestionControl(ccEnable);
        return true;
    }
    else if (!strncmp("backoff", cmd, len))
    {
        double backoffFactor = atof(val);
        if (backoffFactor < 0)
        {
            DMSG(0, "NormSimAgent::ProcessCommand(backoff) invalid txRate!\n");   
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
            DMSG(0, "NormSimAgent::ProcessCommand(interval) Invalid tx object interval: %s\n",
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
        if ((segmentSize < 0) || (segmentSize > 8000))
        {
            DMSG(0, "NormSimAgent::ProcessCommand(segment) invalid segment size!\n");   
            return false;
        }
        segment_size = segmentSize;
    }
    else if (!strncmp("block", cmd, len))
    {
        int blockSize = atoi(val);
        if ((blockSize < 1) || (blockSize > 255))
        {
            DMSG(0, "NormSimAgent::ProcessCommand(block) invalid block size!\n");   
            return false;
        }
        ndata = blockSize;
    }
    else if (!strncmp("parity", cmd, len))
    {
        int numParity = atoi(val);
        if ((numParity < 0) || (numParity > 254))
        {
            DMSG(0, "NormSimAgent::ProcessCommand(parity) invalid value!\n");   
            return false;
        }
        nparity = numParity;
    }
    else if (!strncmp("auto", cmd, len))
    {
        int autoParity = atoi(val);
        if ((autoParity < 0) || (autoParity > 254))
        {
            DMSG(0, "NormSimAgent::ProcessCommand(auto) invalid value!\n");   
            return false;
        }
        auto_parity = autoParity;
        if (session) session->ServerSetAutoParity(autoParity);
    }
    else if (!strncmp("extra", cmd, len))
    {
        int extraParity = atoi(val);
        if ((extraParity < 0) || (extraParity > 254))
        {
            DMSG(0, "NormSimAgent::ProcessCommand(extra) invalid value!\n");   
            return false;
        }
        extra_parity = extraParity;
        if (session) session->ServerSetExtraParity(extraParity);
    }
    else if (!strncmp("gsize", cmd, len))
    {
        if (1 != sscanf(val, "%lf", &group_size))
        {
            DMSG(0, "NormSimAgent::ProcessCommand(gize) invalid value!\n");   
            return false;
        }
        if (session) session->ServerSetGroupSize(group_size);
    }
    else if (!strncmp("txbuffer", cmd, len))
    {
        if (1 != sscanf(val, "%lu", &tx_buffer_size))
        {
            DMSG(0, "NormSimAgent::ProcessCommand(txbuffer) invalid value!\n");   
            return false;
        }
    }
    else if (!strncmp("rxbuffer", cmd, len))
    {
        if (1 != sscanf(val, "%lu", &rx_buffer_size))
        {
            DMSG(0, "NormSimAgent::ProcessCommand(rxbuffer) invalid value!\n");   
            return false;
        }
    }
    else if (!strncmp("start", cmd, len))
    {
        if (!strcmp("server", val))
        {
            return StartServer();
        }
        else if (!strcmp("client", val))
        {
           return StartClient(); 
        }
        else
        {
            DMSG(0, "NormSimAgent::ProcessCommand(start) invalid value!\n");   
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
            DMSG(0, "NormSimAgent::ProcessCommand(sendFile) invalid size!\n");   
            return false;
        }
        if (session)
        {
            return (NULL != session->QueueTxSim(tx_object_size));
        }
        else
        {
            DMSG(0, "NormSimAgent::ProcessCommand(sendFile) no session started!\n");
            return false;
        }   
    }  
    else if (!strncmp("sendStream", cmd, len))
    {
        if (session)
        {
            if (stream)
            {
                DMSG(0, "NormSimAgent::ProcessCommand(sendStream) stream already open!\n");   
                return false;
            }
            if (1 != sscanf(val, "%lu", &tx_object_size))
            {
                DMSG(0, "NormSimAgent::ProcessCommand(sendStream) invalid buffer size!\n");   
                return false;
            }
            if (!(stream = session->QueueTxStream(tx_object_size)))
            {
                DMSG(0, "NormSimAgent::ProcessCommand(sendStream) error opening stream!\n");
                return false;
            }
            auto_stream = true;
        }
        else
        {
            DMSG(0, "NormSimAgent::ProcessCommand(sendStream) session not started!\n");
            return false;
        }   
    }  
    else if (!strncmp("openStream", cmd, len))
    {
        if (session)
        {
            if (stream)
            {
                DMSG(0, "NormSimAgent::ProcessCommand(openStream) stream already open!\n");   
                return false;
            }
            if (1 != sscanf(val, "%lu", &tx_object_size))
            {
                DMSG(0, "NormSimAgent::ProcessCommand(openStream) invalid buffer size!\n");   
                return false;
            }
            if(!(tx_msg_buffer = new char[65536]))
            {
                DMSG(0, "NormSimAgent::ProcessCommand(openStream) error allocating tx_msg_buffer: %s\n",
                        strerror(errno));
                return false;
            } 
            if (!(stream = session->QueueTxStream(tx_object_size)))
            {
                DMSG(0, "NormSimAgent::ProcessCommand(openStream) error opening stream!\n");
                return false;
            }
            auto_stream = false;  
            tx_msg_len = tx_msg_index = 0;
        }
        else
        {
            DMSG(0, "NormSimAgent::ProcessCommand(openStream) session not started!\n");
            return false;
        }   
    }  
    else if (!strncmp("flushStream", cmd, len))
    {
        if (session)
        {
            return FlushStream();
        }
        else
        {
            DMSG(0, "NormSimAgent::ProcessCommand(flushStream) session not started!\n");
            return false;
        }          
    }
    else if (!strncmp("push", cmd, len))
    {
        if (!strcmp(val, "on"))
            push_stream = true;
        else if (!strcmp(val, "off"))
            push_stream = false;
        else
        {
            DMSG(0, "NormSimAgent::ProcessCommand(push) invalid argument!\n");   
            return false;
        }        
        return true;
    }
    else if (!strncmp("unicastNacks", cmd, len))
    {
        if (!strcmp(val, "on"))
            unicast_nacks = true;
        else if (!strcmp(val, "off"))
            unicast_nacks = false;
        else
        {
            DMSG(0, "NormSimAgent::ProcessCommand(unicastNacks) invalid argument!\n");   
            return false;
        }        
        if (session) session->SetUnicastNacks(unicast_nacks);
        return true;
    }
    else if (!strncmp("silentClient", cmd, len))
    {
        if (!strcmp(val, "on"))
            silent_client = true;
        else if (!strcmp(val, "off"))
            silent_client = false;
        else
        {
            DMSG(0, "NormSimAgent::ProcessCommand(silentClient) invalid argument!\n");   
            return false;
        }        
        if (session) session->ClientSetSilent(silent_client);
        return true;
    }
    return true;
}  // end NormSimAgent::ProcessCommand()


NormSimAgent::CmdType NormSimAgent::CommandType(const char* cmd)
{
    if (!cmd) return CMD_INVALID;
    unsigned int len = strlen(cmd);
    bool matched = false;
    CmdType type = CMD_INVALID;
    const char* const* nextCmd = cmd_list;
    while (*nextCmd)
    {
        if (!strncmp(cmd, *nextCmd+1, len))
        {
            if (matched)
            {
                // ambiguous command (command should match only once)
                return CMD_INVALID;
            }
            else
            {
                matched = true;   
                if ('+' == *nextCmd[0])
                    type = CMD_ARG;
                else
                    type = CMD_NOARG;
            }
        }
        nextCmd++;
    }
    return type;
}  // end NormSimAgent::CommandType()

bool NormSimAgent::SendMessage(unsigned int len, const char* txBuffer)
{
    if (session)
    {
        if (!stream)
        {
            if(!(tx_msg_buffer = new char[65536]))
            {
                DMSG(0, "NormSimAgent::SendMessage() error allocating tx_msg_buffer: %s\n",
                        strerror(errno));
                return false;
            } 
            if (!(stream = session->QueueTxStream(tx_object_size)))
            {
                DMSG(0, "NormSimAgent::SendMessage() error opening stream!\n");
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
            DMSG(0, "NormSimAgent::SendMessage() input overflow!\n");
            ASSERT(0);
            return false;
        }
    }
    else
    {
        DMSG(0, "NormSimAgent::SendMessage() session not started!\n");
        return false;
    }
}  // end NormSimAgent::SendMessage()

void NormSimAgent::OnInputReady()
{
    //TRACE("NormSimAgent::OnInputReady() index:%lu len:%lu\n",
    //        tx_msg_index, tx_msg_len);
    if (tx_msg_index < tx_msg_len)
    {
        unsigned int bytesWrote = stream->Write(tx_msg_buffer+tx_msg_index,
                                                tx_msg_len - tx_msg_index,
                                                false, false, push_stream);
        tx_msg_index += bytesWrote;
        if (tx_msg_index == tx_msg_len)
        {
            // Provide EOM indication to norm stream
            stream->Write(NULL, 0, false, true, false);   
            tx_msg_index = tx_msg_len = 0;
        }   
    }
}  // end NormSimAgent::InputReady()

bool NormSimAgent::FlushStream()
{
    if (stream && session && session->IsServer())
    {
        stream->Write(NULL, 0, true, false, false);
        return true;
    }
    else
    {
        DMSG(0, "NormSimAgent::FlushStream() no output stream to flush\n");
    }   
}  // end NormSimAgent::FlushStream()
    

void NormSimAgent::Notify(NormController::Event event,
                     class NormSessionMgr* sessionMgr,
                     class NormSession*    session,
                     class NormServerNode* server,
                     class NormObject*     object)
{
    switch (event)
    {
        case TX_QUEUE_EMPTY:
            // Can queue a new object or write to stream for transmission  
            if (object && (object == stream))
            {
                if (auto_stream)
                {
                    // sending a dummy byte stream
                    char buffer[NormMsg::MAX_SIZE];
                    unsigned int count = stream->Write(buffer, segment_size, false, false, false);
                }
                else
                {
                    // Stream starved, ask for input from "source" ?   
                    OnInputReady();
                }
            }
            else
            {            
                // Schedule or queue next "sim file" transmission
                if (interval_timer.GetInterval() > 0.0)
                {
                    ActivateTimer(interval_timer);            
                }
                else
                {
                    OnIntervalTimeout(interval_timer);
                }
            } 
            break;
           
        case RX_OBJECT_NEW:
        {
            //TRACE("NormSimAgent::Notify(RX_OBJECT_NEW) ...\n");
            // It's up to the app to "accept" the object
            switch (object->GetType())
            {
                case NormObject::STREAM:
                {
                    NormObjectSize size;
                    if (silent_client)
                        size = NormObjectSize(rx_buffer_size);
                    else
                        size = object->Size();
                    if (!((NormStreamObject*)object)->Accept(size.LSB()))
                    {
                        DMSG(0, "NormSimAgent::Notify(RX_OBJECT_NEW) stream object accept error!\n");
                    }
                    if (!stream)
                        stream = (NormStreamObject*)object;
                    else
                        DMSG(0, "NormSimAgent::Notify(RX_OBJECT_NEW) warning! one stream already accepted.\n");
                }
                break;   
                case NormObject::FILE:
                {
                    if (!((NormSimObject*)object)->Accept())
                    {
                        DMSG(0, "NormSimAgent::Notify(RX_OBJECT_NEW) sim object accept error!\n");
                    }
                }
                break;
                case NormObject::DATA: 
                    DMSG(0, "NormSimAgent::Notify() FILE/DATA objects not _yet_ supported...\n");      
                    break;
            }   
            break;
        }
            
        case RX_OBJECT_INFO:
            //TRACE("NormSimAgent::Notify(RX_OBJECT_INFO) ...\n");
            switch(object->GetType())
            {
                case NormObject::FILE:
                case NormObject::DATA:
                case NormObject::STREAM:
                    break;
            }  // end switch(object->GetType())
            break;
            
        case RX_OBJECT_UPDATE:
            //TRACE("NormSimAgent::Notify(RX_OBJECT_UPDATE) ...\n");
            switch (object->GetType())
            {
                case NormObject::FILE:
                    // (TBD) update progress
                    break;
                
                case NormObject::STREAM:
                {
                    // Read the stream when it's updated  
                    if (mgen)
                    {        
                        bool dataReady = true;
                        while (dataReady)  
                        {             
                            if (!mgen_pending_bytes)
                            {
                                // Read 2 byte MGEN msg len header
                                unsigned int want = 2 - mgen_bytes;
                                unsigned int got = want;
                                bool findMsgSync = msg_sync ? false : true;
                                if (((NormStreamObject*)object)->Read(mgen_buffer + mgen_bytes, 
                                                                      &got, findMsgSync))
                                {
                                    mgen_bytes += got;
                                    msg_sync = true;
                                    if (got != want) dataReady = false;
                                }
                                else
                                {
                                    DMSG(0, "NormSimAgent::Notify(1) detected stream break\n");
                                    mgen_bytes = mgen_pending_bytes = 0;
                                    msg_sync = false;
                                    continue;
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
                                    if (((NormStreamObject*)object)->Read(mgen_buffer+mgen_bytes, 
                                                                          &got))
                                    {
                                        mgen_pending_bytes -= got;
                                        mgen_bytes += got;
                                        if (got != want) dataReady = false;
                                    }
                                    else
                                    {
                                        DMSG(0, "NormSimAgent::Notify(2) detected stream break\n");
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
                                    if (((NormStreamObject*)object)->Read(buffer, &got))
                                    {
                                        mgen_pending_bytes -= got;
                                        mgen_bytes += got;
                                        if (got != want) dataReady = false;
                                    }
                                    else
                                    {
                                        DMSG(0, "NormSimAgent::Notify(3) detected stream break\n");
                                        mgen_bytes = mgen_pending_bytes = 0;
                                        msg_sync = false;
                                        break;
                                    }
                                }
                                if (msg_sync && (0 == mgen_pending_bytes))
                                {
                                    ProtoAddress srcAddr;
                                    srcAddr.ResolveFromString(server->GetAddress().GetHostString());
                                    srcAddr.SetPort(server->GetAddress().GetPort());
                                    mgen->HandleMgenMessage(mgen_buffer, mgen_bytes, srcAddr);
                                    mgen_bytes = 0;   
                                }
                            }  // end if (mgen_pending_bytes)
                        }  // end while(dataReady)
                    }
                    else
                    {
                        char buffer[1024];
                        unsigned int want = 1024;
                        unsigned int got = want;
                        while (1)
                        {
                            if (((NormStreamObject*)object)->Read(buffer, &got))
                            {
                                // Break when data is no longer available
                                if (got != want) break;
                            }
                            else
                            {
                                DMSG(0, "NormSimAgent::Notify() detected stream break\n");
                            }
                            got = want = 1024;
                        }
                    }
                    break;
                }
                                        
                case NormObject::DATA: 
                    DMSG(0, "NormSimAgent::Notify() FILE/DATA objects not _yet_ supported...\n");      
                    break;
            }  // end switch (object->GetType())
            break;
        case RX_OBJECT_COMPLETE:
        {
            switch(object->GetType())
            {
                case NormObject::FILE:
                    //DMSG(0, "norm: Completed rx file: %s\n", ((NormFileObject*)object)->Path());
                    break;
                    
                case NormObject::STREAM:
                    ASSERT(0);
                    break;
                case NormObject::DATA:
                    ASSERT(0);
                    break;
            }
            break;
        }
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
            if (tx_repeat_count > 0) tx_repeat_count--;  
            if (!session->QueueTxSim(tx_object_size))
            {
                DMSG(0, "NormSimAgent::OnIntervalTimeout() Error queueing tx object.\n");
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
    

bool NormSimAgent::StartServer()
{
    if (session)
    {
        DMSG(0, "NormSimAgent::StartServer() Error! server or client already started!\n");
        return false;
    }
    // Validate our session settings
    if (!address)
    {
        DMSG(0, "NormSimAgent::StartServer() Error! no session address given.");
        return false;
    }
    
    // Create a new session on multicast group/port
    session = session_mgr.NewSession(address, port, GetAgentId());
    if (session)
    {
        // Common session parameters
        session->SetTxRate(tx_rate);
        session->SetCongestionControl(cc_enable);
        session->SetBackoffFactor(backoff_factor);
        session->SetTrace(tracing);
        session->SetTxLoss(tx_loss);
        session->SetRxLoss(rx_loss);
        session->ServerSetGroupSize(group_size);        
        // StartServer(bufferMax, segmentSize, fec_ndata, fec_nparity)
        if (!session->StartServer(tx_buffer_size, segment_size, ndata, nparity))
        {
            DMSG(0, "NormSimAgent::OnStartup() start server error!\n");
            session_mgr.Destroy();
            return false;
        }        
        session->ServerSetAutoParity(auto_parity);
        session->ServerSetExtraParity(extra_parity);
        return true;
    }
    else
    {
        DMSG(0, "NormSimAgent::StartServer() new session error!\n");
        return false;
    }
}  // end NormSimAgent::StartServer()


bool NormSimAgent::StartClient()
{
    
    if (session)
    {
        DMSG(0, "NormSimAgent::StartClient() Error! server or client already started!\n");
        return false;
    }
    // Validate our session settings
    if (!address)
    {
        DMSG(0, "NormSimAgent::StartClient() Error! no session address given.");
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
        
        session->SetUnicastNacks(unicast_nacks);
        session->ClientSetSilent(silent_client);
        
        // StartClient(bufferSize)
        if (!session->StartClient(rx_buffer_size))
        {
            DMSG(0, "NormSimAgent::StartClient() start client error!\n");
            session_mgr.Destroy();
            return false;
        }
        return true;
    }
    else
    {
        DMSG(0, "NormSimAgent::StartClient() new session error!\n");
        return false;
    }
}  // end NormSimAgent::StartServer()

void NormSimAgent::Stop()
{
    if (session) 
    {
        if (session->IsServer()) session->StopServer();
        if (session->IsClient()) session->StopClient();
        session_mgr.DeleteSession(session);
        session = NULL;
        stream = NULL;
    }   
}  // end NormSimAgent::StopServer()



