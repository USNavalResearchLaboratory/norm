
#include "normApi.h"
#include <stdio.h>       // for printf(), etc
#include <stdlib.h>      // for atoi(), etc
#include <cassert>

#ifdef WIN32
#include <windows.h>
//#include "win32InputHandler.cpp"  // brings in the entire implementation
#else  // UNIX
#include <sys/select.h>
#endif  // if/else WIN32/UNIX

// When the "ack" option _and_ flushing is used, there are a couple of strategies
// that can be used.  Here, the two options implemented are titled
// "SHOOT_FIRST" and "ACK_LATER".  If "SHOOT_FIRST" is #defined, then
// NormSetWatermark() is called immediately after each enqueued transmit
// object.  This call to "NormSetWatermark()" cancels any previous 
// watermark request, immediately advancing the "watermark" to the current
// transmit object id.  Generally, this is OK, but in very high bandwidth*delay,
// this may cause some dead air time since flow control might not be advanced any
// until the ack for watermark is received.  When SHOOT_FIRST is _not_ #defined,
// the NormSetWatermark() call is deferred if an existing flow control watermark
// request is pending acknowledgment.  This potentially allows the flow control
// to be advanced sooner instead of the sender "chasing its own tail" with the 
// SHOOT_FIRST strategy.  Both strategies are implemented here, because under
// certain cases, the SHOOT_FIRST _may_ have benefit. For example, low duty cycle
// transmission may benefit if acknowledgment is requested after each object.  
// Try them both, and you decide ...

// By the way, this SHOOT_FIRST/ACK_LATER only applies to the case when both
// the "ack" and "flush" options are used.  The "flush" option controls the
// NORM sender behavior when object objects are transmitted intermittently.
// I.e., determines if/when NORM_CMD(FLUSH) messages are sent prior to 
// final end-of-transmission (e.g., when a pause in transmission occurs).  
// This will be applicable when normCast gets a few additional options like
// repeat iterations of transmit file/directory list on user-controlled
// timer interval, etc.  For example, "flush active" means NORM_CMD(FLUSH)
// would be sent during such pauses, while "flush none" would result in
// sender immediately going silent at pauses in file transmission.

// Uncomment this to enable the "SHOOT_FIRST" strategy, as opposed to "ACK_LATER"
// #define SHOOT_FIRST 1

// I usually avoid using Protolib stuff for NORM API examples to keep things clearer,  
// but the couple of classes here are useful helpers from the Protolib C++ toolkit.

#include "protoFile.h"    // for ProtoFile::PathList and iterator for tx file/directory queue
#include "protoString.h"  // for ProtoTokenator
#include "protoAddress.h" // for ProtoAddress

class NormCaster
{
    public:
        NormCaster();
        ~NormCaster();
        
        // some day build these directly into NORM API
        enum CCMode {NORM_FIXED, NORM_CC, NORM_CCE, NORM_CCL};
        
        void Destroy();
            
        bool OpenNormSession(NormInstanceHandle instance, 
                             const char*        addr,
                             unsigned short     port,
                             NormNodeId         nodeId);
        void CloseNormSession();
        
        NormSessionHandle GetSession() const {return norm_session;}
        
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
        void SetBufferSize(unsigned int value)
            {buffer_size = value;}
        
        bool Start(bool sender, bool receiver);
        void Stop()
            {is_running = false;}
        bool IsRunning() const
            {return is_running;}
        
        void HandleNormEvent(const NormEvent& event);
        
        // Sender methods
        bool AddTxItem(const char* path);
        bool StageNextTxFile();
        bool TxFilePending()
            {return '\0' != tx_pending_path[0];}
        bool TxReady() const;
        void SendFiles();
        bool EnqueueFileObject();
        unsigned long GetSentCount()
            {return sent_count;}
        void SetTxSocketBufferSize(unsigned int value)
            {tx_socket_buffer_size = value;}

        void SetSegmentSize(unsigned short segmentSize)
            {segment_size = segmentSize;}
        void SetBlockSize(unsigned short blockSize)
            {block_size = blockSize;}
        void SetNumParity(unsigned short numParity)
            {num_parity = numParity;}
        void SetAutoParity(unsigned short autoParity)
            {auto_parity = autoParity;}
        
        // Receiver methods
        void SetRxCacheDirectory(const char* path)
        {
            strncpy(rx_cache_path, path, PATH_MAX);
            unsigned int len = strlen(rx_cache_path);
            if (PROTO_PATH_DELIMITER != rx_cache_path[len - 1])
            {
                if (PATH_MAX == len) len--;
                rx_cache_path[len] = PROTO_PATH_DELIMITER;
                rx_cache_path[len + 1] = '\0';
            }
        }
        const char* GetRxCacheDirectory() const
            {return rx_cache_path;}
        void SetRxSocketBufferSize(unsigned int value)
            {rx_socket_buffer_size = value;}
        
        // These can only be called post-OpenNormSession()
        
        void SetAutoAck(bool enable)
        {
                NormTrackingStatus trackingMode = enable? NORM_TRACK_RECEIVERS : NORM_TRACK_NONE;
                NormSetAutoAckingNodes(norm_session, trackingMode);
                norm_acking = enable;
        }
        void SetSilentReceiver(bool state)
            {NormSetSilentReceiver(norm_session, state);}
        
        void SetProbeTOS(UINT8 value)
            {probe_tos = value;}
        
        void SetTxLoss(double txloss)
            {NormSetTxLoss(norm_session, txloss);}
            
    private:
        bool                                is_running;  
        NormSessionHandle                   norm_session;
        ProtoFile::PathList                 tx_file_list;
        ProtoFile::PathList::PathIterator   tx_file_iterator;
        char                                tx_pending_path[PATH_MAX + 1];
        unsigned int                        tx_pending_prefix_len;
        bool                                is_multicast;
        bool                                loopback;
        UINT8                               probe_tos;
        unsigned int                        norm_tx_queue_max;   // max number of objects that can be enqueued at once 
        unsigned int                        norm_tx_queue_count; // count of unacknowledged enqueued objects (TBD - optionally track size too)
        bool                                norm_flow_control_pending;
        bool                                norm_tx_vacancy;
        bool                                norm_acking;
        bool                                norm_flushing;
        NormObjectHandle                    norm_flush_object;
        NormObjectHandle                    norm_last_object;
        unsigned long                       sent_count;
        unsigned short                      segment_size;
        unsigned short                      block_size;
        unsigned short                      num_parity;
        unsigned short                      auto_parity;
        unsigned int                        tx_socket_buffer_size;
        unsigned int                        rx_socket_buffer_size;
        unsigned int                        buffer_size;

        
        // receiver state variables
        char                                rx_cache_path[PATH_MAX + 1];
            // These are some options mainly for testing purposes
            //bool                            rx_silent;  // TBD - support optional "silent receiver' modes
            //double                          tx_loss;    // cheesy, built-in way to test loss performance
            
};  // end class NormCaster

NormCaster::NormCaster()
 : norm_session(NORM_SESSION_INVALID), tx_file_iterator(tx_file_list), 
   tx_pending_prefix_len(0), is_multicast(false), loopback(false), probe_tos(0), 
   norm_tx_queue_max(8), norm_tx_queue_count(0), 
   norm_flow_control_pending(false), norm_tx_vacancy(true), norm_acking(false), 
   norm_flushing(true), norm_flush_object(NORM_OBJECT_INVALID), norm_last_object(NORM_OBJECT_INVALID),
   sent_count(0), segment_size(1400), block_size(64), num_parity(0), auto_parity(0),
   tx_socket_buffer_size(4*1024*1024), rx_socket_buffer_size(6*1024*1024), buffer_size(64*1024*1024)
   //, rx_silent(false), tx_loss(0.0)
{
    tx_pending_path[0] = '\0';
    tx_pending_path[PATH_MAX] = '\0';
    rx_cache_path[0] = '\0';
    rx_cache_path[PATH_MAX] = '\0';
}

NormCaster::~NormCaster()
{
    Destroy();
}

void NormCaster::Destroy()
{
    tx_file_list.Destroy();
}

bool NormCaster::AddTxItem(const char* path)
{
    bool result = tx_file_list.AppendPath(path);
    if (!result)
        perror("NormCaster::AddTxItem() error");
    if (!TxFilePending()) StageNextTxFile();
    return result;
}  // end NormCaster::AddTxItem()


bool NormCaster::OpenNormSession(NormInstanceHandle instance, const char* addr, unsigned short port, NormNodeId nodeId)
{
    if (NormIsUnicastAddress(addr))
        is_multicast = false;
    else
        is_multicast = true;
    norm_session = NormCreateSession(instance, addr, port, nodeId);
    if (NORM_SESSION_INVALID == norm_session)
    {
        fprintf(stderr, "normCast error: unable to create NORM session\n");
        return false;
    }
    
    if (is_multicast)
    {
        NormSetRxPortReuse(norm_session, true);
        if (loopback)
            NormSetMulticastLoopback(norm_session, true);
    }
    
    // Set some default parameters (maybe we should put all parameter setting in Start())
    if (norm_tx_queue_max > 65535/2) norm_tx_queue_max = 65535/2;
    NormSetRxCacheLimit(norm_session, norm_tx_queue_max);
    NormSetDefaultSyncPolicy(norm_session, NORM_SYNC_ALL);
    
    if (!is_multicast)
        NormSetDefaultUnicastNack(norm_session, true);
    NormSetTxCacheBounds(norm_session, 10*1024*1024, norm_tx_queue_max, norm_tx_queue_max);
    
    //NormSetMessageTrace(norm_session, true);
    
    //NormSetTxRobustFactor(norm_session, 20);
    
    NormSetGrttProbingTOS(norm_session, probe_tos);
    
    return true;
}  // end NormCaster::OpenNormSession()

void NormCaster::CloseNormSession()
{
    if (NORM_SESSION_INVALID == norm_session) return;
    NormDestroySession(norm_session);
    norm_session = NORM_SESSION_INVALID;
}  // end NormCaster::CloseNormSession()

void NormCaster::SetNormCongestionControl(CCMode ccMode)
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
}  // end NormCaster::SetNormCongestionControl()

bool NormCaster::Start(bool sender, bool receiver)
{
    if (receiver)
    {
        if (!NormStartReceiver(norm_session, buffer_size))
        {
            fprintf(stderr, "normCast error: unable to start NORM receiver\n");
            return false;
        }
        // Note: NormPreallocateRemoteSender() MUST be called AFTER NormStartReceiver()
        //NormPreallocateRemoteSender(norm_session, buffer_size, segment_size, block_size, num_parity, buffer_size);
        if (0 != rx_socket_buffer_size)
            NormSetRxSocketBuffer(norm_session, rx_socket_buffer_size);
        fprintf(stderr, "normCast: receiver ready ...\n");
    }
    if (sender)
    {
        if (norm_acking)
        {   
            // ack-based flow control enabled on command-line, 
            // so disable timer-based flow control
            NormSetFlowControl(norm_session, 0.0);
        }
        else
        {
            // Uncommant and adjust this for more/less robust timer-based flow control as desired
            //NormSetFlowControl(norm_session, 10.0);
        }
        NormSetGrttEstimate(norm_session, 0.001);
        //NormSetGrttMax(norm_session, 0.100);
        NormSetBackoffFactor(norm_session, 0);
        
        // Pick a random instance id for now
        NormSessionId instanceId = NormGetRandomSessionId();
        if (!NormStartSender(norm_session, instanceId, buffer_size, segment_size, block_size, num_parity))
        {
            fprintf(stderr, "normCast error: unable to start NORM sender\n");
            if (receiver) NormStopReceiver(norm_session);
            return false;
        }
        if (auto_parity > 0)
            NormSetAutoParity(norm_session, auto_parity < num_parity ? auto_parity : num_parity);
        if (0 != tx_socket_buffer_size)
            NormSetTxSocketBuffer(norm_session, tx_socket_buffer_size);
    }
    is_running = true;
    return true;
}  // end NormCaster::Start();

bool NormCaster::TxReady() const
{
    // This returns true if new tx data can be enqueued to NORM
    // This is based on the state with respect to prior successful file
    // enqueuing (or stream writing) and NORM_TX_QUEUE_EMPTY or
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
}  // end NormCaster::TxReady()

bool NormCaster::StageNextTxFile()
{
    // Pull next file path from "tx_file_list" and stage as "tx_pending_path"
    if (tx_file_iterator.GetNextFile(tx_pending_path))
    {
        // This code omits the source directory prefix from the name transmitted
        // (Note that subdirectory structures will still be conveyed)
        // The "tx_pending_prefix_len" indicates how many chararacters to skip
        const ProtoFile::Path* pathParent = tx_file_iterator.GetCurrentPathItem();
        ASSERT(NULL != pathParent);
        if (pathParent->IsDirectory())
        {
            const char* prefix = pathParent->GetPath();
            tx_pending_prefix_len = strlen(prefix);
            if (PROTO_PATH_DELIMITER != prefix[tx_pending_prefix_len - 1])
                tx_pending_prefix_len += 1;  
        }
        else
        {
            // Set the tx_pending_prefix_len" so only the file basename is conveyed.
            // (We use a reverse ProtoTokenator tokenization limited to a single
            //  split to get the file basename string).
            ProtoTokenator tk(tx_pending_path, PROTO_PATH_DELIMITER, true, 1, true);
            const char* basename = tk.GetNextItem();
            ASSERT(NULL != basename);
            unsigned int namelen = strlen(basename);
            tx_pending_prefix_len = strlen(tx_pending_path) - namelen;
        }
        return true;
    }
    else
    {
        // Either done need to reset tx_file_iterator
        tx_pending_path[0] = '\0';
        tx_pending_prefix_len = 0;
        return false;
    }
}  // end NormCaster::StageNextTxFile()

void NormCaster::SendFiles()
{
    // Send pending file and subsequent files from tx_file_iterator as available/possible
    while (TxFilePending() && TxReady())
    {
        // Note EnqueueFileObject() may be subject to NORM flow control.
        // Will be cued to retry via NORM_TX_QUEUE_VACANCY, NORM_TX_QUEUE_EMPTY,
        // or NORM_TX_WATERMARK_COMPLETED (when ACK is used) notification.
        if (EnqueueFileObject()) 
        {
            sent_count++;
            // Get next file name from our "tx_file_list"
            if (!StageNextTxFile())
            {
                // We have reach end of tx_file_list, so either
                // we're done or we reset tx_file_iterator (when 'repeat' option (TBD) is enabled)
                // If we're done and requesting ACK, finish up nicely with final waterrmark
                if (norm_acking)
                {
                    if ((NORM_OBJECT_INVALID != norm_last_object) && !norm_flushing)
                    {
                        // End-of-transmission ack request (not needed if "norm_flushing"
                        NormSetWatermark(norm_session, norm_last_object, true);
                    }
                }
            }
        }
        else
        {
            // blocked by NORM flow control ... will get cued by notification as described above
            ASSERT(!TxReady());
            break;
        }
    }
}  // end NormCaster::SendFiles()

bool NormCaster::EnqueueFileObject()
{
    if (norm_acking) 
    {
        assert(norm_tx_queue_count < norm_tx_queue_max);
        if (norm_tx_queue_count >= norm_tx_queue_max)
            return false;
    }
        
    // This is our cheesy approach of using the NORM_INFO to convey the file name
    // (TBD - implement something more sophisticated like our FCAST (RFC 6968) concept)
    
    unsigned int nameLen = strlen(tx_pending_path) - tx_pending_prefix_len;
    const char* namePtr = tx_pending_path + tx_pending_prefix_len;
    if (nameLen > segment_size)
    {
        fprintf(stderr, "normCast error: transmit file path \"%s\"  exceeds NORM segment size limit!\n", namePtr);
        nameLen = segment_size;
        // TBD - refactor file name to preserve extension?
    }
    char nameInfo[PATH_MAX + 1];
    strncpy(nameInfo, namePtr, nameLen);
    // Normalize path delimiters to '/' for transfer
    for (unsigned int i = 0; i < nameLen; i++)
    {
        if (PROTO_PATH_DELIMITER == nameInfo[i])
            nameInfo[i] = '/';
    }
    
    // Enqueue the file for transmission
    NormObjectHandle object = NormFileEnqueue(norm_session, tx_pending_path, nameInfo, nameLen);
    if (NORM_OBJECT_INVALID == object)
    {
        // This might happen if a non-acking receiver is present and
        // has nacked for the oldest object in the queue even if all                
        // of our acking receivers have acknowledged it.  
        //fprintf(stderr, "NO VACANCY count:%u max:%u\n", norm_tx_queue_count, norm_tx_queue_max);
        norm_tx_vacancy = false;     
        return false;
    }
    fprintf(stderr, "normCast: enqueued \"%s\" for transmission ...\n", namePtr);
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
        else if (norm_flushing)  // per-file flushing/acking
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
            norm_last_object = object;
        }
    }
    return true;
}  // end NormCaster::EnqueueFileObject()

void NormCaster::HandleNormEvent(const NormEvent& event)
{
    switch (event.type)
    {
        case NORM_TX_QUEUE_EMPTY:
        case NORM_TX_QUEUE_VACANCY:
            norm_tx_vacancy = true;
            if (TxFilePending()) SendFiles();
            break;
            
        case NORM_GRTT_UPDATED:
            //fprintf(stderr, "new GRTT = %lf\n", NormGetGrttEstimate(norm_session));
            break;
            
        case NORM_TX_WATERMARK_COMPLETED:
            if (NORM_ACK_SUCCESS == NormGetAckingStatus(norm_session))
            {
                // All receivers acknowledged.
                norm_last_object = NORM_OBJECT_INVALID;
                bool txFilePending = TxFilePending();  // need to check this _before_ possible call to SendFiles()
                if (norm_flow_control_pending)
                {
                    norm_tx_queue_count -= (norm_tx_queue_max / 2);
                    norm_flow_control_pending = false;
                    if (NORM_OBJECT_INVALID != norm_flush_object)
                    {
                        // Set the deferred watermark per our ACK_LATER strategy
                        NormSetWatermark(norm_session, norm_flush_object, true);
                        norm_last_object = norm_flush_object;
                        norm_flush_object = NORM_OBJECT_INVALID;
                    }
                    if (txFilePending) SendFiles();
                }
                if (!txFilePending)
                {
                    // No more files to send and all have been acknowledged
                    fprintf(stderr, "normCast: final file acknowledged, exiting ...\n");
                    is_running = false;
                }
            }
            else
            {
                
                // In multicast, there is a chance some nodes ACK ...
                // so let's see who did, if any.
                // This iterates through the acking nodes looking for responses
                // to our application-defined NormSetWatermarkEx() request
                NormAckingStatus ackingStatus;
                NormNodeId nodeId = NORM_NODE_NONE;  // this inits NormGetNextAckingNode() iteration
                while (NormGetNextAckingNode(event.session, &nodeId, &ackingStatus))
                {
                    UINT32 tmp = htonl(nodeId);
                    ProtoAddress addr;
                    addr.SetRawHostAddress(ProtoAddress::IPv4, (char*)&tmp, 4);
                    if (NORM_ACK_SUCCESS != ackingStatus)
                        fprintf(stderr, "normCast: node %lu (IP address: %s) failed to acnowledge.\n",
                                        (unsigned long)nodeId, addr.GetHostString());
                    else
                        fprintf(stderr, "normCast: node %lu (IP address: %s) acknowledged.\n",
                                        (unsigned long)nodeId, addr.GetHostString());
                }
                // TBD - we could eventually time out and remove nodes that fail to ack
                // but for now, we are infinitely persistent by resetting watermark 
                // ack request without clearing flow control
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
            if(event.object == norm_flush_object)
                norm_flush_object = NORM_OBJECT_INVALID;
            // This is where we could delete the associated tx file if desired
            // (e.g., for an "outbox" use case)
            break;
        }   
        
        case NORM_ACKING_NODE_NEW:
        {
            NormNodeId id = NormNodeGetId(event.sender);
            UINT32 tmp = htonl(id);
            ProtoAddress addr;
            addr.SetRawHostAddress(ProtoAddress::IPv4, (char*)&tmp, 4);
            fprintf(stderr, "normCast: new acking node: %lu (IP address: %s)\n", (unsigned long)id, addr.GetHostString());
        }
        
        case NORM_REMOTE_SENDER_INACTIVE:
            //fprintf(stderr, "REMOTE SENDER INACTIVE node: %u\n", NormNodeGetId(event.sender));
            //NormNodeDelete(event.sender);
            break;
        
        case NORM_RX_OBJECT_ABORTED:
            //fprintf(stderr, "NORM_RX_OBJECT_ABORTED\n");// %hu\n", NormObjectGetTransportId(event.object));
            break;
            
        case NORM_RX_OBJECT_INFO:
        {
            // We use the NORM_INFO to contain the transferred file name
            if (NORM_OBJECT_FILE != NormObjectGetType(event.object))
            {
                fprintf(stderr, "normCast: received invalid object type?!\n");
                break;
            }
            // Rename rx file using newly received info
            char fileName[PATH_MAX+1];
            fileName[PATH_MAX] = '\0';
            strncpy(fileName, rx_cache_path, PATH_MAX);
            UINT16 pathLen = (UINT16)strlen(rx_cache_path);
            pathLen = MIN(pathLen, PATH_MAX);
            UINT16 len = NormObjectGetInfoLength(event.object);
            len = MIN(len, (PATH_MAX - pathLen));
            NormObjectGetInfo(event.object, fileName + pathLen, len);
            fileName[pathLen + len] = '\0';
            // Convert '/' in file info to directory delimiters
            for (UINT16 i = pathLen; i < (pathLen+len); i++)
            {
                if ('/' == fileName[i]) 
                    fileName[i] = PROTO_PATH_DELIMITER;
            }
            if (!NormFileRename(event.object, fileName))
                perror("normCast: rx file rename error");
            break;
        }   
        case NORM_RX_OBJECT_COMPLETED:
        {  
            if (NORM_OBJECT_FILE != NormObjectGetType(event.object))
            {
                fprintf(stderr, "normCast: received invalid object type?!\n");
                break;
            }
            char fileName[PATH_MAX + 1];
            fileName[PATH_MAX] = '\0';
            NormFileGetName(event.object, fileName, PATH_MAX);
            fprintf(stderr, "normCast: completed reception of \"%s\"\n", fileName);
            // TBD - implement receive post processing option ...
           break;
        }
            
        default:
            break;     
    }
            
}  // end NormCaster::HandleNormEvent()


void Usage()
{
    fprintf(stderr, "Usage: normCast id <nodeIdInteger> {send <file/dir list> &| recv <rxCacheDir>}\n"
                    "                [addr <addr>[/<port>]] [interface <name>] [loopback]\n"
                    "                [ack auto|<node1>[,<node2>,...]] [segment <bytes>]\n"
                    "                [block <count>] [parity <count>] [auto <count>]\n"
                    "                [cc|cce|ccl|rate <bitsPerSecond>] [ptos <value>]\n"
                    "                [flush {none|passive|active}] [silent] [txloss <lossFraction>]\n"
                    "                [buffer <bytes>] [txsockbuffer <bytes>] [rxsockbuffer <bytes>]\n"
                    "                [debug <level>] [trace] [log <logfile>]\n");
}  // end Usage()

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
    NormCaster::CCMode ccMode = NormCaster::NORM_CC;
    const char* mcastIface = NULL;
    
    int debugLevel = 0;
    const char* debugLog = NULL;  // stderr by default
    bool trace = false;
    bool silentReceiver = false;
    double txloss = 0.0;
    bool loopback = false;

    NormCaster normCast;
    
    // Parse command-line
    int i = 1;
    while (i < argc)
    {
        const char* cmd = argv[i++];
        size_t len = strlen(cmd);
        if (0 == strncmp(cmd, "send", len))
        {
            if (i >= argc)
            {
                fprintf(stderr, "normCast error: missing 'send' file/dir argument ...\n");
                Usage();
                return -1;
            }
            // Use ProtoTokenator to process comma-delimited file/dir path items
            ProtoTokenator tk(argv[i++], ',');
            const char* path;
            while (NULL != (path = tk.GetNextItem()))
            {
                // TBD - validate using ProtoFile::Exists() ???
                if (!normCast.AddTxItem(path))
                {
                    perror("normCast: 'send' error");
                    Usage();
                    return -1;
                }
            }
            send = true;
        }
        else if (0 == strncmp(cmd, "recv", len))
        {
            if (i >= argc)
            {
                fprintf(stderr, "normCast error: missing 'recv' <rxDir) argument ...\n");
                Usage();
                return -1;
            }
            const char* ptr = argv[i++];
            if (!ProtoFile::IsWritable(ptr))
            {
                fprintf(stderr, "normCast 'recv' error: invalid <rxDirc>!\n");
                Usage();
                return -1;
            }    
            normCast.SetRxCacheDirectory(ptr);
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
                fprintf(stderr, "normCast error: missing 'ptos' value!\n");
                Usage();
                return -1;
            }
            int tos = -1;
            int result = sscanf(argv[i], "%i", &tos);
            if (1 != result)
                result = sscanf(argv[i], "%x", &tos);
            if ((1 != result) || (tos < 0) || (tos > 255))
            {
                fprintf(stderr, "normCast error: invalid 'ptos' value!\n");
                Usage();
                return -1;
            }
            i++;
            normCast.SetProbeTOS(tos);
        }
        else if (0 == strncmp(cmd, "addr", len))
        {
            if (i >= argc)
            {
                fprintf(stderr, "normCast error: missing 'addr[/port]' value!\n");
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
                fprintf(stderr, "normCast error: missing 'id' value!\n");
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
                fprintf(stderr, "normCast error: missing 'id' <nodeId> value!\n");
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
                        fprintf(stderr, "normCast error: invalid acking node list!\n");
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
                fprintf(stderr, "normCast error: missing 'flush' <mode>!\n");
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
                fprintf(stderr, "normCast error: invalid 'flush' mode \"%s\"\n", mode);
                return -1;
            }   
        }
        else if (0 == strncmp(cmd, "rate", len))
        {
            if (i >= argc)
            {
                fprintf(stderr, "normCast error: missing 'rate' <bitsPerSecond> value!\n");
                Usage();
                return -1;
            }
            if (1 != sscanf(argv[i++], "%lf", &txRate))
            {
                fprintf(stderr, "normCast error: invalid transmit rate!\n");
                Usage();
                return -1;
            }       
            // set fixed-rate operation
            ccMode = NormCaster::NORM_FIXED;     
        }
        else if (0 == strcmp(cmd, "cc"))
        {
            ccMode = NormCaster::NORM_CC;
        }
        else if (0 == strcmp(cmd, "cce"))
        {
            ccMode = NormCaster::NORM_CCE;
        }
        else if (0 == strcmp(cmd, "ccl"))
        {
            ccMode = NormCaster::NORM_CCL;
        }
        else if (0 == strncmp(cmd, "interface", len))
        {
            if (i >= argc)
            {
                fprintf(stderr, "normCast error: missing 'interface' <name>!\n");
                Usage();
                return -1;
            }
            mcastIface = argv[i++];
        }
        else if (0 == strncmp(cmd, "buffer", len))
        {
            unsigned long value = 0 ;
            if (i >= argc)
            {
                fprintf(stderr, "normCast error: missing 'buffer' size!\n");
                Usage();
                return -1;
            }
            if (1 != sscanf(argv[i++], "%lu", &value))
            {
                fprintf(stderr, "normCast error: invalid 'buffer' size!\n");
                Usage();
                return -1;
            }
            normCast.SetBufferSize(value);
        }
        else if (0 == strncmp(cmd, "txsockbuffer", len))
        {
            unsigned long value = 0 ;
            if (i >= argc)
            {
                fprintf(stderr, "normCast error: missing 'txsockbuffer' size!\n");
                Usage();
                return -1;
            }
            if (1 != sscanf(argv[i++], "%lu", &value))
            {
                fprintf(stderr, "normCast error: invalid 'txsockbuffer' size!\n");
                Usage();
                return -1;
            }
            normCast.SetTxSocketBufferSize(value);
        }
        else if (0 == strncmp(cmd, "rxsockbuffer", len))
        {
            unsigned long value = 0 ;
            if (i >= argc)
            {
                fprintf(stderr, "normCast error: missing 'rxsockbuffer' size!\n");
                Usage();
                return -1;
            }
            if (1 != sscanf(argv[i++], "%lu", &value))
            {
                fprintf(stderr, "normCast error: invalid 'rxsockbuffer' size!\n");
                Usage();
                return -1;
            }
            normCast.SetRxSocketBufferSize(value);
        }
        else if (0 == strncmp(cmd, "segment", len))
        {
            if (i >= argc)
            {
                fprintf(stderr, "normCast error: missing 'segment' size!\n");
                Usage();
                return -1;
            }
            unsigned short value;
            if (1 != sscanf(argv[i++], "%hu", &value))
            {
                fprintf(stderr, "normCast error: invalid 'segment' size!\n");
                Usage();
                return -1;
            }
            normCast.SetSegmentSize(value);
        }
        else if (0 == strncmp(cmd, "block", len))
        {
            if (i >= argc)
            {
                fprintf(stderr, "normCast error: missing 'block' size!\n");
                Usage();
                return -1;
            }
            unsigned short value;
            if (1 != sscanf(argv[i++], "%hu", &value))
            {
                fprintf(stderr, "normCast error: invalid 'block' size!\n");
                Usage();
                return -1;
            }
            normCast.SetBlockSize(value);
        }
        else if (0 == strncmp(cmd, "parity", len))
        {
            if (i >= argc)
            {
                fprintf(stderr, "normCast error: missing 'parity' count!\n");
                Usage();
                return -1;
            }
            unsigned short value;
            if (1 != sscanf(argv[i++], "%hu", &value))
            {
                fprintf(stderr, "normCast error: invalid 'parity' count!\n");
                Usage();
                return -1;
            }
            normCast.SetNumParity(value);
        }
        else if (0 == strncmp(cmd, "auto", len))
        {
            if (i >= argc)
            {
                fprintf(stderr, "normCast error: missing 'auto' parity count!\n");
                Usage();
                return -1;
            }
            unsigned short value;
            if (1 != sscanf(argv[i++], "%hu", &value))
            {
                fprintf(stderr, "normCast error: invalid 'auto' parity count!\n");
                Usage();
                return -1;
            }
            normCast.SetAutoParity(value);
        }
        else if (0 == strncmp(cmd, "silent", len))
        {
            silentReceiver = true;
        }
        else if (0 == strncmp(cmd, "txloss", len))
        {
            if (1 != sscanf(argv[i++], "%lf", &txloss))
            {
                fprintf(stderr, "normCast error: invalid 'txloss' value!\n");
                Usage();
                return -1;
            }
        }
        else if (0 == strncmp(cmd, "debug", len))
        {
            if (i >= argc)
            {
                fprintf(stderr, "normCast error: missing 'debug' <level>!\n");
                Usage();
                return -1;
            }
            debugLevel = atoi(argv[i++]);
        }
        else if (0 == strncmp(cmd, "log", len))
        {
            if (i >= argc)
            {
                fprintf(stderr, "normCast error: missing 'log' <fileName>!\n");
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
            fprintf(stderr, "normCast error: invalid command \"%s\"!\n", cmd);
            Usage();
            return -1;
        }
    }
    
    if (!send && !recv)
    {
        fprintf(stderr, "normCast error: not configured to send or recv!\n");
        Usage();
        return -1;
    }

    // TBD - should provide more error checking of calls
    NormInstanceHandle normInstance = NormCreateInstance();
    NormSetDebugLevel(debugLevel);
    if (NULL != debugLog)
        NormOpenDebugLog(normInstance, debugLog);
    
    // TBD - enhance NORM to support per-session or perhaps per-sender rx cache directories?
    if (recv)
        NormSetCacheDirectory(normInstance, normCast.GetRxCacheDirectory());
    
    normCast.SetLoopback(loopback);
    normCast.SetFlushing(flushing);
        
    if (!normCast.OpenNormSession(normInstance, sessionAddr, sessionPort, (NormNodeId)nodeId))
    {
        fprintf(stderr, "normCast error: unable to open NORM session\n");
        NormDestroyInstance(normInstance);
        return -1;
    }
    
    
    if (NORM_NODE_NONE == nodeId)
    {
        // local node id was auto-assigned so let's see what it was assigned
        nodeId = NormGetLocalNodeId(normCast.GetSession());
        // For cross-platform, use ProtoAddress to see if a reasonable IP address was used
        UINT32 tmp = htonl(nodeId);
        ProtoAddress addr;
        addr.SetRawHostAddress(ProtoAddress::IPv4, (char*)&tmp, 4);
        fprintf(stderr, "normCast: auto assigned NormNodeId: %lu (IP address: %s)\n",
                (unsigned long)nodeId, addr.GetHostString());
    }
    
    
    if (silentReceiver) normCast.SetSilentReceiver(true);
    if (txloss > 0.0) normCast.SetTxLoss(txloss);
    
    
    if (autoAck)
    {
        normCast.SetAutoAck(true);
    }
    else
    {
        for (unsigned int i = 0; i < ackingNodeCount; i++)
            normCast.AddAckingNode(ackingNodeList[i]);
    }
    
    normCast.SetNormCongestionControl(ccMode);
    if (NormCaster::NORM_FIXED == ccMode)
        normCast.SetNormTxRate(txRate);
    if (NULL != mcastIface)
        normCast.SetNormMulticastInterface(mcastIface);
    
    if (trace) normCast.SetNormMessageTrace(true);
    
    // TBD - set NORM session parameters
    normCast.Start(send, recv); 

     if (normCast.TxFilePending()) normCast.SendFiles();
    
#ifdef WIN32
    //Win32InputHandler inputHandler;
    //inputHandler.Open();
    HANDLE handleArray[2];
    handleArray[0] = NormGetDescriptor(normInstance);
    bool inputNeeded = false;  // set this to 'true', and uncomment next line for STDIN input notification
    //handleArray[1] = inputHandler.GetEventHandle();
#else
    int normfd = NormGetDescriptor(normInstance);
    int inputfd = fileno(stdin);   // Note that one could get input from something besides 'stdin' if desired
    fd_set fdsetInput;
    FD_ZERO(&fdsetInput);
#endif  // if/else WIN32/UNIX
    
    while (normCast.IsRunning())
    {
        // TBD - could add "stdin" as a descriptor to monitor 
        //       for run-time commands, etc
        //       (on Windows, would use the win32InputHandler code in norm/examples)
        bool normEventPending = false;
        bool inputEventPending = false;
#ifdef WIN32
        DWORD handleCount = inputNeeded ? 2 : 1;
        DWORD waitStatus =  
            MsgWaitForMultipleObjectsEx(handleCount,   // number of handles in array
                                        handleArray,   // object-handle array
                                        INFINITE,      // time-out interval
                                        QS_ALLINPUT,   // input-event type
                                        0);
        if ((WAIT_OBJECT_0 <= waitStatus) && (waitStatus < (WAIT_OBJECT_0 + handleCount)))
        {
            if (0 == (waitStatus - WAIT_OBJECT_0))
                normEventPending = true;
            else
                inputEventPending = true;
        }
        else if (-1 == waitStatus)
        {
            perror("normClient: MsgWaitForMultipleObjectsEx() error");
            break;
        }
        else
        {
            // TBD - any other status we should handle?
            // (e.g. WAIT_TIMEOUT, WAIT_ABANDONED or WAIT_IO_COMPLETION)
            continue;  // ignore for now
        }
#else  // UNIX
        int maxfd = normfd;
        FD_SET(normfd, &fdsetInput);
        //FD_SET(inputfd, &fdsetInput);
        //if (inputfd > maxfd) maxfd = inputfd;
        if (maxfd >= 0)
        {
            struct timeval timeout;
            timeout.tv_sec = 1;
            timeout.tv_usec = 0;
            int result = select(maxfd+1, &fdsetInput, NULL, NULL, NULL); //&timeout);
            switch (result)
            {
                case -1:
                    switch (errno)
                   {
                        case EINTR:
                        case EAGAIN:
                            continue;
                        default:
                            perror("normCast select() error");
                            // TBD - stop NormCaster
                            break;
                    }
                    break;
                case 0:
                    // 'timeout' condition
                    // TBD - implement file transmit / repeat interval timer
                    // shouldn't occur for now (no timeout)
                    //fprintf(stderr, "normCast timeout ...\n");
                    break;
                default:
                    if (FD_ISSET(normfd, &fdsetInput)) normEventPending = true;
                    if (FD_ISSET(inputfd, &fdsetInput)) inputEventPending = true;
                    break; 
            }  // end switch(result
        }  // end if (maxfd >= 0)  
#endif // if/else WIN32/UNIX 
    
        // if (inputEventPending)
        // {
        //      TBD - could parse input for runtime commands (e.g., send <file>), etc
        // }
        if (normEventPending)
        {
            // Handle all pending NORM notification events
            // (Setting waitForEvent=false lets us get all available events without blocking)
            // (Otherwise, one could build a simple main() (or threaded) loop around NormGetNextEvent() if desired)
            NormEvent event;
            while (NormGetNextEvent(normInstance, &event, false))
            {
                normCast.HandleNormEvent(event);
            }
        }
        // Invoke any 'timeout' actions here
        
    }  // end while(normCast.IsRunning()
    
    NormCloseDebugLog(normInstance);
    
    normCast.CloseNormSession();
    
    NormDestroyInstance(normInstance);
    
    normCast.Destroy();
    
    fprintf(stderr, "normCast: done.\n");
    
}  // end main()

