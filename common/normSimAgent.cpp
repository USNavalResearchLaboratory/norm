#include "normSimAgent.h"

#include <errno.h>

NormSimAgent::NormSimAgent()
 : session(NULL), stream(NULL),
   address(NULL), port(0), ttl(3), 
   tx_rate(NormSession::DEFAULT_TRANSMIT_RATE),
   backoff_factor(NormSession::DEFAULT_BACKOFF_FACTOR),
   segment_size(1024), ndata(32), nparity(16), auto_parity(0),
   group_size(NormSession::DEFAULT_GSIZE_ESTIMATE),
   tx_buffer_size(1024*1024), rx_buffer_size(1024*1024),
   tx_object_size(0), tx_object_interval(0.0), 
   tx_repeat_count(0), tx_repeat_interval(0.0),
   tracing(false), tx_loss(0.0), rx_loss(0.0)
{
    // Bind NormSessionMgr to this agent and simulation environment
    session_mgr.Init(ProtoSimAgent::TimerInstaller, this,
                     ProtoSimAgent::SocketInstaller, this,
                     static_cast<NormController*>(this));
    
    interval_timer.Init(0.0, 0, (ProtocolTimerOwner*)this, 
                        (ProtocolTimeoutFunc)&NormSimAgent::OnIntervalTimeout);
}

NormSimAgent::~NormSimAgent()
{
    if (address) delete address;
}


const char* const NormSimAgent::cmd_list[] = 
{
    "+debug",
    "+log",
    "-trace",
    "+txloss",
    "+rxloss",
    "+address",
    "+ttl",
    "+rate",
    "+backoff",
    "+input",
    "+output",
    "+interval",
    "+repeat",
    "+rinterval",  // repeat interval
    "+segment",
    "+block",
    "+parity",
    "+auto",
    "+gsize",
    "+txbuffer",
    "+rxbuffer",
    "+start",
    "-stop",
    "+sendFile",
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
    else if (!strncmp("backoff", cmd, len))
    {
        double backoffFactor = atof(val);
        if (backoffFactor < 0)
        {
            DMSG(0, "NormSimAgent::ProcessCommand(backoff) invalid txRate!\n");   
            return false;
        }
        backoff_factor = backoffFactor;
        if (session) session->SetTxRate(backoffFactor);
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
            if (interval_timer.Interval() > 0.0)
            {
                InstallTimer(interval_timer);            
            }
            else
            {
                OnIntervalTimeout();
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
                    const NormObjectSize& size = object->Size();
                    if (!((NormStreamObject*)object)->Accept(size.LSB()))
                    {
                        DMSG(0, "NormSimAgent::Notify(RX_OBJECT_NEW) stream object accept error!\n");
                    }
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
                    char buffer[256];
                    unsigned int nBytes;
                    while ((nBytes = ((NormStreamObject*)object)->Read(buffer, 256)))
                    {
                        
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


bool NormSimAgent::OnIntervalTimeout()
{
    if (tx_repeat_count)
    {
        if (stream)
        {
            
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
        interval_timer.Deactivate();
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
        session->SetBackoffFactor(backoff_factor);
        session->SetTrace(tracing);
        session->SetTxLoss(tx_loss);
        session->SetRxLoss(rx_loss);
        
        // StartServer(bufferMax, segmentSize, fec_ndata, fec_nparity)
        if (!session->StartServer(tx_buffer_size, segment_size, ndata, nparity))
        {
            DMSG(0, "NormSimAgent::OnStartup() start server error!\n");
            session_mgr.Destroy();
            return false;
        }
        session->ServerSetAutoParity(auto_parity);
         session->ServerSetGroupSize(group_size);
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
    }   
}  // end NormSimAgent::StopServer()



