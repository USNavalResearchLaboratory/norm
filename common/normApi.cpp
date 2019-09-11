
#include "normApi.h"
#include "normSession.h"

// const defs
const NormInstanceHandle NORM_INSTANCE_INVALID = ((NormInstanceHandle)0);
const NormSessionHandle NORM_SESSION_INVALID = ((NormSessionHandle)0);
const NormNodeHandle NORM_NODE_INVALID = ((NormNodeHandle)0);
const NormNodeId NORM_NODE_NONE = ((NormNodeId)0x00000000);
const NormNodeId NORM_NODE_ANY = ((NormNodeId)0xffffffff);
const NormObjectHandle NORM_OBJECT_INVALID = ((NormObjectHandle)0);

/** The "NormInstance" class is a C++ helper class that keeps
 *  state for an instance of the NORM API.  It acts as a
 *  "go between" the API's procedural function calls and
 *  the underlying NORM C++ implementation
 */
class NormInstance : public NormController
{
    public:
        NormInstance();
        virtual ~NormInstance();
        
        void Notify(NormController::Event   event,
                    class NormSessionMgr*   sessionMgr,
                    class NormSession*      session,
                    class NormServerNode*   server,
                    class NormObject*       object);
        
        bool Startup();
        void Shutdown();
        
        bool WaitForEvent();
        bool GetNextEvent(NormEvent* theEvent);
        bool SetCacheDirectory(const char* cachePath);
        
        bool NotifyQueueIsEmpty() const 
            {return notify_queue.IsEmpty();}
        
        void PurgeObjectNotifications(NormObjectHandle objectHandle);
        
        ProtoDispatcher::Descriptor GetDescriptor() const
        {
#ifdef WIN32
            return notify_event;
#else
            return notify_fd[0];
#endif // if/else WIN32/UNIX            
        }
        
        static NormInstance* GetInstanceFromSession(NormSessionHandle sessionHandle)
        {
            NormSession* session = (NormSession*)sessionHandle;
            NormInstance* theInstance = static_cast<NormInstance*>(session->GetSessionMgr().GetController());
            return theInstance;
            //return static_cast<NormInstance*>(session->GetSessionMgr().GetController());   
        }
        static NormInstance* GetInstanceFromNode(NormNodeHandle nodeHandle)
        {
            NormSession& session = ((NormNode*)nodeHandle)->GetSession();
            return static_cast<NormInstance*>(session.GetSessionMgr().GetController());   
        }
        static NormInstance* GetInstanceFromObject(NormObjectHandle objectHandle)
        {
            NormSession& session = ((NormObject*)objectHandle)->GetSession();;
            return static_cast<NormInstance*>(session.GetSessionMgr().GetController());   
        }
        
        class Notification
        {
            public:
                NormEvent   event;
                
                void Append(Notification* n) {next = n;}
                
                Notification* GetNext() const {return next;}
                
            class Queue
            {
                public:
                    Queue();
                    ~Queue();
                    bool IsEmpty() const {return (NULL == head);}
                    void Destroy();
                    void Append(Notification* n)
                    {
                        n->Append(NULL); 
                        if (tail)
                            tail->Append(n);
                        else
                            head = n;  
                        tail = n;                            
                    }
                    Notification* RemoveHead()
                    {
                        Notification* n = head;
                        if (n)
                        {
                            head = n->GetNext();
                            tail = head ? tail : NULL;
                        }
                        return n;   
                    }
                    Notification* GetHead() {return head;}
                    void SetHead(Notification* n) {head = n;}
                private:
                    Notification* head;
                    Notification* tail;
                    
            };  // end class NormInstance::Notification::Queue 
                
            private:
                Notification*           next;
        };  // end class NormInstance::Notification
        
        ProtoDispatcher             dispatcher;
        NormSessionMgr              session_mgr;    
        
    private:
        Notification::Queue         notify_pool;
        Notification::Queue         notify_queue; 
        Notification*               previous_notification;
        
        const char*                 rx_cache_path;
        
#ifdef WIN32
        HANDLE                      notify_event;
#else
        int                         notify_fd[2];
#endif // if/else WIN32/UNIX
};  // end class NormInstance

////////////////////////////////////////////////////
// NormInstance::Notification::Queue implementation
NormInstance::Notification::Queue::Queue()
 : head(NULL), tail(NULL)
{   
}

NormInstance::Notification::Queue::~Queue()
{
    Destroy();   
}

void NormInstance::Notification::Queue::Destroy()
{
    Notification* n;
    while ((n = RemoveHead())) delete n;   
}  // end NormInstance::Notification::Queue::Destroy()

////////////////////////////////////////////////////
// NormInstance implementation
NormInstance::NormInstance()
 : session_mgr(static_cast<ProtoTimerMgr&>(dispatcher), 
               static_cast<ProtoSocket::Notifier&>(dispatcher)),
   previous_notification(NULL), rx_cache_path(NULL)
{
#ifdef WIN32
    notify_event = NULL;
#else
    notify_fd[0] = notify_fd[1] = -1;
#endif // if/else WIN32/UNIX
    session_mgr.SetController(this);
}

NormInstance::~NormInstance()
{
    Shutdown();
}

bool NormInstance::SetCacheDirectory(const char* cachePath)
{
    // (TBD) verify that we can _write_ to this directory!
    bool result = false;
    if (dispatcher.SuspendThread())
    {
        int length = strlen(cachePath);
        if (PROTO_PATH_DELIMITER != cachePath[length-1]) 
            length += 2;
        else
            length += 1;
        length = (length < PATH_MAX) ? length : PATH_MAX;
        char* pathStorage = new char[length];
        if (pathStorage)
        {
            strncpy(pathStorage, cachePath, length);
            pathStorage[length - 2] = PROTO_PATH_DELIMITER;
            pathStorage[length - 1] = '\0';
            if (rx_cache_path) delete[] (char*)rx_cache_path;
            rx_cache_path = pathStorage;
            result = true;
        }
        else
        {
            DMSG(0, "NormInstance::SetCacheDirectory() new pathStorage error: %s\n",
                    GetErrorString());
        }
        dispatcher.ResumeThread();
    }
    return result;
}  // end NormInstance::SetCacheDirectory()

void NormInstance::Notify(NormController::Event   event,
                          class NormSessionMgr*   sessionMgr,
                          class NormSession*      session,
                          class NormServerNode*   server,
                          class NormObject*       object)
{
    Notification* n = notify_pool.RemoveHead();
    if (!n)
    {
        if (!(n = new Notification))
        {
            DMSG(0, "NormInstance::Notify() new Notification error: %s\n",
                    GetErrorString());
            return;   
        }
    }
    
    switch (event)
    { 
        case RX_OBJECT_NEW:
        {
            // new recv object "Accept()" policy implemented here
            switch (object->GetType())
            {
                case NormObject::STREAM:
                {
                    NormStreamObject* stream = static_cast<NormStreamObject*>(object);
                    // (TBD) implement silent_client accept differently
                    NormObjectSize size = stream->GetSize();
                    // We double the size to prevent unecessary data loss
                    // for our threaded API
                    if (!stream->Accept(2*size.LSB()))
                    {
                        DMSG(0, "NormInstance::Notify() stream accept error\n");
                        notify_pool.Append(n);
                        return;   
                    }
                    // By setting a non-zero "block pool threshold", this
                    // gives the API a chance to "catch up" on reading
                    // when the receive stream becomes buffer-constrained
                    UINT32 blockPoolCount = stream->GetBlockPoolCount();
                    stream->SetBlockPoolThreshold(blockPoolCount / 2);
                    break;
                }
                case NormObject::FILE:
                {
                    if (NULL != rx_cache_path)
                    {
                        char fileName[PATH_MAX];
                        strncpy(fileName, rx_cache_path, PATH_MAX);
                        strcat(fileName, "normTempXXXXXX");
#ifdef WIN32
                        if (!_mktemp(fileName))
#else
                        int fd = mkstemp(fileName); 
                        if (fd >= 0)
                        {
                            close(fd);
                        }
                        else   
#endif // if/else WIN32         
                        {
                            DMSG(0, "NormInstance::Notify(RX_OBJECT_NEW) Warning: mkstemp() error: %s\n",
                                    strerror(errno));  
                        } 
                        if (!static_cast<NormFileObject*>(object)->Accept(fileName))
                        {
                            DMSG(0, "NormInstance::Notify(RX_OBJECT_NEW) file object accept error!\n");
                        }
                    }   
                    else
                    {
                        // we're ignoring files
                        return;    
                    }                
                    break;
                }
                case NormObject::DATA:
                {
                    NormDataObject* dataObj = static_cast<NormDataObject*>(object);
                    unsigned int dataLen = (unsigned int)(object->GetSize().GetOffset());
                    char* dataPtr = new char[dataLen];
                    if (NULL == dataPtr)
                    {
                        DMSG(0, "NormInstance::Notify(RX_OBJECT_NEW) new dataPtr error: %s\n",
                             GetErrorString());
                        return;   
                    }
                    // Note that the "true" parameter means the
                    // NORM protocol engine will free the allocated
                    // data on object deletion, so the app should
                    // use NormDataDetachData() to keep the received
                    // data (or copy it before the data object is deleted)
                    dataObj->Accept(dataPtr, dataLen, true);
                    break;
                }
                default:
                    // This shouldn't occur
                    return;
            }  // end switch(object->GetType())
            break;
        }  // end case RX_OBJECT_NEW
        
        case RX_OBJECT_COMPLETED:
        case TX_OBJECT_PURGED:
        case RX_OBJECT_ABORTED:
            object->Retain();
            break;
            
        default:
            break;
    }  // end switch(event)
    
    bool doNotify = notify_queue.IsEmpty();
    n->event.type = (NormEventType)event;
    n->event.session = session;
    n->event.sender = server;
    n->event.object = object;
    notify_queue.Append(n);
    
    if (doNotify)
    {
#ifdef WIN32
        if (0 == SetEvent(notify_event))
        {
            DMSG(0, "NormInstance::Notify() SetEvent() error: %s\n",
                         GetErrorString());
        }
#else
        char byte;
        while (1 != write(notify_fd[1], &byte, 1))
        {
            if ((EINTR != errno) && (EAGAIN != errno))
            {
                DMSG(0, "NormInstance::Notify() write() error: %s\n",
                         GetErrorString());
                break;
            }
        }    
#endif // if/else WIN32/UNIX  
    }  
}  // end NormInstance::Notify()

void NormInstance::PurgeObjectNotifications(NormObjectHandle objectHandle)
{
    Notification* next = notify_queue.GetHead();
    while (next)
    {
        if (objectHandle == next->event.object)
            next->event.type = NORM_EVENT_INVALID;
        next = next->GetNext();
    }
}  // end NormInstance::PurgeObjectNotifications()

// NormInstance::dispatcher MUST be suspended _before_ calling this
bool NormInstance::GetNextEvent(NormEvent* theEvent)
{
    // First, do any garbage collection for "previous_notification"
    if (NULL != previous_notification)
    {
        // "Release" any purged/completed/aborted objects
        switch(previous_notification->event.type)
        {
            case NORM_RX_OBJECT_COMPLETED:
            case NORM_TX_OBJECT_PURGED:
            case NORM_RX_OBJECT_ABORTED:
            {
                NormObject* obj = (NormObject*)(previous_notification->event.object);
                obj->Release();
                break;
            }         
            default:
                break;   
        }
        notify_pool.Append(previous_notification);   
        previous_notification = NULL;   
    }
    Notification* n;
    while ((n = notify_queue.RemoveHead()))
    {
        if (notify_queue.IsEmpty())
        {
#ifdef WIN32
                if (0 == ResetEvent(notify_event))
                {
                    DMSG(0, "NormInstance::GetNextEvent() ResetEvent error: %s\n", 
                             GetErrorString());
                }
#else
               char byte;
               while (read(notify_fd[0], &byte, 1) > 0);  // TBD - error check
#endif // if/else WIN32/UNIX
        }
        switch (n->event.type)
        {
            case NORM_EVENT_INVALID:
                continue;
            case NORM_RX_OBJECT_UPDATED:
                // reset update event notification
                ((NormObject*)n->event.object)->SetNotifyOnUpdate(true);
                break;
            default:
                break;   
        }
        if (theEvent) *theEvent = n->event;
        previous_notification = n;  // keep dispatched event for garbage collection
        return true;
    }
    return false; 
}  // end NormInstance::GetNextEvent()

bool NormInstance::WaitForEvent()
{
#ifdef WIN32
    WaitForSingleObject(notify_event, INFINITE);
#else
    fd_set fdSet;
    FD_ZERO(&fdSet);
    FD_SET(notify_fd[0], &fdSet);
    while (1)
    {
        if (0 > select(notify_fd[0] + 1, &fdSet, (fd_set*)NULL, 
                       (fd_set*)NULL, (struct timeval*)NULL))
        {
            if (EINTR != errno)
            {
                DMSG(0, "NormInstance::WaitForEvent() select() error: %s\n",
                        GetErrorString());
                return false;   
            }
        }
        else
        {
            break;       
        }
    }        
#endif 
    return true;
}  // end NormInstance::WaitForEvent()


bool NormInstance::Startup()
{
    // 1) Create descriptor to use for event notification
#ifdef WIN32
    // Create initially non-signalled, manual reset event
    notify_event = CreateEvent(NULL, TRUE, FALSE, NULL);  
    if (NULL == notify_event)
    {
        DMSG(0, "NormInstance::Startup() CreateEvent() error: %s\n", GetErrorString());
        return false;
    }
#else
    if (0 != pipe(notify_fd))
    {
        DMSG(0, "NormInstance::Startup() pipe() error: %s\n", GetErrorString());
        return false;
    }
    // make reading non-blocking
    if(-1 == fcntl(notify_fd[0], F_SETFL, fcntl(notify_fd[0], F_GETFL, 0)  | O_NONBLOCK))
    {
        DMSG(0, "NormInstance::Startup() fcntl(F_SETFL(O_NONBLOCK)) error: %s\n", GetErrorString());
        close(notify_fd[0]);
        close(notify_fd[1]);
        notify_fd[0] = notify_fd[1] = -1;
        return false;
    }
#endif // if/else WIN32/UNIX
    // 2) Start thread
    return dispatcher.StartThread();
}  // end NormInstance::Startup()

void NormInstance::Shutdown()
{
    dispatcher.Stop();
#ifdef WIN32
    if (NULL != notify_event)
    {
        CloseHandle(notify_event);
        notify_event = NULL;
    }
#else
    if (notify_fd[0] > 0)
    {
        close(notify_fd[0]);  // close read end of pipe
        close(notify_fd[1]);  // close write end of pipe
        notify_fd[0] = notify_fd[1] = -1;
    }
#endif // if/else WIN32/UNIX
    if (rx_cache_path)
    {
        delete[] (char*)rx_cache_path;
        rx_cache_path = NULL;   
    }
    
    // Garbage collect our "previous_notification"
    if (NULL != previous_notification)
    {
        switch(previous_notification->event.type)
        {
            case NORM_RX_OBJECT_COMPLETED:
            case NORM_TX_OBJECT_PURGED:
            case NORM_RX_OBJECT_ABORTED:
            {
                NormObject* obj = (NormObject*)(previous_notification->event.object);
                obj->Release();
                break;
            }         
            default:
                break;   
        }
        notify_pool.Append(previous_notification);   
        previous_notification = NULL;   
    }
    
    Notification* n;
    while ((n = notify_queue.RemoveHead()))
    {
        switch (n->event.type)
        {
            case NORM_RX_OBJECT_NEW:
            {
                NormObject* obj = (NormObject*)n->event.object;
                switch (obj->GetType())
                {
                    case NormObject::FILE:
                        // (TBD) unlink temp file?
                        break;
                    default:
                        break;
                }
                break;
            }
            // Garbage collect retained objects
            case NORM_RX_OBJECT_COMPLETED:
            case NORM_TX_OBJECT_PURGED:
            case NORM_RX_OBJECT_ABORTED:
                ((NormObject*)n->event.object)->Release();
                break; 
            default:
                break;      
        }   
        delete n;        
    }
    notify_pool.Destroy();
}  // end NormInstance::Shutdown()


//////////////////////////////////////////////////////////////////////////
// NORM API FUNCTION IMPLEMENTATIONS
//

/** NORM API Initialization */

NormInstanceHandle NormCreateInstance()
{
    NormInstance* normInstance = new NormInstance;
    if (normInstance)
    {
        if (normInstance->Startup())
            return ((NormInstanceHandle)normInstance); 
        else
            delete normInstance;
    }
    return NORM_INSTANCE_INVALID;  
}  // end NormCreateInstance()

void NormDestroyInstance(NormInstanceHandle instanceHandle)
{
    delete ((NormInstance*)instanceHandle);   
}  // end NormDestroyInstance()

bool NormSetCacheDirectory(NormInstanceHandle instanceHandle, 
                           const char*        cachePath)
{
    NormInstance* instance = (NormInstance*)instanceHandle;
    if (instance)
        return instance->SetCacheDirectory(cachePath);
    else
        return false;
} // end NormSetCacheDirectory()

bool NormGetNextEvent(NormInstanceHandle instanceHandle, NormEvent* theEvent)
{
    bool result = false;
    NormInstance* instance = (NormInstance*)instanceHandle;
    if (instance)
    {
        if (instance->NotifyQueueIsEmpty()) //(TBD) perform this check???
            instance->WaitForEvent();
        if (instance->dispatcher.SuspendThread())
        {
            result = instance->GetNextEvent(theEvent);
            instance->dispatcher.ResumeThread();
        }
    }
    return result;  
}  // end NormGetNextEvent()


/** NORM Session Creation and Control Functions */

NormSessionHandle NormCreateSession(NormInstanceHandle instanceHandle,
                                    const char*        sessionAddr,
                                    unsigned short     sessionPort,
                                    NormNodeId         localNodeId)
{
    // (TBD) wrap this with SuspendThread/ResumeThread ???
    NormInstance* instance = (NormInstance*)instanceHandle;
    if (instance)
    {
        NormSession* session = 
            instance->session_mgr.NewSession(sessionAddr, sessionPort, localNodeId);
        if (session) return ((NormSessionHandle)session);
    }
    return NORM_SESSION_INVALID;
}  // end NormCreateSession()

void NormDestroySession(NormSessionHandle sessionHandle)
{
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (instance)
    {
        if (instance->dispatcher.SuspendThread())
        {    
            NormSession* session = (NormSession*)sessionHandle;
            if (session)
            {
                session->Close();
                session->GetSessionMgr().DeleteSession(session);
            }
            instance->dispatcher.ResumeThread();
        }
    }
}  // end NormDestroySession()

NormNodeId NormGetLocalNodeId(NormSessionHandle sessionHandle)
{
    NormSession* session = (NormSession*)sessionHandle;
    if (session)
        return session->LocalNodeId();
    else
        return NORM_NODE_NONE;      
}  // end NormGetLocalNodeId()

bool NormSetMulticastInterface(NormSessionHandle sessionHandle,
                               const char*       interfaceName)
{
    bool result = false;
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (instance)
    {
        if (instance->dispatcher.SuspendThread())
        {    
            NormSession* session = (NormSession*)sessionHandle;
            if (session)
                result = session->SetMulticastInterface(interfaceName);
            instance->dispatcher.ResumeThread();
        }
    }
    return result;     
}  // end NormSetMulticastInterface()

bool NormSetTTL(NormSessionHandle sessionHandle,
                unsigned char     ttl)
{
    bool result = false;
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (instance)
    {
        if (instance->dispatcher.SuspendThread())
        {    
            NormSession* session = (NormSession*)sessionHandle;
            if (session)
                result = session->SetTTL(ttl);
            instance->dispatcher.ResumeThread();
        }
    }
    return result;     
}  // end NormSetTTL()

bool NormSetTOS(NormSessionHandle sessionHandle,
                unsigned char     tos)
{
    bool result = false;
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (instance)
    {
        if (instance->dispatcher.SuspendThread())
        {    
            NormSession* session = (NormSession*)sessionHandle;
            if (session)
                result = session->SetTTL(tos);
            instance->dispatcher.ResumeThread();
        }
    }
    return result;     
}  // end NormSetTOS()

/** Special test/debug support functions */

bool NormSetLoopback(NormSessionHandle sessionHandle, bool state)
{
    NormSession* session = (NormSession*)sessionHandle;
    if (session)
        return session->SetLoopback(state);
    else
        return false;  
}  // end NormSetLoopback()

void NormSetMessageTrace(NormSessionHandle sessionHandle, bool state)
{
    NormSession* session = (NormSession*)sessionHandle;
    if (session) session->SetTrace(state);
}  // end NormSetMessageTrace()

void NormSetTxLoss(NormSessionHandle sessionHandle, double percent)
{
    NormSession* session = (NormSession*)sessionHandle;
    if (session) session->SetTxLoss(percent);
}  // end NormSetTxLoss()

void NormSetRxLoss(NormSessionHandle sessionHandle, double percent)
{
    NormSession* session = (NormSession*)sessionHandle;
    if (session) session->SetRxLoss(percent);
}  // end NormSetRxLoss()


/** NORM Sender Functions */

bool NormStartSender(NormSessionHandle  sessionHandle,
                     unsigned long      bufferSpace,
                     unsigned short     segmentSize,
                     unsigned char      numData,
                     unsigned char      numParity)
{
    bool result = false;
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {
        NormSession* session = (NormSession*)sessionHandle;
        if (session)
            result = session->StartServer(bufferSpace, segmentSize, numData, numParity);
        else
            result = false;
        instance->dispatcher.ResumeThread();
    }
    return result;
}  // end NormStartSender()

void NormStopSender(NormSessionHandle sessionHandle)
{
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {
        NormSession* session = (NormSession*)sessionHandle;
        if (session) session->StopServer();
        instance->dispatcher.ResumeThread();
    }
}  // end NormStopSender()

void NormSetTransmitRate(NormSessionHandle sessionHandle,
                         double            bitsPerSecond)
{
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {
        NormSession* session = (NormSession*)sessionHandle;
        if (session) session->SetTxRate(bitsPerSecond);
        instance->dispatcher.ResumeThread();
    }
}  // end NormSetTransmitRate()

void NormSetCongestionControl(NormSessionHandle sessionHandle, bool enable)
{
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {
        NormSession* session = (NormSession*)sessionHandle;
        if (session) session->SetCongestionControl(enable);
        instance->dispatcher.ResumeThread();
    }
}  // end NormSetCongestionControl()

void NormSetTransmitRateBounds(NormSessionHandle sessionHandle,
                               double            rateMin,
                               double            rateMax)
{
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {
        NormSession* session = (NormSession*)sessionHandle;
        if (session) session->SetTxRateBounds(rateMin, rateMax);
        instance->dispatcher.ResumeThread();
    }
}  // end NormSetTransmitRateBounds()

void NormSetAutoParity(NormSessionHandle sessionHandle, unsigned char autoParity)
{
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {
        NormSession* session = (NormSession*)sessionHandle;
        if (session) session->ServerSetAutoParity(autoParity);
        instance->dispatcher.ResumeThread();
    }
}  // end NormSetAutoParity()

void NormSetGrttEstimate(NormSessionHandle sessionHandle,
                         double            grttEstimate)
{
    // (TBD) suspend instance if timer reschedule is
    //       added to ServerSetGrtt() method.
    NormSession* session = (NormSession*)sessionHandle;
    if (session) session->ServerSetGrtt(grttEstimate);
}  // end NormSetGrttEstimate()

bool NormAddAckingNode(NormSessionHandle  sessionHandle,
                       NormNodeId         nodeId)
{
    bool result = false;
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {
        NormSession* session = (NormSession*)sessionHandle;
        if (session)
            result = session->ServerAddAckingNode(nodeId);
        instance->dispatcher.ResumeThread();
    }
    return result;
}  // end NormAddAckingNode()

void NormRemoveAckingNode(NormSessionHandle  sessionHandle,
                          NormNodeId         nodeId)
{
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {
        NormSession* session = (NormSession*)sessionHandle;
        if (session) session->ServerRemoveAckingNode(nodeId);
        instance->dispatcher.ResumeThread();
    }
}  // end NormRemoveAckingNode()

NormObjectHandle NormFileEnqueue(NormSessionHandle  sessionHandle,
                                 const char*        fileName,
                                 const char*        infoPtr, 
                                 unsigned int       infoLen)
{
    NormObjectHandle objectHandle = NORM_OBJECT_INVALID;
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {
        NormSession* session = (NormSession*)sessionHandle;
        if (session)
        {
            NormObject* obj = session->QueueTxFile(fileName, infoPtr, infoLen);
            if (obj) objectHandle = (NormObjectHandle)obj;
        }
        instance->dispatcher.ResumeThread();
    }
    return objectHandle;
}  // end NormFileEnqueue()

NormObjectHandle NormDataEnqueue(NormSessionHandle  sessionHandle,
                                 const char*        dataPtr,
                                 unsigned long      dataLen,
                                 const char*        infoPtr = NULL, 
                                 unsigned int       infoLen = 0)
{
    NormObjectHandle objectHandle = NORM_OBJECT_INVALID;
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {
        NormSession* session = (NormSession*)sessionHandle;
        if (session)
        {
            NormObject* obj = session->QueueTxData(dataPtr, dataLen, infoPtr, infoLen);
            if (obj) objectHandle = (NormObjectHandle)obj;
        }
        instance->dispatcher.ResumeThread();
    }
    return objectHandle;
}  // end NormDataEnqueue()

NormObjectHandle NormStreamOpen(NormSessionHandle  sessionHandle,
                                unsigned long      bufferSize)
{
    NormObjectHandle objectHandle = NORM_OBJECT_INVALID;
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {
        NormSession* session = (NormSession*)sessionHandle;
        if (session)
        {
            NormObject* obj = 
                static_cast<NormObject*>(session->QueueTxStream(bufferSize));
            if (obj) objectHandle = (NormObjectHandle)obj;
        }
        instance->dispatcher.ResumeThread();
    }
    return objectHandle;
}  // end NormStreamOpen()

void NormStreamClose(NormObjectHandle streamHandle)
{
    NormObjectCancel(streamHandle);
}  // end NormStreamClose()

unsigned int NormStreamWrite(NormObjectHandle   streamHandle,
                             const char*        buffer,
                             unsigned int       numBytes)
{
    unsigned int result = 0;
    NormInstance* instance = NormInstance::GetInstanceFromObject(streamHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {
        NormStreamObject* stream = 
            static_cast<NormStreamObject*>((NormObject*)streamHandle);
        if (stream)
            result = stream->Write(buffer, numBytes, false);
        instance->dispatcher.ResumeThread();
    }
    return result;
}  // end NormStreamWrite()

void NormStreamFlush(NormObjectHandle streamHandle, 
                     bool             eom,
                     NormFlushMode    flushMode)
{
    NormInstance* instance = NormInstance::GetInstanceFromObject(streamHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {
        NormStreamObject* stream = 
            static_cast<NormStreamObject*>((NormObject*)streamHandle);
        if (stream) stream->Flush(eom);
        instance->dispatcher.ResumeThread();
    }
}  // end NormStreamFlush()

void NormStreamSetAutoFlush(NormObjectHandle    streamHandle,
                            NormFlushMode       flushMode)
{
    NormStreamObject* stream = 
        static_cast<NormStreamObject*>((NormObject*)streamHandle);
    if (stream)
        stream->SetFlushMode((NormStreamObject::FlushMode)flushMode);
}  // end NormStreamSetAutoFlush()

void NormStreamSetPushEnable(NormObjectHandle streamHandle, bool state)
{
    NormStreamObject* stream = 
        static_cast<NormStreamObject*>((NormObject*)streamHandle);
    if (stream) stream->SetPushMode(state);
}  // end NormStreamSetPushEnable()


void NormStreamMarkEom(NormObjectHandle streamHandle)
{
    NormInstance* instance = NormInstance::GetInstanceFromObject(streamHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {
        NormStreamObject* stream = 
            static_cast<NormStreamObject*>((NormObject*)streamHandle);
        if (stream) stream->Write(NULL, 0, true);
        instance->dispatcher.ResumeThread();
    }
}  // end NormStreamMarkEom()

bool NormSetWatermark(NormSessionHandle  sessionHandle,
                      NormObjectHandle   objectHandle)
{
    bool result = false;
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {
        NormSession* session = (NormSession*)sessionHandle;
        NormObject* obj = (NormObject*)objectHandle;
        if (session && obj)
        {
            // (segmentId doesn't matter for non-stream)
            if (obj->IsStream())
            {
                NormStreamObject* stream = static_cast<NormStreamObject*>(obj);
                session->ServerSetWatermark(stream->GetId(), 
                                            stream->FlushBlockId(),
                                            stream->FlushSegmentId());  
            }
            else
            {
                NormBlockId blockId = obj->GetFinalBlockId();
                NormSegmentId segmentId = obj->GetBlockSize(blockId) - 1;
                session->ServerSetWatermark(obj->GetId(), 
                                            blockId,
                                            segmentId);  
            }
            result = true;
        }        
        instance->dispatcher.ResumeThread();
    }
    return result;
}  // end NormSetWatermark()


/** NORM Receiver Functions */

bool NormStartReceiver(NormSessionHandle  sessionHandle,
                       unsigned long      bufferSpace)
{
    bool result = false;
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {
        NormSession* session = (NormSession*)sessionHandle;
        if (session)
            result = session->StartClient(bufferSpace);
        else
            result = false;
        instance->dispatcher.ResumeThread();
    }
    return result;
}  // end NormStartReceiver()

void NormStopReceiver(NormSessionHandle sessionHandle)
{
    
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {
        NormSession* session = (NormSession*)sessionHandle;
        if (session) session->StopClient();
        instance->dispatcher.ResumeThread();
    }
}  // end NormStopReceiver()

void NormSetSilentReceiver(NormSessionHandle sessionHandle,
                           bool              silent)
{
    NormSession* session = (NormSession*)sessionHandle;
    if (session) session->ClientSetSilent(silent);
}  // end NormSetSilentReceiver()

void NormSetDefaultUnicastNack(NormSessionHandle sessionHandle,
                               bool              unicastNacks)
{
    NormSession* session = (NormSession*)sessionHandle;
    if (session) session->ClientSetUnicastNacks(unicastNacks);
}  // end NormSetDefaultUnicastNack()

void NormNodeSetUnicastNack(NormNodeHandle   nodeHandle,
                            bool             unicastNacks)
{
     NormServerNode* node = (NormServerNode*)nodeHandle;
    if (node) node->SetUnicastNacks(unicastNacks);
}  // end NormNodeSetUnicastNack()

void NormSetDefaultNackingMode(NormSessionHandle sessionHandle,
                               NormNackingMode   nackingMode)
{
    NormSession* session = (NormSession*)sessionHandle;
    if (session) session->ClientSetDefaultNackingMode((NormObject::NackingMode)nackingMode);
}  // end NormSetDefaultNackingMode()

void NormNodeSetNackingMode(NormNodeHandle   nodeHandle,
                            NormNackingMode  nackingMode)
{
     NormServerNode* node = (NormServerNode*)nodeHandle;
    if (node) node->SetDefaultNackingMode((NormObject::NackingMode)nackingMode);
}  // end NormNodeSetNackingMode()

void NormObjectSetNackingMode(NormObjectHandle objectHandle,
                              NormNackingMode  nackingMode)
{
    NormObject* object = (NormObject*)objectHandle;
    if (object) object->SetNackingMode((NormObject::NackingMode)nackingMode);
}  // end NormObjectSetNackingMode()

void NormSetDefaultRepairBoundary(NormSessionHandle  sessionHandle,
                                  NormRepairBoundary repairBoundary)
{
    NormSession* session = (NormSession*)sessionHandle;
    if (session)
        session->ClientSetDefaultRepairBoundary((NormServerNode::RepairBoundary)repairBoundary);
}  // end NormSetDefaultRepairBoundary()

void NormNodeSetRepairBoundary(NormNodeHandle     nodeHandle,
                               NormRepairBoundary repairBoundary)
{
    NormServerNode* node = static_cast<NormServerNode*>((NormNode*)nodeHandle);
    if (node) 
        node->SetRepairBoundary((NormServerNode::RepairBoundary)repairBoundary);
}  // end NormNodeSetRepairBoundary()

bool NormStreamRead(NormObjectHandle   streamHandle,
                    char*              buffer,
                    unsigned int*      numBytes)
{
    bool result = false;
    NormInstance* instance = NormInstance::GetInstanceFromObject(streamHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {
        NormStreamObject* stream = 
            static_cast<NormStreamObject*>((NormObject*)streamHandle);
        if (stream)
            result = stream->Read(buffer, numBytes);
        instance->dispatcher.ResumeThread();
    }
    return result;
}  // end NormStreamRead()

bool NormStreamSeekMsgStart(NormObjectHandle streamHandle)
{
    bool result = false;
    NormInstance* instance = NormInstance::GetInstanceFromObject(streamHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {
        NormStreamObject* stream = 
            static_cast<NormStreamObject*>((NormObject*)streamHandle);
        unsigned int numBytes = 0;
        if (stream)
            result = stream->Read(NULL, &numBytes, true);
        instance->dispatcher.ResumeThread();
    }
    return result;
}  // end NormStreamSeekMsgStart()


unsigned long NormStreamGetReadOffset(NormObjectHandle streamHandle)
{
    NormStreamObject* stream = static_cast<NormStreamObject*>((NormObject*)streamHandle);
    return stream->GetCurrentReadOffset();
}  // end NormStreamGetReadOffset()


/** NORM Object Functions */

NormObjectType NormObjectGetType(NormObjectHandle objectHandle)
{
    if (NORM_OBJECT_INVALID != objectHandle)
        return ((NormObjectType)(((NormObject*)objectHandle)->GetType()));
    else
        return NORM_OBJECT_NONE;
}  // end NormObjectGetType()

bool NormObjectHasInfo(NormObjectHandle objectHandle)
{
    return ((NormObject*)objectHandle)->HasInfo();
}  // end NormObjectHasInfo()

unsigned short NormObjectGetInfoLength(NormObjectHandle objectHandle)
{
    return ((NormObject*)objectHandle)->GetInfoLength();
}  // end NormObjectGetInfoLength()

unsigned short NormObjectGetInfo(NormObjectHandle objectHandle,
                                 char*            buffer,
                                 unsigned short   bufferLen)
{
    unsigned short result = 0;
    if (NORM_OBJECT_INVALID != objectHandle)
    {
        NormObject* object =  (NormObject*)objectHandle;
        if (object->HaveInfo())
        {
            result = object->GetInfoLength();
            if (result < bufferLen) 
                bufferLen = result;
            if (buffer)             
                memcpy(buffer, object->GetInfo(), bufferLen);
        }  
    }
    return result;
}  // end NormObjectGetInfo()

off_t NormObjectGetSize(NormObjectHandle objectHandle)
{
    return ((NormObject*)objectHandle)->GetSize().GetOffset();
}  // end NormObjectGetSize()

void NormObjectCancel(NormObjectHandle objectHandle)
{
    if (NORM_OBJECT_INVALID != objectHandle)
    {
        NormInstance* instance = NormInstance::GetInstanceFromObject(objectHandle);
        if (instance && instance->dispatcher.SuspendThread())
        {
            NormObject* obj = (NormObject*)objectHandle;
            NormServerNode* server = obj->GetServer();
            if (server)
                server->DeleteObject(obj); 
            else
                obj->GetSession().DeleteTxObject(obj);
            instance->PurgeObjectNotifications(objectHandle);
            instance->dispatcher.ResumeThread();
        }
    }
}  // end NormObjectCancel()

void NormObjectRetain(NormObjectHandle objectHandle)
{
    if (NORM_OBJECT_INVALID != objectHandle)
    {
        NormInstance* instance = NormInstance::GetInstanceFromObject(objectHandle);
        if (instance && instance->dispatcher.SuspendThread())
        {
            NormObject* obj = (NormObject*)objectHandle;
            obj->Retain();
            instance->dispatcher.ResumeThread();
        }
    }
}  // end NormRetainObject()

void NormObjectRelease(NormObjectHandle objectHandle)
{
    if (NORM_OBJECT_INVALID != objectHandle)
    {
        NormInstance* instance = NormInstance::GetInstanceFromObject(objectHandle);
        if (instance && instance->dispatcher.SuspendThread())
        {
            NormObject* obj = (NormObject*)objectHandle;
            obj->Release();
            instance->PurgeObjectNotifications(objectHandle);
            instance->dispatcher.ResumeThread();
        }
    }
}  // end NormObjectRelease()

bool NormFileGetName(NormObjectHandle   fileHandle,
                     char*              nameBuffer,
                     unsigned int       bufferLen)
{
    bool result = false;
    if (NORM_OBJECT_INVALID != fileHandle)
    {
        // (TBD) verify "fileHandle" is a NORM_FILE ?>??
        NormFileObject* file = 
            static_cast<NormFileObject*>((NormObject*)fileHandle);
        bufferLen = (bufferLen < PATH_MAX) ? bufferLen : PATH_MAX;
        strncpy(nameBuffer, file->GetPath(), bufferLen);
        nameBuffer[bufferLen=1] = '\0';
        result = true;
    }
    return result;
}  // end NormFileGetName()

bool NormFileRename(NormObjectHandle   fileHandle,
                    const char*        fileName)
{
    bool result = false;
    NormInstance* instance = NormInstance::GetInstanceFromObject(fileHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {
        // (TBD) verify "fileHandle" is a NORM_FILE ?>??
        NormFileObject* file = 
            static_cast<NormFileObject*>((NormObject*)fileHandle);
        result = file->Rename(fileName);
        instance->dispatcher.ResumeThread();
    }
    return result;
}  // end NormFileRename()

const char*volatile NormDataAccessData(NormObjectHandle dataHandle) 
{
    NormDataObject* dataObj = 
            static_cast<NormDataObject*>((NormObject*)dataHandle);
    return dataObj->GetData();
}  // end NormDataAccessData()

char* NormDataDetachData(NormObjectHandle dataHandle) 
{
    NormDataObject* dataObj = 
            static_cast<NormDataObject*>((NormObject*)dataHandle);
    return dataObj->DetachData();
}  // end NormDataDetachData()

NormNodeHandle NormObjectGetSender(NormObjectHandle objectHandle)
{
    return (NormNodeHandle)(((NormObject*)objectHandle)->GetServer());
}  // end NormObjectGetSender()


/** NORM Node Functions */

NormNodeId NormNodeGetId(NormNodeHandle nodeHandle)
{
    NormNode* node = (NormNode*)nodeHandle;
    if (node) 
        return node->GetId();
    else
        return NORM_NODE_NONE;
}  // end NormNodeGetId()

void NormNodeRetain(NormNodeHandle nodeHandle)
{
    if (NORM_NODE_INVALID != nodeHandle)
    {
        NormInstance* instance = NormInstance::GetInstanceFromNode(nodeHandle);
        if (instance && instance->dispatcher.SuspendThread())
        {
            NormNode* node = (NormNode*)nodeHandle;
            node->Retain();
            instance->dispatcher.ResumeThread();
        }
    }
}  // end NormNodeRetain()

void NormNodeRelease(NormNodeHandle nodeHandle)
{
    if (NORM_NODE_INVALID != nodeHandle)
    {
        NormInstance* instance = NormInstance::GetInstanceFromNode(nodeHandle);
        if (instance && instance->dispatcher.SuspendThread())
        {
            NormNode* node = (NormNode*)nodeHandle;
            node->Release();
            instance->dispatcher.ResumeThread();
        }
    }
}  // end NormNodeRelease()

// (TBD) Some stream i/o performance improvement can be realized
//       if the option to make "NormWriteStream()" and 
//       "NormReadStream()" _blocking_ calls is implemented.
//        Right now, the stream read/write are non-blocking
//        so applications should use "NormGetNextEvent()" to
//        know when to read or write ... This results in the
//        underlying NORM thread to be suspended/resumed _twice_
//        per read or write ... It would be more efficient to
//        "suspend" the NORM thread while the application
//        processes all pending events and _then_ "resume" the
//        NORM thread ... an approach to this would be enable
//        the app to install an event handler callback and dispatch
//        events with a "NormDispatchEvents()" call ...
//
