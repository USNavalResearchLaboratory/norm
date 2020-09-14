
#include "normApi.h"
#include <unistd.h>      // for read() and write()
#include <stdio.h>       // for printf(), etc
#include <stdlib.h>      // for srand()
#include <string.h>      // for strrchr(), memset(), etc
#include <sys/time.h>    // for gettimeofday()
#include <arpa/inet.h>   // for htons()
#include <fcntl.h>       // for, well, fnctl()
#include <errno.h>       // obvious child
#include <assert.h>      // embarrassingly obvious

#define SHOOT_NOW 1

#include "protoCheck.h"

class NormMsgr
{
    public:
        NormMsgr();
        ~NormMsgr();
        
        // some day build these directly into NORM API
        enum CCMode {NORM_FIXED, NORM_CC, NORM_CCE, NORM_CCL};
        
        enum 
        {
            MSG_SIZE_MAX = 65535,
            MSG_HEADER_SIZE = 2
        };  
            
        // helper class and methods for message management
        class MessageQueue;
        class Message
        {
            friend class MessageQueue;
            public:
                Message();
                Message(char* buffer, unsigned int size);
                ~Message();
                
                bool Init(unsigned int size);
                void Destroy();
                
                unsigned int GetSize() const
                    {return msg_size;}
                const char* GetHeader() const
                    {return msg_header;}
                char* AccessHeader()
                    {return msg_header;}
                const char* GetBuffer() const
                    {return msg_buffer;}
                char* AccessBuffer()
                    {return msg_buffer;}
                
                void ResetIndex(unsigned int index = 0)
                    {msg_index = index;}
                void IncrementIndex(unsigned int count)
                    {msg_index += count;}
                unsigned int GetIndex() const
                    {return msg_index;}
                
                bool IsComplete() const
                    {return (msg_index == (MSG_HEADER_SIZE + msg_size));}
                
            private:
                unsigned int    msg_size;
                unsigned int    msg_index;
                char            msg_header[MSG_HEADER_SIZE];  // this could be externalized to NormMsgr::input_msg_header / output_msg_header members
                char*           msg_buffer;
                Message*        prev;
                Message*        next;
        };  // end class NormMsgr::Message    
        
        class MessageQueue
        {
            public:
                MessageQueue();
                ~MessageQueue();
                
                bool IsEmpty() const
                    {return (NULL == head);}
                
                void Prepend(Message& msg);
                void Append(Message& msg);
                void Remove(Message& msg);
                Message* RemoveHead();
                Message* RemoveTail();
                
                void Destroy();
                
                unsigned int GetCount() const
                    {return msg_count;}
                
            private:
                Message*        head;
                Message*        tail;
                unsigned int    msg_count;
        };  // end class NormMsgr::MessageQueue
        Message* NewMessage(unsigned int size);
        
        void Destroy();
            
        bool OpenNormSession(NormInstanceHandle instance, 
                             const char*        addr,
                             unsigned short     port,
                             NormNodeId         nodeId);
        void CloseNormSession();
        
        void SetNormCongestionControl(CCMode ccMode);
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
        void SetLoopback(bool state)
        {
            loopback = state;
            if (NORM_SESSION_INVALID != norm_session)
                NormSetMulticastLoopback(norm_session, state);
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
            norm_acking = true;  // invoke ack-based flow control
        }
        void SetFlushing(bool state)
            {norm_flushing = state;}
        
        bool Start(bool sender, bool receiver);
        void Stop()
            {is_running = false;}
        bool IsRunning() const
            {return is_running;}
        
        unsigned long GetSentCount()
            {return sent_count;}
        
        void HandleNormEvent(const NormEvent& event);
        
        // Sender methods
        FILE* GetInputFile() const
            {return input_file;}
        int GetInputDescriptor() const
            {return input_fd;}
        bool InputReady() const
            {return input_ready;}
        void SetInputReady()
            {input_ready = true;}
        bool InputNeeded() const
            {return input_needed;}
        bool InputMessageReady() const
            {return ((NULL != input_msg) && !input_needed);}
        bool ReadInput();
        
        bool TxReady() const;
        bool SendMessage();
        bool EnqueueMessageObject();
        
        // Receiver methods
        void SetOutputFile(FILE* filePtr)
        {
            output_file = filePtr;
            output_fd = fileno(filePtr);
        }
        FILE* GetOutputFile() const
            {return output_file;}
        int GetOutputDescriptor() const
            {return output_fd;}
        void SetOutputReady()
            {output_ready = true;}
        bool OutputReady() const
            {return output_ready;}
        bool OutputPending() const
            {return (NULL != output_msg);}
        bool RxNeeded() const
            {return rx_needed;}
        bool WriteOutput();
        
        void OmitHeader(bool state) 
            {omit_header = state;}
        
        // These can only be called post-OpenNormSession
        void SetAutoAck(bool enable)
        {
                NormTrackingStatus trackingMode = enable? NORM_TRACK_RECEIVERS : NORM_TRACK_NONE;
                NormSetAutoAckingNodes(norm_session, trackingMode);
                norm_acking = enable;
        }
        void SetSilentReceiver(bool state)
            {NormSetSilentReceiver(norm_session, state);}
        void SetTxLoss(double txloss)
            {NormSetTxLoss(norm_session, txloss);}
            
    private:
        bool                is_running;                                                  
        FILE*               input_file;      // stdin by default  
        int                 input_fd;
        bool                input_ready;                       
        bool                input_needed;    //                   
        bool                input_finished;                       
        Message*            input_msg;       // current input message being read/sent*   
        MessageQueue        input_msg_list;  // list of enqueued messages (in norm sender cache)
            
        NormSessionHandle   norm_session;
        bool                is_multicast;
        bool                loopback;
        unsigned int        norm_tx_queue_max;   // max number of objects that can be enqueued at once 
        unsigned int        norm_tx_queue_count; // count of unacknowledged enqueued objects (TBD - optionally track size too)
        bool                norm_flow_control_pending;
        bool                norm_tx_vacancy;
        bool                norm_acking;
        bool                norm_flushing;
        NormObjectHandle    norm_flush_object;
        NormObjectHandle    norm_last_object;
        unsigned long       sent_count;
        
        bool                rx_needed;
        FILE*               output_file;
        int                 output_fd;
        bool                output_ready;
        Message*            output_msg;
        MessageQueue        output_msg_queue;
        MessageQueue        temp_msg_queue;
        // These are some options mainly for testing purposes
        bool                omit_header;  // if "true", receive message length header is _not_ written to output
        bool                rx_silent;
        double              tx_loss;
            
};  // end class NormMsgr

NormMsgr::NormMsgr()
 : input_file(stdin), input_fd(fileno(stdin)), input_ready(false), input_needed(false), input_finished(false),
   input_msg(NULL), norm_session(NORM_SESSION_INVALID), is_multicast(false), loopback(false), 
   norm_tx_queue_max(8192), norm_tx_queue_count(0), 
   norm_flow_control_pending(false), norm_tx_vacancy(true), norm_acking(false), 
   norm_flushing(true), norm_flush_object(NORM_OBJECT_INVALID), norm_last_object(NORM_OBJECT_INVALID),
   sent_count(0),  rx_needed(false), output_file(stdout), output_fd(fileno(stdout)), 
   output_ready(true), output_msg(NULL), 
   omit_header(false), rx_silent(false), tx_loss(0.0)
{
}

NormMsgr::~NormMsgr()
{
    Destroy();
}

void NormMsgr::Destroy()
{
    if (NULL != input_msg) 
    {
        delete input_msg;
        input_msg = NULL;
    }
    input_msg_list.Destroy();
    if (NULL != output_msg) 
    {
        delete output_msg;
        delete output_msg;
    }
    output_msg_queue.Destroy();
}


bool NormMsgr::OpenNormSession(NormInstanceHandle instance, const char* addr, unsigned short port, NormNodeId nodeId)
{
    if (NormIsUnicastAddress(addr))
        is_multicast = false;
    else
        is_multicast = true;
    norm_session = NormCreateSession(instance, addr, port, nodeId);
    if (NORM_SESSION_INVALID == norm_session)
    {
        fprintf(stderr, "normMsgr error: unable to create NORM session\n");
        return false;
    }
    
    if (is_multicast)
    {
        NormSetRxPortReuse(norm_session, true);
        if (loopback)
            NormSetMulticastLoopback(norm_session, true);
    }
    
    
    // Set some default parameters (maybe we should put parameter setting in Start())
    fprintf(stderr, "setting rx cache limit to %u\n", norm_tx_queue_max);
    if (norm_tx_queue_max > 65535/2) norm_tx_queue_max = 65535/2;
    NormSetRxCacheLimit(norm_session, norm_tx_queue_max);
    NormSetDefaultSyncPolicy(norm_session, NORM_SYNC_ALL);
    
    if (!is_multicast)
        NormSetDefaultUnicastNack(norm_session, true);
    NormSetTxCacheBounds(norm_session, 10*1024*1024, norm_tx_queue_max, norm_tx_queue_max);
    
    //NormSetMessageTrace(norm_session, true);
    
    //NormSetTxRobustFactor(norm_session, 20);
    
    return true;
}  // end NormMsgr::OpenNormSession()

void NormMsgr::CloseNormSession()
{
    if (NORM_SESSION_INVALID == norm_session) return;
    NormDestroySession(norm_session);
    norm_session = NORM_SESSION_INVALID;
}  // end NormMsgr::CloseNormSession()

void NormMsgr::SetNormCongestionControl(CCMode ccMode)
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
        case NORM_FIXED:
            NormSetEcnSupport(norm_session, false, false, false);
            break;
    }
    if (NORM_FIXED != ccMode)
        NormSetCongestionControl(norm_session, true);
    else
        NormSetCongestionControl(norm_session, false);
}  // end NormMsgr::SetNormCongestionControl()

bool NormMsgr::Start(bool sender, bool receiver)
{
    fprintf(stderr, "enter NormMsgr::Start() ...\n");
    // TBD - make these command-line accessible
    unsigned int bufferSize = 64*1024*1024;
    unsigned int segmentSize = 1400;
    unsigned int blockSize = 64;
    unsigned int numParity = 0;
    unsigned int txSockBufferSize = 4*1024*1024;
    unsigned int rxSockBufferSize = 6*1024*1024;
    
    if (receiver)
    {
        if (!NormStartReceiver(norm_session, bufferSize))
        {
            fprintf(stderr, "normMsgr error: unable to start NORM receiver\n");
            return false;
        }
        // Note: NormPreallocateRemoteSender() MUST be called AFTER NormStartReceiver()
        NormPreallocateRemoteSender(norm_session, bufferSize, segmentSize, blockSize, numParity, bufferSize);
        NormSetRxSocketBuffer(norm_session, rxSockBufferSize);
        rx_needed = true;
        fprintf(stderr, "normMsgr: receiver ready.\n");
    }
    if (sender)
    {
        if (norm_acking)
        {   
            // ack-based flow control enabled on command-line, 
            // so disable timer-based flow control
            NormSetFlowControl(norm_session, 0.0);
        }
        NormSetGrttEstimate(norm_session, 0.001);
        //NormSetGrttMax(norm_session, 0.100);
        NormSetBackoffFactor(norm_session, 0);
        
        // Pick a random instance id for now
        struct timeval currentTime;
        gettimeofday(&currentTime, NULL);
        srand(currentTime.tv_usec);  // seed random number generator
        NormSessionId instanceId = (NormSessionId)rand();
        if (!NormStartSender(norm_session, instanceId, bufferSize, segmentSize, blockSize, numParity))
        {
            fprintf(stderr, "normMsgr error: unable to start NORM sender\n");
            if (receiver) NormStopReceiver(norm_session);
            return false;
        }
        //NormSetAutoParity(norm_session, 2);
        NormSetTxSocketBuffer(norm_session, txSockBufferSize);
        input_needed = true;
    }
    //ProtoCheckResetLogging();
    is_running = true;
    return true;
}  // end NormMsgr::Start();

// Returns "true" when a complete message has been read
bool NormMsgr::ReadInput()
{
    assert(input_needed);
    while (input_needed)
    {
        size_t readLength;
        char* bufferPtr;
        if ((NULL == input_msg) || (input_msg->GetIndex() < MSG_HEADER_SIZE)) 
        {
            if (NULL == input_msg)
            {
                // Allocate a new message/buffer for input
                if (NULL == (input_msg = new Message()))
                {
                    perror("normMsgr new Message error");
                    Stop();  // fatal out of memory error
                    return false;
                }
            }
            // need to read msg "length" header
            readLength = MSG_HEADER_SIZE - input_msg->GetIndex();
            bufferPtr = input_msg->AccessHeader() + input_msg->GetIndex();
        }
        else
        {
            unsigned int offset = input_msg->GetIndex() - MSG_HEADER_SIZE;
            readLength = input_msg->GetSize() - offset;
            bufferPtr = input_msg->AccessBuffer() + offset;
        }
        ssize_t result = read(input_fd, bufferPtr, readLength);
        if (result > 0)
        {
            input_msg->IncrementIndex(result);
            if ((size_t)result < readLength) 
            {
                // Still need more input
                // (wait for next input notification to read more)
                return false;
            }
            else if (MSG_HEADER_SIZE == input_msg->GetIndex())
            {
                // We have now read the message size header
                // TBD - support other message header formats?
                // (for now, assume 2-byte message length header)
                uint16_t msgSize ;
                memcpy(&msgSize, input_msg->GetHeader(), MSG_HEADER_SIZE);
                msgSize = ntohs(msgSize) - MSG_HEADER_SIZE;
                if (!input_msg->Init(msgSize))
                {
                    perror("normMsgr: input message initialization error");
                    Stop();  // fatal out of memory error
                    return false;
                }    
            }
            if (input_msg->IsComplete())  // have read complete header and message
            {
                // We have now read in the complete message
                input_needed = false;
            }
        }
        else if (0 == result)
        {
             // end-of-file reached, TBD - trigger final flushing and wrap-up
            fprintf(stderr, "normMsgr: input end-of-file detected (last:%p)...\n", norm_last_object);
            delete input_msg;
            input_msg = NULL;
            input_ready = false;
            input_needed = false;
            input_finished = true;
            if (norm_acking)
            {
                if (NORM_OBJECT_INVALID == norm_last_object)
                    is_running = false;  // everything sent and acked
                else if (!norm_flushing)
                    NormSetWatermark(norm_session, norm_last_object, true);
            }
        }
        else  // result < 0
        {
            switch (errno)
            {
                case EINTR:
                    continue;  // interupted, try again
                case EAGAIN:
                    // input starved, wait for next notification
                    break;
                default:
                    perror("normMsgr error reading input");
                    break;
            }
            input_ready = false;
            return false;
        }
    }  
    return true;
}  // end NormMsgr::ReadInput()

bool NormMsgr::WriteOutput()
{
    while (NULL != output_msg)
    {
        size_t writeLength;
        const char* bufferPtr;
        if (output_msg->GetIndex() < MSG_HEADER_SIZE)
        {
            writeLength = MSG_HEADER_SIZE - output_msg->GetIndex();
            bufferPtr = output_msg->GetHeader() + output_msg->GetIndex();
        }
        else
        {
            unsigned int offset = output_msg->GetIndex() - MSG_HEADER_SIZE;
            writeLength = output_msg->GetSize() - offset;
            bufferPtr = output_msg->GetBuffer() + offset;
        }
        ssize_t result = write(output_fd, bufferPtr, writeLength);
        if (result >= 0)
        {
            output_msg->IncrementIndex(result);
            if ((size_t)result < writeLength)
                output_ready = false; // blocked, wait for output notification
        }
        else
        {
            switch (errno)
            {
                case EINTR:
                    continue;  // interupted, try again
                case EAGAIN:
                    // input starved, wait for next notification
                    output_ready = false;
                    break;
                default:
                    perror("normMsgr error writing output");
                    break;
            }
            return false;
        }
        if (output_msg->IsComplete())
        {
            fflush(output_file);
            //delete output_msg;
            
            temp_msg_queue.Append(*output_msg); // cache for debugging purposes
            if (temp_msg_queue.GetCount() > 32)
            {
                //fprintf(stderr, "deleting cached recv'd message ...\n");
                delete temp_msg_queue.RemoveHead();
            }
            output_msg = output_msg_queue.RemoveHead();
            if (NULL != output_msg)
            {
                if (omit_header)
                    output_msg->ResetIndex(MSG_HEADER_SIZE);
                else
                    output_msg->ResetIndex();
            }
            else
            {
                rx_needed = true;
            }
            break;
        }
    }
    return true;
}  // end NormMsgr::WriteOutput()

bool NormMsgr::TxReady() const
{
    // This returns true if new tx data can be enqueued to NORM
    // This is based on the state with respect to prior successful data
    // data enqueuing (or stream writing) and NORM_TX_QUEUE_EMPTY or
    // NORM_TX_QUEUE_VACANCY notifications (tracked by the "norm_tx_vacancy" variable,
    // _and_ (if ack-based flow control is enabled) the norm_tx_queue_count or 
    // norm_stream_buffer_count status.
    if (norm_tx_vacancy)
    {
        if (norm_tx_queue_count >= norm_tx_queue_max)
            return false;  // still waiting for ACK
        return true;
    }
    else
    {
        return false;
    }
}  // end NormMsgr::NormTxReady()

bool NormMsgr::SendMessage()
{
    if (EnqueueMessageObject())
    {
        // Our buffered message was sent, so reset input indices
        // and request next message from input
        input_msg_list.Append(*input_msg);
        input_msg = NULL;
        input_needed = true;
        sent_count++;
        return true;   
    }
    // else will be prompted to retry by NORM event (queue vacancy, watermark completion)
    return false;
}  // end NormMsgr::SendMessage()

bool NormMsgr::EnqueueMessageObject()
{
    if (norm_acking) 
    {
        assert(norm_tx_queue_count < norm_tx_queue_max);
        if (norm_tx_queue_count >= norm_tx_queue_max)
            return false;
    }
        
    // Enqueue the message data for transmission
    NormObjectHandle object = NormDataEnqueue(norm_session, input_msg->AccessBuffer(), input_msg->GetSize());
    if (NORM_OBJECT_INVALID == object)
    {
        // This might happen if a non-acking receiver is present and
        // has nacked for the oldest object in the queue even if all                
        // of our acking receivers have acknowledged it.  
        fprintf(stderr, "NO VACANCY count:%u max:%u\n", norm_tx_queue_count, norm_tx_queue_max);
        norm_tx_vacancy = false;     
        return false;
    }
    //NormObjectRetain(object);
    NormObjectSetUserData(object, input_msg); // so we can remove/delete upon purge
    if (norm_acking)
    {
        // ack-based flow control has been enabled
        norm_tx_queue_count++;
        if (!norm_flow_control_pending && (norm_tx_queue_count >= (norm_tx_queue_max / 2)))
        {
            NormSetWatermark(norm_session, object, true);  // overrideFlush == true
            norm_last_object = object;
            norm_flow_control_pending = true;
        }
        else if (norm_flushing)  // per-message acking
        {
#ifdef SHOOT_FIRST
            NormSetWatermark(norm_session, object, true);
            norm_last_object = object;
#else // ACK_LATER
            if (norm_flow_control_pending)
            {
                norm_flush_object = object;  // will be used as watermark upon flow control ack
            }
            else
            {
                NormSetWatermark(norm_session, object, true);
                norm_last_object = object;
            }
#endif // SHOOT_FIRST/ACK_LATER
        }
        else
        {
            //norm_last_object = object;
        }
    }
    return true;
}  // end NormMsgr::EnqueueMessageObject()

void NormMsgr::HandleNormEvent(const NormEvent& event)
{
    switch (event.type)
    {
        case NORM_TX_QUEUE_EMPTY:
        case NORM_TX_QUEUE_VACANCY:
            norm_tx_vacancy = true;
            break;
            
        case NORM_GRTT_UPDATED:
            //fprintf(stderr, "new GRTT = %lf\n", NormGetGrttEstimate(norm_session));
            break;
            
        case NORM_TX_WATERMARK_COMPLETED:
            if (NORM_ACK_SUCCESS == NormGetAckingStatus(norm_session))
            {
                //fprintf(stderr, "WATERMARK COMPLETED\n");
                norm_last_object = NORM_OBJECT_INVALID;
                if (norm_flow_control_pending)
                {
                    norm_tx_queue_count -= (norm_tx_queue_max / 2);
                    norm_flow_control_pending = false;
                    if (NORM_OBJECT_INVALID != norm_flush_object)
                    {
                        NormSetWatermark(norm_session, norm_flush_object, true);
                        norm_last_object = norm_flush_object;
                        norm_flush_object = NORM_OBJECT_INVALID;
                    }
                }
                if (input_finished && (NORM_OBJECT_INVALID == norm_last_object))
                    is_running = false;
            }
            else
            {
                // TBD - we could see who did and how didn't ACK and possibly remove them
                //       from our acking "membership".  For now, we are infinitely
                //       persistent by resetting watermark ack request without clearing
                //       flow control
                if (NORM_OBJECT_INVALID == norm_flush_object)
                {
                    NormResetWatermark(norm_session);
                }
                else  // might as request ack for most recent enqueued object
                {
                    NormSetWatermark(norm_session, norm_flush_object, true);
                    norm_last_object = norm_flush_object;
                    norm_flush_object = NORM_OBJECT_INVALID;
                }
            }
            break; 
            
        case NORM_TX_OBJECT_PURGED:
        {
            NormDataDetachData(event.object);
            Message* msg = (Message*)NormObjectGetUserData(event.object);
            if(event.object == norm_flush_object)
                norm_flush_object = NORM_OBJECT_INVALID;
            if (NULL != msg)
            {
                input_msg_list.Remove(*msg);
                delete msg;
            }
            //NormObjectRelease(event.object);
            //fprintf(stderr, "normMsgr LOGGING ALLOCATIONS\n");
            //ProtoCheckLogAllocations(stderr);
            break;
        }   
        
        case NORM_REMOTE_SENDER_INACTIVE:
            //fprintf(stderr, "REMOTE SENDER INACTIVE node: %u\n", NormNodeGetId(event.sender));
            //NormNodeDelete(event.sender);
            //logAllocs = true;
            break;
        
        case NORM_RX_OBJECT_ABORTED:
            //fprintf(stderr, "NORM_RX_OBJECT_ABORTED\n");// %hu\n", NormObjectGetTransportId(event.object));
            break;
            
        case NORM_RX_OBJECT_COMPLETED:
        {   
            sent_count++;
            char* data = NormDataDetachData(event.object);
            if (NULL != data)
            {
                Message* msg = new Message(data, NormObjectGetSize(event.object));
                if (NULL == msg)
                {
                    perror("normMsgr: new Message() error");
                    delete[] data;  // TBD - may need to finally implement NormDelete() function!
                    // TBD Stop() as a fatal out of memory error?
                    break;
                }
                if (NULL == output_msg)
                {
                    output_msg = msg;
                    if (omit_header)
                        output_msg->ResetIndex(MSG_HEADER_SIZE);
                    else
                        output_msg->ResetIndex();
                }
                else
                {
                    output_msg_queue.Append(*msg);
                }
                rx_needed = false;
            }
            // else TBD - "termination" info-only object?
            break;
        }
            
        default:
            break;     
    }
    //NormReleasePreviousEvent(NormGetInstance(norm_session));
            
}  // end NormMsgr::HandleNormEvent()


void Usage()
{
    fprintf(stderr, "Usage: normMsgr id <nodeIdInteger> {send &| recv} [addr <addr>[/<port>]]\n"
                    "                [ack auto|<node1>[,<node2>,...]] [output <outFile>]\n"
                    "                [cc|cce|ccl|rate <bitsPerSecond>] [interface <name>] [loopback]\n"
                    "                [debug <level>] [trace] [log <logfile>] [silent]\n"
                    "                [flush {none|passive|active}] [omit] [txloss <lossFraction>]\n");
}
int main(int argc, char* argv[])
{
    // REQUIRED parameters initiailization
    NormNodeId nodeId = NORM_NODE_NONE;
    bool send = false;
    bool recv = false;
    
    char sessionAddr[64];
    strcpy(sessionAddr, "224.1.2.3");
    unsigned int sessionPort = 6003;
    
    bool autoAck = false;
    NormNodeId ackingNodeList[256]; 
    unsigned int ackingNodeCount = 0;
    bool flushing = false;
    
    double txRate = 0.0; // used for non-default NORM_FIXED ccMode
    NormMsgr::CCMode ccMode = NormMsgr::NORM_CC;
    const char* mcastIface = NULL;
    
    int debugLevel = 0;
    const char* debugLog = NULL;  // stderr by default
    bool trace = false;
    bool omitHeaderOnOutput = false;
    bool silentReceiver = false;
    double txloss = 0.0;
    bool loopback = false;
    
    NormMsgr normMsgr;
    
    // Parse command-line
    int i = 1;
    while (i < argc)
    {
        const char* cmd = argv[i++];
        size_t len = strlen(cmd);
        if (0 == strncmp(cmd, "send", len))
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
        else if (0 == strncmp(cmd, "addr", len))
        {
            if (i >= argc)
            {
                fprintf(stderr, "nodeMsgr error: missing 'addr[/port]' value!\n");
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
        else if (0 == strncmp(cmd, "output", len))
        {
            if (i >= argc)
            {
                fprintf(stderr, "normMsgr error: missing output 'device' name!\n");
                Usage();
                return -1;
            }
            FILE* outfile = fopen(argv[i++], "w+");
            if (NULL == outfile)
            {
                perror("normMsgr output device fopen() error");
                Usage();
                return -1;
            }
            normMsgr.SetOutputFile(outfile);
        }
        else if (0 == strncmp(cmd, "id", len))
        {
            if (i >= argc)
            {
                fprintf(stderr, "nodeMsgr error: missing 'id' value!\n");
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
                fprintf(stderr, "nodeMsgr error: missing 'id' <nodeId> value!\n");
                Usage();
                return -1;
            }
            const char* alist = argv[i++];
            if (0 == strcmp("auto", alist))
            {
                autoAck = true;
            }
            else
            {
                autoAck = false;
                while ((NULL != alist) && (*alist != '\0'))
                {
                    // TBD - Do we need to skip leading white space?
                    int id;
                    if (1 != sscanf(alist, "%d", &id))
                    {
                        fprintf(stderr, "nodeMsgr error: invalid acking node list!\n");
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
                flushing = false;
            }
            else if (0 == strcmp(mode, "passive"))
            {
                flushing = false;
            }
            else if (0 == strcmp(mode, "active"))
            {
                flushing = true;
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
                fprintf(stderr, "nodeMsgr error: missing 'rate' <bitsPerSecond> value!\n");
                Usage();
                return -1;
            }
            if (1 != sscanf(argv[i++], "%lf", &txRate))
            {
                fprintf(stderr, "nodeMsgr error: invalid transmit rate!\n");
                Usage();
                return -1;
            }       
            // set fixed-rate operation
            ccMode = NormMsgr::NORM_FIXED;     
        }
        else if (0 == strcmp(cmd, "cc"))
        {
            ccMode = NormMsgr::NORM_CC;
        }
        else if (0 == strcmp(cmd, "cce"))
        {
            ccMode = NormMsgr::NORM_CCE;
        }
        else if (0 == strcmp(cmd, "ccl"))
        {
            ccMode = NormMsgr::NORM_CCL;
        }
        else if (0 == strncmp(cmd, "interface", len))
        {
            if (i >= argc)
            {
                fprintf(stderr, "nodeMsgr error: missing 'interface' <name>!\n");
                Usage();
                return -1;
            }
            mcastIface = argv[i++];
        }
        else if (0 == strncmp(cmd, "omit", len))
        {
            omitHeaderOnOutput = true;
        }
        else if (0 == strncmp(cmd, "silent", len))
        {
            silentReceiver = true;
        }
        else if (0 == strncmp(cmd, "txloss", len))
        {
            if (1 != sscanf(argv[i++], "%lf", &txloss))
            {
                fprintf(stderr, "nodeMsgr error: invalid 'txloss' value!\n");
                Usage();
                return -1;
            }
        }
        else if (0 == strncmp(cmd, "debug", len))
        {
            if (i >= argc)
            {
                fprintf(stderr, "nodeMsgr error: missing 'debug' <level>!\n");
                Usage();
                return -1;
            }
            debugLevel = atoi(argv[i++]);
        }
        else if (0 == strncmp(cmd, "log", len))
        {
            if (i >= argc)
            {
                fprintf(stderr, "nodeMsgr error: missing 'log' <fileName>!\n");
                Usage();
                return -1;
            }
            debugLog = argv[i++];
        }
        else if (0 == strncmp(cmd, "trace", len))
        {
            trace = true;
        }
        else if (0 == strncmp(cmd, "help", len))
        {
            Usage();
            return 0;
        }
        else
        {
            fprintf(stderr, "nodeMsgr error: invalid command \"%s\"!\n", cmd);
            Usage();
            return -1;
        }
    }
    
    if (!send && !recv)
    {
        fprintf(stderr, "normMsgr error: not configured to send or recv!\n");
        Usage();
        return -1;
    }
    if (NORM_NODE_NONE == nodeId)
    {
        fprintf(stderr, "normMsgr error: no local 'id' provided!\n");
        Usage();
        return -1;
    }
    
    // TBD - should provide more error checking of calls
    NormInstanceHandle normInstance = NormCreateInstance();
    NormSetDebugLevel(debugLevel);
    if (NULL != debugLog)
        NormOpenDebugLog(normInstance, debugLog);
    
    normMsgr.SetLoopback(loopback);
    normMsgr.SetFlushing(flushing);
    
    if (omitHeaderOnOutput) normMsgr.OmitHeader(true);
    
    if (!normMsgr.OpenNormSession(normInstance, sessionAddr, sessionPort, (NormNodeId)nodeId))
    {
        fprintf(stderr, "normMsgr error: unable to open NORM session\n");
        NormDestroyInstance(normInstance);
        return -1;
    }
    
    if (silentReceiver) normMsgr.SetSilentReceiver(true);
    if (txloss > 0.0) normMsgr.SetTxLoss(txloss);
    
    if (autoAck)
    {
        normMsgr.SetAutoAck(true);
    }
    else
    {
        for (unsigned int i = 0; i < ackingNodeCount; i++)
            normMsgr.AddAckingNode(ackingNodeList[i]);
    }
    
    normMsgr.SetNormCongestionControl(ccMode);
    if (NormMsgr::NORM_FIXED == ccMode)
        normMsgr.SetNormTxRate(txRate);
    if (NULL != mcastIface)
        normMsgr.SetNormMulticastInterface(mcastIface);
    
    if (trace) normMsgr.SetNormMessageTrace(true);
    
    // TBD - set NORM session parameters
    normMsgr.Start(send, recv); 
    
    int normfd = NormGetDescriptor(normInstance);
    // Get input/output descriptors and set to non-blocking i/o
    int inputfd = normMsgr.GetInputDescriptor();
    if (-1 == fcntl(inputfd, F_SETFL, fcntl(inputfd, F_GETFL, 0) | O_NONBLOCK))
        perror("normMsgr: fcntl(inputfd, O_NONBLOCK) error");
    int outputfd = normMsgr.GetOutputDescriptor();
    if (-1 == fcntl(outputfd, F_SETFL, fcntl(outputfd, F_GETFL, 0) | O_NONBLOCK))
        perror("normMsgr: fcntl(outputfd, O_NONBLOCK) error");
    fd_set fdsetInput, fdsetOutput;
    FD_ZERO(&fdsetInput);
    FD_ZERO(&fdsetOutput);
    struct timeval lastTime;
    gettimeofday(&lastTime, NULL);
    while (normMsgr.IsRunning())
    {
        int maxfd = -1;
        // Only wait on NORM if needed for tx readiness
        bool waitOnNorm = true;
        if (!(normMsgr.RxNeeded() || normMsgr.InputMessageReady()))
            waitOnNorm = false; // no need to wait
        else if (normMsgr.InputMessageReady() && normMsgr.TxReady())  
            waitOnNorm = false; // no need to wait if already tx ready
        if (waitOnNorm)
        {
            maxfd = normfd;
            FD_SET(normfd, &fdsetInput);
        }
        else
        {
            FD_CLR(normfd, &fdsetInput);
        }
        if (normMsgr.InputNeeded() && !normMsgr.InputReady())
        {   
            FD_SET(inputfd, &fdsetInput);
            if (inputfd > maxfd) maxfd = inputfd;
        }   
        else
        {
            FD_CLR(inputfd, &fdsetInput);
        }
        if (normMsgr.OutputPending() && !normMsgr.OutputReady())
        {
            FD_SET(outputfd, &fdsetOutput);
            if (outputfd > maxfd) maxfd = outputfd;
        }
        else
        {   
            FD_CLR(outputfd, &fdsetOutput);
        }
        if (maxfd >= 0)
        {
            struct timeval timeout;
            timeout.tv_sec = 1;
            timeout.tv_usec = 0;
            int result = select(maxfd+1, &fdsetInput, &fdsetOutput, NULL, &timeout);
            switch (result)
            {
                case -1:
                    switch (errno)
                    {
                        case EINTR:
                        case EAGAIN:
                            continue;
                        default:
                            perror("normMsgr select() error");
                            // TBD - stop NormMsgr
                            break;
                    }
                    break;
                case 0:
                    // shouldn't occur for now (no timeout)
                    //fprintf(stderr, "normMsgr timeout ...\n");
                    continue;
                default:
                    if (FD_ISSET(inputfd, &fdsetInput))
                    {
                        normMsgr.SetInputReady();
                    }   
                    /*if (FD_ISSET(normfd, &fdsetInput))
                    {
                        NormEvent event;
                        while (NormGetNextEvent(normInstance, &event, false))
                            normMsgr.HandleNormEvent(event);
                    }*/  
                    if (FD_ISSET(outputfd, &fdsetOutput))
                    {
                        normMsgr.SetOutputReady();
                    }
                    break; 
            }
        }
        
        NormSuspendInstance(normInstance);
        NormEvent event;
        while (NormGetNextEvent(normInstance, &event, false))
            normMsgr.HandleNormEvent(event);
        // As a result of reading input or NORM notification events,  
        // we may be ready to read input and/or send a message if it's been read
        const int LOOP_MAX = 100;
        int loopCount = 0;
        while (loopCount < LOOP_MAX)
        {
            loopCount++;
            if (normMsgr.InputNeeded() && normMsgr.InputReady())
                normMsgr.ReadInput();
            if (normMsgr.InputMessageReady() && normMsgr.TxReady())
                normMsgr.SendMessage();
            // and/or output a received message if we need
            if (normMsgr.OutputPending() && normMsgr.OutputReady())
                normMsgr.WriteOutput();  
        }
        NormResumeInstance(normInstance);
            
    }  // end while(normMsgr.IsRunning()
    
    
    
    NormCloseDebugLog(normInstance);
    
    fprintf(stderr, "destroying session ...\n");
    normMsgr.CloseNormSession();
    
    fprintf(stderr, "destroying instance ...\n");
    
    NormDestroyInstance(normInstance);
    
    normMsgr.Destroy();
    
    fprintf(stderr, "normMsgr exiting ...\n");
    
}  // end main()

NormMsgr::Message::Message()
 : msg_size(0), msg_index(0), msg_buffer(NULL),
   prev(NULL), next(NULL)
{
}

NormMsgr::Message::Message(char* buffer, unsigned int size)
 : msg_size(size), msg_index(size+MSG_HEADER_SIZE), msg_buffer(buffer),
   prev(NULL), next(NULL)
{
    assert(2 == MSG_HEADER_SIZE);
    uint16_t msgSize = size + MSG_HEADER_SIZE;
    msgSize = htons(msgSize);
    memcpy(msg_header, &msgSize, MSG_HEADER_SIZE);
} 

NormMsgr::Message::~Message()
{
    // in future we may need to use NormDelete()
    // to delete msg_buffer if it came from NORM
    if (NULL != msg_buffer)
    {
        //fprintf(stderr, "deleting msg_buffer ...\n");
        delete[] msg_buffer;
        msg_buffer = NULL;
    }
}

bool NormMsgr::Message::Init(unsigned int size)
{
    if ((NULL != msg_buffer) && (size != msg_size))
    {
        delete[] msg_buffer;
        msg_buffer = NULL;
    }
    if (NULL == msg_buffer) 
        msg_buffer = new char[size];
    if (NULL != msg_buffer)
    {
        assert(2 == MSG_HEADER_SIZE);
        uint16_t msgSize = size + MSG_HEADER_SIZE;
        msgSize = htons(msgSize);
        memcpy(msg_header, &msgSize, MSG_HEADER_SIZE);
        msg_index = MSG_HEADER_SIZE;  // empty message
        msg_size = size;  
        return true;
    }
    else
    {
        msg_index = msg_size = 0;
        return false;
    }
}  // end NormMsgr::Message::Init()

NormMsgr::MessageQueue::MessageQueue()
 : head(NULL), tail(NULL), msg_count(0)
{
}

NormMsgr::MessageQueue::~MessageQueue()
{
}

void NormMsgr::MessageQueue::Destroy()
{
    while (NULL != head)
    {
        Message* msg = RemoveHead();
        delete msg;
    }
}  // end NormMsgr::MessageQueue::Destroy()

void NormMsgr::MessageQueue::Prepend(Message& msg)
{
    msg.prev = NULL;
    if (NULL != head)
        head->prev = &msg;
    else
        tail = &msg;
    msg.next = head;
    head = &msg;
    msg_count++;
}  // end NormMsgr::MessageQueue::Prepend()

void NormMsgr::MessageQueue::Append(Message& msg)
{
    msg.next = NULL;
    if (NULL != tail)
        tail->next = &msg;
    else 
        head = &msg;
    msg.prev = tail;
    tail = &msg;
    msg_count++;
}  // end NormMsgr::MessageQueue::Append()

void NormMsgr::MessageQueue::Remove(Message& msg)
{
    if (NULL == msg.prev)
        head = msg.next;
    else
        msg.prev->next = msg.next;
    if (NULL == msg.next)
        tail = msg.prev;
    else
        msg.next->prev = msg.prev;
    msg.prev = msg.next = NULL;
    msg_count--;
}  // end NormMsgr::MessageQueue::Remove()

NormMsgr::Message* NormMsgr::MessageQueue::RemoveHead()
{
    Message* msg = head;
    if (NULL != head) Remove(*head);
    return msg;
}  // end NormMsgr::MessageQueue::RemoveHead()

NormMsgr::Message* NormMsgr::MessageQueue::RemoveTail()
{
    Message* msg = tail;
    if (NULL != tail) Remove(*tail);
    return msg;
}  // end NormMsgr::MessageQueue::RemoveTail()
