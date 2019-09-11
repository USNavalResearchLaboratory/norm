#define _NORM_API_BUILD	// force 'dllexport' in "normApi.h"
#include "normApi.h"
#include "normSession.h"

#ifdef WIN32
#ifndef _WIN32_WCE
#include <io.h>  // for _mktemp()
#endif // !_WIN32_WCE
#endif // WIN32

// const defs
extern NORM_API_LINKAGE
const NormInstanceHandle NORM_INSTANCE_INVALID = ((NormInstanceHandle)0);
extern NORM_API_LINKAGE
const NormSessionHandle NORM_SESSION_INVALID = ((NormSessionHandle)0);
extern NORM_API_LINKAGE
const NormNodeHandle NORM_NODE_INVALID = ((NormNodeHandle)0);
extern NORM_API_LINKAGE
const NormNodeId NORM_NODE_NONE = ((NormNodeId)0x00000000);
extern NORM_API_LINKAGE
const NormNodeId NORM_NODE_ANY = ((NormNodeId)0xffffffff);
extern NORM_API_LINKAGE
const NormObjectHandle NORM_OBJECT_INVALID = ((NormObjectHandle)0);

extern NORM_API_LINKAGE
const NormDescriptor NORM_DESCRIPTOR_INVALID = ProtoDispatcher::INVALID_DESCRIPTOR;


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
                    class NormServerNode*   sender,
                    class NormObject*       object);
        
        bool Startup(bool priorityBoost = false);
        void Shutdown();
        
        void Pause()  // pause NORM protocol engine
        {
            dispatcher.Stop();
            Notify(NormController::EVENT_INVALID, &session_mgr, NULL, NULL, NULL);
        }
        bool Resume()
        {
            if (dispatcher.StartThread(priority_boost))
            {
                return true;
            }
            else
            {
                DMSG(0, "NormInstance::Resume() error restarting NORM thread\n");
                return false;
            }
        }
        
        bool WaitForEvent();
        bool GetNextEvent(NormEvent* theEvent);
        bool SetCacheDirectory(const char* cachePath);
        
        bool NotifyQueueIsEmpty() const 
            {return notify_queue.IsEmpty();}
        
        void PurgeObjectNotifications(NormObjectHandle objectHandle);
        
        unsigned long CountCompletedObjects(NormSession* theSession);
        
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
            if (NORM_SESSION_INVALID == sessionHandle) return ((NormInstance*)NULL);
            NormSession* session = (NormSession*)sessionHandle;
            NormInstance* theInstance = static_cast<NormInstance*>(session->GetSessionMgr().GetController());
            return theInstance;
        }
        static NormInstance* GetInstanceFromNode(NormNodeHandle nodeHandle)
        {
            if (NORM_NODE_INVALID == nodeHandle) return ((NormInstance*)NULL);
            NormSession& session = ((NormNode*)nodeHandle)->GetSession();
            return static_cast<NormInstance*>(session.GetSessionMgr().GetController());   
        }
        static NormInstance* GetInstanceFromObject(NormObjectHandle objectHandle)
        {
            if (NORM_OBJECT_INVALID == objectHandle) return ((NormInstance*)NULL);
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
                    void SetTail(Notification* n) {tail = n;}
                private:
                    Notification* head;
                    Notification* tail;
                    
            };  // end class NormInstance::Notification::Queue 
                
            private:
                Notification*           next;
        };  // end class NormInstance::Notification
        
        ProtoDispatcher             dispatcher;
        bool                        priority_boost;
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
 : priority_boost(false),
   session_mgr(static_cast<ProtoTimerMgr&>(dispatcher), 
               static_cast<ProtoSocket::Notifier&>(dispatcher)),
   previous_notification(NULL), rx_cache_path(NULL)
{
#ifdef WIN32
    notify_event = NULL;
#else
    notify_fd[0] = notify_fd[1] = -1;
#endif // if/else WIN32/UNIX
    session_mgr.SetController(static_cast<NormController*>(this));
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
        size_t length = strlen(cachePath);
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
                          class NormServerNode*   sender,
                          class NormObject*       object)
{
    // (TBD) set a limit on how many pending notifications
    // we allow to queue up (it could be large and probably
    // we could base it on how much memory space the pending
    // notifications are allowed to consume.
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
                    // We double the stream buffer to prevent unecessary data loss
                    // for our threaded API
                    if (!stream->Accept(size.LSB(), true))
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
                        size_t catMax = strlen(fileName);
                        if (catMax > PATH_MAX) 
                            catMax = 0;
                        else
                            catMax = PATH_MAX - catMax;
                        strncat(fileName, "normTempXXXXXX", catMax);
                        
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
                            DMSG(0, "NormInstance::Notify(RX_OBJECT_NEW) Warning: mkstemp() error: %s\n",
                                    GetErrorString());  
                        } 
                        if (!static_cast<NormFileObject*>(object)->Accept(fileName))
                        {
                            DMSG(0, "NormInstance::Notify(RX_OBJECT_NEW) file object accept error!\n");
                        }
                    }   
                    else
                    {
                        // we're ignoring files
                        DMSG(8, "NormInstance::Notify() warning: receive file but no cache directory set, so ignoring file\n");
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
        
        default:
            break;
    }  // end switch(event)
    
    // "Retain" any valid "object" or "sender" handles for API access
    if (NORM_OBJECT_INVALID != object)
        ((NormObject*)object)->Retain();
    else if (NORM_NODE_INVALID != sender)
        ((NormServerNode*)sender)->Retain();
    
    bool doNotify = notify_queue.IsEmpty();
    n->event.type = (NormEventType)event;
    n->event.session = session;
    n->event.sender = sender;
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
    if (NORM_OBJECT_INVALID == objectHandle) return;
    Notification* prev = NULL;
    Notification* next = notify_queue.GetHead();
    while (next)
    {
        if (objectHandle == next->event.object)
        {
            // "Release" the previously-retained object handle
            ((NormObject*)objectHandle)->Release();
            // Remove this notification from queue and return to pool
            Notification* current = next;
            next = next->GetNext();
            if (NULL != prev) 
                prev->Append(next);
            else
                notify_queue.RemoveHead();
            if (NULL == next) notify_queue.SetTail(prev);
            notify_pool.Append(current);
        }
        else
        {
            prev = next;
            next = next->GetNext();
        }
    }
    // (TBD) if we put these in the pool, should we check that the queue has been emptied
    // and possible reset event/fd 
}  // end NormInstance::PurgeObjectNotifications()

// NormInstance::dispatcher MUST be suspended _before_ calling this
bool NormInstance::GetNextEvent(NormEvent* theEvent)
{
    // First, do any garbage collection for "previous_notification"
    if (NULL != previous_notification)
    {
        // "Release" any previously-retained object or node handle
        if (NORM_OBJECT_INVALID != previous_notification->event.object)
            ((NormObject*)(previous_notification->event.object))->Release();
        else if (NORM_NODE_INVALID != previous_notification->event.sender)
            ((NormServerNode*)(previous_notification->event.sender))->Release();
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
               char byte[32];
               while (read(notify_fd[0], byte, 32) > 0);  // TBD - error check
#endif // if/else WIN32/UNIX
        }
        switch (n->event.type)
        {
            case NORM_EVENT_INVALID:
                if (!notify_queue.IsEmpty())
                {
                    // Discard this invalid event and get next one
                    notify_pool.Append(n);
                    continue;
                }
                break;
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
    if (!dispatcher.IsThreaded()) 
    {
        DMSG(0, "NormInstance::WaitForEvent() warning: NORM thread not running!\n");
        return false;
    }
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


bool NormInstance::Startup(bool priorityBoost)
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
    priority_boost = priorityBoost;
    return dispatcher.StartThread(priorityBoost);
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
        // Release any previously-retained object or node handles
        if (NORM_OBJECT_INVALID != previous_notification->event.object)
            ((NormObject*)(previous_notification->event.object))->Release();
        else if (NORM_NODE_INVALID != previous_notification->event.sender)
            ((NormServerNode*)(previous_notification->event.sender))->Release();
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
            default:
                break;
        }   
        if (NORM_OBJECT_INVALID != n->event.object)
            ((NormObject*)(n->event.object))->Release();
        else if (NORM_NODE_INVALID != n->event.sender)
            ((NormServerNode*)(n->event.sender))->Release();
        delete n;        
    }
    notify_pool.Destroy();
}  // end NormInstance::Shutdown()

unsigned long NormInstance::CountCompletedObjects(NormSession* session)
{
	unsigned long result = 0UL;
	Notification* n = notify_queue.GetHead();
	while (NULL != n) 
    {
		if ((session == n->event.session) &&
			(NORM_RX_OBJECT_COMPLETED == n->event.type))
        {
			result ++;
        }
		n = n->GetNext();
	}
	return result;
} // end NormInstance::CountCompletedObjects()

//////////////////////////////////////////////////////////////////////////
// NORM API FUNCTION IMPLEMENTATIONS
//

/** NORM API Initialization */

NORM_API_LINKAGE
NormInstanceHandle NormCreateInstance(bool priorityBoost)
{
    NormInstance* normInstance = new NormInstance;
    if (normInstance)
    {
        if (normInstance->Startup(priorityBoost))
            return ((NormInstanceHandle)normInstance); 
        else
            delete normInstance;
    }
    return NORM_INSTANCE_INVALID;  
}  // end NormCreateInstance()

NORM_API_LINKAGE
void NormDestroyInstance(NormInstanceHandle instanceHandle)
{
    delete ((NormInstance*)instanceHandle);   
}  // end NormDestroyInstance()

NORM_API_LINKAGE
bool NormRestartInstance(NormInstanceHandle instanceHandle)
{
    NormInstance* instance = (NormInstance*)instanceHandle;
    if (instance)
    {
        if (instance->dispatcher.IsThreaded())
            instance->Pause();
        return instance->Resume();
    }
    return false;  // invalid instanceHandle
}  // end NormStartInstance()


NORM_API_LINKAGE
void NormStopInstance(NormInstanceHandle instanceHandle)
{
    NormInstance* instance = (NormInstance*)instanceHandle;
    if (instance) instance->Pause();  // stops NORM protocol thread
}  // end NormStopInstance()


NORM_API_LINKAGE
bool NormSetCacheDirectory(NormInstanceHandle instanceHandle, 
                           const char*        cachePath)
{
    NormInstance* instance = (NormInstance*)instanceHandle;
    if (instance)
        return instance->SetCacheDirectory(cachePath);
    else
        return false;
} // end NormSetCacheDirectory()

NORM_API_LINKAGE
NormDescriptor NormGetDescriptor(NormInstanceHandle instanceHandle)
{
    NormInstance* instance = (NormInstance*)instanceHandle;
    if (instance)
        return (instance->GetDescriptor());
    else
        return NORM_DESCRIPTOR_INVALID;
}  // end NormGetDescriptor()

// (TBD) add a timeout option to this?
NORM_API_LINKAGE
bool NormGetNextEvent(NormInstanceHandle instanceHandle, NormEvent* theEvent)
{
    bool result = false;
    NormInstance* instance = (NormInstance*)instanceHandle;
    if (instance)
    {
        if (instance->NotifyQueueIsEmpty()) //(TBD) perform this check???
        {
            if (!instance->WaitForEvent())
            {
                return false;
            }
        }
        if (instance->dispatcher.SuspendThread())
        {
            result = instance->GetNextEvent(theEvent);
            instance->dispatcher.ResumeThread();
        }
    }
    return result;  
}  // end NormGetNextEvent()


/** NORM Session Creation and Control Functions */

NORM_API_LINKAGE
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

NORM_API_LINKAGE
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

NORM_API_LINKAGE
void NormSetUserData(NormSessionHandle sessionHandle, const void* userData)
{
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (instance)
    {
        if (instance->dispatcher.SuspendThread())
        {    
            NormSession* session = (NormSession*)sessionHandle;
            if (session) session->SetUserData(userData);
            instance->dispatcher.ResumeThread();
        }
    }
}  // end NormSetUserData()

NORM_API_LINKAGE
const void* NormGetUserData(NormSessionHandle sessionHandle)
{
    const void* userData = NULL;
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (instance)
    {
        if (instance->dispatcher.SuspendThread())
        {    
            NormSession* session = (NormSession*)sessionHandle;
            if (session) 
                userData = session->GetUserData();
            instance->dispatcher.ResumeThread();
        }
    }
    return userData;
}  // end NormGetUserData()

NORM_API_LINKAGE
NormNodeId NormGetLocalNodeId(NormSessionHandle sessionHandle)
{
    NormSession* session = (NormSession*)sessionHandle;
    if (NULL != session)
        return session->LocalNodeId();
    else
        return NORM_NODE_NONE;      
}  // end NormGetLocalNodeId()

NORM_API_LINKAGE
void NormSetTxPort(NormSessionHandle sessionHandle,
                   unsigned short    txPort)
{
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (NULL != instance)
    {
        if (instance->dispatcher.SuspendThread())
        {    
            NormSession* session = (NormSession*)sessionHandle;
            if (session) session->SetTxPort(txPort);
            instance->dispatcher.ResumeThread();
        }
    } 
}  // end NormSetTxPort()

NORM_API_LINKAGE
void NormSetRxPortReuse(NormSessionHandle sessionHandle,
                        bool              enable,
                        bool              bindToSessionAddress)
{
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (NULL != instance)
    {
        if (instance->dispatcher.SuspendThread())
        {    
            NormSession* session = (NormSession*)sessionHandle;
            if (session) session->SetRxPortReuse(enable, bindToSessionAddress);
            instance->dispatcher.ResumeThread();
        }
    } 
}  // end NormSetTxPort()

NORM_API_LINKAGE
bool NormSetMulticastInterface(NormSessionHandle sessionHandle,
                               const char*       interfaceName)
{
    bool result = false;
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (NULL != instance)
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

NORM_API_LINKAGE
bool NormSetTTL(NormSessionHandle sessionHandle,
                unsigned char     ttl)
{
    bool result = false;
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (NULL != instance)
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

NORM_API_LINKAGE
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
                result = session->SetTOS(tos);
            instance->dispatcher.ResumeThread();
        }
    }
    return result;     
}  // end NormSetTOS()

/** Special test/debug support functions */

NORM_API_LINKAGE
bool NormSetLoopback(NormSessionHandle sessionHandle, bool state)
{
    NormSession* session = (NormSession*)sessionHandle;
    if (session)
        return session->SetLoopback(state);
    else
        return false;  
}  // end NormSetLoopback()

NORM_API_LINKAGE
void NormSetMessageTrace(NormSessionHandle sessionHandle, bool state)
{
    NormSession* session = (NormSession*)sessionHandle;
    if (session) session->SetTrace(state);
}  // end NormSetMessageTrace()

NORM_API_LINKAGE
void NormSetTxLoss(NormSessionHandle sessionHandle, double percent)
{
    NormSession* session = (NormSession*)sessionHandle;
    if (session) session->SetTxLoss(percent);
}  // end NormSetTxLoss()

NORM_API_LINKAGE
void NormSetRxLoss(NormSessionHandle sessionHandle, double percent)
{
    NormSession* session = (NormSession*)sessionHandle;
    if (session) session->SetRxLoss(percent);
}  // end NormSetRxLoss()


/** NORM Sender Functions */

NORM_API_LINKAGE
bool NormStartSender(NormSessionHandle  sessionHandle,
                     NormSessionId      sessionId,
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
            result = session->StartServer(sessionId, bufferSpace, segmentSize, numData, numParity);
        else
            result = false;
        instance->dispatcher.ResumeThread();
    }
    return result;
}  // end NormStartSender()

NORM_API_LINKAGE
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


NORM_API_LINKAGE
double NormGetTransmitRate(NormSessionHandle sessionHandle)
{
    if (NORM_SESSION_INVALID != sessionHandle)
        return (((NormSession*)sessionHandle)->GetTxRate());
    else
        return -1.0;
}  // end NormGetTransmitRate()

NORM_API_LINKAGE
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

NORM_API_LINKAGE
bool NormSetTxSocketBuffer(NormSessionHandle sessionHandle, 
                           unsigned int      bufferSize)
{
    bool result = false;
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {
        NormSession* session = (NormSession*)sessionHandle;
        if (session) 
            result = session->SetTxSocketBuffer(bufferSize);
        instance->dispatcher.ResumeThread();
    }
    return result;
}  // end NormSetTxSocketBuffer()

NORM_API_LINKAGE
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

NORM_API_LINKAGE
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

NORM_API_LINKAGE
void NormSetTransmitCacheBounds(NormSessionHandle sessionHandle,
                                NormSize          sizeMax,
                                unsigned long     countMin,
                                unsigned long     countMax)
{
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {
        NormSession* session = (NormSession*)sessionHandle;
        NormObjectSize theSize(sizeMax);
        if (session) session->SetTxCacheBounds(theSize, countMin, countMax);
        instance->dispatcher.ResumeThread();
    }
}  // end NormSetTransmitCacheBounds()

NORM_API_LINKAGE
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

NORM_API_LINKAGE
void NormSetGrttEstimate(NormSessionHandle sessionHandle,
                         double            grttEstimate)
{
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {
        NormSession* session = (NormSession*)sessionHandle;
        if (session) session->ServerSetGrtt(grttEstimate);
        instance->dispatcher.ResumeThread();
    }
}  // end NormSetGrttEstimate()

NORM_API_LINKAGE
double NormGetGrttEstimate(NormSessionHandle sessionHandle)
{
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {
        NormSession* session = (NormSession*)sessionHandle;
        if (session) session->ServerGrtt();
        instance->dispatcher.ResumeThread();
    }
    
    if (NORM_SESSION_INVALID != sessionHandle)
        return (((NormSession*)sessionHandle)->ServerGrtt());
    else
        return -1.0;
}  // end NormGetGrttEstimate()

NORM_API_LINKAGE
void NormSetGrttMax(NormSessionHandle sessionHandle,
                    double            grttMax)
{
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {
        NormSession* session = (NormSession*)sessionHandle;
        if (session) session->SetGrttMax(grttMax);
        instance->dispatcher.ResumeThread();
    }
}  // end NormSetGrttMax()

NORM_API_LINKAGE
void NormSetGrttProbingMode(NormSessionHandle sessionHandle,
                            NormProbingMode   probingMode)
{
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {
        NormSession* session = (NormSession*)sessionHandle;
        session->SetGrttProbingMode((NormSession::ProbingMode)probingMode);
        instance->dispatcher.ResumeThread();
    }
}  // end NormSetGrttProbingMode()

NORM_API_LINKAGE
void NormSetGrttProbingInterval(NormSessionHandle sessionHandle,
                                double            intervalMin,
                                double            intervalMax)
{
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {
        NormSession* session = (NormSession*)sessionHandle;
        if (session) session->SetGrttProbingInterval(intervalMin, intervalMax);
        instance->dispatcher.ResumeThread();
    }
}  // end NormSetGrttProbingInterval()

NORM_API_LINKAGE
void NormSetBackoffFactor(NormSessionHandle sessionHandle,
                          double            backoffFactor)
{
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {
        NormSession* session = (NormSession*)sessionHandle;
        if (backoffFactor >= 0.0)
            session->SetBackoffFactor(backoffFactor);
        instance->dispatcher.ResumeThread();
    }
}  // end NormSetBackoffFactor()

NORM_API_LINKAGE
void NormSetGroupSize(NormSessionHandle sessionHandle,
                      unsigned int      groupSize)
{
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {
        NormSession* session = (NormSession*)sessionHandle;
        session->ServerSetGroupSize((double)groupSize);
        instance->dispatcher.ResumeThread();
    }
}  // end NormSetGroupSize()

NORM_API_LINKAGE
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
            NormObject* obj = 
                static_cast<NormObject*>(session->QueueTxFile(fileName, infoPtr, infoLen));
            if (obj) objectHandle = (NormObjectHandle)(obj);
        }
        instance->dispatcher.ResumeThread();
    }
    return objectHandle;
}  // end NormFileEnqueue()

NORM_API_LINKAGE
NormObjectHandle NormDataEnqueue(NormSessionHandle  sessionHandle,
                                 const char*        dataPtr,
                                 unsigned long      dataLen,
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
            NormObject* obj = 
                static_cast<NormObject*>(session->QueueTxData(dataPtr, dataLen, infoPtr, infoLen));
            if (obj) objectHandle = (NormObjectHandle)obj;
        }
        instance->dispatcher.ResumeThread();
    }
    return objectHandle;
}  // end NormDataEnqueue()


NORM_API_LINKAGE 
bool NormRequeueObject(NormSessionHandle sessionHandle, NormObjectHandle objectHandle)
{
    bool result = false;
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {
        NormSession* session = (NormSession*)sessionHandle;
        if (session)
        {
            if (NORM_OBJECT_INVALID != objectHandle)
                result = session->RequeueTxObject((NormObject*)objectHandle);
        }
        instance->dispatcher.ResumeThread();
    }
    return result;
}  // end NormRequeueObject()

NORM_API_LINKAGE
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
                static_cast<NormObject*>(session->QueueTxStream(bufferSize, true));
            if (obj) objectHandle = (NormObjectHandle)obj;
        }
        instance->dispatcher.ResumeThread();
    }
    return objectHandle;
}  // end NormStreamOpen()

NORM_API_LINKAGE
void NormStreamClose(NormObjectHandle streamHandle, bool graceful)
{
    NormStreamObject* stream =
        static_cast<NormStreamObject*>((NormObject*)streamHandle);
    if (NULL != stream)
    {
        if (graceful && (NULL == stream->GetServer()))
        {
            NormInstance* instance = NormInstance::GetInstanceFromObject(streamHandle);
            if (instance && instance->dispatcher.SuspendThread())
            {
                stream->Close(true);  // graceful stream closure
                instance->dispatcher.ResumeThread();
            }  
        }
        else
        {
            NormObjectCancel(streamHandle);
        }
    }
}  // end NormStreamClose()

NORM_API_LINKAGE
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

NORM_API_LINKAGE
void NormStreamFlush(NormObjectHandle streamHandle, 
                     bool             eom,
                     NormFlushMode    flushMode)
{
    NormInstance* instance = NormInstance::GetInstanceFromObject(streamHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {
        NormStreamObject* stream = 
            static_cast<NormStreamObject*>((NormObject*)streamHandle);
        if (stream) 
        {
            NormStreamObject::FlushMode saveFlushMode = stream->GetFlushMode();
            stream->SetFlushMode((NormStreamObject::FlushMode)flushMode);
            stream->Flush(eom);
            stream->SetFlushMode(saveFlushMode);
        }
        instance->dispatcher.ResumeThread();
    }
}  // end NormStreamFlush()

NORM_API_LINKAGE
void NormStreamSetAutoFlush(NormObjectHandle    streamHandle,
                            NormFlushMode       flushMode)
{
    NormStreamObject* stream = 
        static_cast<NormStreamObject*>((NormObject*)streamHandle);
    if (stream)
        stream->SetFlushMode((NormStreamObject::FlushMode)flushMode);
}  // end NormStreamSetAutoFlush()

NORM_API_LINKAGE
void NormStreamSetPushEnable(NormObjectHandle streamHandle, bool state)
{
    NormStreamObject* stream = 
        static_cast<NormStreamObject*>((NormObject*)streamHandle);
    if (stream) stream->SetPushMode(state);
}  // end NormStreamSetPushEnable()

NORM_API_LINKAGE
bool NormStreamHasVacancy(NormObjectHandle streamHandle)
{
    bool result = false;
    NormInstance* instance = NormInstance::GetInstanceFromObject(streamHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {
        NormStreamObject* stream = 
            static_cast<NormStreamObject*>((NormObject*)streamHandle);
        if (NULL != stream)
            result = stream->HasVacancy();
        instance->dispatcher.ResumeThread();
    }
    return result;
}  // end NormStreamHasVacancy()

NORM_API_LINKAGE
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

NORM_API_LINKAGE
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

NORM_API_LINKAGE
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

NORM_API_LINKAGE
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

NORM_API_LINKAGE
NormAckingStatus NormGetAckingStatus(NormSessionHandle  sessionHandle,
                                     NormNodeId         nodeId)
{
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {
        NormSession* session = (NormSession*)sessionHandle;
        NormAckingStatus status = 
            (NormAckingStatus)session->ServerGetAckingStatus(nodeId);
        instance->dispatcher.ResumeThread();
        return status;
    }
    else
    {
        return NORM_ACK_INVALID;
    }
}  // end NormGetAckingNodeStatus()

/** NORM Receiver Functions */

NORM_API_LINKAGE
bool NormStartReceiver(NormSessionHandle  sessionHandle,
                       unsigned long      bufferSpace)
{
    bool result = false;
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {
        NormSession* session = (NormSession*)sessionHandle;
        result = session->StartClient(bufferSpace);
        instance->dispatcher.ResumeThread();
    }
    return result;
}  // end NormStartReceiver()

NORM_API_LINKAGE
void NormStopReceiver(NormSessionHandle sessionHandle)
{
    
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {
        NormSession* session = (NormSession*)sessionHandle;
        session->StopClient();
        instance->dispatcher.ResumeThread();
    }
}  // end NormStopReceiver()

NORM_API_LINKAGE
bool NormSetRxSocketBuffer(NormSessionHandle sessionHandle, 
                           unsigned int      bufferSize)
{
    bool result = false;
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {
        NormSession* session = (NormSession*)sessionHandle;
        if (session) 
            result = session->SetRxSocketBuffer(bufferSize);
        instance->dispatcher.ResumeThread();
    }
    return result;
}  // end NormSetRxSocketBuffer()

NORM_API_LINKAGE
void NormSetSilentReceiver(NormSessionHandle sessionHandle,
                           bool              silent,
                           bool              lowDelay)
{
    NormSession* session = (NormSession*)sessionHandle;
    if (session) 
    {
        session->ClientSetSilent(silent);
        session->ReceiverSetLowDelay(lowDelay);
    }
}  // end NormSetSilentReceiver()

NORM_API_LINKAGE
void NormSetDefaultUnicastNack(NormSessionHandle sessionHandle,
                               bool              unicastNacks)
{
    NormSession* session = (NormSession*)sessionHandle;
    if (session) session->ClientSetUnicastNacks(unicastNacks);
}  // end NormSetDefaultUnicastNack()

NORM_API_LINKAGE
void NormNodeSetUnicastNack(NormNodeHandle   nodeHandle,
                            bool             unicastNacks)
{
    
    NormServerNode* node = (NormServerNode*)nodeHandle;
    if (node) node->SetUnicastNacks(unicastNacks);
}  // end NormNodeSetUnicastNack()

NORM_API_LINKAGE
void NormSetDefaultNackingMode(NormSessionHandle sessionHandle,
                               NormNackingMode   nackingMode)
{
    NormSession* session = (NormSession*)sessionHandle;
    if (session) session->ClientSetDefaultNackingMode((NormObject::NackingMode)nackingMode);
}  // end NormSetDefaultNackingMode()

NORM_API_LINKAGE
void NormNodeSetNackingMode(NormNodeHandle   nodeHandle,
                            NormNackingMode  nackingMode)
{
     NormServerNode* node = (NormServerNode*)nodeHandle;
    if (node) node->SetDefaultNackingMode((NormObject::NackingMode)nackingMode);
}  // end NormNodeSetNackingMode()

NORM_API_LINKAGE
void NormObjectSetNackingMode(NormObjectHandle objectHandle,
                              NormNackingMode  nackingMode)
{
    NormObject* object = (NormObject*)objectHandle;
    if (object) object->SetNackingMode((NormObject::NackingMode)nackingMode);
}  // end NormObjectSetNackingMode()

NORM_API_LINKAGE
void NormSetDefaultRepairBoundary(NormSessionHandle  sessionHandle,
                                  NormRepairBoundary repairBoundary)
{
    NormSession* session = (NormSession*)sessionHandle;
    if (session)
        session->ClientSetDefaultRepairBoundary((NormServerNode::RepairBoundary)repairBoundary);
}  // end NormSetDefaultRepairBoundary()

NORM_API_LINKAGE
void NormNodeSetRepairBoundary(NormNodeHandle     nodeHandle,
                               NormRepairBoundary repairBoundary)
{
    NormServerNode* node = static_cast<NormServerNode*>((NormNode*)nodeHandle);
    if (node) 
        node->SetRepairBoundary((NormServerNode::RepairBoundary)repairBoundary);
}  // end NormNodeSetRepairBoundary()

NORM_API_LINKAGE
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
        result = stream->Read(buffer, numBytes);
        instance->dispatcher.ResumeThread();
    }
    return result;
}  // end NormStreamRead()

NORM_API_LINKAGE
bool NormStreamSeekMsgStart(NormObjectHandle streamHandle)
{
    bool result = false;
    NormInstance* instance = NormInstance::GetInstanceFromObject(streamHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {
        NormStreamObject* stream = 
            static_cast<NormStreamObject*>((NormObject*)streamHandle);
        unsigned int numBytes = 0;
        result = stream->Read(NULL, &numBytes, true);
        instance->dispatcher.ResumeThread();
    }
    return result;
}  // end NormStreamSeekMsgStart()


NORM_API_LINKAGE
unsigned long NormStreamGetReadOffset(NormObjectHandle streamHandle)
{
    NormStreamObject* stream = static_cast<NormStreamObject*>((NormObject*)streamHandle);
    if (stream)
        return stream->GetCurrentReadOffset();
    else
        return 0;
}  // end NormStreamGetReadOffset()


/** NORM Object Functions */

NORM_API_LINKAGE
NormObjectType NormObjectGetType(NormObjectHandle objectHandle)
{
    if (NORM_OBJECT_INVALID != objectHandle)
        return ((NormObjectType)(((NormObject*)objectHandle)->GetType()));
    else
        return NORM_OBJECT_NONE;
}  // end NormObjectGetType()

NORM_API_LINKAGE
bool NormObjectHasInfo(NormObjectHandle objectHandle)
{
    return ((NormObject*)objectHandle)->HasInfo();
}  // end NormObjectHasInfo()

NORM_API_LINKAGE
unsigned short NormObjectGetInfoLength(NormObjectHandle objectHandle)
{
    return ((NormObject*)objectHandle)->GetInfoLength();
}  // end NormObjectGetInfoLength()

NORM_API_LINKAGE
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

NORM_API_LINKAGE
NormSize NormObjectGetSize(NormObjectHandle objectHandle)
{
    return ((NormSize)((NormObject*)objectHandle)->GetSize().GetOffset());
}  // end NormObjectGetSize()

NORM_API_LINKAGE
NormSize NormObjectGetBytesPending(NormObjectHandle objectHandle)
{
    NormSize bytesPending = 0;
    if (NORM_OBJECT_INVALID != objectHandle)
    {
        NormInstance* instance = NormInstance::GetInstanceFromObject(objectHandle);
        if (instance && instance->dispatcher.SuspendThread())
        {
            bytesPending = ((NormSize)((NormObject*)objectHandle)->GetBytesPending().GetOffset());
            instance->dispatcher.ResumeThread();
        }
    }
    return bytesPending;
}  // end NormObjectGetBytesPending()

NORM_API_LINKAGE
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

NORM_API_LINKAGE
void NormObjectRetain(NormObjectHandle objectHandle)
{
    if (NORM_OBJECT_INVALID != objectHandle)
    {
        NormInstance* instance = NormInstance::GetInstanceFromObject(objectHandle);
        if (instance && instance->dispatcher.SuspendThread())
        {
            ((NormObject*)objectHandle)->Retain();
            instance->dispatcher.ResumeThread();
        }
    }
}  // end NormRetainObject()

NORM_API_LINKAGE
void NormObjectRelease(NormObjectHandle objectHandle)
{
    // (TBD) we could maintain separate "app_retain" and "interval_retain"
    // counts to prevent bad apps from messing up NORM interval code ???
    if (NORM_OBJECT_INVALID != objectHandle)
    {
        NormInstance* instance = NormInstance::GetInstanceFromObject(objectHandle);
        if (instance && instance->dispatcher.SuspendThread())
        {
            ((NormObject*)objectHandle)->Release();
            instance->dispatcher.ResumeThread();
        }
    }
}  // end NormObjectRelease()

NORM_API_LINKAGE
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
        nameBuffer[bufferLen] = '\0';
        result = true;
    }
    return result;
}  // end NormFileGetName()

NORM_API_LINKAGE
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

NORM_API_LINKAGE
const char*volatile NormDataAccessData(NormObjectHandle dataHandle) 
{
    NormDataObject* dataObj = 
            static_cast<NormDataObject*>((NormObject*)dataHandle);
    return dataObj->GetData();
}  // end NormDataAccessData()

NORM_API_LINKAGE
char* NormDataDetachData(NormObjectHandle dataHandle) 
{
    NormDataObject* dataObj = 
            static_cast<NormDataObject*>((NormObject*)dataHandle);
    return dataObj->DetachData();
}  // end NormDataDetachData()

NORM_API_LINKAGE
NormNodeHandle NormObjectGetSender(NormObjectHandle objectHandle)
{
    return (NormNodeHandle)(((NormObject*)objectHandle)->GetServer());
}  // end NormObjectGetSender()


/** NORM Node Functions */

NORM_API_LINKAGE
NormNodeId NormNodeGetId(NormNodeHandle nodeHandle)
{
    NormNode* node = (NormNode*)nodeHandle;
    if (node) 
        return node->GetId();
    else
        return NORM_NODE_NONE;
}  // end NormNodeGetId()

NORM_API_LINKAGE
bool NormNodeGetAddress(NormNodeHandle  nodeHandle,
                        char*           addrBuffer, 
                        unsigned int*   bufferLen,
                        unsigned short* port)
{
    bool result = false;
    if (NORM_NODE_INVALID != nodeHandle)
    {
        NormInstance* instance = NormInstance::GetInstanceFromNode(nodeHandle);
        if (instance && instance->dispatcher.SuspendThread())
        {
            NormNode* node = (NormNode*)nodeHandle;
            const ProtoAddress& nodeAddr = node->GetAddress();
            unsigned int addrLen = nodeAddr.GetLength();
            if (addrBuffer && bufferLen && (addrLen <= *bufferLen))
            {
                memcpy(addrBuffer, nodeAddr.GetRawHostAddress(), addrLen);
                result = true;
            } 
            else if (NULL == addrBuffer)
            {
                result = true; // just a query for addrLen and/or port
            }      
            if (bufferLen) *bufferLen = addrLen;
            if (port) *port = nodeAddr.GetPort();
            instance->dispatcher.ResumeThread();  
        }
    }
    return result;
}  // end NormNodeGetId()

NORM_API_LINKAGE
double NormNodeGetGrtt(NormNodeHandle nodeHandle)
{
    NormServerNode* node = (NormServerNode*)nodeHandle;
    if (NULL != node)
        return node->GetGrttEstimate();
    else
        return -1.0;
}  // end NormNodeGetGrtt()

NORM_API_LINKAGE
void NormNodeRetain(NormNodeHandle nodeHandle)
{
    if (NORM_NODE_INVALID != nodeHandle)
    {
        NormInstance* instance = NormInstance::GetInstanceFromNode(nodeHandle);
        if (instance && instance->dispatcher.SuspendThread())
        {
            ((NormServerNode*)nodeHandle)->Retain();
            instance->dispatcher.ResumeThread();
        }
    }
}  // end NormNodeRetain()

NORM_API_LINKAGE
void NormNodeRelease(NormNodeHandle nodeHandle)
{
    if (NORM_NODE_INVALID != nodeHandle)
    {
        NormInstance* instance = NormInstance::GetInstanceFromNode(nodeHandle);
        if (instance && instance->dispatcher.SuspendThread())
        {
            ((NormServerNode*)nodeHandle)->Release();
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
//        (Update: I've improved the way the NORM thread suspend/resume
//         is done so that this current approach is not very costly)
//

NORM_API_LINKAGE
unsigned long NormCountCompletedObjects(NormSessionHandle sessionHandle)
{
    unsigned long result = 0;
    NormSession* session = (NormSession*)sessionHandle;
	NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {
        result = instance->CountCompletedObjects(session);
        instance->dispatcher.ResumeThread();
    }
	return result;
}  // end long NormCountCompletedObjects()
