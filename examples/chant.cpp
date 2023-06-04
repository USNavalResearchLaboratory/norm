
#include "normApi.h"
#include "protoTree.h"
#include "protoAddress.h"

#include <stdio.h>       // for printf(), etc
#include <stdlib.h>      // for srand()
#include <string.h>      // for strrchr(), memset(), etc
#include <sys/time.h>    // for gettimeofday()
#include <arpa/inet.h>   // for htons()
#include <fcntl.h>       // for, well, fnctl()
#include <errno.h>       // obvious child
#include <assert.h>      // embarrassingly obvious
#include <unistd.h>      // for read()

const unsigned int MSG_SIZE_MAX = 8192;
const unsigned int CHAT_NAME_MAX = 32;


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


class ChantClient : public ProtoTree::Item
{
    public:
        ChantClient(NormNodeHandle nodeHandle);
        ~ChantClient();
        
        void RecvData();
        void SetRxStream(NormObjectHandle stream);
        void SetChatName(const char* text);
        const char* GetChatName() const {return chat_name;}
        
    private:
        const char* GetKey() const {return ((const char*)&node_handle);}
        unsigned int GetKeysize() const {return (sizeof(NormNodeHandle) << 3);}
        
        NormNodeHandle      node_handle;
        char                chat_name[CHAT_NAME_MAX+1];  // conveyed via NORM_INFO
        NormObjectHandle    rx_stream;
        bool                msg_sync;
        char                rx_buffer[MSG_SIZE_MAX+1];
        unsigned int        rx_index;
        unsigned int        rx_msg_length;
        bool                rx_ready;
        
};  // end class ChantClient

class ChantClientTable : public ProtoTreeTemplate<ChantClient>
{
    public:
        ChantClient* FindClient(NormNodeHandle nodeHandle) 
            {return (ChantClient*)Find((const char*)&nodeHandle, sizeof(nodeHandle) << 3);}
};

ChantClient::ChantClient(NormNodeHandle nodeHandle)
  : node_handle(nodeHandle), rx_stream(NORM_OBJECT_INVALID),
    msg_sync(false), rx_index(0), rx_msg_length(0),
    rx_ready(false)
{
    strcpy(chat_name, "");
}

ChantClient::~ChantClient()
{
}

void ChantClient::SetChatName(const char* text)
{
    strncpy(chat_name, text, CHAT_NAME_MAX);
    chat_name[CHAT_NAME_MAX] = '\0';
}  // end ChatClient::SetChatName()

void ChantClient::SetRxStream(NormObjectHandle stream)
{
    if (rx_stream != stream)
    {
        rx_stream = stream;
        msg_sync = false;
        rx_index = rx_msg_length = 0;
        rx_ready = true;
    }
}  // end ChantClient::SetRxStream


void ChantClient::RecvData()
{   
    while (true)
    {
        if (!msg_sync)
        {
            msg_sync = NormStreamSeekMsgStart(rx_stream);
            if (!msg_sync) 
            {
                break;  // wait for next NORM_RX_OBJECT_UPDATED to re-sync
            }
        }
        unsigned int bytesWanted = MSG_SIZE_MAX - rx_index;
        unsigned int bytesRead = bytesWanted;
        if (!NormStreamRead(rx_stream, rx_buffer + rx_index, &bytesRead))
        {
            // Stream broken (should _not_ happen if norm_acking flow control)
            //fprintf(stderr, "chant error: BROKEN stream detected, re-syncing ...\n");
            msg_sync = false;
            rx_index = rx_msg_length = 0;
            continue;
        }
        // Scan new received data for new lines indicating end of messages,
        // print out complete messages, and save any partial message received
        bool msgReady;
        unsigned int msgIndex = 0;
        const char* ptr = rx_buffer + rx_index; 
        unsigned int dataLen = rx_index + bytesRead;
        //fprintf(stderr, "idx:%u bytesRead:%u dataLen:%u msgIndex:%u\n", rx_index, bytesRead, dataLen, msgIndex);
        unsigned int i = 0;
        do
        {
            msgReady = false;
            for (; i < bytesRead; i++)
            {
                rx_msg_length++;
                if (('\n' == *ptr++) || (MSG_SIZE_MAX == rx_msg_length))
                {
                    msgReady = true;
                    break;
                }
            }
            if (msgReady)
            {
                fprintf(stdout, "%s: ", chat_name);
                rx_buffer[msgIndex + rx_msg_length-1] = '\0';
                fprintf(stdout, "%s\n", rx_buffer+msgIndex);
                msgIndex += rx_msg_length;
                dataLen -= rx_msg_length;
                rx_msg_length = 0;
            }
        } while (msgReady);
        // Move any remaining partial message to beginning of rx_buffer
        memmove(rx_buffer, rx_buffer+msgIndex, dataLen);
        rx_index = rx_msg_length = dataLen;
        msgIndex = 0;
        if (bytesRead != bytesWanted)
        {
            break;  // didn't get all asked for, wait for next NORM_RX_OBJECT_UPDATED
        }
    }  // end while(true)

}  // end ChantClient::RecvData()

class ChantCommand
{
    public:
        ChantCommand();
        ~ChantCommand();
        
        // some day build these directly into NORM API
        enum CCMode {NORM_FIXED, NORM_CC, NORM_CCE, NORM_CCL};
        
        enum 
        {
            MSG_HEADER_SIZE = 2,    // Big Endian message length header size
            MSG_SIZE_MAX = 65535    // (including length header)  
        };  
            
        void SetLoopback(bool state)
        {
            loopback = state;
            if (NORM_SESSION_INVALID != norm_session)
            {
                NormSetMulticastLoopback(norm_session, state);
                //NormSetLoopback(norm_session, state);  //  test code
            }
        }  
        
        void SetChatName(const char* text)
        {
            strncpy(chat_name, text, CHAT_NAME_MAX);
            chat_name[CHAT_NAME_MAX] = '\0';
        }
         
        void SetFtiInfo(bool state)
        {
            fti_info = state;
            if (NORM_SESSION_INVALID != norm_session)
                NormLimitObjectInfo(norm_session, state);
        }    
        
        int GetInputDescriptor() const
            {return fileno(input_file);}
        
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
        
        bool Start();
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
        bool TxPending() const
            {return (!input_needed && (input_index < input_msg_length));}
        bool TxReady() const
            {return (tx_ready && (!norm_acking || (tx_stream_buffer_count < tx_stream_buffer_max)));}
        void SendData();
        unsigned int WriteToStream(const char* buffer, unsigned int numBytes);
        void FlushStream(bool eom, NormFlushMode flushMode);
        
        
        // These can only be called post-OpenNormSession()
        void SetSilentReceiver(bool state)
            {NormSetSilentReceiver(norm_session, state);}
        void SetTxLoss(double txloss)
            {NormSetTxLoss(norm_session, txloss);}
        void SetRxLoss(double rxloss)
            {NormSetRxLoss(norm_session, rxloss);}
        
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
        
        void SetProbeTOS(UINT8 value)
            {probe_tos = value;}
        
    private:
        NormSessionHandle   norm_session;
        bool                is_multicast;
        UINT8               probe_tos;
        bool                loopback;
        bool                is_running;     
        
        char                chat_name[CHAT_NAME_MAX+1];
                                                     
        // State variables for reading input messages for transmission
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
        
        // Receive stream and state variables for writing received messages to output
        ChantClientTable    client_table;
       
        
        // These are some options mainly for testing purposes
        //double              tx_loss;
        unsigned long       input_byte_count;
        unsigned long       tx_byte_count;
        
        unsigned short      segment_size;
        unsigned short      block_size;
        unsigned short      num_parity;
        unsigned short      auto_parity;
        
        unsigned long       stream_buffer_size;
};  // end class ChantCommand

ChantCommand::ChantCommand()
 : norm_session(NORM_SESSION_INVALID), is_multicast(false), probe_tos(0), loopback(false), is_running(false),
   input_file(stdin), input_fd(fileno(stdin)), input_ready(true), 
   input_needed(false), input_msg_length(0), input_index(0),
   tx_stream (NORM_OBJECT_INVALID), tx_ready(true),
   tx_stream_buffer_max(0), tx_stream_buffer_count(0), tx_stream_bytes_remain(0), tx_watermark_pending(false), 
   norm_acking(false), auto_ack(false), acking_node_count(0), tx_ack_pending(false), flush_mode(NORM_FLUSH_ACTIVE),
   fti_info(false), input_byte_count(0), tx_byte_count(0),
   segment_size(1398), block_size(64), num_parity(0), auto_parity(0),
   stream_buffer_size(2*1024*1024)
{
    strcpy(chat_name, "???");
}

ChantCommand::~ChantCommand()
{
}


bool ChantCommand::OpenNormSession(NormInstanceHandle instance, const char* addr, unsigned short port, NormNodeId nodeId)
{
    if (NormIsUnicastAddress(addr))
        is_multicast = false;
    else
        is_multicast = true;
    norm_session = NormCreateSession(instance, addr, port, nodeId);
    if (NORM_SESSION_INVALID == norm_session)
    {
        fprintf(stderr, "chant error: unable to create NORM session\n");
        return false;
    }
    if (is_multicast)
    {
        NormSetTxPort(norm_session, port, true);  // for single port operation
        if (loopback)
        {
            NormSetRxPortReuse(norm_session, true);
            NormSetMulticastLoopback(norm_session, true);
        }
    }
    else
    {
        NormSetTxPort(norm_session, port, false);  // for single port operation
    }
    
    //NormSetLoopback(norm_session, loopback);
    
    // Set some default parameters (maybe we should put parameter setting in Start())
    NormSetDefaultSyncPolicy(norm_session, NORM_SYNC_STREAM);
    
    if (!is_multicast)
        NormSetDefaultUnicastNack(norm_session, true);
    
    NormSetTxRobustFactor(norm_session, 20);
    
    NormSetGrttProbingTOS(norm_session, probe_tos);
    
    NormSetFragmentation(norm_session, true);  // so that IP ID gets set for SMF DPD
    
    return true;
}  // end ChantCommand::OpenNormSession()

void ChantCommand::CloseNormSession()
{
    if (NORM_SESSION_INVALID == norm_session) return;
    NormDestroySession(norm_session);
    norm_session = NORM_SESSION_INVALID;
}  // end ChantCommand::CloseNormSession()

void ChantCommand::SetNormCongestionControl(CCMode ccMode)
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
}  // end ChantCommand::SetNormCongestionControl()

bool ChantCommand::Start()
{
    // Note the session NORM buffer size is set the same s stream_buffer_size
    unsigned int bufferSize = stream_buffer_size;
    if (!NormStartReceiver(norm_session, bufferSize))
    {
        fprintf(stderr, "chant error: unable to start NORM receiver\n");
        return false;
    }
    NormSetGrttEstimate(norm_session, 0.001);
    //NormSetGrttMax(norm_session, 0.100);
    NormSetBackoffFactor(norm_session, 0);
    if (norm_acking)
    {   
        // ack-based flow control enabled on command-line, 
        // so disable timer-based flow control
        NormSetFlowControl(norm_session, 0.0);
        NormTrackingStatus trackingMode = auto_ack ? NORM_TRACK_RECEIVERS : NORM_TRACK_NONE;
        NormSetAutoAckingNodes(norm_session, trackingMode);
        if (auto_ack && (0 == acking_node_count))
        {
            // This allows for the receiver(s) to start after the sender
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
    NormLimitObjectInfo(norm_session, fti_info);
    if (!NormStartSender(norm_session, instanceId, bufferSize, segment_size, block_size, num_parity))
    {
        fprintf(stderr, "chant error: unable to start NORM sender\n");
        NormStopReceiver(norm_session);
        return false;
    }
    if (auto_parity > 0)
        NormSetAutoParity(norm_session, auto_parity < num_parity ? auto_parity : num_parity);
    if (NORM_OBJECT_INVALID == (tx_stream = NormStreamOpen(norm_session, stream_buffer_size, chat_name, strlen(chat_name))))
    {
        fprintf(stderr, "chant error: unable to open NORM tx stream\n");
        NormStopSender(norm_session);
        NormStopReceiver(norm_session);
        return false;
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
    is_running = true;
    return true;
}  // end ChantCommand::Start();



void ChantCommand::ReadInput()
{
    //NormSuspendInstance(NormGetInstance(norm_session));
    while (input_needed && input_ready)
    {
        ssize_t result = read(input_fd, input_buffer + input_index, 1);
        if (result > 0)
        {
            input_msg_length++;
            // scan new input for end-of-line character to denote text message end
            if ('\n' == input_buffer[input_index++])
            {
                if (input_msg_length > 1)
                {
                    input_needed = false;
                    input_index = 0;
                    if (TxReady()) SendData();
                }
                else
                {
                    // ignore empty messages
                    input_index = input_msg_length = 0;
                    continue;
                }
            }
        }
        else if (0 == result)
        {
            // end-of-file reached, TBD - trigger final flushing and wrap-up
            fprintf(stderr, "chant: input end-of-file detected ...\n");
            NormStreamClose(tx_stream, true);
            if (norm_acking)
            {
                NormSetWatermark(norm_session, tx_stream, true);
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
                    perror("chant error reading input");
                    break;
            }
            break;
        }
    }  // end while (input_needed && input_ready)
    //NormResumeInstance(NormGetInstance(norm_session));
}  // end ChantCommand::ReadInput()

void ChantCommand::SendData()
{
    while (TxReady() && !input_needed)
    {
        // Note WriteToStream() or FlushStream() will set "tx_ready" to 
        // false upon flow control thus negating TxReady() status
        assert(input_index < input_msg_length);
        assert(input_msg_length);
        int result = WriteToStream(input_buffer + input_index, input_msg_length - input_index);
        input_index += result;
        if (input_index == input_msg_length)
        {
        
            // Complete message was sent, so set eom and optionally flush
            if (NORM_FLUSH_NONE != flush_mode)
                FlushStream(true, flush_mode); 
            else
                NormStreamMarkEom(tx_stream);
                
            if (input_byte_count > input_msg_length)
            {
                // Move unsent bytes beginning of 
                unsigned int unsentBytes = input_byte_count - input_msg_length;
                memmove(input_buffer, input_buffer+input_msg_length, unsentBytes);
                input_index = input_byte_count = input_msg_length = unsentBytes;
            }
            else
            {
                input_index = input_byte_count = input_msg_length = 0;
            }
            input_needed = true;
        }
        else
        {
            //fprintf(stderr, "SendData() impeded by flow control\n");
        }
    }  // end while (TxReady() && !input_needed)
}  // end ChantCommand::SendData()

unsigned int ChantCommand::WriteToStream(const char* buffer, unsigned int numBytes)
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
                NormSetWatermark(norm_session, tx_stream);//, true);
                tx_watermark_pending = true;
                tx_ack_pending = false;
            }
        }
        else
        {
            fprintf(stderr, "chant: sender flow control limited\n");
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
}  // end ChantCommand::WriteToStream()

void ChantCommand::FlushStream(bool eom, NormFlushMode flushMode)
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
            NormSetWatermark(norm_session, tx_stream, true);
            tx_ack_pending = false;
        }
    }
    else
    {
        NormStreamFlush(tx_stream, eom, flushMode);
    }
}  // end ChantCommand::FlushStream()


void ChantCommand::HandleNormEvent(const NormEvent& event)
{
    switch (event.type)
    {
        case NORM_TX_QUEUE_EMPTY:
            //TRACE("chant: flow control empty ...\n");
            tx_ready = true;
            break;
        case NORM_TX_QUEUE_VACANCY:
            //TRACE("chant: flow control relieved ...\n");
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
                        NormSetWatermark(norm_session, tx_stream, true);
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
                //fprintf(stderr, "flow control watermark reset\n");
                if (tx_ack_pending)
                {
                    // May as well advance the ack request point
                    NormSetWatermark(norm_session, tx_stream, true);
                }
                else
                {
                    // Uncomment for _persistent_ request, but may block forward progress
                    //NormResetWatermark(norm_session);
                }
            }
            break; 
            
        case NORM_TX_OBJECT_PURGED:
            // tx_stream graceful close completed
            NormStopSender(norm_session);
            tx_stream = NORM_OBJECT_INVALID;
            if (client_table.IsEmpty()) Stop();
            break;
        
        case NORM_REMOTE_SENDER_INACTIVE:
            //fprintf(stderr, "REMOTE SENDER INACTIVE node: %u\n", NormNodeGetId(event.sender));
            //NormNodeDelete(event.sender);
            break;
            
        case NORM_RX_OBJECT_NEW:
        {
            ChantClient* client = client_table.FindClient(event.sender);
            if (NULL == client)
            {
                if (NULL == (client = new ChantClient(event.sender)))
                {
                    fprintf(stderr, "chant error: unable to allocate new remote client state!\n");
                    break;
                }
                client_table.Insert(*client);
                char addrBuffer[16];
                unsigned int addrLen = 16;
                if (NormNodeGetAddress(event.sender, addrBuffer, &addrLen))
                {
                    ProtoAddress addr;
                    ProtoAddress::Type addrType = (4 == addrLen) ? ProtoAddress::IPv4 : ProtoAddress::IPv6;
                    addr.SetRawHostAddress(addrType, addrBuffer, addrLen);
                    char text[256];
                    addr.GetHostString(text, 256);
                    client->SetChatName(text);
                }
            }
            if (NORM_OBJECT_STREAM != NormObjectGetType(event.object))
            {
                fprintf(stderr, "chant error: received non-stream object?!\n");
                break;
            }
            client->SetRxStream(event.object);
            break;
        }
        case NORM_RX_OBJECT_INFO:
        {
            ChantClient* client = client_table.FindClient(event.sender);
            if (NULL == client)
            {
                fprintf(stderr, "chant: NORM_RX_OBJECT_INFO new rx stream ...\n");
                if (NULL == (client = new ChantClient(event.sender)))
                {
                    fprintf(stderr, "chant error: unable to allocate new remote client state!\n");
                    break;
                }
                client_table.Insert(*client);
                client->SetRxStream(event.object);
            }
            char text[CHAT_NAME_MAX+1];
            unsigned int infoLen = NormObjectGetInfo(event.object, text, CHAT_NAME_MAX+1);
            if (infoLen > CHAT_NAME_MAX) infoLen = CHAT_NAME_MAX;
            text[infoLen] = '\0';
            fprintf(stdout, "(%s is \"%s\")\n", client->GetChatName(), text);
            client->SetChatName(text);
            break;
        }
        
        case NORM_RX_OBJECT_UPDATED:
        {
            ChantClient* client = client_table.FindClient(event.sender);
            if (NULL == client)
            {
                if (NULL == (client = new ChantClient(event.sender)))
                {
                    fprintf(stderr, "chant error: unable to allocate new remote client state!\n");
                    break;
                }
                client_table.Insert(*client);
                client->SetRxStream(event.object);
            }
            client->RecvData();
            break;
        }
        case NORM_RX_OBJECT_ABORTED:
            //fprintf(stderr, "chant: NORM_RX_OBJECT_ABORTED\n");
            break;
            
        case NORM_RX_OBJECT_COMPLETED:
            //fprintf(stderr, "chant: NORM_RX_OBJECT_COMPLETED\n");
            break;
            
        default:
            break;     
    }
        
}  // end ChantCommand::HandleNormEvent()

void Usage()
{
    fprintf(stderr, "Usage: chant [name <chatName>][addr <addr>[/<port>]]\n"
                    "             [interface <name>] [loopback]\n"
                    "             [cc|cce|ccl|rate <bitsPerSecond>]\n"
                    "             [ack _auto_|<node1>[,<node2>,...]]\n"
                    "             [id <normNodeId>][flush {none|passive|active}]\n"
                    "             [boost] [debug <level>] [trace]\n"
                    "             [log <logfile>] [segment <bytes>] [block <count>]\n"
                    "             [parity <count>] [auto <count>]\n"
                    "             [streambuffer <bytes>][silent]\n");
}  // end Usage()

void PrintHelp()
{
    fprintf(stderr, "\nHelp for chant:\n\n") ;
    fprintf(stderr,
            "The 'chant' application sends text messages read from STDIN and outputs received messages to STDOUT.\n"
            "Key command line options are:\n\n"
            "   name <chatHandle>       -- specifies chat user name ('handle') to advertise\n"           
            "   addr <addr>[/<port>]    -- specifies the network address over which to send/receive NORM protocol\n"
            "   interface <name>        -- Specifies the name of the network interface on which to conduct NORM protocol\n"
            "                              (e.g., 'eth0')\n"
            "   rate <bitsPerSecond>    -- sets fixed sender rate\n"
            "   [cc|cce|ccl]            -- Enables optional NORM congestion control mode (overrides 'rate')\n"
            "   ack [auto|none|<list>]  -- Instructs sender to request positive acknowledgement from receiver nodes (auto or comma-delimited nodeId list)\n"
            "   id <nodeId>             -- Specifies the node id for the local NORM instance\n"           
            "   loopback                -- Enables 'loopback' sessions on the same host machine.  Required for multicast loopback.\n"
            "   flush [<flushMode>]     -- Choose 'none', 'passive', or 'active' message stream flushing mode.  If 'none',\n"
            "                              NORM_DATA packets will always be packed with message content up to the full\n"
            "                              segment size.  If 'passive', short NORM_DATA packets will be sent to transmit\n"
            "                              any messages as soon as possible.  If 'active', NORM stream will be flushed\n"
            "                              on a per-message basis as with 'passive' mode, but positive acknowledgment will\n"
            "                              _also_ be requested if a list of acking receiver node ids has beeen provided.\n"
            "   streambuffer <bytes>    -- Specifies the size of the NORM stream buffer (optional).\n\n");
    Usage();

}  // end PrintHelp()

int main(int argc, char* argv[])
{
    // REQUIRED parameters initiailization
    NormNodeId nodeId = NORM_NODE_NONE;
    
    char sessionAddr[64];
    strcpy(sessionAddr, "224.1.2.3");
    unsigned int sessionPort = 6003;
    
    double txRate = 0.0; // used for non-default NORM_FIXED ccMode
    ChantCommand::CCMode ccMode = ChantCommand::NORM_CCL;
    const char* mcastIface = NULL;
    NormNodeId ackingNodeList[256]; 
    unsigned int ackingNodeCount = 0;
            
    bool loopback = false;
    bool ftiInfo = true;
    int debugLevel = 0;
    bool trace = false;
    const char* logFile = NULL;
    bool silentReceiver = false;
    double txloss = 0.0;
    double rxloss = 0.0;
    // TBD - set these defaults to reasonable values or just use ChantCommand constructor defaults
    unsigned long streamBufferSize = 1*1024*1024;

    // Instantiate a ChantCommand and set default params
    ChantCommand chant;
    chant.SetFlushMode(NORM_FLUSH_ACTIVE);
    
    chant.SetAutoAck(true);  // more succinct flushing
    
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
        else if (0 == strncmp(cmd, "name", len))
        {
            if (i >= argc)
            {
                fprintf(stderr, "chant error: missing 'name' value!\n");
                Usage();
                return -1;
            }
            chant.SetChatName(argv[i++]);
        }
        else if (0 == strncmp(cmd, "loopback", len))
        {
            loopback = true;
        }
        else if (0 == strncmp(cmd, "info", len))
        {
            ftiInfo = true;
        }
        else if (0 == strncmp(cmd, "addr", len))
        {
            if (i >= argc)
            {
                fprintf(stderr, "chant error: missing 'addr[/port]' value!\n");
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
        else if (0 == strncmp(cmd, "id", len))
        {
            if (i >= argc)
            {
                fprintf(stderr, "chant error: missing 'id' value!\n");
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
                fprintf(stderr, "chant error: missing 'id' <nodeId> value!\n");
                Usage();
                return -1;
            }
            const char* alist = argv[i++];
            if (0 == strcmp(alist, "auto"))
            {
                chant.SetAutoAck(true);
            }
            else if (0 == strcmp(alist, "none"))
            {
                chant.SetAutoAck(false);
            }
            else
            {
                while ((NULL != alist) && (*alist != '\0'))
                {
                    // TBD - Do we need to skip leading white space?
                    int id;
                    if (1 != sscanf(alist, "%d", &id))
                    {
                        fprintf(stderr, "chant error: invalid acking node list!\n");
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
                chant.SetFlushMode(NORM_FLUSH_NONE);
            }
            else if (0 == strcmp(mode, "passive"))
            {
                chant.SetFlushMode(NORM_FLUSH_PASSIVE);
            }
            else if (0 == strcmp(mode, "active"))
            {
                chant.SetFlushMode(NORM_FLUSH_ACTIVE);
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
                fprintf(stderr, "chant error: missing 'rate' <bitsPerSecond> value!\n");
                Usage();
                return -1;
            }
            if (1 != sscanf(argv[i++], "%lf", &txRate))
            {
                fprintf(stderr, "chant error: invalid transmit rate!\n");
                Usage();
                return -1;
            }       
            // set fixed-rate operation
            ccMode = ChantCommand::NORM_FIXED;
        }
        else if (0 == strcmp(cmd, "cc"))
        {
            ccMode = ChantCommand::NORM_CC;
        }
        else if (0 == strcmp(cmd, "cce"))
        {
            ccMode = ChantCommand::NORM_CCE;
        }
        else if (0 == strcmp(cmd, "ccl"))
        {
            ccMode = ChantCommand::NORM_CCL;
        }
        else if (0 == strncmp(cmd, "interface", len))
        {
            if (i >= argc)
            {
                fprintf(stderr, "chant error: missing 'interface' <name>!\n");
                Usage();
                return -1;
            }
            mcastIface = argv[i++];
        }
        else if (0 == strncmp(cmd, "segment", len))
        {
            if (i >= argc)
            {
                fprintf(stderr, "chant error: missing 'segment' size!\n");
                Usage();
                return -1;
            }
            unsigned short value;
            if (1 != sscanf(argv[i++], "%hu", &value))
            {
                fprintf(stderr, "chant error: invalid 'segment' size!\n");
                Usage();
                return -1;
            }
            chant.SetSegmentSize(value);
        }
        else if (0 == strncmp(cmd, "block", len))
        {
            if (i >= argc)
            {
                fprintf(stderr, "chant error: missing 'block' size!\n");
                Usage();
                return -1;
            }
            unsigned short value;
            if (1 != sscanf(argv[i++], "%hu", &value))
            {
                fprintf(stderr, "chant error: invalid 'block' size!\n");
                Usage();
                return -1;
            }
            chant.SetBlockSize(value);
        }
        else if (0 == strncmp(cmd, "parity", len))
        {
            if (i >= argc)
            {
                fprintf(stderr, "chant error: missing 'parity' count!\n");
                Usage();
                return -1;
            }
            unsigned short value;
            if (1 != sscanf(argv[i++], "%hu", &value))
            {
                fprintf(stderr, "chant error: invalid 'parity' count!\n");
                Usage();
                return -1;
            }
            chant.SetNumParity(value);
        }
        else if (0 == strncmp(cmd, "auto", len))
        {
            if (i >= argc)
            {
                fprintf(stderr, "chant error: missing 'auto' parity count!\n");
                Usage();
                return -1;
            }
            unsigned short value;
            if (1 != sscanf(argv[i++], "%hu", &value))
            {
                fprintf(stderr, "chant error: invalid 'auto' parity count!\n");
                Usage();
                return -1;
            }
            chant.SetAutoParity(value);
        }
        else if (0 == strncmp(cmd, "streambuffer", len))
        {
            unsigned long value = 0 ;
            if (i >= argc)
            {
                fprintf(stderr, "chant error: missing 'streambuffer' size!\n");
                Usage();
                return -1;
            }
            if (1 != sscanf(argv[i++], "%lu", &value))
            {
                fprintf(stderr, "chant error: invalid 'streambuffer' size!\n");
                Usage();
                return -1;
            }
            streamBufferSize = value;
        }
        else if (0 == strncmp(cmd, "silent", len))
        {
            silentReceiver = true;
        }
        else if (0 == strncmp(cmd, "txloss", len))
        {
            if (1 != sscanf(argv[i++], "%lf", &txloss))
            {
                fprintf(stderr, "chant error: invalid 'txloss' value!\n");
                Usage();
                return -1;
            }
        }
        else if (0 == strncmp(cmd, "rxloss", len))
        {
            if (1 != sscanf(argv[i++], "%lf", &rxloss))
            {
                fprintf(stderr, "chant error: invalid 'rxloss' value!\n");
                Usage();
                return -1;
            }
        }
        else if (0 == strncmp(cmd, "debug", len))
        {
            if (i >= argc)
            {
                fprintf(stderr, "chant error: missing 'debug' <level>!\n");
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
                fprintf(stderr, "chant error: missing 'log' <fileName>!\n");
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
            fprintf(stderr, "chant error: invalid command \"%s\"!\n", cmd);
            Usage();
            return -1;
        }
    }
    
    
    /*
    if (NORM_NODE_NONE == nodeId)
    {
        fprintf(stderr, "chant error: no local 'id' provided!\n");
        Usage();
        return -1;
    }
    */
    
    
    // TBD - should provide more error checking of NORM API calls
    NormInstanceHandle normInstance = NormCreateInstance();
    
    NormSetDebugLevel(debugLevel);
    if ((NULL != logFile) && !NormOpenDebugLog(normInstance, logFile))
    {
        perror("chant error: unable to open log file");
        Usage();
        return -1;
    }
    chant.SetStreamBufferSize(streamBufferSize);

    chant.SetLoopback(loopback);
    chant.SetFtiInfo(ftiInfo);
    
    if (!chant.OpenNormSession(normInstance, sessionAddr, sessionPort, (NormNodeId)nodeId))
    {
        fprintf(stderr, "chant error: unable to open NORM session\n");
        NormDestroyInstance(normInstance);
        return -1;
    }
    
    if (silentReceiver) chant.SetSilentReceiver(true);
    if (txloss > 0.0) chant.SetTxLoss(txloss);
    if (rxloss > 0.0) chant.SetRxLoss(rxloss);
    
    for (unsigned int i = 0; i < ackingNodeCount; i++)
        chant.AddAckingNode(ackingNodeList[i]);
    
    chant.SetNormCongestionControl(ccMode);
    if (ChantCommand::NORM_FIXED == ccMode)
        chant.SetNormTxRate(txRate);
    if (NULL != mcastIface)
        chant.SetNormMulticastInterface(mcastIface);
    
    if (trace) chant.SetNormMessageTrace(true);
    
    // TBD - set NORM session parameters
    chant.Start(); 
    
    // TBD - add WIN32 support using win32InputHandler code
    //       and MsgWaitForMultipleObjectsEx() instead of select()
    
    int normfd = NormGetDescriptor(normInstance);
    // Get input/output descriptors and set to non-blocking i/o
    int inputfd = chant.GetInputDescriptor();
    if (-1 == fcntl(inputfd, F_SETFL, fcntl(inputfd, F_GETFL, 0) | O_NONBLOCK))
        perror("chant: fcntl(inputfd, O_NONBLOCK) error");
    fd_set fdsetInput, fdsetOutput;
    FD_ZERO(&fdsetInput);
    FD_ZERO(&fdsetOutput);
    
    while (chant.IsRunning())
    {
        int maxfd = -1;
        bool wait = true;
        bool waitOnNorm = false;
        if (chant.InputNeeded())
        {
            if (chant.InputReady())
            {
                FD_CLR(inputfd, &fdsetInput);
                wait = false;
            }
            else
            {
                FD_SET(inputfd, &fdsetInput);
                if (inputfd > maxfd) maxfd = inputfd;
            }
        }
        else
        {
            FD_CLR(inputfd, &fdsetInput);
        } 
        if (chant.TxPending())
        {
            if (chant.TxReady())
                wait = false;
        }
        waitOnNorm = true;  // always looking for receive notifications
        
        if (waitOnNorm)
        {
            // we need to wait until NORM is tx_ready or rx_ready
            FD_SET(normfd, &fdsetInput);
            if (normfd > maxfd) maxfd = normfd;
        }
        else
        {
            FD_CLR(normfd, &fdsetInput);
        }
        if (wait)
        {
            int result = select(maxfd+1, &fdsetInput, NULL, NULL, NULL);
            switch (result)
            {
                case -1:
                    switch (errno)
                    {
                        case EINTR:
                        case EAGAIN:
                            continue;
                        default:
                            perror("chant select() error");
                            // TBD - stop ChantCommand
                            break;
                    }
                    break;
                case 0:
                    // timeout
                    break;
                default:
                    if (FD_ISSET(inputfd, &fdsetInput))
                        chant.SetInputReady();
                    break; 
            }
        }
        // We always clear out/handle pending NORM API events
        // (to keep event queue from building up)
        unsigned int eventCount = 0;
        const unsigned int maxBurstCount = 50;
        NormEvent event;
        while ((eventCount < maxBurstCount) && (NormGetNextEvent(normInstance, &event, false)))
        {
            chant.HandleNormEvent(event);
            eventCount += 1;
        }
        
        // As a result of input/output ready or NORM notification events:
        // 1) Read from input if needed and ready 
        if (chant.InputNeeded() && chant.InputReady())
            chant.ReadInput(); 
        // 2) Send any pending tx message
        if (chant.TxPending() && chant.TxReady())
            chant.SendData();
        
    }  // end while(chant.IsRunning()

    fflush(stderr);
    
    NormDestroyInstance(normInstance);
    
    fprintf(stderr, "chant exiting ...\n");
    
    return 0;
    
}  // end main()

