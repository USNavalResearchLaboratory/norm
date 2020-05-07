
// normApp.cpp - Command-line NORM "demo" application

// Note this code does _not_ use the NORM API.  It is kept as a demonstrator
// and test tool and serves as a performance comparison reference for testing
// the threaded NORM API code.  So this should _not_ be used as a NORM
// programming example!!!!

#include "protokit.h"
#include "normSession.h"
#include "normPostProcess.h"

#include <stdio.h>   // for stdout/stderr printouts
#include <stdlib.h>
#include <ctype.h>  // for "isspace()"

#ifdef UNIX
#include <sys/types.h>
#include <sys/wait.h>  // for "waitpid()"
#include <signal.h>
#else
#ifndef _WIN32_WCE
#include <io.h>  // for _mktmp()
#endif // _WIN32-WCE
#endif // if/else UNIX/WIN32

// Command-line application using Protolib EventDispatcher
class NormApp : public NormController, public ProtoApp
{
    public:
        NormApp();
        virtual ~NormApp();
        
        // Overrides from ProtoApp or NsProtoSimAgent base
        bool OnStartup(int argc, const char*const* argv);
        bool ProcessCommands(int argc, const char*const* argv);
        void OnShutdown();
        
        bool OnCommand(const char* cmd, const char* val);
        
        static void DoInputReady(ProtoDispatcher::Descriptor descriptor, 
                                 ProtoDispatcher::Event      theEvent, 
                                 const void*                 userData);
        
    private:
        // we use this class to cache tx file paths
        // and their last tx object id to support the
        // "requeue" option      
        class FileCacheItem  : public ProtoTree::Item
        {
            public:
                FileCacheItem(const char* thePath);
                ~FileCacheItem();
                
                void SetObjectId(NormObjectId objectId)
                    {object_id = objectId;}
                NormObjectId GetObjectId() const
                    {return object_id;}
                    
            private:
                // ProtoTree::Item overrides
                const char* GetKey() const 
                    {return file_path;}
                unsigned int GetKeysize() const
                    {return (PATH_MAX << 3);}
                
                char            file_path[PATH_MAX];
                NormObjectId    object_id;
        };  // end class NormApp::FileCacheItem
        
        class Notification : public ProtoList::Item
        {
            public:
                NormController::Event event;
        };  // end class NormApp::Notification
        class NotificationQueue : public ProtoListTemplate<Notification> {};
        Notification* GetNotification(); // get from pool or create as needed
            
        void ShowHelp();
        void OnInputReady();
        bool AddAckingNodes(const char* ackingNodeList);
            
        enum CmdType {CMD_INVALID, CMD_NOARG, CMD_ARG};
        CmdType CommandType(const char* cmd);
    
        virtual void Notify(NormController::Event event,
                        class NormSessionMgr* sessionMgr,
                        class NormSession*    session,
                        class NormNode*       node,
                        class NormObject*     object);
        
        bool OnIntervalTimeout(ProtoTimer& theTimer);
        void OnControlEvent(ProtoSocket& theSocket, ProtoSocket::Event theEvent);
    
        static const char* const cmd_list[];
        
        enum EcnMode {ECN_OFF, ECN_ON, ECN_ONLY};
        
#ifdef UNIX
        static void SignalHandler(int sigNum);
#endif // UNIX
            
        ProtoPipe           control_pipe;  // for remote control
        bool                control_remote;
        NormSessionMgr      session_mgr;
        NormSession*        session;
        NormStreamObject*   tx_stream;
        NormStreamObject*   rx_stream;
        NotificationQueue   notify_queue;
        NotificationQueue   notify_pool;
        bool                notify_reentry;
              
        // application parameters
        FILE*               input;  // input stream
        FILE*               output; // output stream
        char*               output_io_buffer;
        unsigned int        output_io_bufsize;
        char                input_buffer[65535];
        unsigned int        input_index;
        unsigned int        input_length;
        bool                input_active;
        bool                push_mode;
        NormStreamObject::FlushMode msg_flush_mode;
        bool                input_messaging; // stream input mode   
        UINT16              input_msg_length;
        UINT16              input_msg_index;
        bool                msg_test;
        unsigned int        msg_test_length;
        UINT32              msg_test_seq;
        char                output_buffer[65535];
        UINT16              output_index;
        bool                output_messaging;
        UINT16              output_msg_length;
        bool                output_msg_sync;
        bool                precise;
        bool                boost;
        
        // NormSession common parameters 
        char*               address;        // session address
        UINT16              port;           // session port number
        UINT16              tx_port;
        UINT8               ttl;
        bool                loopback;
        char*               interface_name; // for multi-home hosts
        double              tx_rate;        // bits/sec
        double              tx_rate_min;
        double              tx_rate_max;
        bool                cc_enable;
        EcnMode             ecn_mode;
        bool                tolerate_loss;  // for "lost tolerant" congestion control option
        UINT32              node_id;
        
        // NormSession sender-only parameters
        UINT16              segment_size;
        UINT16              ndata;
        UINT16              nparity;
        UINT16              auto_parity;
        UINT16              extra_parity;
        double              backoff_factor;
        double              grtt_estimate; // initial grtt estimate
        NormSession::ProbingMode grtt_probing_mode;
        double              group_size;
        unsigned long       tx_buffer_size; // bytes
	    unsigned int	    tx_sock_buffer_size;
        unsigned long       tx_cache_min;
        unsigned long       tx_cache_max;
        NormObjectSize      tx_cache_size;        
        
        NormFileList        tx_file_list;
        char                tx_file_name[PATH_MAX];
        bool                tx_file_info;
        ProtoTree           tx_file_cache; 
        bool                tx_one_shot;
        bool                tx_ack_shot;
        bool                tx_file_queued;
        int                 tx_robust_factor;
        
        // save last obj/block/seg id for later in case needed
        NormObjectId        tx_last_object_id;
        NormBlockId         tx_last_block_id;
        NormSegmentId       tx_last_segment_id;
        double              tx_object_interval;
        int                 tx_repeat_count;
        double              tx_repeat_interval;
        bool                tx_repeat_clear;
        int                 tx_requeue;       // master requeue value
        int                 tx_requeue_count; // current requeue counter vale
        ProtoTimer          interval_timer;
        char*               acking_node_list; // comma-delimited string
        bool                acking_flushes;
        bool                watermark_pending;
        
        // NormSession receiver-only parameters
        unsigned long       rx_buffer_size; // bytes
        unsigned int        rx_sock_buffer_size;
        NormFileList        rx_file_cache;
        char*               rx_cache_path;
        NormPostProcessor*  post_processor;
        bool                unicast_nacks;
        bool                silent_receiver;
        bool                low_delay;
        bool                realtime;
        int                 rx_robust_factor;
        bool                rx_persistent;
        bool                process_aborted_files;
        bool                preallocate_sender;
        NormSenderNode::RepairBoundary repair_boundary;
        
        // Debug parameters
        bool                tracing;
        double              tx_loss;
        double              rx_loss;
    
}; // end class NormApp


NormApp::FileCacheItem::FileCacheItem(const char* thePath)
{
    strncpy(file_path, thePath, PATH_MAX);
}

NormApp::FileCacheItem::~FileCacheItem()
{
}

NormApp::NormApp()
 : control_pipe(ProtoPipe::MESSAGE), control_remote(false), 
   session_mgr(GetTimerMgr(), GetSocketNotifier(), &dispatcher),
   session(NULL), tx_stream(NULL), rx_stream(NULL), notify_reentry(false), input(NULL), output(NULL), 
   output_io_buffer(NULL), output_io_bufsize(0),
   input_index(0), input_length(0), input_active(false),
   push_mode(false), msg_flush_mode(NormStreamObject::FLUSH_PASSIVE),
   input_messaging(false), input_msg_length(0), input_msg_index(0),
   msg_test(false), msg_test_length(0), msg_test_seq(0),
   output_index(0), output_messaging(false), output_msg_length(0), output_msg_sync(false),
   precise(false), boost(false),
   address(NULL), port(0), ttl(32), loopback(false), interface_name(NULL),
   tx_rate(64000.0), tx_rate_min(-1.0), tx_rate_max(-1.0), 
   cc_enable(false), ecn_mode(ECN_OFF), tolerate_loss(false),
   node_id(NORM_NODE_ANY), segment_size(1024), ndata(32), nparity(16), auto_parity(0), extra_parity(0),
   backoff_factor(NormSession::DEFAULT_BACKOFF_FACTOR), grtt_estimate(NormSession::DEFAULT_GRTT_ESTIMATE), 
   grtt_probing_mode(NormSession::PROBE_ACTIVE), group_size(NormSession::DEFAULT_GSIZE_ESTIMATE),
   tx_buffer_size(1024*1024), tx_sock_buffer_size(0), tx_cache_min(8), tx_cache_max(256), tx_cache_size((UINT32)20*1024*1024),
   tx_file_info(true), tx_one_shot(false), tx_ack_shot(false), tx_file_queued(false),
   tx_robust_factor(NormSession::DEFAULT_ROBUST_FACTOR), tx_object_interval(0.0), tx_repeat_count(0), 
   tx_repeat_interval(2.0), tx_repeat_clear(true), tx_requeue(0), tx_requeue_count(0), acking_node_list(NULL), 
   acking_flushes(false), watermark_pending(false), rx_buffer_size(1024*1024), rx_sock_buffer_size(0),
   rx_cache_path(NULL), post_processor(NULL), unicast_nacks(false), silent_receiver(false), 
   low_delay(false), realtime(false), rx_robust_factor(NormSession::DEFAULT_ROBUST_FACTOR), rx_persistent(true), process_aborted_files(false),
   preallocate_sender(false), repair_boundary(NormSenderNode::BLOCK_BOUNDARY), tracing(false), tx_loss(0.0), rx_loss(0.0)
{
    control_pipe.SetListener(this, &NormApp::OnControlEvent);
    control_pipe.SetNotifier(&GetSocketNotifier());
    
    // Init tx_timer for 1.0 second interval, infinite repeats
    session_mgr.SetController(this);
    
    interval_timer.SetListener(this, &NormApp::OnIntervalTimeout);
    interval_timer.SetInterval(0.0);
    interval_timer.SetRepeat(0);
    
    tx_file_name[0] = '\0';
    
    struct timeval currentTime;
    ProtoSystemTime(currentTime);
    srand(currentTime.tv_usec);
}

NormApp::~NormApp()
{
    notify_queue.Destroy();
    notify_pool.Destroy();
    if (address) delete[] address;
    if (interface_name) delete[] interface_name;
    
    tx_file_cache.Destroy();
    
    if (rx_cache_path) delete[] rx_cache_path;
    if (post_processor) delete post_processor;
    if (acking_node_list) delete[] acking_node_list;
    if (NULL != output_io_buffer)
    {
        delete[] output_io_buffer;
        output_io_buffer = NULL;
    }
}

NormApp::Notification* NormApp::GetNotification()
{
    Notification* notification = notify_pool.RemoveHead();
    if (NULL == notification)
    {
        if (NULL == (notification = new Notification()))
        {
            PLOG(PL_ERROR, "NormApp::GetNotification() new Notification error: %s\n", GetErrorString());
            return NULL;
        }
    }
    return notification;
}  // end NormApp::GetNotification()

// NOTE on message flushing mode:
//
// "none"    - messages are aggregated to make NORM_DATA
//             payloads a full "segmentSize"
//
// "passive" - At the end-of-message, short NORM_DATA
//             payloads are sent as needed so that
//             the message is immediately sent
//             (i.e. not held for aggregation)
// "active"  - Same as "passive", plus NORM_CMD(FLUSH)
//             messages are generated until new
//             message is written to stream
//             (This helps keep receivers in sync
//              with sender when intermittent message
//              traffic is sent)


const char* const NormApp::cmd_list[] = 
{
    "-help",         // show this help"
    "+debug",        // debug level
    "+log",          // log file name
    "+trace",        // message tracing on
    "+txloss",       // tx packet loss percent (for testing)
    "+rxloss",       // rx packet loss percent (for testing)
    "+address",      // session destination address/port
    "+txport",       // use specific transmission source port number 
    "+ttl",          // multicast hop count scope
    "+loopback",     // "off" or "on" to recv our own packets
    "+interface",    // multicast interface name to use
    "+cc",           // congestion control on/off
    "+ecn",          // explicit congestion notification 'on','off', or 'only'\n"
    "-tolerant",     // enables "loss-tolerant" congestion control operation
    "+rate",         // tx date rate (bps)
    "+limit",        // tx rate limits <rateMin:rateMax>
    "-push",         // push stream writes for real-time messaging
    "+flush",        // message flushing mode ("none", "passive", or "active")
    "+input",        // send stream input
    "+output",       // recv stream output
    "+minput",       // send message stream input
    "+moutput",      // recv message stream output
    "+sendfile",     // file/directory list to transmit
    "+info",         // 'on' | 'off' to enable/disable file info (name) transmission (default = 'on')
    "+interval",     // delay time (sec) between files (0.0 sec default)
    "+repeatcount",  // How many times to repeat the file/directory list tx
    "+rinterval",    // Interval (sec) between file/directory list repeats
    "+requeue",      // <count> how many times files are retransmitted w/ same objId
    "+boundary",     // 'block' or 'file' to set NORM_REPAIR_BOUNDARY (default is 'block')
    "-oneshot",      // Transmit file(s), exiting upon TX_FLUSH_COMPLETED
    "-ackshot",      // Transmit file(s), exiting upon TX_WATERMARK_COMPLETED
    "-updatesOnly",  // only send updated files on repeat transmission
    "+ackingNodes",  // comma-delimited list of node id's to from which to collect acks
    "-ackflush",     // when set, acking completion truncates end-of-transmission flushing
    "+id",           // set the local NormNodeId to a specific value
    "+rxcachedir",   // recv file cache directory
    "+segment",      // payload segment size (bytes)
    "+block",        // User data packets per FEC coding block (blockSize)
    "+parity",       // FEC packets calculated per coding block (nparity)
    "+auto",         // Number of FEC packets to proactively send (<= nparity)
    "+extra",        // Number of extra FEC packets sent in response to repair requests
    "+backoff",      // Backoff factor to use
    "+grtt",         // Set sender's initial GRTT estimate
    "+probe",        // {'active', 'passive' | 'none'} Set sender;s GRTT probing mode 'active' is default)
    "+gsize",        // Set sender's group size estimate
    "+txbuffer",     // Size of sender's buffer
    "+txsockbuffer", // tx socket buffer size
    "+txcachebounds",// <countMin:countMax:sizeMax> limits on sender tx object caching
    "+txrobustfactor", // integer tx robust factor
    "+rxbuffer",     // Size receiver allocates for buffering each sender
    "+rxsockbuffer", // Optional recv socket buffer size.
    "-unicastNacks", // unicast instead of multicast feedback messages
    "-silentReceiver", // "silent" (non-nacking) receiver (EMCON mode) (must set for sender too)
    "-presetSender",   // causes receiver to preallocate resources for remote sender w/ segmentSize, block, and parity params
    "-lowDelay",     // for silent receivers only, delivers data/files to app sooner\n"
    "-realtime",     // for NACKing (non-silent) receivers, flips buffer mgmnt to favor low latency over reliability
    "+rxpersist",    // "off" or "on" to make receiver keep state on sender forever ("on" by default)
    "-saveAborts",   // save (and possibly post-process) aborted receive files\n"
    "+processor",    // receive file post processing command
    "+instance",     // specify norm instance name for remote control commands
    "-precise",      // run the NormApp ProtoDispatcher in "precise timing" mode
    "-boost",        // run the NormApp with "boosted" process priority (super user only)
    "+mtest",        // <size> send test messages via NORM_OBJECT_STREAM
    "+stest",        // <size> send test stream of bytes via NORM_OBJECT_STREAM at 
    "+obuf",         // <size> non-standard buffer size for "output" or "moutput" FILE* (incl. stdout)
    NULL          
};

// Thanks to Marinho Barcellos taking initiative with this help function
void NormApp::ShowHelp() 
{
      // TBD: this should be taken automatically from the cmd array
    fprintf(stderr, 
        "List of commands taken by \"norm\" (+ indicates that an argument is required):\n"
        "   -help,         // show this help\n"
        "   +debug,        // debug level\n"
        "   +log,          // log file name\n"
        "   +trace,        // message tracing on\n"
        "   +txloss,       // tx packet loss percent (for testing)\n"
        "   +rxloss,       // rx packet loss percent (for testing)\n"
        "   +address,      // session destination address\n"
        "   +txport,       // use a specific transmission source port number\n"
        "   +ttl,          // multicast hop count scope\n"
        "   +loopback,     // 'on' or 'off' to recv our own packets (default = 'off')\n"
        "   +interface,    // multicast interface name to use\n"
        "   +cc,           // congestion control 'on' or 'off'\n"
        "   +ecn,          // explicit congestion notification 'on' or 'off'\n"
        "   +rate,         // tx date rate (bps)\n"
        "   +limit         // tx rate bounds <rateMin:rateMax>\n"
        "   -push,         // push stream writes for real-time messaging\n"
        "   +flush,        // message flushing mode ('none', 'passive', or 'active')\n"
        "   +input,        // send stream input\n"
        "   +output,       // recv stream output\n"
        "   +minput,       // sender message stream input\n"
        "   +moutput,      // receiver message stream output\n"
        "   +sendfile,     // file/directory list to transmit\n"
        "   +info,         // 'on' | 'off' to enable/disable file info (name) transmission (default = 'on')\n"
        "   +interval,     // delay time (sec) between files (0.0 sec default)\n"
        "   +repeatcount,  // How many times to repeat the file/directory list tx\n"
        "   +rinterval,    // Interval (sec) between file/directory list repeats\n"
        "   +requeue,      // <count> how many times files are retransmitted w/ same objId\n"
        "   +boundary      // 'block' or 'file' to set NORM_REPAIR_BOUNDARY (default is 'block')\n"
        "   -oneshot,      // Exit upon sender TX_FLUSH_COMPLETED event (sender exits after transmission)\n"
        "   -ackshot,      // Exit upon sender TX_WATERMARK_COMPLETED event (sender exits after transmission)\n"
        "   -updatesOnly,  // only send updated files on repeat transmission\n"
        "   +ackingNodes,  // comma-delimited list of node id's to from which to collect acks\n"
        "   -ackflush,     // when set, acking completion truncates flushing\n"
        "   +id,           // set the local NormNodeId to a specific value\n"
        "   +rxcachedir,   // recv file cache directory\n"
        "   +segment,      // payload segment size (bytes)\n"
        "   +block,        // User data packets per FEC coding block (blockSize)\n"
        "   +parity,       // FEC packets calculated per coding block (nparity)\n"
        "   +auto,         // Number of FEC packets to proactively send (<= nparity)\n"
        "   +extra,        // Number of extra FEC packets sent in response to repair requests\n"
        "   +backoff,      // Backoff factor to use\n"
        "   +grtt,         // Set sender's initial GRTT estimate\n"
        "   +probe,        // {'active', 'passive' | 'none'} Set sender;s GRTT probing mode 'active' is default)\n"
        "   +gsize,        // Set sender's group size estimate\n"
        "   +txbuffer,     // Size of sender's buffer\n"
        "   +txcachebounds,// <countMin:countMax:sizeMax> limits on sender tx object caching\n"
        "   +rxbuffer,     // Size receiver allocates for buffering each sender\n"
        "   +rxsockbuffer, // Optional recv socket buffer size.\n"
        "   -unicastNacks, // unicast instead of multicast feedback messages\n"
        "   -silentReceiver, // silent (non-nacking) receiver (EMCON mode) (must set for sender too)\n"
        "   -lowDelay,     // for silent receivers only, delivers data/files to app sooner\n"
        "   -presetSender, // causes receiver to preallocate resources for remote sender w/ segmentSize, block, and parity params\n"
        "   -realtime,     // for NACKing (non-silent) receivers, flips buffer mgmnt to favor low latency over reliability\n"
        "   +rxpersist,    // 'off' or 'on' to make receiver keep state on sender forever ('on' by default)\n"
        "   -saveAborts,   // save (and possibly post-process) aborted receive files\n"
        "   +processor,    // receive file post processing command\n"
        "   +instance,     // specify norm instance name for remote control commands\n"
        "   -precise,      // run the NormApp ProtoDispatcher in 'precise timing' mode\n"
        "   -boost,        // run the NormApp with 'boosted' process priority (super user only)\n"
        "   +mtest,        // <size> send test messages via NORM_OBJECT_STREAM\n"
        "   +stest,        // <size> send test stream of bytes via NORM_OBJECT_STREAM\n" 
        "\n");
}  // end NormApp::ShowHelp() 


void NormApp::OnControlEvent(ProtoSocket& /*theSocket*/, ProtoSocket::Event theEvent)
{
    switch (theEvent)
    {
        case ProtoSocket::RECV:
        {
            char buffer[8192];
            unsigned int len = 8192;
            if (control_pipe.Recv(buffer, len))
            {
                buffer[len] = '\0';
                PLOG(PL_DEBUG, "norm: received command \"%s\"\n", buffer);
                char* cmd = buffer;
                char* val = NULL;
                for (unsigned int i = 0; i < len; i++)
                {
                    if (isspace(buffer[i]))
                    {
                        buffer[i++] = '\0';   
                        val = buffer + i;
                        break;
                    }
                }
                if (!OnCommand(cmd, val))
                    PLOG(PL_ERROR, "NormApp::OnControlEvent() bad command received\n");  
            }
            else
            {
                PLOG(PL_ERROR, "NormApp::OnControlEvent() error receiving remote command\n");   
            }
            break;
        }
            
        default: 
            PLOG(PL_ERROR, "NormApp::OnControlEvent() unhandled event type: %d\n", theEvent);
            break; 
    }
}  // end NormApp::OnControlEvent()
    
bool NormApp::OnCommand(const char* cmd, const char* val)
{
    CmdType type = CommandType(cmd);
    ASSERT(CMD_INVALID != type);
    size_t len = strlen(cmd);
    if ((CMD_ARG == type) && !val)
    {
        PLOG(PL_FATAL, "NormApp::OnCommand(%s) missing argument\n", cmd);
        return false;        
    }
    
    if (!strncmp("help", cmd, len))
    {   
        ShowHelp();
        return false;
    }
    
    if (control_remote)
    {
        // Since the "instance" already exists, we send the command
        // to the pre-existing "instance" instead of processing it ourself
        char buffer[8192];
        buffer[0] = '\0';
        if (cmd) strncpy(buffer, cmd, 8191);
        size_t len = strlen(buffer);
        len = (len > 8191) ? 8191 : len;
        if (val)
        {
            buffer[len++] = ' ';
            strncat(buffer, val, 8192 - strlen(buffer));
        }
        len = strlen(buffer);
        len = (len > 8191) ? 8191 : len;
        buffer[len++] = '\0';
        unsigned int numBytes = (unsigned int)len;
        if (!control_pipe.Send(buffer, numBytes))
		{
            PLOG(PL_FATAL, "NormApp::OnCommand() error sending command to remote instance\n");
            return false;
        }     
        else
        {
            PLOG(PL_FATAL, "NormApp::OnCommand() sent \"%s\" to remote\n", buffer);
        }   
    }
    
    if (!strncmp("debug", cmd, len))
    {
        
        int debugLevel = atoi(val);
        if ((debugLevel < 0) || (debugLevel > 12))
        {
            PLOG(PL_FATAL, "NormApp::OnCommand(segment) invalid debug level!\n");   
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
        if (!strcmp("on", val))
            tracing = true;
        else if (!strcmp("off", val))
            tracing = false;
        else
        {
            PLOG(PL_FATAL, "NormApp::OnCommand(trace) invalid argument!\n");   
            return false;
        }
        if (session) session->SetTrace(tracing);
    }
    else if (!strncmp("precise", cmd, len))
    {
        precise = true;  // NormApp::dispatcher will run in "precision" mode
    }
    else if (!strncmp("boost", cmd, len))
    {
        boost = true;  // normApp will run w/ high process priority
    }
    else if (!strncmp("txloss", cmd, len))
    {
        double txLoss = atof(val);
        if (txLoss < 0)
        {
            PLOG(PL_FATAL, "NormApp::OnCommand(txloss) invalid txRate!\n");   
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
            PLOG(PL_FATAL, "NormApp::OnCommand(rxloss) invalid txRate!\n");   
            return false;
        }
        rx_loss = rxLoss;
        if (session) session->SetRxLoss(rxLoss);
    }
    else if (!strncmp("address", cmd, len))
    {
        size_t len = strlen(val);
        if (address) delete address;
        if (!(address = new char[len+1]))
        {
            PLOG(PL_FATAL, "NormApp::OnCommand(address) allocation error:%s\n",
                GetErrorString()); 
            return false;
        }
        strcpy(address, val);
        char* ptr = strchr(address, '/');
        if (!ptr)
        {
            delete address;
            address = NULL;
            PLOG(PL_FATAL, "NormApp::OnCommand(address) missing port number!\n");   
            return false;
        }
        *ptr++ = '\0';
        int portNum = atoi(ptr);
        if ((portNum < 1) || (portNum > 65535))
        {
            delete address;
            address = NULL;
            PLOG(PL_FATAL, "NormApp::OnCommand(address) invalid port number!\n");   
            return false;
        }
        port = portNum;
    }
    else if (!strncmp("txport", cmd, len))
    {
        int txPort = atoi(val);
        if ((txPort < 0) || (txPort > 65535))
        {
            PLOG(PL_FATAL, "NormApp::OnCommand(txport) invalid port number!\n");   
            return false;
        }
        tx_port = (UINT16)txPort;
        if (session) session->SetTxPort(txPort);
    }
    else if (!strncmp("ttl", cmd, len))
    {
        int ttlTemp = atoi(val);
        if ((ttlTemp < 0) || (ttlTemp > 255))
        {
            PLOG(PL_FATAL, "NormApp::OnCommand(ttl) invalid value!\n");   
            return false;
        }
        bool result = session ? session->SetTTL((UINT8)ttlTemp) : true;
        ttl = result ? ttlTemp : ttl;
        if (!result)
        {
            PLOG(PL_FATAL, "NormApp::OnCommand(ttl) error setting socket ttl!\n");   
            return false;
        }
    }
    else if (!strncmp("loopback", cmd, len))
    {
        bool loopTemp = loopback;
        if (!strcmp("on", val))
            loopTemp = true;
        else if (!strcmp("off", val))
            loopTemp = false;
        else
        {
            PLOG(PL_FATAL, "NormApp::OnCommand(loopback) invalid argument!\n");   
            return false;
        }
        bool result = session ? session->SetLoopback(loopTemp) : true;
        loopback = result ? loopTemp : loopback;
        if (loopback && (NULL != session) && session->Address().IsMulticast())
            session->SetRxPortReuse(true);
        if (!result)
        {
            PLOG(PL_FATAL, "NormApp::OnCommand(loopback) error setting socket loopback!\n");   
            return false;
        }
    }
    else if (!strncmp("interface", cmd, len))
    {
        if (interface_name) delete[] interface_name;
        if (!(interface_name = new char[strlen(val)+1]))
        {
            PLOG(PL_FATAL, "NormApp::OnCommand(interface) error allocating string: %s\n",
                GetErrorString());
            return false;
        }
        strcpy(interface_name, val);
    }
    else if (!strncmp("rate", cmd, len))
    {
        double txRate = atof(val);
        if (txRate < 0)
        {
            PLOG(PL_FATAL, "NormApp::OnCommand(rate) invalid txRate!\n");   
            return false;
        }
        tx_rate = txRate;
        if (session) session->SetTxRate(txRate);
    }
    else if (!strncmp("limit", cmd, len))
    {
        double rateMin, rateMax;
        if (2 != sscanf(val, "%lf:%lf", &rateMin, &rateMax))
        {
            PLOG(PL_FATAL, "NormApp::OnCommand(limit) invalid txRate limits!\n");
            return false;
        }
        tx_rate_min = rateMin;
        tx_rate_max = rateMax;
        if (session) session->SetTxRateBounds(rateMin, rateMax);
    }
    else if (!strncmp("cc", cmd, len))
    {
        if (!strcmp("on", val))
        {
            cc_enable = true;
        }
        else if (!strcmp("off", val))
        {
            cc_enable = false;   
        }
        else
        {
            PLOG(PL_FATAL, "NormApp::OnCommand(cc) invalid option!\n");   
            return false;
        }
        if (session) session->SetCongestionControl(cc_enable);
    }
    else if (!strncmp("ecn", cmd, len))
    {
        // (TBD) change this to "off" "on" and "only"
        if (!strcmp("on", val))
        {
            ecn_mode = ECN_ON;
            if (session) session->SetEcnSupport(true, false, tolerate_loss);
        }
        else if (!strcmp("off", val))
        {
            ecn_mode = ECN_OFF;   
            if (session) session->SetEcnSupport(false, false, tolerate_loss);
        }
        else if (!strcmp("only", val))
        {
            ecn_mode = ECN_ONLY;   
            if (session) session->SetEcnSupport(true, true, tolerate_loss);
        }
        else
        {
            PLOG(PL_FATAL, "NormApp::OnCommand(ecn) invalid option!\n");   
            return false;
        }
    }
    else if (!strncmp("tolerant", cmd, len))
    {
        tolerate_loss = true;
        if (session) session->SetEcnSupport(ECN_OFF != ecn_mode, ECN_ONLY == ecn_mode, true);   
    }
    else if (!strncmp("input", cmd, len))
    {
        if (!strcmp(val, "STDIN"))
	    {
            input = stdin;
	    }
        else if (!(input = fopen(val, "rb")))
        {
            PLOG(PL_FATAL, "NormApp::OnCommand(input) fopen() error: %s\n",
                    GetErrorString());
            return false;   
        }
        input_index = input_length = 0;
        input_messaging = false;
#ifdef UNIX
        // Set input non-blocking
        if(-1 == fcntl(fileno(input), F_SETFL, fcntl(fileno(input), F_GETFL, 0) | O_NONBLOCK))
            perror("NormApp::OnCommand(input) fcntl(F_SETFL(O_NONBLOCK)) error");
#endif // UNIX
    }
    else if (!strncmp("output", cmd, len))
    {
        if (!strcmp(val, "STDOUT"))
	    {
            output = stdout;
	    }
        else if (!(output = fopen(val, "wb")))
        {
            PLOG(PL_FATAL, "NormApp::OnCommand(output) fopen() error: %s\n",
                    GetErrorString());
            return false;   
        }
        output_messaging = false;
        output_index = 0;
        output_msg_sync = true;
    }
    else if (!strncmp("stest", cmd, len))
    {
        msg_test = true;
        msg_test_length = atoi(val);
        input_messaging = false;
    }
    else if (!strncmp("minput", cmd, len))
    {
        if (!strcmp(val, "STDIN"))
	    {
            input = stdin;
	    }
        else if (!(input = fopen(val, "rb")))
        {
            PLOG(PL_FATAL, "NormApp::OnCommand(input) fopen() error: %s\n",
                    GetErrorString());
            return false;   
        }
        input_index = input_length = 0;
        input_messaging = true;
#ifdef UNIX
        // Set input non-blocking
        if(-1 == fcntl(fileno(input), F_SETFL, fcntl(fileno(input), F_GETFL, 0) | O_NONBLOCK))
            perror("NormApp::OnCommand(input) fcntl(F_SETFL(O_NONBLOCK)) error");
#endif // UNIX
    }
    else if (!strncmp("moutput", cmd, len))
    {
        if (!strcmp(val, "STDOUT"))
	    {
            output = stdout;
	    }
        else if (!(output = fopen(val, "wb")))
        {
            PLOG(PL_FATAL, "NormApp::OnCommand(output) fopen() error: %s\n",
                    GetErrorString());
            return false;   
        }
        output_messaging = true;
        output_msg_length = output_index = 0;
        output_msg_sync = false;
    }
    else if (!strncmp("mtest", cmd, len))
    {
        msg_test = true;
        msg_test_length = atoi(val);
        input_messaging = true;
    }
    else if (!strncmp("obuf", cmd, len))
    {
        if (NULL != output_io_buffer) delete[] output_io_buffer;
        output_io_bufsize = atoi(val);
        if (NULL == (output_io_buffer = new char[output_io_bufsize]))
        {
            PLOG(PL_FATAL, "NormApp::OnCommand(obuf): error allocating buffer: %s\n",
                    GetErrorString());
            return false;   
        }
    }
    else if (!strncmp("sendfile", cmd, len))
    {
        if (!tx_file_list.Append(val))
        {
            PLOG(PL_FATAL, "NormApp::OnCommand(sendfile) Error appending \"%s\" "
                    "to tx file list.\n", val);
            return false;   
        }
        // TBD - add code here to see if "session" exists (i.e. already running)
        // and, if so, StartSender operation, if needed, and kick-off "OnIntervalTimeout()"
        // process if needed to kick-start file transmission.  To do this, we probably
        // need to create a NormApp::StartSender() method that can be used here as well
        // as for initial command-line sender startup, if applicable.
        // (This will allow the "send" command to be invoked at run-time via the ProtoPipe
        //  remote control interface).
    }
    else if (!strncmp("info", cmd, len))
    {
        if (!strcmp("on", val))
            tx_file_info = true;
        else if (!strcmp("off", val))
            tx_file_info = false;
        else
        {
            PLOG(PL_FATAL, "NormApp::OnCommand(info) invalid argument!\n");   
            return false;
        }
    }
    else if (!strncmp("interval", cmd, len))
    {
        if (1 != sscanf(val, "%lf", &tx_object_interval))
        {
            PLOG(PL_FATAL, "NormApp::OnCommand(interval) Invalid tx object interval: %s\n", val);
            return false;
        } 
    }    
    else if (!strncmp("repeatcount", cmd, len))
    {
        tx_repeat_count = atoi(val);  
    }
    else if (!strncmp("rinterval", cmd, len))
    {
        if (1 != sscanf(val, "%lf", &tx_repeat_interval)) 
            tx_repeat_interval = -1.0;
        if (tx_repeat_interval < 0.0)
        {
            PLOG(PL_FATAL, "NormApp::OnCommand(rinterval) Invalid tx repeat interval: %s\n",
                     val);
            tx_repeat_interval = 0.0;
            return false;
        }
    }       
    else if (!strncmp("requeue", cmd, len))
    {
        tx_requeue = tx_requeue_count = atoi(val); 
    } 
    else if (!strncmp("boundary", cmd, len))
    {
        if (0 == strcmp("block", val))
        {
            repair_boundary = NormSenderNode::BLOCK_BOUNDARY;
        }
        else if (0 == strcmp("file", val))
        {
            repair_boundary = NormSenderNode::OBJECT_BOUNDARY;
        }
        else
        {
            PLOG(PL_FATAL, "NormApp::OnCommand(boundary) error: invalid repair boundary \"%s\"\n", val);
            return false;
        }
        if (session) session->ReceiverSetDefaultRepairBoundary(repair_boundary);
    }    
    else if (!strncmp("oneshot", cmd, len))
    {
        tx_one_shot = true;
    }    
    else if (!strncmp("ackshot", cmd, len))
    {
        tx_ack_shot = true;
    }      
    else if (!strncmp("updatesOnly", cmd, len))
    {
        tx_file_list.InitUpdateTime(true);
    }      
    else if (!strncmp("ackingNodes", cmd, len))
    {
        size_t length = strlen(val);
        if (NULL != acking_node_list)
            length += strlen(acking_node_list) + 1;
        char* tempString = new char[length + 1];
        if (NULL == tempString)
        {
            PLOG(PL_FATAL, "NormApp::OnCommand(ackingNodes) error: %s\n", GetErrorString());
            return false;
        }
        if (NULL != acking_node_list)
        {
            strcpy(tempString, acking_node_list);
            strcat(tempString, ",");
            delete[] acking_node_list;
        }
        else
        {
            tempString[0] = '\0';
        }
        acking_node_list = tempString;
        strcat(acking_node_list, val);
        if (NULL != session) return AddAckingNodes(acking_node_list);
    }    
    else if (!strncmp("ackflush", cmd, len))
    {
        acking_flushes = true;
    }
    else if (!strncmp("id", cmd, len))
    {
        node_id = atoi(val);
    }
    else if (!strncmp("rxcachedir", cmd, len))
    {
        size_t length = strlen(val);   
        // Make sure there is a trailing PROTO_PATH_DELIMITER
        if (PROTO_PATH_DELIMITER != val[length-1]) 
            length += 2;
        else
            length += 1;
        if (!(rx_cache_path = new char[length]))
        {
             PLOG(PL_FATAL, "NormApp::OnCommand(rxcachedir) alloc error: %s\n",
                    GetErrorString());
            return false;  
        }
        strcpy(rx_cache_path, val);
        rx_cache_path[length-2] = PROTO_PATH_DELIMITER;
        rx_cache_path[length-1] = '\0';
    }
    else if (!strncmp("segment", cmd, len))
    {
        int segmentSize = atoi(val);
        if ((segmentSize < 0) || (segmentSize > 65535))
        {
            PLOG(PL_FATAL, "NormApp::OnCommand(segment) invalid segment size!\n");   
            return false;
        }
        segment_size = segmentSize;
    }
    else if (!strncmp("block", cmd, len))
    {
        int blockSize = atoi(val);
        if ((blockSize < 1) || (blockSize > 65535))
        {
            PLOG(PL_FATAL, "NormApp::OnCommand(block) invalid block size!\n");   
            return false;
        }
        ndata = blockSize;
    }
    else if (!strncmp("parity", cmd, len))
    {
        int numParity = atoi(val);
        if ((numParity < 0) || (numParity > 65534))
        {
            PLOG(PL_FATAL, "NormApp::OnCommand(parity) invalid value!\n");   
            return false;
        }
        nparity = numParity;
    }
    else if (!strncmp("auto", cmd, len))
    {
        int autoParity = atoi(val);
        if ((autoParity < 0) || (autoParity > 65534))
        {
            PLOG(PL_FATAL, "NormApp::OnCommand(auto) invalid value!\n");   
            return false;
        }
        auto_parity = autoParity;
        if (session) session->SenderSetAutoParity(autoParity);
    }
    else if (!strncmp("extra", cmd, len))
    {
        int extraParity = atoi(val);
        if ((extraParity < 0) || (extraParity > 65534))
        {
            PLOG(PL_FATAL, "NormApp::OnCommand(extra) invalid value!\n");   
            return false;
        }
        extra_parity = extraParity;
        if (session) session->SenderSetExtraParity(extraParity);
    }
    else if (!strncmp("backoff", cmd, len))
    {
        double backoffFactor = atof(val);
        if (backoffFactor < 0)
        {
            PLOG(PL_FATAL, "NormApp::OnCommand(backoff) invalid value!\n");   
            return false;
        }
        backoff_factor = backoffFactor;
        if (session) session->SetBackoffFactor(backoffFactor);
    }
    else if (!strncmp("grtt", cmd, len))
    {
        double grttEstimate = atof(val);
        if (grttEstimate < 0)
        {
            PLOG(PL_FATAL, "NormApp::OnCommand(grtt) invalid value!\n");   
            return false;
        }
        grtt_estimate = grttEstimate;
        if (session) session->SenderSetGrtt(grttEstimate);
    }
    else if (!strncmp("probe", cmd, len))
    {
        size_t valLen = strlen(val);
        if (!strncmp("none", val, valLen))
           grtt_probing_mode = NormSession::PROBE_NONE;
        else if (!strncmp("passive", val, valLen))
            grtt_probing_mode = NormSession::PROBE_PASSIVE;
        else if (!strncmp("active", val, valLen))
            grtt_probing_mode = NormSession::PROBE_ACTIVE;
        else
        {
            PLOG(PL_FATAL, "NormApp::OnCommand(probe) invalid grtt probing mode!\n");   
            return false;
        }
        if (NULL != session) session->SetGrttProbingMode(grtt_probing_mode);
    }
    else if (!strncmp("gsize", cmd, len))
    {
        double groupSize = atof(val);
        if (groupSize < 0)
        {
            PLOG(PL_FATAL, "NormApp::OnCommand(gsize) invalid value!\n");   
            return false;
        }
        group_size = groupSize;
        if (session) session->SenderSetGroupSize(groupSize);
    }
    else if (!strncmp("txbuffer", cmd, len))
    {
        if (1 != sscanf(val, "%lu", &tx_buffer_size))
        {
            PLOG(PL_FATAL, "NormApp::OnCommand(txbuffer) invalid value!\n");   
            return false;
        }
    }
    else if (!strncmp("txcachebounds", cmd, len))
    {
        unsigned long countMin, countMax, sizeMax;
        if (3 != sscanf(val, "%lu:%lu:%lu\n", &countMin, &countMax, &sizeMax))
        {
            PLOG(PL_FATAL, "NormApp::OnCommand(txcachebounds) invalid value!\n");   
            return false;
        }
        tx_cache_min = countMin;
        tx_cache_max = countMax;
        tx_cache_size = sizeMax;
        if (session) 
        {
            session->SetTxCacheBounds(tx_cache_size, tx_cache_min, tx_cache_max);
            session->SetRxCacheMax(tx_cache_max);
        }
    }
    else if (!strncmp("txrobustfactor", cmd, len))
    {
        tx_robust_factor = atoi(val);
        if (session) session->SetTxRobustFactor(tx_robust_factor);
    }
    else if (!strncmp("rxbuffer", cmd, len))
    {
        if (1 != sscanf(val, "%lu", &rx_buffer_size))
        {
            PLOG(PL_FATAL, "NormApp::OnCommand(rxbuffer) invalid value!\n");   
            return false;
        }
    }
    else if (!strncmp("rxsockbuffer", cmd, len))
    {
        if (1 != sscanf(val, "%u", &rx_sock_buffer_size))
        {
            PLOG(PL_FATAL, "NormApp::OnCommand(rxsockbuffer) invalid value!\n");   
            return false;
        }
        if (session && (rx_sock_buffer_size > 0)) 
            session->SetRxSocketBuffer(rx_sock_buffer_size);
    }
    else if (!strncmp("txsockbuffer", cmd, len))
    {
	    if (1 != sscanf(val, "%u", &tx_sock_buffer_size))
	    {
		    PLOG(PL_FATAL, "NormApp::OnCommand(txsockbuffer) invalid value!\n");
		    return false;
	    }
	    if (session && (tx_sock_buffer_size))
	    	session->SetTxSocketBuffer(tx_sock_buffer_size);
    }
    else if (!strncmp("unicastNacks", cmd, len))
    {
        unicast_nacks = true;
        if (session) session->ReceiverSetUnicastNacks(true);
    }
    else if (!strncmp("silentReceiver", cmd, len))
    {
        silent_receiver = true;
        if (session) 
        {
            session->ReceiverSetSilent(true);
            session->SndrSetEmcon(true);
        }
    }
    else if (!strncmp("presetSender", cmd, len))
    {
        preallocate_sender = true;
    }
    else if (!strncmp("lowDelay", cmd, len))
    {
        low_delay = true;
        if (session) session->RcvrSetMaxDelay(1);
    }
    else if (!strncmp("realtime", cmd, len))
    {
        realtime = true;
        if (session) session->RcvrSetRealtime(true);
    }
    else if (!strncmp("rxrobustfactor", cmd, len))
    {
        rx_robust_factor = atoi(val);
        if (session) session->SetRxRobustFactor(rx_robust_factor);
    }
    else if (!strncmp("rxpersist", cmd, len))
    {
        if (!strcmp("on", val))
            rx_persistent = true;
        else if (!strcmp("off", val))
            rx_persistent = false;
        else
        {
            PLOG(PL_FATAL, "NormApp::OnCommand(rxpersist) invalid argument!\n");   
            return false;
        }
    }
    else if (!strncmp("saveAborts", cmd, len))
    {
        process_aborted_files = true;
    }
    else if (!strncmp("push", cmd, len))
    {
        push_mode = true;
        if (tx_stream) tx_stream->SetPushMode(push_mode);
    }
    else if (!strncmp("flush", cmd, len))
    {
        size_t valLen = strlen(val);
        if (!strncmp("none", val, valLen))
            msg_flush_mode = NormStreamObject::FLUSH_NONE;
        else if (!strncmp("passive", val, valLen))
            msg_flush_mode = NormStreamObject::FLUSH_PASSIVE;
        else if (!strncmp("active", val, valLen))
            msg_flush_mode = NormStreamObject::FLUSH_ACTIVE;
        else
        {
            PLOG(PL_FATAL, "NormApp::OnCommand(flush) invalid msg flush mode!\n");   
            return false;
        }
        if (tx_stream) tx_stream->SetFlushMode(msg_flush_mode);
    }
    else if (!strncmp("processor", cmd, len))
    {
        if (!post_processor->SetCommand(val))
        {
            PLOG(PL_FATAL, "NormApp::OnCommand(processor) error!\n");   
            return false;
        }
    }
    else if (!strncmp("instance", cmd, len))
    {
        // First, try to connect to see if instance is already running
        if (control_pipe.Connect(val))
        {
            control_remote = true;
        }
        else if (control_pipe.Listen(val))
        {
            control_remote = false;
        }
        else
        {
            PLOG(PL_FATAL, "NormApp::OnCommand(instance) error opening control pipe\n");
            return false;   
        }
    }
    return true;
}  // end NormApp::OnCommand()


NormApp::CmdType NormApp::CommandType(const char* cmd)
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
}  // end NormApp::CommandType()


bool NormApp::ProcessCommands(int argc, const char*const* argv)
{
    int i = 1;
    while ( i < argc)
    {
        CmdType cmdType = CommandType(argv[i]);   
        switch (cmdType)
        {
            case CMD_INVALID:
                PLOG(PL_FATAL, "NormApp::ProcessCommands() Invalid command:%s\n", 
                        argv[i]);
                return false;
            case CMD_NOARG:
                if (!OnCommand(argv[i], NULL))
                {
                    PLOG(PL_FATAL, "NormApp::ProcessCommands() OnCommand(%s) error\n", 
                            argv[i]);
                    return false;
                }
                i++;
                break;
            case CMD_ARG:
                if (!OnCommand(argv[i], argv[i+1]))
                {
                    PLOG(PL_FATAL, "NormApp::ProcessCommands() OnCommand(%s, %s) error\n", 
                            argv[i], argv[i+1]);
                    return false;
                }
                i += 2;
                break;
        }
    }
    return true;
}  // end NormApp::ProcessCommands()

bool NormApp::AddAckingNodes(const char* ackingNodeList)
{
    if (NULL == ackingNodeList) return true;
    if (NULL != session)
    {
        const char* ptr = ackingNodeList;
        while ((NULL != ptr) && ('\0' != *ptr))
        {
            const char* end = strchr(ptr, ',');
            size_t len = (NULL != end) ? (end - ptr) : strlen(ptr);
            if (len >= 256)
            {
                PLOG(PL_FATAL, "NormApp::AddAckingNodes() error: bad acking node list\n");
                return false;
            }
            char nodeString[256];
            strncpy(nodeString, ptr, len);
            nodeString[len] = '\0';
            bool isNumber = true;
            for (size_t i = 0; i < len; i++)
            {
                if (0 == isdigit(nodeString[i]))
                {
                    isNumber = false;
                    break;
                }
            }
            unsigned long nodeId;
            if (isNumber)
            {
                if (1 != sscanf(nodeString, "%lu", &nodeId))
                {
                    PLOG(PL_FATAL, "NormApp::AddAckingNodes() error: bad acking node id: %s\n", nodeString);
                    return false;
                }
            }
            else
            {
                // It's an address or host name
                ProtoAddress nodeAddr;
                if (!nodeAddr.ResolveFromString(nodeString))
                {
                    PLOG(PL_FATAL, "NormApp::AddAckingNodes() error: bad acking node id: %s\n", nodeString);
                    return false;
                }
                nodeId = nodeAddr.EndIdentifier();
            }
            if (!session->SenderAddAckingNode((UINT32)nodeId))
            {
                PLOG(PL_FATAL, "NormApp::AddAckingNodes() error: couldn't add acking node \n");
                return false;
            }
            ptr = (NULL != end) ? ++end : NULL;
        }
        return true;
    }
    else
    {
        PLOG(PL_FATAL, "NormApp::AddAckingNodes() error: no session instantiated\n");
        return false;
    }
}  // end NormApp::AddAckingNodes()


void NormApp::DoInputReady(ProtoDispatcher::Descriptor /*descriptor*/, 
                           ProtoDispatcher::Event      /*theEvent*/, 
                           const void*                 userData)
{
    ((NormApp*)userData)->OnInputReady();
}  // end NormApp::DoDataReady()


void NormApp::OnInputReady()
{
    
    //DMSG(0, "NormApp::OnInputReady() ...\n");
    bool endOfStream = false;
    // write to the stream while input is available _and_
    // the stream has buffer space for input
    while (input || msg_test)
    {
        bool inputStarved = false;
            
        if (0 == input_length)
        {
            // We need input ...
            UINT16 readLength;
            if (input_messaging)
            {
                if (input_msg_length)
                {
                    UINT16 bufferSpace = 65535 - input_index;
                    UINT16 msgRemainder = input_msg_length - input_msg_index;
                    readLength = MIN(bufferSpace, msgRemainder);
                }
                else if (input_index < 2)
                {
                    readLength = 2 - input_index;   
                }
                else
                {
                    memcpy(&input_msg_length, input_buffer, 2);
                    input_msg_length = ntohs(input_msg_length);
                    ASSERT(input_msg_length >= 2);
                    UINT16 bufferSpace = 65535 - input_index;
                    UINT16 msgRemainder = input_msg_length - 2;
                    readLength = MIN(bufferSpace, msgRemainder);  
                }
            }
            else
            {
                readLength = segment_size;   
            }            
            
            if (msg_test)
            {
                // we're just sending our test message
                int hdrOffset;
                if (input_messaging)
                {
                    input_msg_length = msg_test_length;
                    UINT16 temp16 = ntohs(msg_test_length);
                    memcpy(input_buffer, &temp16, 2);
                    hdrOffset = 2;
                }
                else
                {
                    hdrOffset = 0;
                }
                int hdrLen = sprintf(input_buffer + hdrOffset, "NORM Test Msg %08u ", msg_test_seq++);
                char testChar = 'a' + msg_test_seq % 26;
                memset(input_buffer + hdrLen + hdrOffset, testChar, msg_test_length - (hdrLen + hdrOffset));
                input_buffer[msg_test_length - 1] = '\n';
                input_length = msg_test_length;
                input_index = 0;
            }
            else
            {
                // Read from "input" into our "input_buffer"
                size_t result = fread(input_buffer+input_index, sizeof(char), readLength, input);
                if (result > 0)
                {
                    if (input_messaging)
                    {
                        if (input_msg_length)
                        {
                            input_length = input_index + result;
                            input_index = 0;
                        }
                        else
                        {
                            input_length = 0;
                            input_index += (unsigned int)result;
                        }
                    }
                    else
                    {
                        input_length = input_index + result;
                        input_index = 0;    
                    }             
                }
                else
                {
                    if (feof(input))
                    {
                        PLOG(PL_ALWAYS, "norm: input end-of-file.\n");
                        if (input_active) 
                        {
    #ifdef WIN32
                            ASSERT(0);  // no Win32 support for stream i/o yet
    #else
                            dispatcher.RemoveGenericInput(fileno(input));
    #endif // if/else WIN32/UNIX
                            input_active = false;
                        }
                        if (stdin != input) fclose(input);
                        input = NULL;
                        endOfStream = true;   
                        tx_stream->SetFlushMode(NormStreamObject::FLUSH_ACTIVE);
                    }
                    else if (ferror(input))
                    {
    #ifndef WIN32  // no Win32 stream support yet
                        switch (errno)
                        {
                            case EINTR:
                                continue;
                            case EAGAIN:
                                // Input not ready, will get notification when ready
                                inputStarved = true;
                                break;
                            default:
                                PLOG(PL_ERROR, "norm: input error:%s\n", GetErrorString());
                                break;   
                        }
    #endif // !WIN32
                        clearerr(input);
                    }
                } 
            }  // end if/else msg_test
        }  // end if (0 == input_length)
        
        unsigned int writeLength = input_length;// ? input_length - input_index : 0;
            
        if (writeLength || endOfStream)
        {
            unsigned int wroteLength = tx_stream->Write(input_buffer+input_index,
                                                        writeLength, false);
            input_length -= wroteLength;
            if (0 == input_length)
                input_index = 0;
            else
                input_index += wroteLength;
            if (input_messaging && (0 != wroteLength)) 
            {
                input_msg_index += wroteLength;
                if (input_msg_index == input_msg_length)
                {
                    input_msg_index = 0;
                    input_msg_length = 0;  
                    // Mark EOM 
                    tx_stream->Write(NULL, 0, true); 
                    // No need to explicitly flush with "flush mode" set.
                    //tx_stream->Flush();
                }
            }
            if (wroteLength < writeLength) 
            {
                // Stream buffer full, temporarily deactive "input" and 
                // wait for next TX_QUEUE_EMPTY notification
                if (input_active)
                {
#ifdef WIN32
                    ASSERT(0);  // no Win32 support for stream i/o yet
#else
                    dispatcher.RemoveGenericInput(fileno(input));
#endif // if/else WIN32/UNIX
                    input_active = false;   
                }
                break;  
            }
        }
        else if (inputStarved)
        {
            // Input not ready, wait for "input" to be ready
            // (Activate/reactivate "input" as necessary
            if (!input_active)
            {
#ifdef WIN32
                ASSERT(0);  // no Win32 support for stream i/o yet
                if (false)
#else
                if (dispatcher.InstallGenericInput(fileno(input), NormApp::DoInputReady, this))
#endif // if/else WIN32/UNIX
                    input_active = true;
                else
                    PLOG(PL_ERROR, "NormApp::OnInputReady() error adding input notification!\n");
            } 
            break; 
        }
    }  // end while (input)
}  // NormApp::OnInputReady()


void NormApp::Notify(NormController::Event event,
                     class NormSessionMgr* sessionMgr,
                     class NormSession*    session,
                     class NormNode*       sender,
                     class NormObject*     object)
{
    if (notify_reentry)
    {
        Notification* notification;
        if (NULL == (notification = GetNotification()))
        {
            PLOG(PL_ERROR, "NormApp::Notify() error: unable to create Notification item!\n");
            return;
        }
        notification->event = event;
        return;
    }
    notify_reentry = true;
    switch (event)
    {
        case TX_QUEUE_VACANCY:
            PLOG(PL_DEBUG, "NormApp::Notify(TX_QUEUE_VACANCY) ...\n");
            if (NULL != object)
            {
                if ((msg_test || input) && (object == tx_stream))
                    OnInputReady();
            }
            break;
        case TX_QUEUE_EMPTY:
            // Write to stream as needed
            PLOG(PL_DEBUG, "NormApp::Notify(TX_QUEUE_EMPTY) ...\n");
            if (NULL != object)
            {
                if ((msg_test || input) && (object == tx_stream))
                    OnInputReady();
            }
            else if (tx_object_interval >= 0.0)
            {
                // Can queue a new object for transmission 
                if (interval_timer.IsActive()) interval_timer.Deactivate();
                if (interval_timer.GetInterval() > 0.0)
                    ActivateTimer(interval_timer); 
                else
                    OnIntervalTimeout(interval_timer);
            }
            break;
            
        case TX_OBJECT_SENT:
            PLOG(PL_DEBUG, "NormApp::Notify(TX_OBJECT_SENT) ...\n");
            break;
            
        case TX_FLUSH_COMPLETED:
            PLOG(PL_DEBUG, "NormApp::Notify(TX_FLUSH_COMPLETED) ...\n");
            if (tx_one_shot)
            {
                PLOG(PL_ALWAYS, "norm: transmit flushing completed, exiting.\n");
                Stop();
            }
            else if (tx_object_interval < 0.0)
            {
                // Can queue a new object for transmission 
                if (interval_timer.IsActive()) interval_timer.Deactivate();
                if (interval_timer.GetInterval() > 0.0)
                    ActivateTimer(interval_timer); 
                else
                    OnIntervalTimeout(interval_timer);
            }
            break;
            
        case TX_WATERMARK_COMPLETED:
        {
            PLOG(PL_DEBUG, "NormApp::Notify(TX_WATERMARK_COMPLETED) ...\n");
            //NormSession::AckingStatus status = session->SenderGetAckingStatus(NORM_NODE_ANY);
            watermark_pending = false;  // enable new watermark to be set
            if (tx_ack_shot)
            {
                PLOG(PL_ALWAYS, "norm: transmit flushing completed, exiting.\n");
                Stop();     
            }
            break;
        }
        
        case REMOTE_SENDER_ACTIVE:
            PLOG(PL_DEBUG, "NormApp::Notify(REMOTE_SENDER_ACTIVE) ...\n");
            break;
        
        case REMOTE_SENDER_INACTIVE:
            PLOG(PL_DEBUG, "NormApp::Notify(REMOTE_SENDER_INACTIVE) ...\n");
            if (!rx_persistent) 
                static_cast<NormSenderNode*>(sender)->FreeBuffers();
            break;
           
           
        case RX_OBJECT_NEW:
        {
            PLOG(PL_DEBUG, "NormApp::Notify(RX_OBJECT_NEW) ...\n");
            struct timeval currentTime;
            ProtoSystemTime(currentTime);
            time_t secs = (time_t)currentTime.tv_sec;
            struct tm* timePtr = gmtime(&secs); 
            PLOG(PL_INFO, "%02d:%02d:%02d.%06lu start rx object>%hu sender>%lu\n",
		            timePtr->tm_hour, timePtr->tm_min, timePtr->tm_sec, 
		            (unsigned long)currentTime.tv_usec, (UINT16)object->GetId(), sender->GetId());
            
            // It's up to the app to "accept" the object
            switch (object->GetType())
            {
                case NormObject::STREAM:
                {
                    // Only have one rx_stream at a time for now.
                    // Reset stream i/o mgmt                    
                    output_msg_length = output_index = 0;
                    if (output_messaging) output_msg_sync = false;
                    
                    // object Size() has recommended buffering size
                    NormObjectSize size;
                    if (silent_receiver)
                        size =  rx_buffer_size;
                    else
                        size = (UINT32)object->GetSize().LSB();  
                    ASSERT(0 == size.MSB());
                    if (((NormStreamObject*)object)->Accept(size.LSB()))
                    {
                        rx_stream = (NormStreamObject*)object;
                    }
                    else
                    {
                        PLOG(PL_ERROR, "NormApp::Notify(RX_OBJECT_NEW) stream object accept error!\n");
                    }
                }
                break;                        
                case NormObject::FILE:
                {
                    if (rx_cache_path)
                    {
                        // (TBD) re-arrange so if we've already recv'd INFO,
                        // we can use that for file name right away ???
                        // (TBD) Manage recv file name collisions, etc ...
                        char fileName[PATH_MAX];
                        strcpy(fileName, rx_cache_path);
                        size_t catMax = strlen(fileName);
                        if (catMax > PATH_MAX)
                            catMax = 0;
                        else
                            catMax = PATH_MAX - catMax;
                        strcat(fileName, "normTempXXXXXX");
#ifdef WIN32
#ifdef _WIN32_WCE
                        bool tempFileOK = false;
                        for (int i = 0; i < 255; i++)
                        {
                            strncpy(fileName, rx_cache_path, PATH_MAX);
                            catMax = strlen(fileName);
                            if (catMax > PATH_MAX) 
                                catMax = 0;
                            else
                                catMax = PATH_MAX - catMax;
                            strncat(fileName, "normTempXXXXXX", catMax);
                            char tempName[16];
                            sprintf(tempName, "normTemp%06u", i);
                            strcat(fileName, tempName);
                            if(!NormFile::IsLocked(fileName))
                            {
                                tempFileOK = true;
                                break;
                            }
                        }
                        if (!tempFileOK)
#else
                        if (!_mktemp(fileName))
#endif // if/else _WIN32_WCE
#else
                        int fd = mkstemp(fileName); 
                        if (fd >= 0)
                        {
                            close(fd);
                        }
                        else   
#endif // if/else WIN32         
                        {
                            PLOG(PL_ERROR, "NormApp::Notify(RX_OBJECT_NEW) Warning: mkstemp() error: %s\n",
                                    GetErrorString());  
                        } 
                        if (!static_cast<NormFileObject*>(object)->Accept(fileName))
                        {
                            PLOG(PL_ERROR, "NormApp::Notify(RX_OBJECT_NEW) file object accept error!\n");
                        }
                    }
                    else
                    {
                        PLOG(PL_ERROR, "NormApp::Notify(RX_OBJECT_NEW) no rx cache for file\n");   
                    }                    
                }
                break;
                case NormObject::DATA: 
                    PLOG(PL_ERROR, "NormApp::Notify() FILE/DATA objects not _yet_ supported...\n");      
                    break;
                    
                case NormObject::NONE:
                    break;
            }   
            break;
        }
            
        case RX_OBJECT_INFO:
            PLOG(PL_DEBUG, "NormApp::Notify(RX_OBJECT_INFO) ...\n");
            switch(object->GetType())
            {
                case NormObject::FILE:
                {
                    // Rename rx file using newly received info
                    char fileName[PATH_MAX];
                    strncpy(fileName, rx_cache_path, PATH_MAX);
                    UINT16 pathLen = (UINT16)strlen(rx_cache_path);
                    pathLen = MIN(pathLen, PATH_MAX);
                    UINT16 len = object->GetInfoLength();
                    len = MIN(len, (PATH_MAX - pathLen));
                    strncat(fileName, object->GetInfo(), len);
                    // Convert '/' in file info to directory delimiters
                    for (UINT16 i = pathLen; i < (pathLen+len); i++)
                    {
                        if ('/' == fileName[i]) 
                            fileName[i] = PROTO_PATH_DELIMITER;
                    }
                    pathLen += len;
                    if (pathLen < PATH_MAX) fileName[pathLen] = '\0';
                    
                    // Deal with concurrent rx name collisions
                    // (TBD) and implement overwrite policy
                    //       and cache files in cache mode
                    
                    if (!(static_cast<NormFileObject*>(object)->Rename(fileName)))
                    {
                        PLOG(PL_ERROR, "NormApp::Notify() Error renaming rx file: %s\n",
                                fileName);
                    }
                    break;
                }
                case NormObject::DATA:
                case NormObject::STREAM:
                case NormObject::NONE:
                    break;
            }  // end switch(object->GetType())
            break;
            
        case RX_OBJECT_UPDATED:
            PLOG(PL_TRACE, "NormApp::Notify(RX_OBJECT_UPDATED) ...\n");
            switch (object->GetType())
            {
                case NormObject::FILE:
                    // (TBD) update reception progress display when applicable
                    // Call object->SetNotifyOnUpdate(true) here to keep
                    // the update notifications coming. Otherwise they stop!
                    break;
                
                case NormObject::STREAM:
                {
                    if (object != rx_stream)
                    {
                        PLOG(PL_ERROR, "NormApp::Notify(RX_OBJECT_UPDATED) update for invalid stream\n");
                        break;   
                    }
                    // Read the stream when it's updated  
                    ASSERT(NULL != output);
                    bool reading = true;
                    bool seekMsgStart;
                    while (reading)
                    {
                        unsigned int readLength;
                        if (output_messaging)
                        {
                            if (output_msg_length)
                            {
                                readLength = output_msg_length - output_index;
                            }
                            else if (output_index < 2)
                            {
                                readLength = 2 - output_index;
                            }
                            else
                            {
                                memcpy(&output_msg_length, output_buffer, 2);
                                output_msg_length = ntohs(output_msg_length);
                                ASSERT(output_msg_length >= 2);
                                readLength = output_msg_length - output_index;
                            }     
                            seekMsgStart = output_msg_sync ? false : true;                                            
                        }
                        else
                        {
                            output_index = 0;
                            readLength = 512; 
                            seekMsgStart = false;   
                        } 
                        
                        if(!((NormStreamObject*)object)->Read(output_buffer+output_index, 
                                                              &readLength, seekMsgStart))
                        {
                            // The stream broke
                            if (output_messaging)
                            {
                                if (output_msg_sync)
                                    PLOG(PL_ERROR, "NormApp::Notify() detected broken stream ...\n");
                                output_msg_length = output_index = 0;
                                output_msg_sync = false;
                                if (seekMsgStart)
                                    break;  // didn't find msg start yet
                                else
                                    continue;  // try to re-sync
                            }
                        }
                        else if (readLength > 0)
                        {
                            output_msg_sync = true;
                            output_index += readLength;
                        }
                        else
                        {
                            reading = false;
                        }

                        unsigned int writeLength;
                        if (output_messaging)
                        {
                            if (output_msg_length && (output_index >= output_msg_length))
                            {
                                writeLength = output_msg_length;
                                output_msg_length = 0;
                                output_index = 0;
                            }
                            else
                            {
                                writeLength = 0;
                            }
                        }
                        else
                        {
                            writeLength = readLength;
                            output_index = 0;
                        }      
                        
                        unsigned int put = 0;
                        while (put < writeLength)
                        {
                            size_t result = fwrite(output_buffer+put, sizeof(char), writeLength-put, output);
                            
                            if (result)
                            {
                                put += (unsigned int)result;   
                            }
                            else
                            {
                                if (ferror(output))
                                {
#ifndef WIN32  // no stream support in this app for Win32
                                    if (EINTR == errno) 
                                    {
                                        clearerr(output);
                                        continue;
                                    }
                                    else
#endif // !WIN32
                                    {
                                        PLOG(PL_ERROR, "norm: output error:%s\n", GetErrorString());
                                        clearerr(output);
                                        break;
                                    }   
                                }
                            }   
                        }  // end while(put < nBytes)
                        if (writeLength > 0) 
                        {
                            memset(output_buffer, 0, writeLength); // why did do this?
                            fflush(output);
                        }
                    }  // end while (reading)
                    
                    break;
                }
                                        
                case NormObject::DATA: 
                    PLOG(PL_ERROR, "NormApp::Notify() DATA objects not _yet_ supported...\n");      
                    break;
                case NormObject::NONE:
                    break;
            }  // end switch (object->GetType())
            break;
            
        case RX_OBJECT_COMPLETED:
        {
            // (TBD) if we're not archiving files we should
            //       manage our cache, deleting the cache
            //       on shutdown ...
            PLOG(PL_DEBUG, "NormApp::Notify(RX_OBJECT_COMPLETED) ...\n");
            // We had to split the debug output line here into two because of some
            // weirdness with the underlying PLOG() code and too many arguments?
            if (GetDebugLevel() >= PL_INFO)
            {
                struct timeval currentTime;
                ProtoSystemTime(currentTime);
                time_t secs = (time_t)currentTime.tv_sec;
                struct tm* timePtr = gmtime(&secs);
                PLOG(PL_INFO, "%02d:%02d:%02d.%06u completed rx object>%hu ",
		                (int)timePtr->tm_hour, (int)timePtr->tm_min, (int)timePtr->tm_sec, (unsigned int)currentTime.tv_usec, 
                        (UINT16)object->GetId());
                //TRACE("sender>%lu\n", sender->GetId());
            }
            switch(object->GetType())
            {
                case NormObject::FILE:
                {
                    const char* filePath = static_cast<NormFileObject*>(object)->GetPath();
                    //DMSG(0, "norm: Completed rx file: %s\n", filePath);
                    if (post_processor->IsEnabled())
                    {
                        if (!post_processor->ProcessFile(filePath))
                        {
                            PLOG(PL_ERROR, "norm: post processing error\n");
                        }  
                    }
                    break;
                }
                    
                case NormObject::STREAM:
                    ASSERT(0);
                    break;
                case NormObject::DATA:
                    ASSERT(0);
                    break;
                case NormObject::NONE:
                    break;
            }
            break;
        }
        
        case RX_OBJECT_ABORTED:
        {
            PLOG(PL_FATAL, "NormApp::Notify(RX_OBJECT_ABORTED) ...\n");
            struct timeval currentTime;
            ProtoSystemTime(currentTime);
            time_t secs = (time_t)currentTime.tv_sec;
            struct tm* timePtr = gmtime(&secs); 
            PLOG(PL_INFO, "%02d:%02d:%02d.%06lu aborted rx object>%hu sender>%lu\n",
		            timePtr->tm_hour, timePtr->tm_min, timePtr->tm_sec, 
		            (unsigned long)currentTime.tv_usec, (UINT16)object->GetId(), sender->GetId());
            switch(object->GetType())
            {
                case NormObject::FILE:
                {
                    const char* filePath = static_cast<NormFileObject*>(object)->GetPath();
                    if (process_aborted_files)
                    {
                        // in case file size isn't padded properly
                        static_cast<NormFileObject*>(object)->PadToSize();
                        if (post_processor->IsEnabled())
                        {
                            if (!post_processor->ProcessFile(filePath))
                            {
                                PLOG(PL_ERROR, "norm: post processing error\n");
                            }  
                        }
                    }
                    else
                    {
                        NormFile::Unlink(filePath);
                    }
                    break;
                }
                default:
                    break;
            }
            break;
        }
        
        default:
        {
            PLOG(PL_DEBUG, "NormApp::Notify() unhandled event: %d\n", event);
            break;
        }
    }  // end switch(event)
    notify_reentry = false;
    // Now handle any deferred, reentrant notifications
    Notification* notification;
    while (NULL != (notification = notify_queue.RemoveHead()))
    {
        Notify(notification->event, sessionMgr, session, sender, object);
        notify_pool.Append(*notification);
    }
}  // end NormApp::Notify()


bool NormApp::OnIntervalTimeout(ProtoTimer& theTimer)
{
    char fileName[PATH_MAX];
    if (('\0' != tx_file_name[0]) || tx_file_list.GetNextFile(fileName))
    {
        tx_repeat_clear = true;
        
        // The "tx_file_name" is used to cache the path of the current file for enqueue
        // re-attempt upon possible flow control NormSession::QueueTxFile() failure
        if ('\0' == tx_file_name[0])
            strcpy(tx_file_name, fileName);
        else
            strcpy(fileName, tx_file_name);  // used cached tx_file_name for enqueue re-attempt
        
        // 1) Build up the full path name of the file
        char pathName[PATH_MAX];
        tx_file_list.GetCurrentBasePath(pathName);
        size_t len = strlen(pathName);
        len = MIN(len, PATH_MAX);
        size_t maxLen = PATH_MAX - len;
        char* ptr = fileName + len;
        len = strlen(ptr);
        len = MIN(len, maxLen);
        // (TBD) Make sure len <= segment_size)
        char fileNameInfo[PATH_MAX];
        strncpy(fileNameInfo, ptr, len);
        // Normalize directory delimiters in file name info
        for (unsigned int i = 0; i < len; i++)
        {
            if (PROTO_PATH_DELIMITER == fileNameInfo[i]) 
                fileNameInfo[i] = '/';
        }
        
        FileCacheItem* fileCacheItem = NULL;
        NormFileObject* obj = NULL;
        if (0 != tx_requeue)
        {
            // Is this file already in our "requeue" tx_file_cache?
            if (NULL != (fileCacheItem = static_cast<FileCacheItem*>(tx_file_cache.Find(fileName, PATH_MAX << 3))))
            {
                // Try to find and requeue cached tx object
                obj = static_cast<NormFileObject*>(session->SenderFindTxObject(fileCacheItem->GetObjectId()));
                bool requeueSuccess = false;
                if (NULL != obj) requeueSuccess = session->RequeueTxObject(obj);
                if (!requeueSuccess)
                {
                    PLOG(PL_ERROR, "norm warning: requeue attempt exceeded configured tx cache bounds!\n");
                    // TBD - remove file from tx_file_cache???
                }
                // RequeueTxObject() is not subject to flow control since the object is already buffered
                tx_file_name[0] = '\0'; // reset so next file will be fetched
            }
        }
        if (NULL == obj)
        {
            // This makes sure it's not an empty file
            // (TBD - provide for empty files to be sent)
            if (0 == NormFile::GetSize(fileName))
            {
                PLOG(PL_WARN, "norm warning: empty file \"%s\" will not be transmitted\n");
                tx_file_name[0] = '\0'; // reset so next file will be fetched
                return OnIntervalTimeout(theTimer);
            }
            // It's a new object
            const char* infoPtr = tx_file_info ? fileNameInfo : NULL;
            UINT16 infoLen = tx_file_info ? (UINT16)len : 0;
            if (NULL == (obj = session->QueueTxFile(fileName, infoPtr, infoLen)))
            {
                PLOG(PL_DEBUG, "NormApp::OnIntervalTimeout() error queuing tx file: %s\n", fileName);
                // The code currently assumes that QueueTxFile() failed because of
                // flow control and so we simply return and wait for a TX_QUEUE_EMPTY notification
                // to trigger a re-attempt to enqueue the file name in "tx_file_name"
                if (interval_timer.IsActive()) interval_timer.Deactivate();
                // Old behavior was to wait a second and go to next file
                // (commented out here)
                //interval_timer.SetInterval(1.0);
                //ActivateTimer(interval_timer);
                return false;
            }
            else
            {
                tx_file_name[0] = '\0';  // reset so next file will be fetched
            }
            if (0 != tx_requeue)
            {
                if (NULL == fileCacheItem)
                {
                    // Cache for "requeue" if applicable
                    // It's not in the cache, so add it ...
                    if (NULL == (fileCacheItem = new FileCacheItem(fileName)))
                    {
                        PLOG(PL_ERROR, "NormApp::OnIntervalTimeout(): new FileCachItem error: %s\n",
                                GetErrorString());
                    }
                    tx_file_cache.Insert(*fileCacheItem);
                }
                if (NULL != fileCacheItem)
                    fileCacheItem->SetObjectId(obj->GetId());
            }
        }
        
        tx_file_queued = true;
        
        
        struct timeval currentTime;
        ProtoSystemTime(currentTime);
        time_t secs = (time_t)currentTime.tv_sec;
        struct tm* timePtr = gmtime(&secs); 
        PLOG(PL_INFO, "%02d:%02d:%02d.%06lu enqueued tx object>%hu sender>%lu\n",
		        timePtr->tm_hour, timePtr->tm_min, timePtr->tm_sec, 
		        (unsigned long)currentTime.tv_usec, (UINT16)obj->GetId(), session->LocalNodeId());
        
        // save last obj/block/seg id for later in case needed
        tx_last_object_id = obj->GetId();
        tx_last_block_id = obj->GetFinalBlockId();
        tx_last_segment_id = obj->GetBlockSize(tx_last_block_id) - 1;
        if (!watermark_pending && (NULL != acking_node_list) && !tx_ack_shot)
        {
            session->SenderSetWatermark(tx_last_object_id, 
                                        tx_last_block_id,
                                        tx_last_segment_id,
                                        acking_flushes);
            watermark_pending = true;  // only allow one pending watermark at a time
        }
        //DMSG(0, "norm: File \"%s\" queued for transmission.\n", fileName);
        interval_timer.SetInterval(tx_object_interval);
    }
    else if (0 != tx_requeue_count)
    {
        if (tx_requeue_count > 0) tx_requeue_count--;
        tx_file_list.ResetIterator();
        return OnIntervalTimeout(interval_timer);
    }
    else if (0 != tx_repeat_count)
    {
        // (TBD) When repeating, remove previous instance from tx queue???
        if (tx_repeat_count > 0) tx_repeat_count--;
        tx_file_cache.Destroy();
        tx_requeue_count = tx_requeue;
        tx_file_list.ResetIterator();
        if (tx_repeat_interval > tx_object_interval)
        {
            if (interval_timer.IsActive()) interval_timer.Deactivate();
            interval_timer.SetInterval(tx_repeat_interval - tx_object_interval);
            ActivateTimer(interval_timer);
            return false;       
        }
        else
        {
            if (tx_repeat_clear)
            {
                tx_repeat_clear = false;
                return OnIntervalTimeout(interval_timer);
            }
            else
            {
                tx_repeat_clear = true;
                if (interval_timer.IsActive()) interval_timer.Deactivate();
                interval_timer.SetInterval(tx_repeat_interval);
                ActivateTimer(interval_timer);
                return false; 
            }
        }
    }
    else
    {
        tx_repeat_clear = true;
        PLOG(PL_ALWAYS, "norm: End of tx file list reached.\n");  
        if (tx_ack_shot && !watermark_pending && tx_file_queued)
        {
            session->SenderSetWatermark(tx_last_object_id, 
                                        tx_last_block_id,
                                        tx_last_segment_id,
                                        acking_flushes);
            watermark_pending = true;  // only allow one pending watermark at a time
        }
    }   
    return true;
}  // end NormApp::OnIntervalTimeout()

bool NormApp::OnStartup(int argc, const char*const* argv)
{
    if (argc < 3) 
    {
        ShowHelp();
        return false; 
    }
       
    if (!(post_processor = NormPostProcessor::Create()))
    {
        PLOG(PL_FATAL, "NormApp::OnStartup() error creating post processor\n");
        return false;   
    }
    
    if (!ProcessCommands(argc, argv))
    {
        PLOG(PL_FATAL, "NormApp::OnStartup() error processing command-line commands\n");
        return false; 
    }
    
    

    if (control_remote) return false;

#ifdef UNIX    
    signal(SIGCHLD, SignalHandler);
#endif // UNIX
        
    // Validate our application settings
    if (!address)
    {
        PLOG(PL_FATAL, "NormApp::OnStartup() Error! no session address given.\n");
        return false;
    }
    if (!input && !output && tx_file_list.IsEmpty() && !rx_cache_path && !msg_test)
    {
        PLOG(PL_FATAL, "NormApp::OnStartup() Error! no \"input\", \"output\", "
                "\"sendfile\", \"stest\", \"mtest\", or \"rxcache\" given.\n");
        return false;
    }   
    
    if ((NULL != output) && (NULL != output_io_buffer))
    {
        if (0 != setvbuf(output, output_io_buffer, _IOFBF, output_io_bufsize))
        {
            PLOG(PL_ERROR, "NormApp::OnStartup() setvbuf() error: %s\n", GetErrorString());
        }
    }
    
    // Create a new session on multicast group/port
    session = session_mgr.NewSession(address, port, node_id);
    if (session)
    {
        // Common session parameters
        session->SetTxPort(tx_port);
        session->SetTxRate(tx_rate);
        session->SetTxRateBounds(tx_rate_min, tx_rate_max);
        session->SetTrace(tracing);
        session->SetTxLoss(tx_loss);
        session->SetRxLoss(rx_loss);
        session->SetTTL(ttl);
        session->SetLoopback(loopback);
        if (loopback && session->Address().IsMulticast())
            session->SetRxPortReuse(true);
        if (NULL != interface_name)
            session->SetMulticastInterface(interface_name);
        
        session->SetEcnSupport(ECN_OFF != ecn_mode, ECN_ONLY == ecn_mode, tolerate_loss);
        
        // We turn off flow control when no NACKing is happening
        // (TBD - make flow control a command-line option)
        if (silent_receiver)
            session->SetFlowControl(0.0); 
        
        if (msg_test || input || !tx_file_list.IsEmpty())
        {
            NormObjectId baseId = (unsigned short)(rand() * (65535.0/ (double)RAND_MAX));
            session->SenderSetBaseObjectId(baseId);
            session->SetCongestionControl(cc_enable);
            session->SetBackoffFactor(backoff_factor);
            session->SenderSetGrtt(grtt_estimate);
            session->SetGrttProbingMode(grtt_probing_mode);
            session->SenderSetGroupSize(group_size);
            session->SetTxRobustFactor(tx_robust_factor);
            session->SetTxCacheBounds(tx_cache_size, tx_cache_min, tx_cache_max);
            if (!AddAckingNodes(acking_node_list))
            {
                PLOG(PL_FATAL, "NormApp::OnStartup() error: bad acking node list\n");
                session_mgr.Destroy();
                return false;
            }
            // redundant info transmission if we have silent receivers
            if (silent_receiver) session->SndrSetEmcon(true);
            // We also use the baseId as our sender's "instance id" for illustrative purposes
            UINT16 instanceId = baseId;
            if (!session->StartSender(instanceId, tx_buffer_size, segment_size, ndata, nparity))
            {
                PLOG(PL_FATAL, "NormApp::OnStartup() start sender error!\n");
                session_mgr.Destroy();
                return false;
            }              
	        if (tx_sock_buffer_size > 0)
		        session->SetTxSocketBuffer(tx_sock_buffer_size);
            session->SenderSetAutoParity(auto_parity);
            session->SenderSetExtraParity(extra_parity);
            if (input || msg_test)
            {
                // Open a stream object to write to (QueueTxStream(stream bufferSize))
                tx_stream = session->QueueTxStream(tx_buffer_size);
                if (!tx_stream)
                {
                    PLOG(PL_FATAL, "NormApp::OnStartup() queue tx stream error!\n");
                    session_mgr.Destroy();
                    return false;
                }
                tx_stream->SetFlushMode(msg_flush_mode);
                tx_stream->SetPushMode(push_mode);
            }
            else
            {
                OnIntervalTimeout(interval_timer);
            }            
        }
        
        if (output || rx_cache_path)
        {
            // StartReceiver(bufferMax (per-sender))
            session->SetRxCacheMax(tx_cache_max);
            session->ReceiverSetUnicastNacks(unicast_nacks);
            session->ReceiverSetSilent(silent_receiver);
            if (preallocate_sender)
            {
                if (!session->PreallocateRemoteSender(rx_buffer_size, segment_size, ndata, nparity))
                {
                    PLOG(PL_FATAL, "NormApp::OnStartup() remote sender preallocation error!\n");
                    session_mgr.Destroy();
                    return false;
                }
            }
            if (low_delay)
                session->RcvrSetMaxDelay(1);
            else
                session->RcvrSetMaxDelay(-1); 
            if (realtime)
                session->RcvrSetRealtime(true);           
            session->SetRxRobustFactor(rx_robust_factor);
            session->ReceiverSetDefaultRepairBoundary(repair_boundary);
            if (!session->StartReceiver(rx_buffer_size))
            {
                PLOG(PL_FATAL, "NormApp::OnStartup() start receiver error!\n");
                session_mgr.Destroy();
                return false;
            }
            if (rx_sock_buffer_size > 0)
                session->SetRxSocketBuffer(rx_sock_buffer_size);
        }
        if (precise) dispatcher.SetPreciseTiming(true);
        if (boost) dispatcher.SetPriorityBoost(true);
        return true;
    }
    else
    {
        PLOG(PL_FATAL, "NormApp::OnStartup() new session error!\n");
        return false;
    }
}  // end NormApp::OnStartup()

void NormApp::OnShutdown()
{
    //DMSG(0, "NormApp::OnShutdown() ...\n");
    session_mgr.Destroy();
    if (input)
    {
        if (input_active) 
        {
#ifdef WIN32
            ASSERT(0);   // no Win32 support for stream i/o yet
#else
            dispatcher.RemoveGenericInput(fileno(input));
#endif // if/else WIN32/UNIX
            input_active = false;
        }
        if (stdin != input) fclose(input);
        input = NULL;
    }
    if (output) 
    {
        if (stdout != output) fclose(output);
        output = NULL;
    }
    if (post_processor)
    {
        delete post_processor;
        post_processor = NULL;   
    }
}  // end NormApp::OnShutdown()

// Out application instance 
PROTO_INSTANTIATE_APP(NormApp)

#ifdef UNIX
void NormApp::SignalHandler(int sigNum)
{
    switch(sigNum)
    {
        case SIGCHLD:
        {
            NormApp* app = static_cast<NormApp*>(ProtoApp::GetApp());
            if (app->post_processor) app->post_processor->OnDeath();
            // The use of "waitpid()" here is a work-around for
            // an IRIX SIGCHLD issue
            int status;
            while (waitpid(-1, &status, WNOHANG) > 0);
            signal(SIGCHLD, SignalHandler);
            break;
        }
            
        default:
            fprintf(stderr, "norm: Unexpected signal: %d\n", sigNum);
            break; 
    }  
}  // end NormApp::SignalHandler()
#endif // UNIX
