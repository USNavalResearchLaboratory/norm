
#include "normApi.h"

#include "protoCheck.h"

#include <stdio.h>       // for printf(), etc
#include <stdlib.h>      // for srand()
#include <string.h>      // for strrchr(), memset(), etc
#include <sys/time.h>    // for gettimeofday()
#include <arpa/inet.h>   // for htons()
#include <fcntl.h>       // for, well, fnctl()
#include <errno.h>       // obvious child
#include <assert.h>      // embarrassingly obvious

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
                
            private:
                Message*    head;
                Message*    tail;
        };  // end class NormMsgr::MessageQueue
        Message* NewMessage(unsigned int size);
            
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
        
        bool Start(bool sender, bool receiver);
        void Stop()
            {is_running = false;}
        bool IsRunning() const
            {return is_running;}
        void HandleNormEvent(const NormEvent& event);
        
        // Sender methods
        FILE* GetInputFile() const
            {return input_file;}
        bool InputNeeded() const
            {return input_needed;}
        bool InputMessageReady() const
            {return ((NULL != input_msg) && !input_needed);}
        bool ReadInput();
        
        bool NormTxReady() const;
        bool SendMessage();
        bool EnqueueMessageObject();
        
        // Receiver methods
        FILE* GetOutputFile() const
            {return output_file;}
        void SetOutputReady()
            {output_ready = true;}
        bool OutputReady() const
            {return output_ready;}
        bool OutputPending() const
            {return output_pending;}
        bool WriteOutput();
        
        void OmitHeader(bool state) 
            {omit_header = state;}
        
        // These can only be called post-OpenNormSession
        void SetSilentReceiver(bool state)
            {NormSetSilentReceiver(norm_session, true);}
        void SetTxLoss(double txloss)
            {NormSetTxLoss(norm_session, txloss);}
            
    private:
        bool                is_running;                                                  
        FILE*               input_file;      // stdin by default                         
        bool                input_needed;    //                                          
        Message*            input_msg;       // current input message being read/sent*   
        MessageQueue        input_msg_list;  // list of enqueued messages (in norm sender cache)
            
        NormSessionHandle   norm_session;
        bool                is_multicast;
        unsigned int        norm_tx_queue_max;   // max number of objects that can be enqueued at once 
        unsigned int        norm_tx_queue_count; // count of unacknowledged enqueued objects (TBD - optionally track size too)
        bool                norm_tx_watermark_pending;
        bool                norm_tx_vacancy;
        bool                norm_acking;
        
        FILE*               output_file;
        bool                output_ready;
        bool                output_pending;
        Message*            output_msg;
        MessageQueue        output_msg_queue;
        // These are some options mainly for testing purposes
        bool                omit_header;  // if "true", receive message length header is _not_ written to output
        bool                rx_silent;
        double              tx_loss;
            
};  // end class NormMsgr

NormMsgr::NormMsgr()
 : input_file(stdin), input_needed(false), input_msg(NULL),
   norm_session(NORM_SESSION_INVALID), is_multicast(false), norm_tx_queue_max(2048), norm_tx_queue_count(0), 
   norm_tx_watermark_pending(false), norm_tx_vacancy(true), norm_acking(false),
   output_file(stdout), output_ready(true), output_pending(false), output_msg(NULL), 
   omit_header(false), rx_silent(false), tx_loss(0.0)
{
}

NormMsgr::~NormMsgr()
{
    if (NULL != input_msg) delete input_msg;
    input_msg_list.Destroy();
    if (NULL != output_msg) delete output_msg;
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
        // TBD - make full loopback a command line option?
        NormSetMulticastLoopback(norm_session, true);
    }
    
    
    // Set some default parameters (maybe we should put parameter setting in Start())
    NormSetRxCacheLimit(norm_session, norm_tx_queue_max);
    NormSetDefaultSyncPolicy(norm_session, NORM_SYNC_ALL);
    
    NormSetDefaultUnicastNack(norm_session, true);
    NormSetTxCacheBounds(norm_session, 10*1024*1024, norm_tx_queue_max, norm_tx_queue_max);
    
    //NormSetMessageTrace(norm_session, true);
    
    NormSetTxRobustFactor(norm_session, 2);
    
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
    if (receiver)
    {
        if (!NormStartReceiver(norm_session, 10*1024*1024))
        {
            fprintf(stderr, "normMsgr error: unable to start NORM receiver\n");
            return false;
        }
    }
    if (sender)
    {
        if (norm_acking)
        {   
            // ack-based flow control enabled on command-line, 
            // so disable timer-based flow control
            NormSetFlowControl(norm_session, 0.0);
        }
        // Pick a random instance id for now
        struct timeval currentTime;
        gettimeofday(&currentTime, NULL);
        srand(currentTime.tv_usec);  // seed random number generator
        NormSessionId instanceId = (NormSessionId)rand();
        if (!NormStartSender(norm_session, instanceId, 10*1024*1024, 1400, 16, 4))
        {
            fprintf(stderr, "normMsgr error: unable to start NORM sender\n");
            if (receiver) NormStopReceiver(norm_session);
            return false;
        }
        input_needed = true;
    }
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
        size_t result = fread(bufferPtr, 1, readLength, input_file);
        if (result > 0)
        {
            input_msg->IncrementIndex(result);
            if (result < readLength) 
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
        else 
        {
            if (feof(input_file))
            {
                // end-of-file reached, TBD - trigger final flushing and wrap-up
                fprintf(stderr, "normMsgr: input end-of-file detected ...\n");
            }
            else if (ferror(input_file))
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
            }
            return false;
        }
    }  
    return true;
}  // end NormMsgr::ReadInput()

bool NormMsgr::WriteOutput()
{
    while (output_pending)
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
        size_t result = fwrite(bufferPtr, 1, writeLength, output_file);
        
        output_msg->IncrementIndex(result);
        if (result < writeLength)
        {
            if (feof(output_file))
            {
                // end-of-file reached, TBD - stop acting as receiver, signal sender we're done?
                fprintf(stderr, "normMsgr: output end-of-file detected ...\n");
            }
            else if (ferror(input_file))
            {
                fprintf(stderr, "normMsgr: output error detected ...\n");
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
            }
            return false;
        }
        if (output_msg->IsComplete())
        {
            fflush(output_file);
            delete output_msg;
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
                output_pending = false;
            }
            break;
        }
    }
    return true;
}  // end NormMsgr::WriteOutput()

bool NormMsgr::NormTxReady() const
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
    // TBD - call EnqueueMessageObject() or WriteMessageStream()
    //       depending upon on configured mode
    if (EnqueueMessageObject())
    {
        // Our buffered message was sent, so reset input indices
        // and request next message from input
        input_msg_list.Append(*input_msg);
        input_msg = NULL;
        input_needed = true;
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
        norm_tx_vacancy = false;     
        return false;
    }
    NormObjectSetUserData(object, input_msg); // so we can remove/delete upon purge
    if (norm_acking)
    {
        // ack-based flow control has been enabled
        norm_tx_queue_count++;
        if (!norm_tx_watermark_pending && (norm_tx_queue_count >= (norm_tx_queue_max / 2)))
        {
            NormSetWatermark(norm_session, object);
            norm_tx_watermark_pending = true;
        }
        else
        {
            // TBD - make non-flow control acking separable option?
            NormSetWatermark(norm_session, object);
        }
    }
    return true;
}  // end NormMsgr::EnqueueMessageObject()

void NormMsgr::HandleNormEvent(const NormEvent& event)
{
    bool logAllocs = false;
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
                if (norm_tx_watermark_pending)
                {
                    norm_tx_queue_count -= (norm_tx_queue_max / 2);
                    norm_tx_watermark_pending = false;
                }
            }
            else
            {
                // TBD - we could see who didn't ACK and possibly remove them
                //       from our acking list.  For now, we are infinitely
                //       persistent by resetting watermark ack request
                NormResetWatermark(norm_session);
            }
            break; 
            
        case NORM_TX_OBJECT_PURGED:
        {
            NormDataDetachData(event.object);
            Message* msg = (Message*)NormObjectGetUserData(event.object);
            if (NULL != msg)
            {
                input_msg_list.Remove(*msg);
                delete msg;
            }
            break;
        }   
        
        case NORM_REMOTE_SENDER_INACTIVE:
            //fprintf(stderr, "REMOTE SENDER INACTIVE node: %u\n", NormNodeGetId(event.sender));
            //NormNodeDelete(event.sender);
            //logAllocs = true;
            break;
        
        case NORM_RX_OBJECT_ABORTED:
            //fprintf(stderr, "NORM_RX_OBJECT_ABORTED\n");// %hu\n", NormObjectGetTransportId(event.object));
            logAllocs = true;
            break;
            
        case NORM_RX_OBJECT_COMPLETED:
        {   
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
                    output_pending = true;
                }
                else
                {
                    output_msg_queue.Append(*msg);
                }
            }
            // else TBD - "termination" info-only object?
            break;
        }
            
        default:
            break;     
    }
    //NormReleasePreviousEvent(NormGetInstance(norm_session));

    if (logAllocs) 
    {
#ifdef USE_PROTO_CHECK
        ProtoCheckLogAllocations(stderr);
#endif // USE_PROTO_CHECK
    }
            
}  // end NormMsgr::HandleNormEvent()


void Usage()
{
    fprintf(stderr, "Usage: normMsgr id <nodeId> {send &| recv} [addr <addr>[/<port>]][ack <node1>[,<node2>,...]\n"
                    "                [cc|cce|ccl|rate <bitsPerSecond>][interface <name>][debug <level>][trace]\n"
                    "                [omit][silent][txloss <lossFraction>]\n");
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
    
    NormNodeId ackingNodeList[256]; 
    unsigned int ackingNodeCount = 0;
    
    double txRate = 0.0; // used for non-default NORM_FIXED ccMode
    NormMsgr::CCMode ccMode = NormMsgr::NORM_CC;
    const char* mcastIface = NULL;
    
    int debugLevel = 0;
    bool trace = false;
    bool omitHeaderOnOutput = false;
    bool silentReceiver = false;
    double txloss = 0.0;
    
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
            while ((NULL != alist) && (*alist != '\0'))
            {
                // TBD - Do we need to skip leading white space?
                if (1 != sscanf(alist, "%d", ackingNodeList + ackingNodeCount))
                {
                    fprintf(stderr, "nodeMsgr error: invalid acking node list!\n");
                    Usage();
                    return -1;
                }
                ackingNodeCount++;
                alist = strchr(alist, ',');
                if (NULL != alist) alist++;  // point past comma
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
                fprintf(stderr, "nodeMsgr error: missing 'interface' <name>!\n");
                Usage();
                return -1;
            }
            debugLevel = atoi(argv[i++]);
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
    
    NormMsgr normMsgr;
    
    if (omitHeaderOnOutput) normMsgr.OmitHeader(true);
    
    if (!normMsgr.OpenNormSession(normInstance, sessionAddr, sessionPort, (NormNodeId)nodeId))
    {
        fprintf(stderr, "normMsgr error: unable to open NORM session\n");
        NormDestroyInstance(normInstance);
        return false;
    }
    
    
    if (silentReceiver) normMsgr.SetSilentReceiver(true);
    if (txloss > 0.0) normMsgr.SetTxLoss(txloss);
    
    for (unsigned int i = 0; i < ackingNodeCount; i++)
        normMsgr.AddAckingNode(ackingNodeList[i]);
    
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
    int inputfd = fileno(normMsgr.GetInputFile());
    if (-1 == fcntl(inputfd, F_SETFL, fcntl(inputfd, F_GETFL, 0) | O_NONBLOCK))
        perror("normMsgr: fcntl(inputfd, O_NONBLOCK) error");
    int outputfd = fileno(normMsgr.GetOutputFile());
    if (-1 == fcntl(outputfd, F_SETFL, fcntl(outputfd, F_GETFL, 0) | O_NONBLOCK))
        perror("normMsgr: fcntl(outputfd, O_NONBLOCK) error");
    fd_set fdsetInput, fdsetOutput;
    FD_ZERO(&fdsetInput);
    FD_ZERO(&fdsetOutput);
    while (normMsgr.IsRunning())
    {
        int maxfd = normfd;
        FD_SET(normfd, &fdsetInput);
        if (normMsgr.InputNeeded())
        {   
            //fprintf(stderr, "NEED INPUT ...\n");
            FD_SET(inputfd, &fdsetInput);
            if (inputfd > maxfd)
                maxfd = inputfd;
        }   
        else
        {
            FD_CLR(inputfd, &fdsetInput);
        }
        int result;
        if (normMsgr.OutputPending() && !normMsgr.OutputReady())
        {
            FD_SET(outputfd, &fdsetOutput);
            if (outputfd > maxfd) maxfd = outputfd;
            result = select(maxfd+1, &fdsetInput, &fdsetOutput, NULL, NULL);
        }
        else
        {   
            FD_CLR(outputfd, &fdsetOutput);
            result = select(maxfd+1, &fdsetInput, NULL, NULL, NULL);
        }
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
                continue;
            default:
                if (FD_ISSET(inputfd, &fdsetInput))
                {
                    normMsgr.ReadInput();
                }   
                if (FD_ISSET(normfd, &fdsetInput))
                {
                    NormEvent event;
                    if (NormGetNextEvent(normInstance, &event))
                        normMsgr.HandleNormEvent(event);
                }   
                if (FD_ISSET(outputfd, &fdsetOutput))
                {
                    normMsgr.SetOutputReady();
                }
                break; 
        }
        // As a result of reading input or NORM notification events,  
        // we may be ready to send a message if it's been read
        if (normMsgr.InputMessageReady() && normMsgr.NormTxReady())
            normMsgr.SendMessage();
        // and/or output a received message if we need
        if (normMsgr.OutputPending() && normMsgr.OutputReady())
            normMsgr.WriteOutput();  // TBD - implement output async i/o notification as needed
            
    }  // end while(normMsgr.IsRunning()
    
    fprintf(stderr, "normMsgr exiting ...\n");
    
    NormDestroyInstance(normInstance);
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
 : head(NULL), tail(NULL)
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
