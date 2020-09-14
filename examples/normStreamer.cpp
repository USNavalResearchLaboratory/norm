
#include "normApi.h"
#include "protoSocket.h"

#include <stdio.h>       // for printf(), etc
#include <stdlib.h>      // for srand()
#include <string.h>      // for strrchr(), memset(), etc
#include <sys/time.h>    // for gettimeofday()
#include <arpa/inet.h>   // for htons()
#include <fcntl.h>       // for, well, fnctl()
#include <errno.h>       // obvious child
#include <assert.h>      // embarrassingly obvious
#include <sys/mman.h>    // Memory Lock.
#include <sched.h>       // Adjust scheduler (linux)
#include <sys/resource.h> // for setpriority() stuff
#ifdef LINUX
#include <sys/timerfd.h>
#endif // LINUX

const unsigned int LOOP_MAX = 100;

// Setting SHOOT_FIRST to non-zero means that an ACK request
// will be used to advance the acking "watermark" point
// with each message fully written to the transmit stream.
// The alternative "ack later" behavior waits to send a new
// ACK request until any pending flow control ACK requeset
// has completed.  This latter approach favors throughput
// over timeliness of message delivery.  I.e., lower data
// rate applications that are concerned with low-latency message
// delivery can potentially benefit from the "shoot first" 
// behavior while very high throughput applications that want
// to "keep the pipe full as possible" can benefit from the
// "ack later" behavior.  The difference between these behaviors,
// since ACK requests are cued for all messages when flow
// control is _not_ pending, is somewhat subtle and developers
// may want to assess both behaviors for their application.  
// Additionally, limiting ACK request to flow control only is
// another possible approach as well as dynamically updating
// something like the "tx_stream_buffer_count" with each 
// message ACK request initiated could be possible.  The caution
// with the SHOOT_FIRST type strategies and high throughput is
// the application may end up "chasing" the ACK request until
// flow control buffer limits are reached and end up with 
// "dead air" time.  There are always tradeoffs!

//#define SHOOT_FIRST 0

class NormStreamer
{
    public:
        NormStreamer();
        ~NormStreamer();
        
        // some day build these directly into NORM API
        enum CCMode {NORM_FIXED, NORM_CC, NORM_CCE, NORM_CCL};
        
        enum 
        {
            MSG_HEADER_SIZE = 2,    // Big Endian message length header size
            MSG_SIZE_MAX = 65535    // (including length header)  
        };  
            
        void SetOutputFile(FILE* filePtr)
        {
            output_file = filePtr;
            output_fd = fileno(filePtr);
        }
        
        void SetLoopback(bool state)
        {
            loopback = state;
            if (NORM_SESSION_INVALID != norm_session)
                NormSetMulticastLoopback(norm_session, state);
        }  
        void SetFtiInfo(bool state)
        {
            fti_info = state;
            if (NORM_SESSION_INVALID != norm_session)
                NormLimitObjectInfo(norm_session, state);
        }     
        
       void SetAckEx(bool state)
           {ack_ex = state;}
        
        bool EnableUdpRelay(const char* relayAddr, unsigned short relayPort);
        bool EnableUdpListener(unsigned short thePort, const char* groupAddr, const char * interfaceName);
        bool UdpListenerEnabled() const
            {return input_socket.IsOpen();}
        bool UdpRelayEnabled() const
            {return output_socket.IsOpen();}
        
        int GetInputDescriptor() const
            {return (input_socket.IsOpen() ? input_socket.GetHandle() : fileno(input_file));}
        int GetOutputDescriptor() const
            {return (output_socket.IsOpen() ? output_socket.GetHandle() : fileno(output_file));} 
            
        bool OpenNormSession(NormInstanceHandle instance, 
                             const char*        addr,
                             unsigned short     port,
                             NormNodeId         nodeId);
        void CloseNormSession();
        
        void SetNormCongestionControl(CCMode ccMode);
        void SetFlushMode(NormFlushMode flushMode)
            {flush_mode = flushMode;}
        void SetNormTxRate(double bitsPerSecond)
        {
            assert(NORM_SESSION_INVALID != norm_session);
            NormSetTxRate(norm_session, bitsPerSecond);
        }
        void SetNormMulticastInterface(const char* ifaceName)
        {
            assert(NORM_SESSION_INVALID != norm_session);
            NormSetMulticastInterface(norm_session, ifaceName);
        } 
        void SetNormMessageTrace(bool state)
        {
            assert(NORM_SESSION_INVALID != norm_session);
            NormSetMessageTrace(norm_session, state);
        } 
        void AddAckingNode(NormNodeId ackId)
        {
            assert(NORM_SESSION_INVALID != norm_session);
            NormAddAckingNode(norm_session, ackId);
            acking_node_count++;
            norm_acking = true;  // invoke ack-based flow control
        }
        void SetAutoAck(bool enable)
        {
            auto_ack = enable;
            norm_acking = enable;
        }
        
        bool Start(bool sender, bool receiver);
        void Stop()
            {is_running = false;}
        bool IsRunning() const
            {return is_running;}
        void HandleNormEvent(const NormEvent& event);
        
        // Sender methods
        int GetInputFile() const
            {return input_fd;}
        void SetInputReady() 
            {input_ready = true;}
        bool InputReady() const
            {return input_ready;}
        bool InputNeeded() const
            {return input_needed;}
        void ReadInput();
        void ReadInputSocket();
        bool TxPending() const
            {return (!input_needed && (input_index < input_msg_length));}
        bool TxReady() const
            {return (tx_ready && (!norm_acking || (tx_stream_buffer_count < tx_stream_buffer_max)));}
        void SendData();
        unsigned int WriteToStream(const char* buffer, unsigned int numBytes);
        void FlushStream(bool eom, NormFlushMode flushMode);
        
        // Receiver methods
        bool RxNeeded() const
            {return rx_needed;}
        bool RxReady() const
            {return rx_ready;}
        void RecvData();
        int GetOutputFile() const
            {return output_fd;}
        void SetOutputReady()
            {output_ready = true;}
        bool OutputReady() const
            {return output_ready;}
        bool OutputPending() const
            {return (!rx_needed && (output_index < output_msg_length));}
        
        
        void SetOutputBucketRate(double bitsPerSecond) 
        {
            output_bucket_rate = bitsPerSecond / 8.0;  // convert to bytes per second
            output_bucket_interval = 1.0 / output_bucket_rate;
        }
        
        void SetOutputBucketDepth(unsigned int numBytes)
            {output_bucket_depth = numBytes;}
        unsigned int GetOutputBucketDepth() const
            {return output_bucket_depth;}
        
        double GetOutputBucketTimeout() const
        {
            if (0 != output_bucket_depth)
            {
                if (OutputPending())
                {
                    unsigned int pendingBytes = output_msg_length - output_index;
                    if (pendingBytes > output_bucket_count)
                    {
                        return ((double)(pendingBytes - output_bucket_count)) * output_bucket_interval; 
                    }
                    else
                    {
                        return 0.0;
                    }
                }
                else
                {
                    return -1.0;
                }
            }
            else
            {
                return 0.0;
            }   
        }
        
        double GetOutputBucketFillTime() const
        {
            return (output_bucket_count < output_bucket_depth) ?
                ((double)(output_bucket_depth - output_bucket_count)) * output_bucket_interval :
                0.0;     
        }
        
        bool OutputBucketReady() const
        {
            if (0 != output_bucket_depth)
            {
                unsigned int pendingBytes = output_msg_length - output_index;
                return (output_bucket_count >= pendingBytes);
            }
            else
            {
                return true;
            }
        }
        void CreditOutputBucket(double interval)
        {
            if (0 != output_bucket_depth)
            {
                output_bucket_count += interval * output_bucket_rate;
                if (output_bucket_count > output_bucket_depth)
                    output_bucket_count = output_bucket_depth;
            }
        }
        
        void WriteOutput();
        void WriteOutputSocket();
        
        void OmitHeader(bool state) 
            {omit_header = state;}
        
        unsigned long GetInputByteCount() const
            {return input_byte_count;}
        unsigned long GetTxByteCount() const
            {return tx_byte_count;}
        
        // These can only be called post-OpenNormSession()
        void SetSilentReceiver(bool state)
            {NormSetSilentReceiver(norm_session, state);}
        void SetTxLoss(double txloss)
            {NormSetTxLoss(norm_session, txloss);}
        // Set the scheduler for running the app and norm threads.
        static bool BoostPriority();
        
        void SetSegmentSize(unsigned short segmentSize)
            {segment_size = segmentSize;}
        void SetBlockSize(unsigned short blockSize)
            {block_size = blockSize;}
        void SetNumParity(unsigned short numParity)
            {num_parity = numParity;}
        void SetAutoParity(unsigned short autoParity)
            {auto_parity = autoParity;}
        
        void SetStreamBufferSize(unsigned int value)
            {stream_buffer_size = value;}
        void SetTxSocketBufferSize(unsigned int value)
            {tx_socket_buffer_size = value;}
        void SetRxSocketBufferSize(unsigned int value)
            {rx_socket_buffer_size = value;}
        
        void SetInputSocketBufferSize(unsigned int value)
            {input_socket_buffer_size = value;}
        void SetOutputSocketBufferSize(unsigned int value)
            {output_socket_buffer_size = value;}
        
        void SetProbeTOS(UINT8 value)
            {probe_tos = value;}
        
        // Check that sequence numbers increase by one each time.
        // Assumes that sequence number is 8- or 4-byte network-order first 8 bytes of buffer.
        void CheckSequenceNumber(const char* buffer, const char* source);
        void CheckSequenceNumber64(const char* buffer, const char* source);
        void CheckSequenceNumber32(const char* buffer, const char* source);
        void SetCheckSequence(unsigned int value)  // 64 or 32
            {check_sequence = value;}
        
    private:
        NormSessionHandle   norm_session;
        bool                is_multicast;
        UINT8               probe_tos;
        bool                loopback;
        bool                is_running;     
                                                     
        // State variables for reading input messages for transmission
        ProtoSocket         input_socket;    // optional UDP socket to "listen"
        FILE*               input_file;
        int                 input_fd;      // stdin by default  
        bool                input_ready;                       
        bool                input_needed;    
        char                input_buffer[MSG_SIZE_MAX]; 
        unsigned int        input_msg_length;
        unsigned int        input_index;
                                                
        NormObjectHandle    tx_stream;
        bool                tx_ready;
        unsigned int        tx_stream_buffer_max;
        unsigned int        tx_stream_buffer_threshold; // flow control threshold
        unsigned int        tx_stream_buffer_count;
        unsigned int        tx_stream_bytes_remain;
        bool                tx_watermark_pending;
        bool                norm_acking;
        bool                auto_ack;
        unsigned int        acking_node_count;
        bool                tx_ack_pending;
        NormFlushMode       flush_mode;  // TBD - allow for "none", "passive", "active" options
        bool                fti_info;
        bool                ack_ex;
        
        // Receive stream and state variables for writing received messages to output
        NormObjectHandle    rx_stream;
        bool                rx_ready;
        bool                rx_needed;
        bool                msg_sync;
        double              output_bucket_rate;     // bytes per second
        double              output_bucket_interval; // seconds per byte
        unsigned int        output_bucket_depth;    // bytes
        unsigned int        output_bucket_count;    // bytes
        ProtoSocket         output_socket;  // optional UDP socket for recv msg output
        ProtoAddress        relay_addr;     // dest addr for recv msg relay
        FILE*               output_file;
        int                 output_fd;    // stdout by default
        bool                output_ready;
        char                output_buffer[MSG_SIZE_MAX];
        unsigned int        output_msg_length;
        unsigned int        output_index;
        
        
        // These are some options mainly for testing purposes
        bool                omit_header;  // if "true", receive message length header is _not_ written to output
        bool                rx_silent;
        //double              tx_loss;
        unsigned long       input_byte_count;
        unsigned long       tx_byte_count;
        
        unsigned short      segment_size;
        unsigned short      block_size;
        unsigned short      num_parity;
        unsigned short      auto_parity;
        
        unsigned long       stream_buffer_size;
        unsigned int        tx_socket_buffer_size;
        unsigned int        rx_socket_buffer_size;
        unsigned int        input_socket_buffer_size;
        unsigned int        output_socket_buffer_size;
        
        unsigned int        check_sequence;
        uint64_t            sequence_prev;
};  // end class NormStreamer

NormStreamer::NormStreamer()
 : norm_session(NORM_SESSION_INVALID), is_multicast(false), probe_tos(0), loopback(false), is_running(false),
   input_socket(ProtoSocket::UDP), input_file(stdin), input_fd(fileno(stdin)), input_ready(true), 
   input_needed(false), input_msg_length(0), input_index(0),
   tx_stream (NORM_OBJECT_INVALID), tx_ready(true),
   tx_stream_buffer_max(0), tx_stream_buffer_count(0), tx_stream_bytes_remain(0), tx_watermark_pending(false), 
   norm_acking(false), auto_ack(false), acking_node_count(0), tx_ack_pending(false), flush_mode(NORM_FLUSH_ACTIVE),
   fti_info(false), ack_ex(false), rx_stream(NORM_OBJECT_INVALID), rx_ready(false), rx_needed(false), msg_sync(false),
   output_bucket_rate(0.0), output_bucket_interval(0.0), output_bucket_depth(0), output_bucket_count(0),
   output_socket(ProtoSocket::UDP), output_file(stdout), output_fd(fileno(stdout)), output_ready(true), 
   output_msg_length(0), output_index(0), 
   omit_header(false), rx_silent(false), input_byte_count(0), tx_byte_count(0),
   segment_size(1398), block_size(64), num_parity(0), auto_parity(0),
   stream_buffer_size(2*1024*1024),
   tx_socket_buffer_size(0), rx_socket_buffer_size(0),
   input_socket_buffer_size(0), output_socket_buffer_size(0),
   check_sequence(0), sequence_prev(0)
{
}

NormStreamer::~NormStreamer()
{
}

bool NormStreamer::BoostPriority()
{
#ifdef LINUX
    pid_t this_process = getpid() ;
    int policy = SCHED_FIFO ;
    int max_priority = sched_get_priority_max(policy) ;
    struct sched_param schedule_parameters ;
    memset((void*)&schedule_parameters, 0, sizeof(schedule_parameters)) ;
    schedule_parameters.sched_priority = max_priority ;
    int status = sched_setscheduler(this_process, policy, &schedule_parameters) ;
    if (0 != status)
    {
        fprintf(stderr, "%s:=>sched_setscheduler failed (%d), %s\n", __PRETTY_FUNCTION__, errno, strerror(errno) ) ;
        return false ;
    }
    else
    {
        fprintf(stderr, "%s:=>sched_setscheduler set priority to %d for process %u \n", __PRETTY_FUNCTION__, max_priority, this_process ) ;
    }
#else
    // (TBD) Do something differently if "pthread sched param"?
    if (0 != setpriority(PRIO_PROCESS, getpid(), -20))
    {
        PLOG(PL_ERROR, "NormStreamer::BoostPriority() error: setpriority() error: %s\n", GetErrorString());
        return false;
    }
#endif // if/else LINUX
    return true;
}

#ifndef ntohll
//Convert net-order to host-order.
uint64_t ntohll(uint64_t value)
{
    static const int betest = 1 ;
    union MyUnion
    {
        uint64_t i64;
        uint32_t i32[2];
    };

    uint64_t rval = value;
    bool host_is_little_endian =  ( 1 == (int)(*(char*)&betest) ) ;
    if ( host_is_little_endian )
    {

        MyUnion u;
        u.i64 = value;
        uint32_t temp = u.i32[0];
        u.i32[0] = ntohl(u.i32[1]);
        u.i32[1] = ntohl(temp);
        rval = u.i64;
    }
    return rval ;
}
#endif // !nothll

void NormStreamer::CheckSequenceNumber64(const char* buffer, const char* source)
{
    uint64_t temp;
    memcpy((void*)&temp, (void*)buffer, sizeof(temp));
    uint64_t sequence = ntohll(temp);
    if (0 != sequence_prev)
    {
        int64_t delta = (int64_t)(sequence - sequence_prev);
        if (1 != delta)
        {
            fprintf(stderr, "normStreamer: %s dropped %lu packets seq:%lu seq_prev:%lu\n",
                            source, (unsigned long)delta, (unsigned long)sequence,
                            (unsigned long)sequence_prev);
        }
    }
    sequence_prev = sequence;
}  // end NormStreamer::CheckSequenceNumber64()

void NormStreamer::CheckSequenceNumber32(const char* buffer, const char* source)
{
    uint32_t temp;
    memcpy((void*)&temp, (void*)buffer, sizeof(temp));
    uint32_t sequence = ntohll(temp);
    if (0 != sequence_prev)
    {
        int32_t delta = (int32_t)(sequence - sequence_prev);
        if (1 != delta)
        {
            fprintf(stderr, "normStreamer: %s dropped %lu packets seq:%lu seq_prev:%lu\n",
                            source, (unsigned long)delta, (unsigned long)sequence,
                            (unsigned long)sequence_prev);
        }
    }
    sequence_prev = sequence;
}  // end NormStreamer::CheckSequenceNumber32()

void NormStreamer::CheckSequenceNumber(const char* buffer, const char* source)
{
    switch (check_sequence)
    {
        case 32:
            CheckSequenceNumber32(buffer, source);
            break;
        case 64:
            CheckSequenceNumber64(buffer, source) ;
            break;
        default:
            break;
    }
}  // end NormStreamer::CheckSequenceNumber()

bool NormStreamer::EnableUdpRelay(const char* relayAddr, unsigned short relayPort)
{
    if (!output_socket.Open())
    {
        fprintf(stderr, "normStreamer error: unable to open 'relay' socket\n");
        return false ;
    }
    if (!output_socket.SetTxBufferSize(output_socket_buffer_size))
    {
        fprintf(stderr, "normStreamer warning: unable to set desired 'relay' socket buffer size (retrieved value:%u)\n", 
                        output_socket.GetTxBufferSize());
    }
    if (!relay_addr.ResolveFromString(relayAddr))
    {
        fprintf(stderr, "normStreamer error: invalid relay address\n");
        output_socket.Close();
        return false;
    }
    relay_addr.SetPort(relayPort);  // TBD - validate port number??
    return true;
}  // end bool EnableUdpRelay()

bool NormStreamer::EnableUdpListener(unsigned short thePort, const char* groupAddr, const char * interfaceName)
{
    if (!input_socket.Open(thePort))
    {
        fprintf(stderr, "normStreamer error: unable to open 'listen' socket on port %hu\n", thePort);
        return false;
    }
    if (!input_socket.SetRxBufferSize(input_socket_buffer_size))
    {
        fprintf(stderr, "normStreamer error: unable to set desired 'listen' socket buffer size\n");
        return false;
    }
    if (NULL != groupAddr)
    {
        ProtoAddress addr;
        if (!addr.ResolveFromString(groupAddr) || (!addr.IsMulticast()))
        {
            fprintf(stderr, "normStreamer error: invalid 'listen' group address\n");
            input_socket.Close();
            return false ;
        }
        if (!input_socket.JoinGroup(addr, interfaceName))
        {
            fprintf(stderr, "normStreamer error: unable to join 'listen' group address\n");
            input_socket.Close();
            return false;
        }
    }
    return true;
}  // end NormStreamer::EnableUdpListener()

bool NormStreamer::OpenNormSession(NormInstanceHandle instance, const char* addr, unsigned short port, NormNodeId nodeId)
{
    if (NormIsUnicastAddress(addr))
        is_multicast = false;
    else
        is_multicast = true;
    norm_session = NormCreateSession(instance, addr, port, nodeId);
    if (NORM_SESSION_INVALID == norm_session)
    {
        fprintf(stderr, "normStreamer error: unable to create NORM session\n");
        return false;
    }
    if (is_multicast)
    {
        NormSetRxPortReuse(norm_session, true);
        if (loopback)
            NormSetMulticastLoopback(norm_session, true);
    }
    
    // Set some default parameters (maybe we should put parameter setting in Start())
    NormSetDefaultSyncPolicy(norm_session, NORM_SYNC_STREAM);
    
    if (!is_multicast)
        NormSetDefaultUnicastNack(norm_session, true);
    
    NormSetTxRobustFactor(norm_session, 20);
    
    NormSetGrttProbingTOS(norm_session, probe_tos);
    
    NormSetFragmentation(norm_session, true);  // so that IP ID gets set for SMF DPD
    
    return true;
}  // end NormStreamer::OpenNormSession()

void NormStreamer::CloseNormSession()
{
    if (NORM_SESSION_INVALID == norm_session) return;
    NormDestroySession(norm_session);
    norm_session = NORM_SESSION_INVALID;
}  // end NormStreamer::CloseNormSession()

void NormStreamer::SetNormCongestionControl(CCMode ccMode)
{
    assert(NORM_SESSION_INVALID != norm_session);
    switch (ccMode)
    {
        case NORM_CC:  // default TCP-friendly congestion control
            NormSetEcnSupport(norm_session, false, false, false);
            break;
        case NORM_CCE: // "wireless-ready" ECN-only congestion control
            NormSetEcnSupport(norm_session, true, true);
            break;
        case NORM_CCL: // "loss tolerant", non-ECN congestion control
            NormSetEcnSupport(norm_session, false, false, true);
            break;
        case NORM_FIXED: // "fixed" constant data rate
            NormSetEcnSupport(norm_session, false, false, false);
            break;
    }
    if (NORM_FIXED != ccMode)
        NormSetCongestionControl(norm_session, true);
    else
        NormSetCongestionControl(norm_session, false);
}  // end NormStreamer::SetNormCongestionControl()

bool NormStreamer::Start(bool sender, bool receiver)
{
    // Note the session NORM buffer size is set the same s stream_buffer_size
    unsigned int bufferSize = stream_buffer_size;
    if (receiver)
    {
        if (!NormPreallocateRemoteSender(norm_session, bufferSize, segment_size, block_size, num_parity, stream_buffer_size))
            fprintf(stderr, "normStreamer warning: unable to preallocate remote sender\n");
        fprintf(stderr, "normStreamer: receiver ready.\n");
        if (!NormStartReceiver(norm_session, bufferSize))
        {
            fprintf(stderr, "normStreamer error: unable to start NORM receiver\n");
            return false;
        }
        if (0 != mlockall(MCL_CURRENT | MCL_FUTURE))
            fprintf(stderr, "normStreamer error: failed to lock memory for receiver.\n");
        if (0 != rx_socket_buffer_size)
            NormSetRxSocketBuffer(norm_session, rx_socket_buffer_size);
        rx_needed = true;
        rx_ready = false;
    }
    if (sender)
    {
        NormSetGrttEstimate(norm_session, 0.001);
        //NormSetGrttMax(norm_session, 0.100);
        NormSetBackoffFactor(norm_session, 0);
        if (norm_acking)
        {   
            // ack-based flow control enabled on command-line, 
            // so disable timer-based flow control
            NormSetFlowControl(norm_session, 0.0);
            NormTrackingStatus trackingMode = auto_ack? NORM_TRACK_RECEIVERS : NORM_TRACK_NONE;
            NormSetAutoAckingNodes(norm_session, trackingMode);
            if (auto_ack && (0 == acking_node_count))
            {
                // This allows for the receivrer(s) to start after the sender
                // as the sender will persistently send ack requests until
                // a receiver responds.
                NormAddAckingNode(norm_session, NORM_NODE_NONE);
            }
        }
        // Pick a random instance id for now
        struct timeval currentTime;
        gettimeofday(&currentTime, NULL);
        srand(currentTime.tv_usec);  // seed random number generator
        NormSessionId instanceId = (NormSessionId)rand();
        if (fti_info)
            NormLimitObjectInfo(norm_session, true);
        if (!NormStartSender(norm_session, instanceId, bufferSize, segment_size, block_size, num_parity))
        {
            fprintf(stderr, "normStreamer error: unable to start NORM sender\n");
            if (receiver) NormStopReceiver(norm_session);
            return false;
        }
        if (auto_parity > 0)
            NormSetAutoParity(norm_session, auto_parity < num_parity ? auto_parity : num_parity);
        if (0 != tx_socket_buffer_size)
            NormSetTxSocketBuffer(norm_session, tx_socket_buffer_size);
        if (NORM_OBJECT_INVALID == (tx_stream = NormStreamOpen(norm_session, stream_buffer_size)))
        {
            fprintf(stderr, "normStreamer error: unable to open NORM tx stream\n");
            NormStopSender(norm_session);
            if (receiver) NormStopReceiver(norm_session);
            return false;
        }
        else
        {
            if (0 != mlockall(MCL_CURRENT|MCL_FUTURE))
                fprintf(stderr, "normStreamer warning: failed to lock memory for sender.\n");
        }
        tx_stream_buffer_max = NormGetStreamBufferSegmentCount(bufferSize, segment_size, block_size);
        tx_stream_buffer_max -= block_size;  // a little safety margin (perhaps not necessary)
        tx_stream_buffer_threshold = tx_stream_buffer_max / 8;
        tx_stream_buffer_count = 0;
        tx_stream_bytes_remain = 0;
        tx_watermark_pending = false;
        tx_ack_pending = false;
        tx_ready = true;
        input_index = input_msg_length = 0;
        input_needed = true;
        input_ready = true;
    }
    is_running = true;
    return true;
}  // end NormStreamer::Start();

void NormStreamer::ReadInputSocket()
{
    unsigned int loopCount = 0;
    NormSuspendInstance(NormGetInstance(norm_session));
    while (input_needed && input_ready && (loopCount < LOOP_MAX))
    {
        loopCount++;
        unsigned int numBytes = MSG_SIZE_MAX - MSG_HEADER_SIZE;
        ProtoAddress srcAddr;
        if (input_socket.RecvFrom(input_buffer+MSG_HEADER_SIZE, numBytes, srcAddr))
        {
            if (0 == numBytes)
            {
                input_ready = false;
                break;
            }
            input_index = 0;
            input_msg_length = numBytes + MSG_HEADER_SIZE;
            input_byte_count += input_msg_length;
            unsigned short msgSize = input_msg_length;;
            msgSize = htons(msgSize);
            memcpy(input_buffer, &msgSize, MSG_HEADER_SIZE);
            input_needed = false;
            if (TxReady()) SendData();
        }
        else
        {
            // TBD - handle error?
            input_ready = false;
        }
    }
    NormResumeInstance(NormGetInstance(norm_session));
}  // end NormStreamer::ReadInputSocket()

void NormStreamer::ReadInput()
{
    if (UdpListenerEnabled()) return ReadInputSocket();
    // The loop count makes sure we don't spend too much time here
    // before going back to the main loop to handle NORM events, etc
    unsigned int loopCount = 0;
    NormSuspendInstance(NormGetInstance(norm_session));
    while (input_needed && input_ready && (loopCount < LOOP_MAX))
    {
        loopCount++;
        //if (100 == loopCount)
        //    fprintf(stderr, "normStreamer ReadInput() loop count max reached\n");
        unsigned int numBytes;
        if (input_index < MSG_HEADER_SIZE)
        {
            // Reading message length header for next message to send
            numBytes = MSG_HEADER_SIZE - input_index;
        }
        else
        {
            // Reading message body
            assert(input_index < input_msg_length);
            numBytes = input_msg_length - input_index;
        }
        ssize_t result = read(input_fd, input_buffer + input_index, numBytes);
        if (result > 0)
        {
            input_index += result;
            input_byte_count += result;
            if (MSG_HEADER_SIZE == input_index)
            {
                // We have now read the message size header
                // TBD - support other message header formats?
                // (for now, assume 2-byte message length header)
                uint16_t msgSize ;
                memcpy(&msgSize, input_buffer, MSG_HEADER_SIZE);
                msgSize = ntohs(msgSize);
                input_msg_length = msgSize;
            }
            else if (input_index == input_msg_length)
            {
                // Message input complete
                input_index = 0;  // reset index for transmission phase
                input_needed = false;
                if (TxReady()) SendData();
            }   
            else
            {
                // Still need more input
                // (wait for next input notification to read more)
                input_ready = false;
            }
        }
        else if (0 == result)
        {
            // end-of-file reached, TBD - trigger final flushing and wrap-up
            fprintf(stderr, "normStreamer: input end-of-file detected ...\n");
            NormStreamClose(tx_stream, true);
            if (norm_acking)
            {
                if (ack_ex)
                {
                    const char* req = "Hello, acker";
                    NormSetWatermarkEx(norm_session, tx_stream, req, strlen(req) + 1, true);
                }
                else
                {
                    NormSetWatermark(norm_session, tx_stream, true);
                }
                tx_ack_pending = false;
            }
            input_needed = false;
        }
        else
        {
            switch (errno)
            {
                case EINTR:
                    continue;  // interrupted, try again
                case EAGAIN:
                    // input starved, wait for next notification
                    input_ready = false;
                    break;
                default:
                    // TBD - handle this better
                    perror("normStreamer error reading input");
                    break;
            }
            break;
        }
    }  // end while (input_needed && input_ready)
    NormResumeInstance(NormGetInstance(norm_session));
}  // end NormStreamer::ReadInput()

void NormStreamer::SendData()
{
    while (TxReady() && !input_needed)
    {
        // Note WriteToStream() or FlushStream() will set "tx_ready" to 
        // false upon flow control thus negating TxReady() status
        assert(input_index < input_msg_length);
        assert(input_msg_length);
        if ((0 != check_sequence) && (0 == input_index))
            CheckSequenceNumber(input_buffer+MSG_HEADER_SIZE, __func__);
        input_index += WriteToStream(input_buffer + input_index, input_msg_length - input_index);
        if (input_index == input_msg_length)
        {
            // Complete message was sent, so set eom and optionally flush
            if (NORM_FLUSH_NONE != flush_mode)
                FlushStream(true, flush_mode); 
            else
                NormStreamMarkEom(tx_stream);
            input_index = input_msg_length = 0;
            input_needed = true;
        }
        else
        {
            //fprintf(stderr, "SendData() impeded by flow control\n");
        }
    }  // end while (TxReady() && !input_needed)
}  // end NormStreamer::SendData()

unsigned int NormStreamer::WriteToStream(const char* buffer, unsigned int numBytes)
{
    unsigned int bytesWritten;
    if (norm_acking)
    {
        // This method uses NormStreamWrite(), but limits writes by explicit ACK-based flow control status
        if (tx_stream_buffer_count < tx_stream_buffer_max)
        {
            // 1) How many buffer bytes are available?
            unsigned int bytesAvailable = segment_size * (tx_stream_buffer_max - tx_stream_buffer_count);
            bytesAvailable -= tx_stream_bytes_remain;  // unflushed segment portion
            if (bytesAvailable < numBytes) numBytes = bytesAvailable;
            assert(numBytes);
            // 2) Write to the stream
            bytesWritten = NormStreamWrite(tx_stream, buffer, numBytes);
            tx_byte_count += bytesWritten;
            // 3) Update "tx_stream_buffer_count" accordingly
            unsigned int totalBytes = bytesWritten + tx_stream_bytes_remain;
            unsigned int numSegments = totalBytes / segment_size;
            tx_stream_bytes_remain = totalBytes % segment_size;
            tx_stream_buffer_count += numSegments;
            
            //assert(bytesWritten == numBytes);  // this could fail if timer-based flow control is left enabled
            // 3) Check if we need to issue a watermark ACK request?
            if (!tx_watermark_pending && (tx_stream_buffer_count >= tx_stream_buffer_threshold))
            {
                // Initiate flow control ACK request
                //fprintf(stderr, "write-initiated flow control ACK REQUEST\n");
                if (ack_ex)
                {
                    const char* req = "Hello, acker";
                    NormSetWatermarkEx(norm_session, tx_stream, req, strlen(req) + 1);
                }
                else
                {
                    NormSetWatermark(norm_session, tx_stream);
                }
                
                tx_watermark_pending = true;
                tx_ack_pending = false;
            }
        }
        else
        {
            fprintf(stderr, "normStreamer: sender flow control limited\n");
            return 0;
        }
    }
    else
    {
        bytesWritten = NormStreamWrite(tx_stream, buffer, numBytes);
        tx_byte_count += bytesWritten;
    }
    if (bytesWritten != numBytes) //NormStreamWrite() was (at least partially) blocked
    {
        //fprintf(stderr, "NormStreamWrite() blocked by flow control ...\n");
        tx_ready = false;
    }
    return bytesWritten;
}  // end NormStreamer::WriteToStream()

void NormStreamer::FlushStream(bool eom, NormFlushMode flushMode)
{ 
    if (norm_acking)
    {
        bool setWatermark = false;
        if (0 != tx_stream_bytes_remain)
        {
            // The flush will force the runt segment out, so we increment our buffer usage count
            // (and initiate flow control watermark ack request if buffer mid-point threshold exceeded
            tx_stream_buffer_count++;
            tx_stream_bytes_remain = 0;
            if (!tx_watermark_pending && (tx_stream_buffer_count >= tx_stream_buffer_threshold))
            {
                setWatermark = true;
                tx_watermark_pending = true;
                //fprintf(stderr, "flush-initiated flow control ACK REQUEST\n");
            }
        }
        // The check for "tx_watermark_pending" here prevents a new watermark
        // ack request from being set until the pending flow control ack is 
        // received. This favors avoiding dead air time over saving "chattiness"
        if (setWatermark)
        {
            // Flush passive since watermark will invoke active request
            // (TBD - do non-acking nodes NACK to watermark when not ack target?)
            NormStreamFlush(tx_stream, eom, NORM_FLUSH_PASSIVE);
        }
        else if (tx_watermark_pending)
        {
            // Pre-existing pending flow control watermark ack request
#if SHOOT_FIRST
            // Go ahead and set a fresh watermark
            // TBD - not sure this mode works properly ... may need to 
            // keep track of unacknowledged byte count and decrement accordingly
            // when ack arrives
            NormStreamFlush(tx_stream, eom, NORM_FLUSH_PASSIVE);
            setWatermark = true;
#else // ACK_LATER
            // Wait until flow control ACK is received before issuing another ACK request
            NormStreamFlush(tx_stream, eom, flushMode);
            tx_ack_pending = true;  // will call NormSetWatermark() upon flow control ack completion
#endif
        }
        else
        {
            // Since we're acking, use active ack request in lieu of active flush
            NormStreamFlush(tx_stream, eom, NORM_FLUSH_PASSIVE);
            setWatermark = true;
        }
        if (setWatermark) 
        {
            if (ack_ex)
            {
                const char* req = "Hello, acker";
                NormSetWatermarkEx(norm_session, tx_stream, req, strlen(req) + 1, true);
            }
            else
            {
                NormSetWatermark(norm_session, tx_stream, true);
            }
            tx_ack_pending = false;
        }
    }
    else
    {
        NormStreamFlush(tx_stream, eom, flushMode);
    }
}  // end NormStreamer::FlushStream()

void NormStreamer::RecvData()
{    
    // The loop count makes sure we don't spend too much time here
    // before going back to the main loop to handle NORM events, etc
    unsigned int loopCount = 0;
    // Reads data from rx_stream to available output_buffer
    NormSuspendInstance(NormGetInstance(norm_session));
    while (rx_needed && rx_ready && (loopCount < LOOP_MAX))
    {
        loopCount++;
        //if (100 == loopCount)
        //    fprintf(stderr, "normStreamer RecvData() loop count max reached.\n");
        // Make sure we have msg_sync (TBD - skip this for byte streaming)
        if (!msg_sync)
        {
            msg_sync = NormStreamSeekMsgStart(rx_stream);
            if (!msg_sync) 
            {
                rx_ready = false;
                break;  // wait for next NORM_RX_OBJECT_UPDATED to re-sync
            }
        }
        unsigned int bytesWanted;
        if (output_index < MSG_HEADER_SIZE)
        {
            // Receiving message header
            bytesWanted = MSG_HEADER_SIZE - output_index;
        }
        else
        {
            // Receiving message body
            assert(output_index < output_msg_length);
            bytesWanted = output_msg_length - output_index;
        }
        unsigned bytesRead = bytesWanted;
        if (!NormStreamRead(rx_stream, output_buffer + output_index, &bytesRead))
        {
            // Stream broken (should _not_ happen if norm_acking flow control)
            //fprintf(stderr, "normStreamer error: BROKEN stream detected, re-syncing ...\n");
            msg_sync = false;
            output_index = output_msg_length = 0;
            continue;
        }
        output_index += bytesRead;
        /*if (0 == bytesRead)
        {
            rx_ready = false;
        } 
        else*/ if (bytesRead != bytesWanted)
        {
            //continue;
            rx_ready = false;  // didn't get all we need
        }
        else if (MSG_HEADER_SIZE == output_index)
        {
            // We have now read the message size header
            // TBD - support other message header formats?
            // (for now, assume 2-byte message length header)
            uint16_t msgSize ;
            memcpy(&msgSize, output_buffer, MSG_HEADER_SIZE);
            output_msg_length = ntohs(msgSize);
        }
        else if (output_index == output_msg_length)
        {
            // Received full message
            rx_needed = false;
            output_index = 0;  // reset for writing to output
            if (output_ready && OutputBucketReady())
                WriteOutput();
        }
    }
    NormResumeInstance(NormGetInstance(norm_session));
    
}  // end NormStreamer::RecvData()

void NormStreamer::WriteOutputSocket()
{
    if (output_ready && !rx_needed)
    {
        assert(output_index < output_msg_length);
        unsigned int payloadSize = output_msg_length - MSG_HEADER_SIZE;
        unsigned int numBytes = payloadSize;
        if ((0 != check_sequence))
            CheckSequenceNumber(output_buffer+MSG_HEADER_SIZE, __func__);
        if (output_socket.SendTo(output_buffer+MSG_HEADER_SIZE, numBytes, relay_addr))
        {
            if (numBytes != payloadSize)
            {
                // sendto() was blocked
                output_ready = false;
                return;
            }
            if (0 != output_bucket_depth)
            {
                // Debit output token bucket since it's active
                ASSERT(output_bucket_count >= payloadSize);
                output_bucket_count -= payloadSize;
            }
            rx_needed = true;
            output_index = output_msg_length = 0;
        }
        else
        {
            output_ready = false;
        }
    }
}  // end NormStreamer::WriteOutputSocket()

void NormStreamer::WriteOutput()
{
    if (UdpRelayEnabled()) 
    {
         WriteOutputSocket();
         return;
    }
    while (output_ready && !rx_needed)
    {
        assert(output_index < output_msg_length);
        if ((0 != check_sequence) && (0 == output_index))
            CheckSequenceNumber(output_buffer+MSG_HEADER_SIZE,__func__);
        ssize_t result = write(output_fd, output_buffer + output_index, output_msg_length - output_index);
        if (result >= 0)
        {
            if (0 != output_bucket_depth)
            {
                // Debit output token bucket since it's active
                if (result > output_bucket_count)
                    TRACE("result:%d output_bucket_count:%u\n", (int)result, output_bucket_count);
                ASSERT(output_bucket_count >= result);
                output_bucket_count -= result; 
            }
            output_index += result;
            if (output_index == output_msg_length)
            {
                // Complete message written
                rx_needed = true;
                output_index = output_msg_length = 0;
                if ((NORM_OBJECT_INVALID == tx_stream) && (NORM_OBJECT_INVALID == rx_stream)) 
                    Stop();  // receive stream was terminated by sender
            }
            else
            {
                output_ready = false;
            }
        }
        else
        {
            switch (errno)
            {
                case EINTR:
                    perror("normStreamer output EINTR");
                    continue;  // interupted, try again
                case EAGAIN:
                    // output blocked, wait for next notification
                    //perror("normStreamer output blocked");
                    output_ready = false;
                    break;
                default:
                    perror("normStreamer error writing output");
                    break;
            }
            break;
        }
    }
}  // end NormStreamer::WriteOutput()

void NormStreamer::HandleNormEvent(const NormEvent& event)
{
    switch (event.type)
    {
        case NORM_TX_QUEUE_EMPTY:
            //TRACE("normStreamer: flow control empty ...\n");
            tx_ready = true;
            break;
        case NORM_TX_QUEUE_VACANCY:
            //TRACE("normStreamer: flow control relieved ...\n");
            tx_ready = true;
            break;
            
        case NORM_GRTT_UPDATED:
            //fprintf(stderr, "new GRTT = %lf\n", NormGetGrttEstimate(norm_session));
            break;
            
        case NORM_ACKING_NODE_NEW:
            if (0 == acking_node_count)
                NormRemoveAckingNode(event.session, NORM_NODE_NONE);
            acking_node_count++;
            break;
            
        case NORM_TX_WATERMARK_COMPLETED:
            TRACE("NORM_TX_WATERMARK_COMPLETED ...\n");
            if (NORM_ACK_SUCCESS == NormGetAckingStatus(norm_session))
            {
                //fprintf(stderr, "WATERMARK COMPLETED\n");
                if (0 == acking_node_count)
                {
                    // Keep probing until some receiver shows up
                    NormResetWatermark(norm_session);
                }                
                else if (tx_watermark_pending)
                {
                    // Flow control ack request was pending.
                    tx_watermark_pending = false;
                    tx_stream_buffer_count -= tx_stream_buffer_threshold;
                    //fprintf(stderr, "flow control ACK completed\n");
                    if (tx_ack_pending)
                    {
                        if (ack_ex)
                        {
                            const char* req = "Hello, acker";
                            NormSetWatermarkEx(norm_session, tx_stream, req, strlen(req) + 1, true);
                        }
                        else
                        {
                            NormSetWatermark(norm_session, tx_stream, true);
                        }
                        tx_ack_pending = false;
                    }
                }
            }
            else
            {
                // TBD - we could see who didn't ACK and possibly remove them
                //       from our acking list.  For now, we are infinitely
                //       persistent by always resetting the watermark ack request
                //       For example, an application could make a decision at this
                //       point, depending upon some count of ACK request failures
                //       to choose to remove a previously included receiver.
                fprintf(stderr, "flow control watermark reset\n");
                if (tx_ack_pending)
                {
                    // May as well advance the ack request point
                    if (ack_ex)
                    {
                        const char* req = "Hello, acker";
                        NormSetWatermarkEx(norm_session, tx_stream, req, strlen(req) + 1, true);
                    }
                    else
                    {
                        NormSetWatermark(norm_session, tx_stream, true);
                    }
                    tx_ack_pending = false;
                }
                else
                {
                    NormResetWatermark(norm_session);
                }
            }
            if (ack_ex)
            {
                // This iterates through the acking nodes looking for responses
                // to our application-defined NormSetWatermarkEx() request
                NormAckingStatus ackingStatus;
                NormNodeId nodeId = NORM_NODE_NONE;  // this inits NormGetNextAckingNode() iteration
                while (NormGetNextAckingNode(event.session, &nodeId, &ackingStatus))
                {
                    if (NORM_ACK_SUCCESS == ackingStatus)
                    {
                        // This node acked, so look for AckEx response
                        // In our example/test case here, we use strings for the content
                        char buffer[256];
                        buffer[0] = '\0';
                        unsigned int buflen = 256;
                        if (NormGetAckEx(event.session, nodeId, buffer, &buflen))
                            fprintf(stderr, "Received APP_ACK from node>%u \"%s\"\n", nodeId, buffer);
                    }
                }
            }
            break; 
            
        case NORM_TX_OBJECT_PURGED:
            // tx_stream graceful close completed
            NormStopSender(norm_session);
            tx_stream = NORM_OBJECT_INVALID;
            if (NORM_OBJECT_INVALID == rx_stream) Stop();
            break;
        
        case NORM_REMOTE_SENDER_INACTIVE:
            //fprintf(stderr, "REMOTE SENDER INACTIVE node: %u\n", NormNodeGetId(event.sender));
            //NormNodeDelete(event.sender);
            break;
            
        case NORM_RX_OBJECT_NEW:
            if ((NORM_OBJECT_INVALID == rx_stream) &&
                (NORM_OBJECT_STREAM == NormObjectGetType(event.object)))
            {
                rx_stream = event.object;
                rx_ready = true;
                // By setting initial "msg_sync" to true, we can detect when 
                // stream beginning was missed (for NORM_SYNC_STREAM only)
                msg_sync = false;
                rx_needed = true;
                output_index = output_msg_length = 0;
            }
            else
            {
                fprintf(stderr, "normStreamer warning: NORM_RX_OBJECT_NEW while already receiving?!\n");
            }
            
        case NORM_RX_OBJECT_UPDATED:
            rx_ready = true;
            break;
        
        case NORM_RX_OBJECT_ABORTED:
            //fprintf(stderr, "NORM_RX_OBJECT_ABORTED\n");// %hu\n", NormObjectGetTransportId(event.object));
            rx_stream = NORM_OBJECT_INVALID;
            rx_needed = false;
            rx_ready = false;
            break;
            
        case NORM_RX_OBJECT_COMPLETED:
            // Rx stream has closed 
            // TBD - set state variables so any pending output is
            //       written out and things shutdown if not sender, too
            fprintf(stderr, "normStreamer: rx_stream completed.\n");
            // if rx_needed is true, all output has been written
            if (rx_needed && (NORM_OBJECT_INVALID == tx_stream))
            {
                NormNodeHandle sender = NormObjectGetSender(rx_stream);
                // Wait a couple of GRTT's to ACK sender
                double exitTime = 20.0 * NormNodeGetGrtt(sender);
                if (exitTime < 1.0) exitTime = 1.0;
                fprintf(stderr, "normStreamer reception completed, exiting in %f seconds ...\n", (float)exitTime);
                sleep(exitTime);  // TBD - use our user-defined NormSession timeout instead? (retaining rx_stream)
                if (rx_needed && (NORM_OBJECT_INVALID == tx_stream)) 
                    Stop();  
            }
            rx_stream = NORM_OBJECT_INVALID;
            rx_ready = false;
            rx_needed = false;
            break;
            
        case NORM_RX_ACK_REQUEST:
        {
            char buffer[256];
            buffer[0] = '\0';
            unsigned int buflen = 256;
            NormNodeGetWatermarkEx(event.sender, buffer, &buflen);
            fprintf(stderr, "Received NORM_RX_ACK_REQUEST: \"%s\"\n", buffer);
            // Send a reply
            const char* ack = "Yes, master";
            NormNodeSendAckEx(event.sender, ack, strlen(ack) + 1);
            break;
        }
            
        default:
            break;     
    }
    //NormReleasePreviousEvent(NormGetInstance(norm_session));
        
}  // end NormStreamer::HandleNormEvent()

void Usage()
{
    fprintf(stderr, "Usage: normStreamer id <nodeIdInteger> {send|recv} [addr <addr>[/<port>]]\n"
                    "                    [interface <name>] [loopback] [info] [ptos <value>] [ex]\n"
                    "                    [cc|cce|ccl|rate <bitsPerSecond>]\n"
                    "                    [ack auto|<node1>[,<node2>,...]]\n"
                    "                    [flush {none|passive|active}]\n"
                    "                    [listen [<mcastAddr>/]<port>] [linterface <name>]\n"
                    "                    [relay <dstAddr>/<port>] [limit [<rate>/]<depth>]\n"
                    "                    [output <device>] [boost] [debug <level>] [trace]\n"
                    "                    [log <logfile>] [segment <bytes>] [block <count>]\n"
                    "                    [parity <count>] [auto <count>]\n"
                    "                    [insockbuffer <bytes>] [outsockbuffer <bytes>]\n"
                    "                    [txsockbuffer <bytes>] [rxsockbuffer <bytes>]\n"
                    "                    [streambuffer <bytes>]\n"
                    "                    [check64 | check32]\n"
                    "                    [omit] [silent] [txloss <lossFraction>]\n");
}  // end Usage()

void PrintHelp()
{
    fprintf(stderr, "\nHelp for normStreamer:\n\n") ;
    fprintf(stderr,
            "The 'normStreamer' application sends messages from STDIN (or a listening UDP socket) to one or more\n"
            "receiving nodes using the NORM protocol.  Received messages are output to STDOUT (or relayed to\n"
            "to a UDP destination address/port).  Key command line options are:\n\n"
            "   id <nodeId>             -- Specifies the node id for the local NORM instance (required)\n"
            "   send | recv             -- Specifies whether this node will be a sender and/or receiver (must choose  at least one)\n"
            "   addr <addr>[/<port>]    -- specifies the network address over which to send/receive NORM protocol\n"
            "   interface <name>        -- Specifies the name of the network interface on which to conduct NORM protocol\n"
            "                              (e.g., 'eth0')\n"
            "   loopback                -- Enables 'loopback' sessions on the same host machine.  Required for multicast loopback.\n"
            "   ptos <value>            -- Set special IP traffic class (TOS) for GRTT probing and acknowledgments\n"
            "   info                    -- Limits FTI header extension to NORM_INFO message only (reduced overhead)\n"
            "   rate <bitsPerSecond>    -- sets fixed sender rate (and receiver token bucket rate if 'limit' option is used)\n"
            "   [cc|cce|ccl]            -- Enables optional NORM congestion control mode (overrides 'rate')\n"
            "   ack [<nodeId list>]     -- Instructs sender to request positive acknowledgement from listed receiver nodes\n"
            "   flush [<flushMode>]     -- Choose 'none', 'passive', or 'active' message stream flushing mode.  If 'none',\n"
            "                              NORM_DATA packets will always be packed with message content up to the full\n"
            "                              segment size.  If 'passive', short NORM_DATA packets will be sent to transmit\n"
            "                              any messages as soon as possible.  If 'active', NORM stream will be flushed\n"
            "                              on a per-message basis as with 'passive' mode, but positive acknowledgment will\n"
            "                              _also_ be requested if a list of acking receiver node ids has beeen provided.\n"
            "   listen [<addr>/]<port>  -- Specifies the port and optional multicast address which the sender uses to listen\n"
            "                              for UDP packets to transmit to the receiver(s) via the NORM protocol\n"
            "   linterface <name>       -- Specifies the name of the network interface on which to listen for UDP packet\n"
            "                              payloads to send to the receiver(s) via NORM protocol\n"
            "   relay <dstAddr>/<port>  -- Specifies the address/port for which to relay (as UDP datagrams) received messages\n"
            "   limit [<rate>/]<depth>  -- Token bucket rate/depth for optional receiver output limiter (smooths bursty output\n" 
            "                              upon NORM loss recovery).  When UDP 'relay' is used, this option is useful to avoid\n"
            "                              overly bursty UDP output.  The <rate> is in units of bits/second and the <depth> is\n"
            "                              in units of bytes.  If not specified  here, the value set by 'rate' command is used\n"
            "                              as the token bucket rate.\n"
            "   check64 | check32       -- Enables checking that packet sequence numbers in the first 4/8 bytes of received\n"
            "                              packets increment properly (optional)\n"
            "   insockbuffer <bytes>    -- Specifies the size of the 'listen' UDP socket buffer (optional).\n"
            "   outsockbuffer <bytes>   -- Specifies the size of the 'relay' UDP socket buffer (optional).\n"
            "   txsockbuffer <bytes>    -- Specifies the size of the NORM/UDP transmit socket buffer (optional).\n"
            "   rxsockbuffer <bytes>    -- Specifies the size of the NORM/UDP receive socket buffer (optional).\n"
            "   streambuffer <bytes>    -- Specifies the size of the NORM stream buffer (optional).\n\n");
    Usage();

}  // end PrintHelp()

int main(int argc, char* argv[])
{
    // REQUIRED parameters initiailization
    NormNodeId nodeId = NORM_NODE_NONE;
    bool send = false;
    bool recv = false;
    
    char sessionAddr[64];
    strcpy(sessionAddr, "224.1.2.3");
    unsigned int sessionPort = 6003;
    
    char listenAddr[64];            // UDP :listen" multicast addr 
    listenAddr[0] = '\0';
    unsigned int listenPort = 0;    // UDP "listen" port for UDP "listen"
    const char* listenIface = NULL; // UDP "listen" interface
    
    char relayAddr[64]; 
    relayAddr[0] = '\0';
    unsigned int relayPort = 0;
    
    double txRate = 0.0; // used for non-default NORM_FIXED ccMode
    NormStreamer::CCMode ccMode = NormStreamer::NORM_CC;
    const char* mcastIface = NULL;
    NormNodeId ackingNodeList[256]; 
    unsigned int ackingNodeCount = 0;
            
    bool loopback = false;
    bool ftiInfo = false;
    bool ackEx = false;
    int debugLevel = 0;
    bool trace = false;
    const char* logFile = NULL;
    bool omitHeaderOnOutput = false;
    bool silentReceiver = false;
    double txloss = 0.0;
    bool boostPriority = false;
    unsigned int checkSequence = 0;  // can set to 64 or 32
    // TBD - set these defaults to reasonable values or just use NormStreamer constructor defaults
    // A zero value for socket buffers means use the operating system default sizing
    unsigned long inputSocketBufferSize = 0;    // 6*1024*1024;
    unsigned long outputSocketBufferSize = 0;   // 6*1024*1024;
    unsigned long txSocketBufferSize = 0;       // 6*1024*1024;
    unsigned long rxSocketBufferSize = 0;       // 6*1024*1024;
    unsigned long streamBufferSize = 1*1024*1024;

    // Instantiate a NormStreamer and set default params
    NormStreamer normStreamer;
    normStreamer.SetFlushMode(NORM_FLUSH_NONE);
    
    // Parse command-line
    int i = 1;
    while (i < argc)
    {
        const char* cmd = argv[i++];
        size_t len = strlen(cmd);
        if (0 == strncmp(cmd, "help", len))
        {
            PrintHelp() ;
            exit(0);
        }
        else if (0 == strncmp(cmd, "send", len))
        {
            send = true;
        }
        else if (0 == strncmp(cmd, "recv", len))
        {
            recv = true;
        }
        else if (0 == strncmp(cmd, "loopback", len))
        {
            loopback = true;
        }
        else if (0 == strncmp(cmd, "ptos", len))
        {
            if (i >= argc)
            {
                fprintf(stderr, "normStreamer error: missing 'ptos' value!\n");
                Usage();
                return -1;
            }
            int tos = -1;
            int result = sscanf(argv[i], "%i", &tos);
            if (1 != result)
            {
                unsigned int utos;
                result = sscanf(argv[i], "%x", &utos);
                tos = utos;
            }
            if ((1 != result) || (tos < 0) || (tos > 255))
            {
                fprintf(stderr, "normStreamer error: invalid 'ptos' value!\n");
                Usage();
                return -1;
            }
            i++;
            normStreamer.SetProbeTOS((UINT8)tos);
        }
        else if (0 == strncmp(cmd, "info", len))
        {
            ftiInfo = true;
        }
        else if (0 == strncmp(cmd, "ex", len))
        {
            // This enables testing/demonstrating NormSetWatermarkEx() operation
            ackEx = true;
        }
        else if (0 == strncmp(cmd, "addr", len))
        {
            if (i >= argc)
            {
                fprintf(stderr, "normStreamer error: missing 'addr[/port]' value!\n");
                Usage();
                return -1;
            }
            const char* addrPtr = argv[i++];
            const char* portPtr = strchr(addrPtr, '/');
            if (NULL == portPtr)
            {
                strncpy(sessionAddr, addrPtr, 63);
                sessionAddr[63] = '\0';
            }
            else
            {
                size_t addrLen = portPtr - addrPtr;
                if (addrLen > 63) addrLen = 63;  // should issue error message
                strncpy(sessionAddr, addrPtr, addrLen);
                sessionAddr[addrLen] = '\0';
                portPtr++;
                sessionPort = atoi(portPtr);
            }
        }
        else if (0 == strncmp(cmd, "listen", len))
        {
            if (i >= argc)
            {
                fprintf(stderr, "normStreamer error: missing '[mcastAddr/]port]' value!\n");
                Usage();
                return -1;
            }
            const char* addrPtr = argv[i++];
            const char* portPtr = strchr(addrPtr, '/');
            if (NULL != portPtr)
            {
                size_t addrLen = portPtr - addrPtr;
                if (addrLen > 63) addrLen = 63;  // should issue error message
                strncpy(listenAddr, addrPtr, addrLen);
                listenAddr[addrLen] = '\0';
                portPtr++;
                listenPort = atoi(portPtr);
            }
            else
            {
                // no address, just port
                listenPort = atoi(addrPtr);
                addrPtr = NULL;
            }
        }
        else if (0 == strncmp(cmd, "relay", len))
        {
            if (i >= argc)
            {
                fprintf(stderr, "normStreamer error: missing relay 'dstAddr/port' value!\n");
                Usage();
                return -1;
            }
            const char* addrPtr = argv[i++];
            const char* portPtr = strchr(addrPtr, '/');
            if (NULL == portPtr)
            {
                fprintf(stderr, "normStreamer error: missing relay 'port' value!\n");
                Usage();
                return -1;
            }
            if (NULL != portPtr)
            {
                size_t addrLen = portPtr - addrPtr;
                if (addrLen > 63) addrLen = 63;  // should issue error message
                strncpy(relayAddr, addrPtr, addrLen);
                relayAddr[addrLen] = '\0';
                portPtr++;
                relayPort = atoi(portPtr);
            }
            
        }
        else if (0 == strncmp(cmd, "output", len))
        {
            if (i >= argc)
            {
                fprintf(stderr, "normStreamer error: missing output 'device' name!\n");
                Usage();
                return -1;
            }
            FILE* outfile = fopen(argv[i++], "w+");
            if (NULL == outfile)
            {
                fprintf(stderr, "normStreamer output device fopen() error: %s\n", GetErrorString());
                Usage();
                return -1;
            }
            normStreamer.SetOutputFile(outfile);
        }
        else if (0 == strncmp(cmd, "id", len))
        {
            if (i >= argc)
            {
                fprintf(stderr, "normStreamer error: missing 'id' value!\n");
                Usage();
                return -1;
            }
            nodeId = atoi(argv[i++]);
        }
        else if (0 == strncmp(cmd, "ack", len))
        {
            // comma-delimited acking node id list
            if (i >= argc)
            {
                fprintf(stderr, "normStreamer error: missing 'id' <nodeId> value!\n");
                Usage();
                return -1;
            }
            const char* alist = argv[i++];
            if (0 == strcmp(alist, "auto"))
            {
                normStreamer.SetAutoAck(true);
            }
            else
            {
                while ((NULL != alist) && (*alist != '\0'))
                {
                    // TBD - Do we need to skip leading white space?
                    int id;
                    if (1 != sscanf(alist, "%d", &id))
                    {
                        fprintf(stderr, "normStreamer error: invalid acking node list!\n");
                        Usage();
                        return -1;
                    }
                    ackingNodeList[ackingNodeCount] = NormNodeId(id);
                    ackingNodeCount++;
                    alist = strchr(alist, ',');
                    if (NULL != alist) alist++;  // point past comma
                }
            }
        }
        else if (0 == strncmp(cmd, "flush", len))
        {
            // "none", "passive", or "active"
            if (i >= argc)
            {
                fprintf(stderr, "nodeMsgr error: missing 'flush' <mode>!\n");
                Usage();
                return -1;
            }
            const char* mode = argv[i++];
            if (0 == strcmp(mode, "none"))
            {
                normStreamer.SetFlushMode(NORM_FLUSH_NONE);
            }
            else if (0 == strcmp(mode, "passive"))
            {
                normStreamer.SetFlushMode(NORM_FLUSH_PASSIVE);
            }
            else if (0 == strcmp(mode, "active"))
            {
                normStreamer.SetFlushMode(NORM_FLUSH_ACTIVE);
            }
            else
            {
                fprintf(stderr, "normMsgr error: invalid 'flush' mode \"%s\"\n", mode);
                return -1;
            }   
        }
        else if (0 == strncmp(cmd, "rate", len))
        {
            if (i >= argc)
            {
                fprintf(stderr, "normStreamer error: missing 'rate' <bitsPerSecond> value!\n");
                Usage();
                return -1;
            }
            if (1 != sscanf(argv[i++], "%lf", &txRate))
            {
                fprintf(stderr, "normStreamer error: invalid transmit rate!\n");
                Usage();
                return -1;
            }       
            // set fixed-rate operation
            ccMode = NormStreamer::NORM_FIXED;     
            normStreamer.SetOutputBucketRate(txRate);
        }
        else if (0 == strcmp(cmd, "cc"))
        {
            ccMode = NormStreamer::NORM_CC;
        }
        else if (0 == strcmp(cmd, "cce"))
        {
            ccMode = NormStreamer::NORM_CCE;
        }
        else if (0 == strcmp(cmd, "ccl"))
        {
            ccMode = NormStreamer::NORM_CCL;
        }
        else if (0 == strncmp(cmd, "interface", len))
        {
            if (i >= argc)
            {
                fprintf(stderr, "normStreamer error: missing 'interface' <name>!\n");
                Usage();
                return -1;
            }
            mcastIface = argv[i++];
        }
        else if (0 == strncmp(cmd, "linterface", len))
        {
            if (i >= argc)
            {
                fprintf(stderr, "normStreamer error: missing 'linterface' <name>!\n");
                Usage();
                return -1;
             }
             listenIface = argv[i++];
        }
        else if (0 == strncmp(cmd, "insockbuffer", len))
        {
            unsigned long value = 0 ;
            if (i >= argc)
            {
                fprintf(stderr, "normStreamer error: missing 'insockbuffer' size!\n");
                Usage();
                return -1;
            }
            if (1 != sscanf(argv[i++], "%lu", &value))
            {
                fprintf(stderr, "normStreamer error: invalid 'insockbuffer' size\n");
                Usage();
                return -1;
            }
            inputSocketBufferSize = value;
        }
        else if (0 == strncmp(cmd, "outsockbuffer", len))
        {
            unsigned long value = 0 ;
            if (i >= argc)
            {
                fprintf(stderr, "normStreamer error: missing 'outsockbuffer' size!\n");
                Usage();
                return -1;
            }
            if (1 != sscanf(argv[i++], "%lu", &value))
            {
                fprintf(stderr, "normStreamer error: invalid 'outsockbuffer' size!\n");
                Usage();
                return -1;
            }
            outputSocketBufferSize = value;
            }
        else if (0 == strncmp(cmd, "limit", len))
        {
            // format: limit [<rate>/<size>]  with 'rate' in bps and 'size' in bytes
            if (i >= argc)
            {
                fprintf(stderr, "normStreamer error: missing 'limit' size!\n");
                Usage();
                return -1;
            }
            const char* ratePtr = argv[i++];
            const char* sizePtr = strchr(ratePtr, '/');
            unsigned int rateLen = 0;
            if (NULL != sizePtr)
                rateLen = sizePtr++ - ratePtr;
            else
                sizePtr = ratePtr;
            if (0 != rateLen)
            {
                if (rateLen > 63)
                {
                    fprintf(stderr, "normStreamer error: out-of-bounds 'limit' rate\n");
                    Usage();
                    return -1;
                }
                char rateText[64];
                strncpy(rateText, ratePtr, rateLen);
                rateText[rateLen] = '\0';
                double value;
                if (1 != sscanf(rateText, "%lf", &value))
                {
                    fprintf(stderr, "normStreamer error: invalid 'limit' rate\n");
                    Usage();
                    return -1;
                }
                normStreamer.SetOutputBucketRate(value);
            }
            unsigned long value;
            if (1 != sscanf(sizePtr, "%lu", &value))
            {
                fprintf(stderr, "normStreamer error: invalid 'limit' size\n");
                Usage();
                return -1;
            }
            normStreamer.SetOutputBucketDepth(value);
        }
        else if (0 == strncmp(cmd, "txsockbuffer", len))
        {
            unsigned long value = 0 ;
            if (i >= argc)
            {
                fprintf(stderr, "normStreamer error: missing 'txsockbuffer' size!\n");
                Usage();
                return -1;
            }
            if (1 != sscanf(argv[i++], "%lu", &value))
            {
                fprintf(stderr, "normStreamer error: invalid 'txsockbuffer' size!\n");
                Usage();
                return -1;
            }
            txSocketBufferSize = value;
        }
        else if (0 == strncmp(cmd, "rxsockbuffer", len))
        {
            unsigned long value = 0 ;
            if (i >= argc)
            {
                fprintf(stderr, "normStreamer error: missing 'rxsockbuffer' size!\n");
                Usage();
                return -1;
            }
            if (1 != sscanf(argv[i++], "%lu", &value))
            {
                fprintf(stderr, "normStreamer error: invalid 'rxsockbuffer' size!\n");
                Usage();
                return -1;
            }
            rxSocketBufferSize = value;
        }
        else if (0 == strncmp(cmd, "segment", len))
        {
            if (i >= argc)
            {
                fprintf(stderr, "normStreamer error: missing 'segment' size!\n");
                Usage();
                return -1;
            }
            unsigned short value;
            if (1 != sscanf(argv[i++], "%hu", &value))
            {
                fprintf(stderr, "normStreamer error: invalid 'segment' size!\n");
                Usage();
                return -1;
            }
            normStreamer.SetSegmentSize(value);
        }
        else if (0 == strncmp(cmd, "block", len))
        {
            if (i >= argc)
            {
                fprintf(stderr, "normStreamer error: missing 'block' size!\n");
                Usage();
                return -1;
            }
            unsigned short value;
            if (1 != sscanf(argv[i++], "%hu", &value))
            {
                fprintf(stderr, "normStreamer error: invalid 'block' size!\n");
                Usage();
                return -1;
            }
            normStreamer.SetBlockSize(value);
        }
        else if (0 == strncmp(cmd, "parity", len))
        {
            if (i >= argc)
            {
                fprintf(stderr, "normStreamer error: missing 'parity' count!\n");
                Usage();
                return -1;
            }
            unsigned short value;
            if (1 != sscanf(argv[i++], "%hu", &value))
            {
                fprintf(stderr, "normStreamer error: invalid 'parity' count!\n");
                Usage();
                return -1;
            }
            normStreamer.SetNumParity(value);
        }
        else if (0 == strncmp(cmd, "auto", len))
        {
            if (i >= argc)
            {
                fprintf(stderr, "normStreamer error: missing 'auto' parity count!\n");
                Usage();
                return -1;
            }
            unsigned short value;
            if (1 != sscanf(argv[i++], "%hu", &value))
            {
                fprintf(stderr, "normStreamer error: invalid 'auto' parity count!\n");
                Usage();
                return -1;
            }
            normStreamer.SetAutoParity(value);
        }
        else if (0 == strncmp(cmd, "streambuffer", len))
        {
            unsigned long value = 0 ;
            if (i >= argc)
            {
                fprintf(stderr, "normStreamer error: missing 'streambuffer' size!\n");
                Usage();
                return -1;
            }
            if (1 != sscanf(argv[i++], "%lu", &value))
            {
                fprintf(stderr, "normStreamer error: invalid 'streambuffer' size!\n");
                Usage();
                return -1;
            }
            streamBufferSize = value;
        }
        else if ( 0 == strncmp(cmd,"chkseq", len) )
        {
            checkSequence = 64;  // same as "check64" for "historical" reasons
        }
        else if ( 0 == strncmp(cmd,"check64", len) )
        {
            checkSequence = 64;
        }
        else if ( 0 == strncmp(cmd,"check32", len) )
        {
            checkSequence = 32;
        }
        else if (0 == strncmp(cmd, "omit", len))
        {
            omitHeaderOnOutput = true;
        }
        else if (0 == strncmp(cmd, "silent", len))
        {
            silentReceiver = true;
        }
        else if (0 == strncmp(cmd, "boost", len))
        {
            boostPriority = true;
        }
        else if (0 == strncmp(cmd, "txloss", len))
        {
            if (1 != sscanf(argv[i++], "%lf", &txloss))
            {
                fprintf(stderr, "normStreamer error: invalid 'txloss' value!\n");
                Usage();
                return -1;
            }
        }
        else if (0 == strncmp(cmd, "debug", len))
        {
            if (i >= argc)
            {
                fprintf(stderr, "normStreamer error: missing 'debug' <level>!\n");
                Usage();
                return -1;
            }
            debugLevel = atoi(argv[i++]);
        }
        else if (0 == strncmp(cmd, "trace", len))
        {
            trace = true;
        }
        else if (0 == strncmp(cmd, "log", len))
        {
            if (i >= argc)
            {
                fprintf(stderr, "normStreamer error: missing 'log' <fileName>!\n");
                Usage();
                return -1;
            }
            logFile = argv[i++];
        }
        else if (0 == strncmp(cmd, "help", len))
        {
            Usage();
            return 0;
        }
        else
        {
            fprintf(stderr, "normStreamer error: invalid command \"%s\"!\n", cmd);
            Usage();
            return -1;
        }
    }
    
    if (!send && !recv)
    {
        fprintf(stderr, "normStreamer error: not configured to send or recv!\n");
        Usage();
        return -1;
    }
    /*
    if (NORM_NODE_NONE == nodeId)
    {
        fprintf(stderr, "normStreamer error: no local 'id' provided!\n");
        Usage();
        return -1;
    }
    */
    
    if (boostPriority)
    {
        if (!normStreamer.BoostPriority())
        {
            fprintf(stderr, "normStreamer error: setting scheduler/ priority boost failed (requires 'sudo').\n");
            return -1;
        }
    }
    
    
    
    if (0 != listenPort)
    {
        normStreamer.SetInputSocketBufferSize(inputSocketBufferSize);
        if (!normStreamer.EnableUdpListener(listenPort, listenAddr, listenIface))
        {
            fprintf(stderr, "normStreamer error: Failed to enable UDP listener\n") ;
            return -1;
        }
    }
    if (0 != relayPort)
    {
        // TBD - check addr/port validity?
        normStreamer.SetOutputSocketBufferSize(outputSocketBufferSize);
        if (! normStreamer.EnableUdpRelay(relayAddr, relayPort))
        {
            fprintf(stderr, "normStreamer error: Failed to open UDP relay socket\n") ;
            return -1;
        }
    }
    
    // TBD - should provide more error checking of NORM API calls
    NormInstanceHandle normInstance = NormCreateInstance(boostPriority);
    NormSetDebugLevel(debugLevel);
    if ((NULL != logFile) && !NormOpenDebugLog(normInstance, logFile))
    {
        perror("normStreamer error: unable to open log file");
        Usage();
        return -1;
    }
    normStreamer.SetCheckSequence(checkSequence);
    
    
    normStreamer.SetTxSocketBufferSize(txSocketBufferSize);
    normStreamer.SetRxSocketBufferSize(rxSocketBufferSize);
    normStreamer.SetStreamBufferSize(streamBufferSize);

    normStreamer.SetLoopback(loopback);
    normStreamer.SetFtiInfo(ftiInfo);
    normStreamer.SetAckEx(ackEx);
    
    if (omitHeaderOnOutput) normStreamer.OmitHeader(true);
    
    if (!normStreamer.OpenNormSession(normInstance, sessionAddr, sessionPort, (NormNodeId)nodeId))
    {
        fprintf(stderr, "normStreamer error: unable to open NORM session\n");
        NormDestroyInstance(normInstance);
        return -1;
    }
    
    if (silentReceiver) normStreamer.SetSilentReceiver(true);
    if (txloss > 0.0) normStreamer.SetTxLoss(txloss);
    
    for (unsigned int i = 0; i < ackingNodeCount; i++)
        normStreamer.AddAckingNode(ackingNodeList[i]);
    
    normStreamer.SetNormCongestionControl(ccMode);
    if (NormStreamer::NORM_FIXED == ccMode)
        normStreamer.SetNormTxRate(txRate);
    if (NULL != mcastIface)
        normStreamer.SetNormMulticastInterface(mcastIface);
    
    if (trace) normStreamer.SetNormMessageTrace(true);
    
    // TBD - set NORM session parameters
    normStreamer.Start(send, recv); 
    
    // TBD - add WIN32 support using win32InputHandler code
    //       and MsgWaitForMultipleObjectsEx() instead of select()
    
    int normfd = NormGetDescriptor(normInstance);
    // Get input/output descriptors and set to non-blocking i/o
    int inputfd = normStreamer.GetInputDescriptor();
    int outputfd = normStreamer.GetOutputDescriptor();
    if (-1 == fcntl(inputfd, F_SETFL, fcntl(inputfd, F_GETFL, 0) | O_NONBLOCK))
        perror("normStreamer: fcntl(inputfd, O_NONBLOCK) error");
    //if (!normStreamer.UdpRelayEnabled())
    if (-1 == fcntl(outputfd, F_SETFL, fcntl(outputfd, F_GETFL, 0) | O_NONBLOCK))
        perror("normStreamer: fcntl(outputfd, O_NONBLOCK) error");
    fd_set fdsetInput, fdsetOutput;
    FD_ZERO(&fdsetInput);
    FD_ZERO(&fdsetOutput);
    
#ifdef LINUX
    // We user timerfd on Linux for more precise timeouts
    int timerfd = timerfd_create(CLOCK_MONOTONIC, 0);
    if (timerfd < 0)
    {
        perror("normStreamer: timerfd_create() error");
        return -1;  
    }
#endif // LINUX 
    struct timeval lastTime;
    gettimeofday(&lastTime, NULL);
    struct timeval bucketTime = lastTime;
    while (normStreamer.IsRunning())
    {
        int maxfd = -1;
        int fdMask = 0;
        bool waitOnNorm = false;
        double timeoutInterval = -1.0;
        if (send)
        {
            if (normStreamer.InputNeeded())
            {
                if (normStreamer.InputReady())
                {
                    FD_CLR(inputfd, &fdsetInput);
                    timeoutInterval = 0.0;
                }
                else
                {
                    FD_SET(inputfd, &fdsetInput);
                    if (inputfd > maxfd) maxfd = inputfd;
                    fdMask |= 0x01;
                }
            }
            else
            {
                FD_CLR(inputfd, &fdsetInput);
            } 
            if (normStreamer.TxPending())
            {
                if (normStreamer.TxReady())
                    timeoutInterval = 0.0;
                else
                    waitOnNorm = true;
            }
        }
        if (recv)
        {
            if (normStreamer.RxNeeded())
            {
                if (normStreamer.RxReady())
                    timeoutInterval = 0.0;
                else
                    waitOnNorm = true;
            }
            if (normStreamer.OutputPending())
            {
                if (normStreamer.OutputReady())
                {
                    FD_CLR(outputfd, &fdsetOutput);
                    if (timeoutInterval < 0.0)
                        timeoutInterval = normStreamer.GetOutputBucketTimeout();
                }
                else
                {
                    FD_SET(outputfd, &fdsetOutput);
                    if (outputfd > maxfd) maxfd = outputfd;
                    fdMask |= 0x02;
                }
            }
            else
            {
                FD_CLR(outputfd, &fdsetOutput);
            }
        }
        if (waitOnNorm)
        {
            // we need to wait until NORM is tx_ready or rx_ready
            FD_SET(normfd, &fdsetInput);
            if (normfd > maxfd) maxfd = normfd;
            fdMask |= 0x04;
        }
        else
        {
            FD_CLR(normfd, &fdsetInput);
        }
        // Set timeout for select() ... TBD - it may be a slight
        // performance enhancement to skip the select() call when
        // the timeout needed is zero???
        struct timeval timeout;
        struct timeval* timeoutPtr = &timeout;
#ifdef LINUX
        if (timeoutInterval > 0.0)   
        {   
            // On Linux, we use the timerfd with our select() call to get
            // more precise timeouts than select() does alone on Linux
            struct timespec timeoutSpec;
            timeoutSpec.tv_sec = (unsigned int)timeoutInterval;
            timeoutSpec.tv_nsec = 1.0e+09*(timeoutInterval - (double)timeoutSpec.tv_sec);
            struct itimerspec timerSpec;
            timerSpec.it_interval.tv_sec = timerSpec.it_interval.tv_nsec = 0;
            timerSpec.it_value = timeoutSpec;
            if (0 == timerfd_settime(timerfd, 0, &timerSpec, 0))
            {
                timeoutPtr = NULL;
                FD_SET(timerfd, &fdsetInput);
                if (outputfd > maxfd) maxfd = timerfd;
                fdMask |= 0x08;
            }
            else
            {
                FD_CLR(timerfd, &fdsetInput); 
                timeout.tv_sec = (unsigned int)timeoutInterval;
                timeout.tv_usec = 1.0e+06*(timeoutInterval - (double)timeout.tv_sec);
                perror("normStreamer: timerfd_settime() error");
            }
        }
        else
        {
            // No precision timing needed 
            FD_CLR(timerfd, &fdsetInput);   
            if (timeoutInterval < 0.0)
            {
                // We wait one second maximum for debugging purposes
                timeout.tv_sec = 1;
                timeout.tv_usec = 0;
            }
            else // if (0.0 == timeoutInterval)
            {
                timeout.tv_sec = timeout.tv_usec = 0;
            }
        }
#else // non-LINUX  
        if (timeoutInterval > 0.0)
        {
            timeout.tv_sec = (unsigned int)timeoutInterval;
            timeout.tv_usec = 1.0e+06*(timeoutInterval - (double)timeout.tv_sec);
        }
        else if (timeoutInterval < 0.0)
        {
            // We wait one second maximum for debugging purposes
            timeout.tv_sec = 1;
            timeout.tv_usec = 0;
        }
        else // if (0.0 == timeoutInterval)
        {
            timeout.tv_sec = timeout.tv_usec = 0;
        }
#endif // if/else LINUX
        int result = select(maxfd+1, &fdsetInput, &fdsetOutput, NULL, timeoutPtr);
        switch (result)
        {
            case -1:
                switch (errno)
                {
                    case EINTR:
                    case EAGAIN:
                        continue;
                    default:
                        perror("normStreamer select() error");
                        // TBD - stop NormStreamer
                        break;
                }
                break;
            case 0:
                // timeout
                break;
            default:
                if (FD_ISSET(inputfd, &fdsetInput))
                    normStreamer.SetInputReady();
                if (FD_ISSET(outputfd, &fdsetOutput))
                    normStreamer.SetOutputReady();
#ifdef LINUX
                if (FD_ISSET(timerfd, &fdsetInput))
                {
                    // clear the timerfd status by reading from it
                    uint64_t expirations = 0;
                    if (read(timerfd, &expirations, sizeof(expirations)) < 0)
                        perror("normStreamer read(timerfd) error");
                }
#endif // LINUX
                break; 
        }
        // We always clear out/handle pending NORM API events
        // (to keep event queue from building up)
        NormEvent event;
        while (NormGetNextEvent(normInstance, &event, false))
            normStreamer.HandleNormEvent(event);
        
        struct timeval thisTime;
        gettimeofday(&thisTime, NULL);
        
        if (0 != normStreamer.GetOutputBucketDepth())
        {
            // Credit output token bucket for time that has passed
            double interval = (double)(thisTime.tv_sec - bucketTime.tv_sec);
            if (thisTime.tv_usec > bucketTime.tv_usec)
                interval += 1.0e-06 * (thisTime.tv_usec - bucketTime.tv_usec);
            else
                interval -= 1.0e-06 * (bucketTime.tv_usec - thisTime.tv_usec);
            normStreamer.CreditOutputBucket(interval);
            bucketTime = thisTime;
        }
        
        // for debugging to see if anything gets "stuck"
        if ((thisTime.tv_sec - lastTime.tv_sec) >= 100)
        {
            if (send)
                fprintf(stderr, "normStreamer: inputNeeded:%d inputReady:%d txPending:%d txReady:%d inputCount:%lu txCount:%lu fdMask:%d\n",
                            normStreamer.InputNeeded(), normStreamer.InputReady(), normStreamer.TxPending(), normStreamer.TxReady(),
                            normStreamer.GetInputByteCount(), normStreamer.GetTxByteCount(), fdMask);
            if (recv)
                fprintf(stderr, "normStreamer: rxNeeded:%d rxReady:%d outputPending:%d outputReady:%d fdMask:%d\n",
                            normStreamer.RxNeeded(), normStreamer.RxReady(), normStreamer.OutputPending(), normStreamer.OutputReady(), fdMask);
            
            lastTime = thisTime;
        }   
        
        // As a result of input/output ready or NORM notification events:
        // 1) Recv from rx_stream if needed and ready
        if (normStreamer.RxNeeded() && normStreamer.RxReady())
            normStreamer.RecvData(); 
        // 2) Write any pending data to output if output is ready
        if (normStreamer.OutputPending() && normStreamer.OutputReady())
        {
            if (normStreamer.OutputBucketReady())
                normStreamer.WriteOutput();  
        }
        // 3) Read from input if needed and ready 
        if (normStreamer.InputNeeded() && normStreamer.InputReady())
            normStreamer.ReadInput(); 
        // 4) Send any pending tx message
        if (normStreamer.TxPending() && normStreamer.TxReady())
            normStreamer.SendData();
        
    }  // end while(normStreamer.IsRunning()
    
#ifdef LINUX
    close(timerfd);
#endif // LINUX
    fflush(stderr);
    
    close(normStreamer.GetOutputDescriptor());  // TBD - do this in the destructor?
    
    NormDestroyInstance(normInstance);
    
    fprintf(stderr, "normStreamer exiting ...\n");
    
    return 0;
    
}  // end main()

