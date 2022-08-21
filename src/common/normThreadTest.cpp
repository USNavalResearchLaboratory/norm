// This is a test application for experimenting
// with the NORM API implementation during its
// development.  A better-documented and complete
// example of the NORM API usage will be provided
// when the NORM API is more complete.

// This tests the use of the NORM API in a multi-threaded app

#include "normApi.h"
#include "protokit.h"  // for protolib debug, stuff, etc

#include <stdio.h>
#include <stdlib.h>  // for srand()

#ifdef UNIX
#include <unistd.h>  // for "sleep()"
#endif // UNIX


class NormThreadApp : public ProtoApp
{
    public:
        NormThreadApp();
        virtual ~NormThreadApp();
        
        // Overrides from ProtoApp or NsProtoSimAgent base
        bool OnStartup(int argc, const char*const* argv);
        bool ProcessCommands(int argc, const char*const* argv);
        void OnShutdown();
        
        bool OnCommand(const char* cmd, const char* val);
            
    private:
        enum CmdType {CMD_INVALID, CMD_NOARG, CMD_ARG};
        CmdType CommandType(const char* cmd);
        static const char* const cmd_list[];
        
        static void DoNormEvent(ProtoDispatcher::Descriptor descriptor, 
                                ProtoDispatcher::Event      theEvent, 
                                const void*                 userData);
        
        void OnNormEvent();
        
        static void DoWorkerEvent(const void* receiverData);
        void WorkerReadStream();
        
        enum {MSG_SIZE_MAX = 1024};
        
        bool OnTxTimeout(ProtoTimer& timer);
        
        // Our NORM API handles
        NormInstanceHandle  norm_instance;
        NormSessionHandle   norm_session;
        NormObjectHandle    norm_tx_stream;   
        NormObjectHandle    norm_rx_stream;    
        
        bool                sender;
        bool                receiver;
        
        // This timer controls our message tx rate
        ProtoTimer          tx_msg_timer;
        char                tx_msg_buffer[MSG_SIZE_MAX];
        unsigned int        tx_msg_length;
        unsigned int        tx_msg_index;
        
        // We use a ProtoDispatcher for a "worker" thread
        // to handle NORM events for a specific session
        ProtoDispatcher worker_thread_dispatcher;
        
            
    
};  // end class NormThreadApp

// Our application instance 
PROTO_INSTANTIATE_APP(NormThreadApp)
        
NormThreadApp::NormThreadApp()
 : norm_instance(NORM_INSTANCE_INVALID),
   norm_session(NORM_SESSION_INVALID),
   norm_tx_stream(NORM_OBJECT_INVALID), norm_rx_stream(NORM_OBJECT_INVALID),
   tx_msg_length(0), tx_msg_index(0)
{
    tx_msg_timer.SetListener(this, &NormThreadApp::OnTxTimeout);
    tx_msg_timer.SetInterval(0.00001);
    tx_msg_timer.SetRepeat(-1);
    
    worker_thread_dispatcher.SetPromptCallback(DoWorkerEvent, this);
    
}

NormThreadApp::~NormThreadApp()
{
    
}

bool NormThreadApp::OnStartup(int argc, const char*const* argv)
{
    // 1) (TBD) Process any command-line options
    
    if (!ProcessCommands(argc, argv))
    {
        TRACE("error with commands\n");
        return false;
    }
    
    // 2) Create a Norm API instance and "generic" input handler for notifications
    norm_instance = NormCreateInstance();
    ASSERT(NORM_INSTANCE_INVALID != norm_instance);
    
    SetDebugLevel(3);
    
    // Set a callback that will call NormGetNextEvent()
    if (!dispatcher.InstallGenericInput(NormGetDescriptor(norm_instance), DoNormEvent, this))
    {
        PLOG(PL_FATAL, "NormThreadApp::OnStartup() InstallGenericInput() error\n");
        NormDestroyInstance(norm_instance);
        return false;
    }
    
    
    // 3) Create a "custom" unique node identifier
    // Here's a trick to generate a _hopefully_ unique NormNodeId
    // based on an XOR of the system's IP address and the process id.
    // (We use ProtoAddress::GetEndIdentifier() to get the local
    //  "default" IP address for the system)
    // (Note that passing "NORM_NODE_ANY" to the last arg of 
    //  NormCreateSession() does a similar thing but without
    //  the processId XOR ... perhaps we should add the processId
    //  hack to that default "NORM_NODE_ANY" local NormNodeId picker???
    ProtoAddress localAddr;
    if (!localAddr.ResolveLocalAddress())
    {
        fprintf(stderr, "normTest: error resolving local IP address\n");
        OnShutdown();
        return false;
    }
    NormNodeId localId = localAddr.EndIdentifier();
#ifdef WIN32
    DWORD processId = GetCurrentProcessId();
#else
    pid_t processId = getpid();
#endif // if/else WIN32/UNIX
    localId ^= (NormNodeId)processId;
    
    // If needed, permutate to a valid, random NormNodeId
    while ((NORM_NODE_ANY == localId) ||
           (NORM_NODE_NONE == localId))
    {
        localId ^= (NormNodeId)rand();
    }
    //localId = 15;  // for testing purposes

    // 4) Create a NORM session
    norm_session = NormCreateSession(norm_instance,
                                    "224.1.1.1", 
                                     6001,
                                     localId);
    ASSERT(NORM_SESSION_INVALID != norm_session);
    
    if(!NormSetMulticastInterface(norm_session,"eth0"))
    {
        fprintf(stderr, "normTest: Unable to set multicast interface to \"eth0\"\n");
        //return false;
    }
    
    NormSetGrttEstimate(norm_session, 0.250);  // 1 msec initial grtt
    
    NormSetTxRate(norm_session, 80.0e+06);  // in bits/second
    
    
    NormSetTxRateBounds(norm_session, 10.0e+06, 10.0e+06);
    
    NormSetCongestionControl(norm_session, true);
    
    NormSetTxLoss(norm_session, 2.0);
    
    //NormSetMessageTrace(norm_session, true);
    
    //NormSetLoopback(norm_session, true);  
    
    // 5) If sender pick a random "session id", start sender, and create a stream
    // We use a random "sessionId"
    if (sender)
    {
        NormSessionId sessionId = (NormSessionId)rand();
        //NormStartSender(norm_session, sessionId, 1024*1024, 1400, 64, 8);
        NormStartSender(norm_session, sessionId, 2000000, 1400, 64, 8);
        norm_tx_stream = NormStreamOpen(norm_session, 1800000);
    
        // Activate tx timer and force first message transmission
        ActivateTimer(tx_msg_timer);
        OnTxTimeout(tx_msg_timer);
    }
    
    
    // 6) If receiver, start receiver
    if (receiver)
    {    
        worker_thread_dispatcher.StartThread();
        NormStartReceiver(norm_session, 2000000); 
    }
    
    return true;
}  // end NormThreadApp::OnStartup()

void NormThreadApp::OnShutdown()
{
    if (tx_msg_timer.IsActive()) tx_msg_timer.Deactivate();
    
    if (NORM_INSTANCE_INVALID != norm_instance)
    {
        dispatcher.RemoveGenericInput(NormGetDescriptor(norm_instance));
        NormDestroyInstance(norm_instance);
        norm_instance = NORM_INSTANCE_INVALID;
    }
    
    
    worker_thread_dispatcher.Stop();   
}  // end NormThreadApp::OnShutdown()

bool NormThreadApp::OnTxTimeout(ProtoTimer& /*timer*/)
{
    //TRACE("enter NormThreadApp::OnTxTimeout() ...\n");
    while (1)
    {
        if (0 == tx_msg_length)
        {
            // Send a new message
            unsigned int sendCount = 1;
            sprintf(tx_msg_buffer, "normThreadTest says hello %u ", sendCount);
            unsigned int msgLength = strlen(tx_msg_buffer);
            memset(tx_msg_buffer + msgLength, 'a', 900 - msgLength);
            tx_msg_length = 900;
            tx_msg_index = 0;
        }

        unsigned int bytesHave = tx_msg_length - tx_msg_index;
        unsigned int bytesWritten =
            NormStreamWrite(norm_tx_stream, tx_msg_buffer + tx_msg_index, tx_msg_length - tx_msg_index);
        //TRACE("wrote %u bytes to stream ...\n", bytesWritten);
        if (bytesWritten == bytesHave)
        {

            tx_msg_length = 0;
        }
        else
        {
            if (0 == bytesWritten)
            {
                TRACE("ZERO bytes written\n");
            }
            // We filled the stream buffer
            tx_msg_index += bytesWritten;
            if (tx_msg_timer.IsActive()) tx_msg_timer.Deactivate();
            break;
        }
    }
    return true;   
}  // end NormThreadApp::OnTxTimeout()

void NormThreadApp::DoNormEvent(ProtoDispatcher::Descriptor descriptor, 
                                ProtoDispatcher::Event      theEvent, 
                                const void*                 userData)
{
    NormThreadApp* theApp = reinterpret_cast<NormThreadApp*>((void*)userData);
    theApp->OnNormEvent();
}  // end NormThreadApp::DoNormEvent()
        
void NormThreadApp::OnNormEvent()
{
    static unsigned long updateCount = 0;
    NormEvent theEvent;
    if (NormGetNextEvent(norm_instance, &theEvent))
    {
        switch (theEvent.type)
        {
            case NORM_TX_QUEUE_EMPTY:
            case NORM_TX_QUEUE_VACANCY:
            
                /*if (NORM_TX_QUEUE_VACANCY == theEvent.type)
                    TRACE("NORM_TX_QUEUE_VACANCY ...\n");
                else
                    TRACE("NORM_TX_QUEUE_EMPTY ...\n");*/
                if (!tx_msg_timer.IsActive())
                {
                    //ActivateTimer(tx_msg_timer);
                    OnTxTimeout(tx_msg_timer);   
                }
                break;
                
            case NORM_GRTT_UPDATED:
                break;
                
            case NORM_CC_ACTIVE:
                TRACE("NORM_CC_ACTIVE ...\n");
                break;
            
            case NORM_CC_INACTIVE:
                TRACE("NORM_CC_INACTIVE ...\n");
                break;
                
            case NORM_REMOTE_SENDER_NEW:
            case NORM_REMOTE_SENDER_ACTIVE:
                break;
                
            case NORM_REMOTE_SENDER_INACTIVE:
                NormNodeFreeBuffers(theEvent.sender);
                break;
            
            case NORM_RX_OBJECT_NEW:
                TRACE("NORM_RX_OBJECT_NEW ...\n");
                norm_rx_stream = theEvent.object;
                break;
            
            case NORM_RX_OBJECT_UPDATED:
                //TRACE("NORM_RX_OBJECT_UPDATED ...\n");
                updateCount++;
                if ((updateCount % 1000) == 0)
                    TRACE("updateCount:%lu\n", updateCount);
                worker_thread_dispatcher.PromptThread();
                break;
                
            case NORM_RX_OBJECT_ABORTED:
                TRACE("NORM_RX_OBJECT_ABORTED ...\n");
                break;
            
            default:
                TRACE("UNHANDLED NORM EVENT : %d...\n", theEvent.type);
                break;
        }
    }
    else
    {
        PLOG(PL_ERROR, "NormThreadApp::OnNormEvent() NormGetNextEvent() error?\n");
    }
}  // end NormThreadApp::OnNormEvent()

void NormThreadApp::DoWorkerEvent(const void* receiverData)
{
    NormThreadApp* theApp = (NormThreadApp*)receiverData;
    theApp->WorkerReadStream();
}  // end NormThreadApp::DoWorkerEvent()

void NormThreadApp::WorkerReadStream()
{
    unsigned int loopCount = 0;
    static unsigned long readCount = 0;
    char rxBuffer[2048];
    unsigned int bytesRead = 1400;
    do
    {
        if (NormStreamRead(norm_rx_stream, rxBuffer, &bytesRead))
        {
            //TRACE("read %u bytes from stream ...\n", bytesRead);
            if (0 != bytesRead) 
            {
                readCount++;
                if ((readCount % 1000) == 0)
                    TRACE("readCount:%lu\n", readCount);
                bytesRead = 1400;
            }
        }
        else
        {
            TRACE("NormThreadApp::WorkerReadStream() stream broken!\n");
            bytesRead = 0;
        }
        loopCount++;
    } while (0 != bytesRead);
    //TRACE("loopCount: %lu\n", loopCount);
}  // end NormThreadApp::WorkerReadStream()

const char* const NormThreadApp::cmd_list[] = 
{
    "+debug",        // debug level
    "-send",
    "-recv",
    NULL
};  // end  NormThreadApp::cmd_list[]   
    
bool NormThreadApp::OnCommand(const char* cmd, const char* val)
{
    size_t cmdlen = strlen(cmd);
    if (!strncmp("debug", cmd, cmdlen))
    {
    }
    else if (!strncmp("send", cmd, cmdlen))
    {
        sender = true;
    }
    else if (!strncmp("recv", cmd, cmdlen))
    {
        receiver = true;
    }
    else if (!strncmp("debug", cmd, cmdlen))
    {
        PLOG(PL_FATAL, "NormThreadApp::OnCommand(%s) unknown command\n", cmd);
        return false;
    }
    return true;
}  // end NormThreadApp::ProcessCommands()
    
NormThreadApp::CmdType NormThreadApp::CommandType(const char* cmd)
{
    if (!cmd) return CMD_INVALID;
    size_t len = strlen(cmd);
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
}  // end NormThreadApp::CommandType()


bool NormThreadApp::ProcessCommands(int argc, const char*const* argv)
{
    int i = 1;
    while ( i < argc)
    {
        CmdType cmdType = CommandType(argv[i]);   
        switch (cmdType)
        {
            case CMD_INVALID:
                PLOG(PL_FATAL, "NormThreadApp::ProcessCommands() Invalid command:%s\n", 
                        argv[i]);
                return false;
            case CMD_NOARG:
                if (!OnCommand(argv[i], NULL))
                {
                    PLOG(PL_FATAL, "NormThreadApp::ProcessCommands() OnCommand(%s) error\n", 
                            argv[i]);
                    return false;
                }
                i++;
                break;
            case CMD_ARG:
                if (!OnCommand(argv[i], argv[i+1]))
                {
                    PLOG(PL_FATAL, "NormThreadApp::ProcessCommands() OnCommand(%s, %s) error\n", 
                            argv[i], argv[i+1]);
                    return false;
                }
                i += 2;
                break;
        }
    }
    return true;
}  // end NormThreadApp::ProcessCommands()

