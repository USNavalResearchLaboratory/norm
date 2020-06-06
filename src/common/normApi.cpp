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
                    class NormNode*         node,
                    class NormObject*       object);
        
        bool Startup(bool priorityBoost = false);
        void Shutdown();
        
        void Stop()  // pause NORM protocol engine
        {
            dispatcher.Stop();
            Notify(NormController::EVENT_INVALID, &session_mgr, NULL, NULL, NULL);
        }
        bool Start()
        {
            if (dispatcher.StartThread(priority_boost))
            {
                return true;
            }
            else
            {
                PLOG(PL_FATAL, "NormInstance::Resume() error restarting NORM thread\n");
                return false;
            }
        }
        
        bool WaitForEvent();
        bool GetNextEvent(NormEvent* theEvent);
        bool SetCacheDirectory(const char* cachePath);
        
        void SetAllocationFunctions(NormAllocFunctionHandle allocFunc, 
                                    NormFreeFunctionHandle  freeFunc)
        {
            data_alloc_func = allocFunc;
            session_mgr.SetDataFreeFunction(freeFunc);
        }
        
        void ReleasePreviousEvent();
        
        bool NotifyQueueIsEmpty() const 
            {return notify_queue.IsEmpty();}
        
        void PurgeSessionNotifications(NormSessionHandle sessionHandle);
        void PurgeNodeNotifications(NormNodeHandle nodeHandle);
        void PurgeObjectNotifications(NormObjectHandle objectHandle);
        void PurgeNotifications(NormSessionHandle sessionHandle, NormEventType eventType);
        
        UINT32 CountCompletedObjects(NormSession* theSession);
        
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
        
        class Notification : public ProtoList::Item
        {
            public:
                NormEvent   event;
            
            class Queue : public ProtoListTemplate<Notification> {};
        };  // end class NormInstance::Notification
        
        ProtoDispatcher             dispatcher;
        bool                        priority_boost;
        NormSessionMgr              session_mgr;   
        NormAllocFunctionHandle     data_alloc_func;
        
    private:
        void ResetNotificationEvent()
        {
#ifdef WIN32
            if (0 == ResetEvent(notify_event))
                PLOG(PL_ERROR, "NormInstance::ResetNotificationEvent() ResetEvent error: %s\n", GetErrorString());
#else
           char byte[32];
           while (read(notify_fd[0], byte, 32) > 0);  // TBD - error check
#endif // if/else WIN32/UNIX
        }  
         
        Notification::Queue         notify_pool;
        Notification::Queue         notify_queue; 
        Notification*               previous_notification;
        
        const char*                 rx_cache_path;
        
#ifdef WIN32
        HANDLE                      notify_event;
#else
        int                         notify_fd[2];  // TBD - use eventfd on Linux for this, may EVT_USER on MacOS
#endif // if/else WIN32/UNIX
};  // end class NormInstance


////////////////////////////////////////////////////
// NormInstance implementation
NormInstance::NormInstance()
 : priority_boost(false),
   session_mgr(static_cast<ProtoTimerMgr&>(dispatcher), 
               static_cast<ProtoSocket::Notifier&>(dispatcher),
               static_cast<ProtoChannel::Notifier*>(&dispatcher)),
   data_alloc_func(NULL), previous_notification(NULL), rx_cache_path(NULL)
{
#ifdef WIN32
    notify_event = NULL;
#else
    notify_fd[0] = notify_fd[1] = -1;
#endif // if/else WIN32/UNIX
    dispatcher.SetUserData(&session_mgr);  // for debugging
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
            PLOG(PL_ERROR, "NormInstance::SetCacheDirectory() new pathStorage error: %s\n",
                    GetErrorString());
        }
        dispatcher.ResumeThread();
    }
    return result;
}  // end NormInstance::SetCacheDirectory()

void NormInstance::Notify(NormController::Event   event,
                          class NormSessionMgr*   sessionMgr,
                          class NormSession*      session,
                          class NormNode*         node,
                          class NormObject*       object)
{
    switch (event)
    {
        case SEND_OK:
            // Purge any pending NORM_SEND_ERROR notifications for session
            PurgeNotifications(session, NORM_SEND_ERROR);
            return;
        default:
            break;
    }
    
    // (TBD) set a limit on how many pending notifications
    // we allow to queue up (it could be large and probably
    // we could base it on how much memory space the pending
    // notifications are allowed to consume.
    Notification* next = notify_pool.RemoveHead();
    if (NULL == next)
    {
        if (NULL == (next = new Notification))
        {
            PLOG(PL_FATAL, "NormInstance::Notify() new Notification error: %s\n", GetErrorString());
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
                    // (TBD) implement silent_receiver accept differently
                    NormObjectSize size = stream->GetSize();
                    // We double the stream buffer to prevent unecessary data loss
                    // for our threaded API
                    if (!stream->Accept(size.LSB(), true))
                    {
                        PLOG(PL_FATAL, "NormInstance::Notify() stream accept error\n");
                        notify_pool.Append(*next);
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
                            PLOG(PL_ERROR, "NormInstance::Notify(RX_OBJECT_NEW) Warning: mkstemp() error: %s\n",
                                    GetErrorString());  
                        } 
                        if (!static_cast<NormFileObject*>(object)->Accept(fileName))
                        {
                            PLOG(PL_ERROR, "NormInstance::Notify(RX_OBJECT_NEW) file object accept error!\n");
                        }
                    }   
                    else
                    {
                        // we're ignoring files
                        PLOG(PL_DETAIL, "NormInstance::Notify() warning: receive file but no cache directory set, so ignoring file\n");
                        notify_pool.Append(*next);
                        return;    
                    }                
                    break;
                }
                case NormObject::DATA:
                {
                    NormDataObject* dataObj = static_cast<NormDataObject*>(object);
                    unsigned int dataLen = (unsigned int)(object->GetSize().GetOffset());
                    char* dataPtr = (NULL != data_alloc_func) ? data_alloc_func(dataLen) : new char[dataLen];
                    if (NULL == dataPtr)
                    {
                        PLOG(PL_FATAL, "NormInstance::Notify(RX_OBJECT_NEW) new dataPtr error: %s\n",
                                       GetErrorString());
                        notify_pool.Append(*next);
                        return;   
                    }
                    // Note that the "true" parameter means the
                    // NORM protocol engine will free the allocated
                    // data on object deletion, so the app should
                    // use NormDataDetachData() to keep the received
                    // data (or copy it before the data object is deleted)
                    if (!dataObj->Accept(dataPtr, dataLen, true))
                    {
                        PLOG(PL_FATAL, "NormInstance::Notify() data object accept error\n");
                        notify_pool.Append(*next);
                        return;   
                    }
                    break;
                }
                default:
                    // This shouldn't occur
                    notify_pool.Append(*next);
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
    else if (NORM_NODE_INVALID != node)
        ((NormNode*)node)->Retain();
    
    bool doNotify = notify_queue.IsEmpty();
    next->event.type = (NormEventType)event;
    next->event.session = session;
    next->event.sender = node;
    next->event.object = object;
    notify_queue.Append(*next);
    
    if (doNotify)
    {
#ifdef WIN32
        if (0 == SetEvent(notify_event))
        {
            PLOG(PL_ERROR, "NormInstance::Notify() SetEvent() error: %s\n",
                           GetErrorString());
        }
#else
        char byte = 0;
        while (1 != write(notify_fd[1], &byte, 1))
        {
            if ((EINTR != errno) && (EAGAIN != errno))
            {
                PLOG(PL_FATAL, "NormInstance::Notify() write() error: %s\n",
                               GetErrorString());
                break;
            }
        }    
#endif // if/else WIN32/UNIX  
    }  
}  // end NormInstance::Notify()

// Purge any notifications associated with a specific object
void NormInstance::PurgeObjectNotifications(NormObjectHandle objectHandle)
{
    if (NORM_OBJECT_INVALID == objectHandle) return;
    
    Notification::Queue::Iterator iterator(notify_queue);
    Notification* next;
    while (NULL != (next = iterator.GetNextItem()))
    {
        if (objectHandle == next->event.object)
        {
            // "Release" the previously-retained object handle
            ((NormObject*)objectHandle)->Release();
            // Remove from queue and put in pool
            notify_queue.Remove(*next);
            notify_pool.Append(*next);
        }
    }
    if ((NULL != previous_notification) && (objectHandle == previous_notification->event.object))
    {
        // "Release" any previously-retained object or node handle
        ((NormObject*)(previous_notification->event.object))->Release();
        notify_pool.Append(*previous_notification);   
        previous_notification = NULL;   
    }
    // TBD - check if event queue is emptied and reset event/fd
}  // end NormInstance::PurgeObjectNotifications()

// Purge any notifications associated with a specific remote sender node
void NormInstance::PurgeNodeNotifications(NormNodeHandle nodeHandle)
{
    if (NORM_NODE_INVALID == nodeHandle) return;
    Notification::Queue::Iterator iterator(notify_queue);
    Notification* next;
    while (NULL != (next = iterator.GetNextItem()))
    {
        if (nodeHandle == next->event.sender)
        {
            // "Release" the previously-retained object handle
            ((NormNode*)nodeHandle)->Release();
            // Remove this notification from queue and return to pool
            notify_queue.Remove(*next);
            notify_pool.Append(*next);
        }
    }
    if ((NULL != previous_notification) && (nodeHandle == previous_notification->event.sender))
    {
        // "Release" any previously-retained object or node handle
        if (NORM_OBJECT_INVALID != previous_notification->event.object)
            ((NormObject*)(previous_notification->event.object))->Release();
        else
            ((NormNode*)(previous_notification->event.sender))->Release();
        notify_pool.Append(*previous_notification);   
        previous_notification = NULL;   
    }
    if (notify_queue.IsEmpty()) ResetNotificationEvent();
}  // end NormInstance::PurgeNodeNotifications()

void NormInstance::PurgeSessionNotifications(NormSessionHandle sessionHandle)
{
    if (NORM_SESSION_INVALID == sessionHandle) return;
    Notification::Queue::Iterator iterator(notify_queue);
    Notification* next;
    while (NULL != (next = iterator.GetNextItem()))
    {
        if (next->event.session == sessionHandle)
        {
             if (NORM_OBJECT_INVALID != next->event.object)
                ((NormObject*)next->event.object)->Release();
            else if (NORM_NODE_INVALID != next->event.sender)
                ((NormNode*)next->event.sender)->Release();
            // Remove this notification from queue and return to pool
            notify_queue.Remove(*next);
            notify_pool.Append(*next);
        }   
    }
    if ((NULL != previous_notification) && (sessionHandle == previous_notification->event.session))
    {
        // "Release" any previously-retained object or node handle
        if (NORM_OBJECT_INVALID != previous_notification->event.object)
            ((NormObject*)(previous_notification->event.object))->Release();
        else if (NORM_NODE_INVALID != previous_notification->event.sender)
            ((NormNode*)(previous_notification->event.sender))->Release();
        notify_pool.Append(*previous_notification);   
        previous_notification = NULL;   
    }
    if (notify_queue.IsEmpty()) ResetNotificationEvent();
}  // end NormInstance::PurgeSessionNotifications()

// Purges notifications of a specific type for a specific session
void NormInstance::PurgeNotifications(NormSessionHandle sessionHandle, NormEventType eventType)
{
    if (NORM_SESSION_INVALID == sessionHandle) return;
    Notification::Queue::Iterator iterator(notify_queue);
    Notification* next;
    while (NULL != (next = iterator.GetNextItem()))
    {
        if ((next->event.session == sessionHandle) &&
            (next->event.type == eventType))
        {
            if (NORM_OBJECT_INVALID != next->event.object)
                ((NormObject*)next->event.object)->Release();
            else if (NORM_NODE_INVALID != next->event.sender)
                ((NormNode*)next->event.sender)->Release();
            // Remove this notification from queue and return to pool
            notify_queue.Remove(*next);
            notify_pool.Append(*next);
        }
    }
    if (notify_queue.IsEmpty()) ResetNotificationEvent();
}  // end NormInstance::PurgeNotifications()

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
            ((NormNode*)(previous_notification->event.sender))->Release();
        notify_pool.Append(*previous_notification);   
        previous_notification = NULL;   
    }
    Notification* next;
    while (NULL != (next = notify_queue.RemoveHead()))
    {
        switch (next->event.type)
        {
            case NORM_EVENT_INVALID:
                if (!notify_queue.IsEmpty())
                {
                    // Discard this invalid event and get next one
                    notify_pool.Append(*next);
                    continue;
                }
                break;
            case NORM_RX_OBJECT_UPDATED:
            {
                // reset update event notification for non-streams
                // (NormStreamRead() takes care of streams)
                NormObject* obj = ((NormObject*)next->event.object);
                if (!obj->IsStream()) obj->SetNotifyOnUpdate(true);
                break;
            }
            case NORM_SEND_ERROR:
            {
                NormSession* session = (NormSession*)next->event.session;
                session->ClearSendError();
                break;
            }
            default:
                break;   
        }
	    break;
    }
    if (NULL != next)
    {
        previous_notification = next;  // keep dispatched event for garbage collection
        if (NULL != theEvent) *theEvent = next->event;
    }
    else if (NULL != theEvent)
    {
    	theEvent->type = NORM_EVENT_INVALID;
	    theEvent->session = NORM_SESSION_INVALID;
	    theEvent->sender = NORM_NODE_INVALID;
	    theEvent->object = NORM_OBJECT_INVALID;
    }
    if (notify_queue.IsEmpty()) ResetNotificationEvent();
    return (NULL != next); 
}  // end NormInstance::GetNextEvent()

bool NormInstance::WaitForEvent()
{
    if (!dispatcher.IsThreaded()) 
    {
        PLOG(PL_FATAL, "NormInstance::WaitForEvent() warning: NORM thread not running!\n");
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
                PLOG(PL_FATAL, "NormInstance::WaitForEvent() select() error: %s\n",
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
        PLOG(PL_FATAL, "NormInstance::Startup() CreateEvent() error: %s\n", GetErrorString());
        return false;
    }
#else
    if (0 != pipe(notify_fd))
    {
        PLOG(PL_FATAL, "NormInstance::Startup() pipe() error: %s\n", GetErrorString());
        return false;
    }
    // make reading non-blocking
    if(-1 == fcntl(notify_fd[0], F_SETFL, fcntl(notify_fd[0], F_GETFL, 0)  | O_NONBLOCK))
    {
        PLOG(PL_FATAL, "NormInstance::Startup() fcntl(F_SETFL(O_NONBLOCK)) error: %s\n", GetErrorString());
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



void NormInstance::ReleasePreviousEvent()
{
    // Garbage collect our "previous_notification"
    if (NULL != previous_notification)
    {
        // Release any previously-retained object or node handles
        if (NORM_OBJECT_INVALID != previous_notification->event.object)
            ((NormObject*)(previous_notification->event.object))->Release();
        else if (NORM_NODE_INVALID != previous_notification->event.sender)
            ((NormNode*)(previous_notification->event.sender))->Release();
        notify_pool.Append(*previous_notification);   
        previous_notification = NULL;   
    }
}  // end NormInstance::ReleasePreviousEvent()

NORM_API_LINKAGE
void NormReleasePreviousEvent(NormInstanceHandle instanceHandle)
{
    NormInstance* instance = (NormInstance*)instanceHandle;
    if (instance && instance->dispatcher.SuspendThread())
    {
        instance->ReleasePreviousEvent();
        instance->dispatcher.ResumeThread();
    }
}  // end NormReleasePreviousEvent()


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
    if (notify_fd[0] >= 0)
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
            ((NormNode*)(previous_notification->event.sender))->Release();
        notify_pool.Append(*previous_notification);   
        previous_notification = NULL;   
    }
    
    Notification* next;
    while (NULL != (next = notify_queue.RemoveHead()))
    {
        switch (next->event.type)
        {
            case NORM_RX_OBJECT_NEW:
            {
                NormObject* obj = (NormObject*)next->event.object;
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
        if (NORM_OBJECT_INVALID != next->event.object)
            ((NormObject*)(next->event.object))->Release();
        else if (NORM_NODE_INVALID != next->event.sender)
            ((NormNode*)(next->event.sender))->Release();
        delete next;        
    }
    notify_pool.Destroy();
}  // end NormInstance::Shutdown()

/*
This function doesn't make sense?
UINT32 NormInstance::CountCompletedObjects(NormSession* session)
{
	UINT32 result = 0UL;
    Notification::Queue::Iterator iterator(notify_queue);
    Notification* next;
    while (NULL != (next = iterator.GetNextItem()))
    {
		if ((session == next->event.session) &&
			(NORM_RX_OBJECT_COMPLETED == next->event.type))
        {
			result ++;
        }
	}
	return result;
} // end NormInstance::CountCompletedObjects()
*/

//////////////////////////////////////////////////////////////////////////
// NORM API FUNCTION IMPLEMENTATIONS
//

NORM_API_LINKAGE 
int NormGetVersion(int* major, int* minor, int* patch)
{
    if (NULL != major) *major = NORM_VERSION_MAJOR; 
    if (NULL != minor) *minor = NORM_VERSION_MINOR;
    if (NULL != patch) *patch = NORM_VERSION_PATCH; 
    return NORM_VERSION_MAJOR;
}  // end NormGetVersion()

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
void NormStopInstance(NormInstanceHandle instanceHandle)
{
    NormInstance* instance = (NormInstance*)instanceHandle;
    if (instance) instance->Stop();  // stops NORM protocol thread
}  // end NormStopInstance()

NORM_API_LINKAGE
bool NormRestartInstance(NormInstanceHandle instanceHandle)
{
    NormInstance* instance = (NormInstance*)instanceHandle;
    if (instance)
    {
        if (instance->dispatcher.IsThreaded())
            instance->Stop();
        return instance->Start();
    }
    return false;  // invalid instanceHandle
}  // end NormStartInstance()


NORM_API_LINKAGE
bool NormSuspendInstance(NormInstanceHandle instanceHandle)
{
    NormInstance* instance = (NormInstance*)instanceHandle;
    if (instance) 
        return instance->dispatcher.SuspendThread();  // stops NORM protocol thread
    else
        return false;
}  // end NormSuspendInstance()

NORM_API_LINKAGE
void NormResumeInstance(NormInstanceHandle instanceHandle)
{
    NormInstance* instance = (NormInstance*)instanceHandle;
    if (instance) instance->dispatcher.ResumeThread();  
}  // end NormResumeInstance()


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


NORM_API_LINKAGE
void NormSetAllocationFunctions(NormInstanceHandle      instanceHandle, 
                                NormAllocFunctionHandle allocFunc, 
                                NormFreeFunctionHandle  freeFunc)
{
    NormInstance* instance = (NormInstance*)instanceHandle;
    instance->SetAllocationFunctions(allocFunc, freeFunc);
}  // end NormSetAllocationFunctions()


// if "waitForEvent" is false, this is a non-blocking call
// (TBD) add a timeout option to this?

NORM_API_LINKAGE
bool NormGetNextEvent(NormInstanceHandle instanceHandle, NormEvent* theEvent, bool waitForEvent)
{
    NormInstance* instance = (NormInstance*)instanceHandle;
    bool result = false;
    if (instance)
    {
        if (instance->dispatcher.SuspendThread())
        {
            if (waitForEvent)
            {
                if (instance->NotifyQueueIsEmpty()) 
                {
                    // no pending events, so resume and wait
                    instance->dispatcher.ResumeThread();
                    if (!instance->WaitForEvent())
                    {
                        // Indication that NormInstance is dead
			            // TBD - how do we inform app although this shouldn't
			            // happen unless the app destroys the "instance"
                        return false;
                    }
                    // re-suspend thread after wait
                    if (!instance->dispatcher.SuspendThread()) return false;
                }
            }
            result = instance->GetNextEvent(theEvent);
            instance->dispatcher.ResumeThread();
        }
    }
    return result;  
}  // end NormGetNextEvent()


NORM_API_LINKAGE
bool NormIsUnicastAddress(const char* address)
{
    // TBD - is this really a thread-safe thing to do?
    ProtoAddress addr;
    if ((NULL != address) && addr.ResolveFromString(address))
        return addr.IsUnicast();
    else
        return false;  // not valid, so not unicast
}  // end NormIsUnicast()

/** NORM Session Creation and Control Functions */
NORM_API_LINKAGE
NormSessionHandle NormCreateSession(NormInstanceHandle instanceHandle,
                                    const char*        sessionAddr,
                                    UINT16             sessionPort,
                                    NormNodeId         localNodeId)
{
    // (TBD) wrap this with SuspendThread/ResumeThread ???
    NormInstance* instance = (NormInstance*)instanceHandle;
    if (instance && instance->dispatcher.SuspendThread())
    {
        NormSession* session = 
            instance->session_mgr.NewSession(sessionAddr, sessionPort, localNodeId);
        instance->dispatcher.ResumeThread();
        if (NULL != session) 
            return ((NormSessionHandle)session);
    }
    return NORM_SESSION_INVALID;
}  // end NormCreateSession()

NORM_API_LINKAGE
void NormDestroySession(NormSessionHandle sessionHandle)
{
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {    
        NormSession* session = (NormSession*)sessionHandle;
        if (NULL != session)
        {
            session->Close();
            session->GetSessionMgr().DeleteSession(session);
            instance->PurgeSessionNotifications(sessionHandle);
        }
        instance->dispatcher.ResumeThread();
    }
}  // end NormDestroySession()

NORM_API_LINKAGE 
NormInstanceHandle NormGetInstance(NormSessionHandle sessionHandle)
{
    
    return (NormInstanceHandle)NormInstance::GetInstanceFromSession(sessionHandle);
}  // end NormGetIntance()

NORM_API_LINKAGE
void NormSetUserData(NormSessionHandle sessionHandle, const void* userData)
{
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {    
        NormSession* session = (NormSession*)sessionHandle;
        if (session) session->SetUserData(userData);
        instance->dispatcher.ResumeThread();
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
void NormSetUserTimer(NormSessionHandle sessionHandle, double seconds)
{
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (instance)
    {
        if (instance->dispatcher.SuspendThread())
        {    
            NormSession* session = (NormSession*)sessionHandle;
            if (session) session->SetUserTimer(seconds);
            instance->PurgeNotifications(sessionHandle, NORM_USER_TIMEOUT);
            instance->dispatcher.ResumeThread();
        }
    }
}   // end NormSetUserTimer()

NORM_API_LINKAGE 
void NormCancelUserTimer(NormSessionHandle sessionHandle)
{
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (NULL != instance)
    {
        if (instance->dispatcher.SuspendThread())
        {    
            NormSession* session = (NormSession*)sessionHandle;
            if (session) session->SetUserTimer(-1.0);  // interval less than zero cancels timer
            instance->PurgeNotifications(sessionHandle, NORM_USER_TIMEOUT);
            instance->dispatcher.ResumeThread();
        }
    }
}  // end NormCancelUserTimer()


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
bool NormGetAddress(NormSessionHandle   sessionHandle,
                    char*               addrBuffer, 
                    unsigned int*       bufferLen,
                    UINT16*             port)
{
    bool result = false;
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (NULL != instance)
    {
        if (instance->dispatcher.SuspendThread())
        {    
            NormSession* session = (NormSession*)sessionHandle;
            const ProtoAddress& sessionAddr = session->Address();
            unsigned int addrLen = sessionAddr.GetLength();
            if (addrBuffer && bufferLen && (addrLen <= *bufferLen))
            {
                memcpy(addrBuffer, sessionAddr.GetRawHostAddress(), addrLen);
                result = true;
            } 
            else if (NULL == addrBuffer)
            {
                result = true; // just a query for addrLen and/or port
            }      
            if (bufferLen) *bufferLen = addrLen;
            if (port) *port = sessionAddr.GetPort();
            instance->dispatcher.ResumeThread();  
        }
    }
    return result;
}  // end NormGetAddress()

NORM_API_LINKAGE
UINT16 NormGetRxPort(NormSessionHandle sessionHandle)
{
    NormSession* session = (NormSession*)sessionHandle;
    if (NULL != session)
        return session->GetRxPort();
    else
        return 0;      
}  // end NormGetRxPort()


NORM_API_LINKAGE
bool NormGetRxBindAddress(NormSessionHandle sessionHandle, char* addr, unsigned int& addrLen, UINT16& port)
{
    bool result = false;
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if ((NULL != instance) && instance->dispatcher.SuspendThread())
    {    
        NormSession* session = (NormSession*)sessionHandle;
        port = session->GetRxPort();
        ProtoAddress bindAddr = session->GetRxBindAddr();
        if (!bindAddr.IsValid())
        {
            addrLen = 0;
            result = true;
        }
        else if (addrLen < bindAddr.GetLength())
        {
            addrLen = bindAddr.GetLength();
        }
        else
        {
            addrLen = bindAddr.GetLength();
            memcpy(addr, bindAddr.GetRawHostAddress(), addrLen);
            result = true;
        }    
        instance->dispatcher.ResumeThread();  
    }
    else
    {
        addrLen = port = 0;
    }    
    return result;
}  // end NormGetRxBindAddress()

NORM_API_LINKAGE
bool NormSetTxPort(NormSessionHandle sessionHandle,
                   UINT16            txPort,
                   bool              enableReuse,
                   const char*       txAddress)
{
    bool result = false;
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (NULL != instance)
    {
        if (instance->dispatcher.SuspendThread())
        {    
            NormSession* session = (NormSession*)sessionHandle;
            if (NULL != session) 
                result = session->SetTxPort(txPort, enableReuse, txAddress);
            instance->dispatcher.ResumeThread();
        }
    } 
    return result;
}  // end NormSetTxPort()

NORM_API_LINKAGE
UINT16 NormGetTxPort(NormSessionHandle sessionHandle)
{
    NormSession* session = (NormSession*)sessionHandle;
    if (NULL != session)
        return session->GetTxPort();
    else
        return 0;
}  // end NormGetTxPort()

NORM_API_LINKAGE
void NormSetTxOnly(NormSessionHandle sessionHandle,
                   bool              txOnly,
                   bool              connectToSessionAddress)
{
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {
        NormSession* session = (NormSession*)sessionHandle;
        if (session) session->SetTxOnly(txOnly, connectToSessionAddress);
        instance->dispatcher.ResumeThread();
    }
}  // end NormSetTxOnly()

NORM_API_LINKAGE
bool NormPresetObjectInfo(NormSessionHandle  sessionHandle,
                          unsigned long      objectSize,
                          UINT16             segmentSize, 
                          UINT16             numData, 
                          UINT16             numParity)
{
    bool result = false;
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {
        NormSession* session = (NormSession*)sessionHandle;
        if (session) 
        {
            result = session->SetPresetFtiData((unsigned int)objectSize, segmentSize, numData, numParity);
            if (result) session->SenderSetFtiMode(NormSession::FTI_PRESET);
        }
        instance->dispatcher.ResumeThread();
    }
    return result;
} // end NormPresetObjectInfo()

NORM_API_LINKAGE
void NormLimitObjectInfo(NormSessionHandle sessionHandle, bool state)
{
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {
        NormSession* session = (NormSession*)sessionHandle;
        if (session) 
        {
            if (state)
                session->SenderSetFtiMode(NormSession::FTI_INFO);
            else
                session->SenderSetFtiMode(NormSession::FTI_ALWAYS);
        }
        instance->dispatcher.ResumeThread();
    }
} // end NormLimitObjectInfo()

// These functions are used internally by the NormSocket API extension

NORM_API_LINKAGE
void NormSetId(NormSessionHandle sessionHandle, NormNodeId normId)
{
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {
        NormSession* session = (NormSession*)sessionHandle;
        if (NULL != session) session->SetNodeId(normId);
        instance->dispatcher.ResumeThread();
    }
}  // end NormSetId()

NORM_API_LINKAGE
bool NormChangeDestination(NormSessionHandle sessionHandle,
	                       const char*       sessionAddress,
	                       UINT16            sessionPort,
	                       bool              connectToSessionAddress)
{
	NormSession* session = (NormSession*)sessionHandle;
	if (NULL == session) return false;
	// First, see if we can make a valid ProtoAddress
	ProtoAddress dest;
	if (NULL != sessionAddress)
	{
		if (!dest.ResolveFromString(sessionAddress))
			return false;
	}
	else
	{
		dest = session->Address();
	}
    dest.SetPort(sessionPort);
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {
        if (NULL != session) 
        {
            session->SetAddress(dest);
            if (connectToSessionAddress)
                session->SetTxOnly(session->GetTxOnly(), true);
        }
        instance->dispatcher.ResumeThread();
    }
    return true;
}  // end NormChangeDestination()

NORM_API_LINKAGE
void NormSetServerListener(NormSessionHandle sessionHandle, bool state)
{
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (NULL != instance)
    {
        if (instance->dispatcher.SuspendThread())
        {    
            NormSession* session = (NormSession*)sessionHandle;
            if (session) session->SetServerListener(state);
            instance->dispatcher.ResumeThread();
        }
    } 
}  // end NormSetServerListener()


NORM_API_LINKAGE
bool NormTransferSender(NormSessionHandle sessionHandle, NormNodeHandle senderHandle)
{
	bool result = false;
    NormInstance* dstInstance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (NULL != dstInstance)
    {
        if (dstInstance->dispatcher.SuspendThread())
        {
            NormInstance* srcInstance = NormInstance::GetInstanceFromNode(senderHandle);
            if (srcInstance->dispatcher.SuspendThread())
            {
                NormSession* session = (NormSession*)sessionHandle;
                NormSenderNode* sender = (NormSenderNode*)senderHandle;
                if ((NULL != session) && (NULL != sender))
                    result = session->InsertRemoteSender(*sender);
                srcInstance->dispatcher.ResumeThread();
            }
            dstInstance->dispatcher.ResumeThread();
        }
    }
	return result;
}  // end NormTransferSender()

NORM_API_LINKAGE
void NormSetRxPortReuse(NormSessionHandle sessionHandle,
                        bool              enableReuse,
                        const char*       rxAddress,     // bind() to <rxAddress>/<sessionPort>
                        const char*       senderAddress, // connect() to <senderAddress>/<senderPort>
                        UINT16            senderPort)
{
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (NULL != instance)
    {
        if (instance->dispatcher.SuspendThread())
        {    
            NormSession* session = (NormSession*)sessionHandle;
            if (session) session->SetRxPortReuse(enableReuse, rxAddress, senderAddress, senderPort);
            instance->dispatcher.ResumeThread();
        }
    } 
}  // end NormSetRxPortReuse()


NORM_API_LINKAGE
void NormSetEcnSupport(NormSessionHandle sessionHandle, bool ecnEnable, bool ignoreLoss, bool tolerateLoss)
{
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (NULL != instance)
    {
        if (instance->dispatcher.SuspendThread())
        {    
            NormSession* session = (NormSession*)sessionHandle;
            if (session) session->SetEcnSupport(ecnEnable, ignoreLoss, tolerateLoss);
            instance->dispatcher.ResumeThread();
        }
    } 
}  // end NormSetEcnSupport()

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
bool NormSetSSM(NormSessionHandle sessionHandle,
                const char*       sourceAddress)
{
    bool result = false;
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (NULL != instance)
    {
        if (instance->dispatcher.SuspendThread())
        {    
            NormSession* session = (NormSession*)sessionHandle;
            if (session)
                result = session->SetSSM(sourceAddress);
            instance->dispatcher.ResumeThread();
        }
    }
    return result;     
}  // end NormSetSSM()

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
bool NormSetMulticastLoopback(NormSessionHandle sessionHandle, bool state)
{
    NormSession* session = (NormSession*)sessionHandle;
    if (session)
        return session->SetMulticastLoopback(state);
    else
        return false;  
}  // end NormSetLoopback()

NORM_API_LINKAGE
bool NormSetFragmentation(NormSessionHandle sessionHandle, bool state)
{
    NormSession* session = (NormSession*)sessionHandle;
    if (session)
        return session->SetFragmentation(state);
    else
        return false;  
}  // end NormSetFragmentation()

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

NORM_API_LINKAGE
bool NormOpenDebugLog(NormInstanceHandle instanceHandle, const char *path)
{
    /* NOTE: This only locks one thread.  Multiple NormInstances could
    cause problems with this. */
    bool result = false;
    NormInstance* instance = (NormInstance*)instanceHandle;
    if (instance->dispatcher.SuspendThread()) {
        result = OpenDebugLog(path);
        instance->dispatcher.ResumeThread();
    }
    return result;
}

NORM_API_LINKAGE
void NormCloseDebugLog(NormInstanceHandle instanceHandle)
{
    /* NOTE: This only locks one thread.  Multiple NormInstances could
    cause problems with this. */
    NormInstance* instance = (NormInstance*)instanceHandle;
    if (instance->dispatcher.SuspendThread()) {
        CloseDebugLog();
        instance->dispatcher.ResumeThread();
    }
}

NORM_API_LINKAGE
bool NormOpenDebugPipe(NormInstanceHandle instanceHandle, const char *pipeName)
{
    /* NOTE: This only locks one thread.  Multiple NormInstances could
    cause problems with this. */
    bool result = false;
    NormInstance* instance = (NormInstance*)instanceHandle;
    if (instance->dispatcher.SuspendThread()) {
        result = OpenDebugPipe(pipeName);
        instance->dispatcher.ResumeThread();
    }
    return result;
}   // end NormOpenDebugPipe()

NORM_API_LINKAGE
void NormCloseDebugPipe(NormInstanceHandle instanceHandle)
{
    /* NOTE: This only locks one thread.  Multiple NormInstances could
    cause problems with this. */
    NormInstance* instance = (NormInstance*)instanceHandle;
    if (instance->dispatcher.SuspendThread()) {
        CloseDebugPipe();
        instance->dispatcher.ResumeThread();
    }
}  // end NormCloseDebugPipe()

NORM_API_LINKAGE
void NormSetDebugLevel(unsigned int level)
{
    // Sets underlying Protolib debug leve
    SetDebugLevel(level);
}

NORM_API_LINKAGE
unsigned int NormGetDebugLevel()
{
    return GetDebugLevel();
}

NORM_API_LINKAGE
void NormSetReportInterval(NormSessionHandle sessionHandle, double interval)
{
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {
        NormSession* session = (NormSession*)sessionHandle;
        if (session)
            session->SetReportTimerInterval(interval);
        instance->dispatcher.ResumeThread();
    }
}  // end NormSetReportInterval()

NORM_API_LINKAGE
double NormGetReportInterval(NormSessionHandle sessionHandle)
{
    double result = 0;
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {
        NormSession* session = (NormSession*)sessionHandle;
        if (session)
            result = session->GetReportTimerInterval();
        instance->dispatcher.ResumeThread();
    }
    return result;
}  // end NormGetReportInterval()

/** NORM Sender Functions */

NORM_API_LINKAGE
NormSessionId NormGetRandomSessionId()
{
    ProtoTime currentTime;
    currentTime.GetCurrentTime();
    srand((unsigned int)currentTime.usec());  // seed random number generator
    return (NormSessionId)rand();
}  // end NormGetRandomSessionId()

NORM_API_LINKAGE
bool NormStartSender(NormSessionHandle  sessionHandle,
                     NormSessionId      sessionId,
                     UINT32             bufferSpace,
                     UINT16             segmentSize,
                     UINT16             numData,
                     UINT16             numParity,
                     UINT8              fecId)
{
    bool result = false;
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {
        NormSession* session = (NormSession*)sessionHandle;
        if (session)
            result = session->StartSender(sessionId, bufferSpace, segmentSize, numData, numParity, fecId);
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
        if (session) session->StopSender();
        instance->dispatcher.ResumeThread();
    }
}  // end NormStopSender()


NORM_API_LINKAGE
double NormGetTxRate(NormSessionHandle sessionHandle)
{
    if (NORM_SESSION_INVALID != sessionHandle)
        return (((NormSession*)sessionHandle)->GetTxRate());
    else
        return -1.0;
}  // end NormGetTxRate()

NORM_API_LINKAGE
void NormSetTxRate(NormSessionHandle sessionHandle,
                         double            bitsPerSecond)
{
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {
        NormSession* session = (NormSession*)sessionHandle;
        if (session) session->SetTxRate(bitsPerSecond);
        instance->dispatcher.ResumeThread();
    }
}  // end NormSetTxRate()

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
void NormSetFlowControl(NormSessionHandle sessionHandle, double flowControlFactor)
{
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {
        NormSession* session = (NormSession*)sessionHandle;
        if (session) session->SetFlowControl(flowControlFactor);
        instance->dispatcher.ResumeThread();
    }
}  // end NormSetFlowControl()

NORM_API_LINKAGE
void NormSetCongestionControl(NormSessionHandle sessionHandle, bool enable, bool adjustRate)
{
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {
        NormSession* session = (NormSession*)sessionHandle;
        if (session) session->SetCongestionControl(enable, adjustRate);
        instance->dispatcher.ResumeThread();
    }
}  // end NormSetCongestionControl()

NORM_API_LINKAGE
void NormSetTxRateBounds(NormSessionHandle sessionHandle,
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
}  // end NormSetTxRateBounds()

NORM_API_LINKAGE
void NormSetTxCacheBounds(NormSessionHandle sessionHandle,
                          NormSize          sizeMax,
                          UINT32            countMin,
                          UINT32            countMax)
{
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {
        NormSession* session = (NormSession*)sessionHandle;
        NormObjectSize theSize(sizeMax);
        if (session) session->SetTxCacheBounds(theSize, countMin, countMax);
        instance->dispatcher.ResumeThread();
    }
}  // end NormSetTxCacheBounds()

NORM_API_LINKAGE
void NormSetAutoParity(NormSessionHandle sessionHandle, unsigned char autoParity)
{
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {
        NormSession* session = (NormSession*)sessionHandle;
        if (session) session->SenderSetAutoParity(autoParity);
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
        if (session) session->SenderSetGrtt(grttEstimate);
        instance->dispatcher.ResumeThread();
    }
}  // end NormSetGrttEstimate()

NORM_API_LINKAGE
double NormGetGrttEstimate(NormSessionHandle sessionHandle)
{
    if (NORM_SESSION_INVALID != sessionHandle)
    {
        NormSession* session = (NormSession*)sessionHandle;
        session->ResetGrttNotification();
        return (session->SenderGrtt());
    }
    else
    {
        return -1.0;
    }
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
void NormSetGrttProbingTOS(NormSessionHandle sessionHandle,
                           UINT8              probeTOS)
{
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {
        NormSession* session = (NormSession*)sessionHandle;
        session->SetProbeTOS(probeTOS);
        instance->dispatcher.ResumeThread();
    }
}  // end NormSetGrttProbingTOS()

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
        session->SenderSetGroupSize((double)groupSize);
        instance->dispatcher.ResumeThread();
    }
}  // end NormSetGroupSize()

NORM_API_LINKAGE 
void NormSetTxRobustFactor(NormSessionHandle sessionHandle,
                           int               robustFactor)
{
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {
        NormSession* session = (NormSession*)sessionHandle;
        session->SetTxRobustFactor(robustFactor);
        instance->dispatcher.ResumeThread();
    }
}  // end NormSetTxRobustFactor()

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
                                 UINT32             dataLen,
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
            if (NULL != obj) objectHandle = (NormObjectHandle)obj;
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
                                UINT32             bufferSize,
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
            NormStreamObject* streamObj = session->QueueTxStream(bufferSize, true, infoPtr, infoLen);
            NormObject* obj = 
                static_cast<NormObject*>(streamObj);
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
        if (graceful && (NULL == stream->GetSender()))
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
unsigned int NormGetStreamBufferSegmentCount(unsigned int bufferBytes, UINT16 segmentSize, UINT16 blockSize)
{
    // This same computation is performed in NormStreamObject::Open() in "normObject.cpp"
    // (Note the number of stream buffer segments is always smaller than the 
    //  blockSize*pending_mask.GetSize())
    unsigned int numBlocks = bufferBytes / (blockSize * segmentSize);
    if (numBlocks < 2) numBlocks = 2; // NORM enforces a 2-block minimum buffer size
    return (numBlocks * blockSize);
}  // end NormGetStreamBufferSegmentCount()

NORM_API_LINKAGE
unsigned int NormStreamWrite(NormObjectHandle   streamHandle,
                             const char*        buffer,
                             unsigned int       numBytes)
{
    // Note: Since an underlying issue with ProtoDispatcher::SignalThread() had been resolved,
    //       using  ProtoDispatcher::SuspendThread() should be sufficient since the underlying
    //       protolib time scheduling, etc. code actually invokes SignalThread() on an
    //       as-needed basis.  Thus, using SuspendThread() (lighter weight) should suffice
    unsigned int result = 0;
    NormInstance* instance = NormInstance::GetInstanceFromObject(streamHandle);
    if ((NULL != instance) && instance->dispatcher.SuspendThread())
    {
        NormStreamObject* stream = 
            static_cast<NormStreamObject*>((NormObject*)streamHandle);
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
    if ((NULL != instance) && instance->dispatcher.SuspendThread())
    {
        NormStreamObject* stream = 
            static_cast<NormStreamObject*>((NormObject*)streamHandle);
        NormStreamObject::FlushMode saveFlushMode = stream->GetFlushMode();
        stream->SetFlushMode((NormStreamObject::FlushMode)flushMode);
        stream->Flush(eom);
        stream->SetFlushMode(saveFlushMode);
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
unsigned int NormStreamGetVacancy(NormObjectHandle streamHandle, unsigned int bytesWanted)
{
    bool result = false;
    NormInstance* instance = NormInstance::GetInstanceFromObject(streamHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {
        NormStreamObject* stream = 
            static_cast<NormStreamObject*>((NormObject*)streamHandle);
        if (NULL != stream)
            result = stream->GetVacancy(bytesWanted);
        instance->dispatcher.ResumeThread();
    }
    return result;
}  // end NormStreamGetVacancy()

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
                      NormObjectHandle   objectHandle,
                      bool               overrideFlush)
{
    bool result = false;
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {
        NormSession* session = (NormSession*)sessionHandle;
        NormObject* obj = (NormObject*)objectHandle;
        if (session && obj)
        {
            // Purge any existing NORM_TX_WATERMARK_COMPLETED notifications to be safe
            instance->PurgeNotifications(sessionHandle, NORM_TX_WATERMARK_COMPLETED);
            // (segmentId doesn't matter for non-stream)
            if (obj->IsStream())
            {
                NormStreamObject* stream = static_cast<NormStreamObject*>(obj);
                session->SenderSetWatermark(stream->GetId(), 
                                            stream->FlushBlockId(),
                                            stream->FlushSegmentId(),
                                            overrideFlush);  
            }
            else
            {
                NormBlockId blockId = obj->GetFinalBlockId();
                NormSegmentId segmentId = obj->GetBlockSize(blockId) - 1;
                session->SenderSetWatermark(obj->GetId(), 
                                            blockId,
                                            segmentId,
                                            overrideFlush);  
            }
            result = true;
        }        
        instance->dispatcher.ResumeThread();
    }
    return result;
}  // end NormSetWatermark()

NORM_API_LINKAGE 
bool NormSetWatermarkEx(NormSessionHandle  sessionHandle,
                        NormObjectHandle   objectHandle,
                        const char*        buffer,
                        unsigned int       numBytes,
                        bool               overrideFlush)
{
    bool result = false;
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {
        NormSession* session = (NormSession*)sessionHandle;
        NormObject* obj = (NormObject*)objectHandle;
        if (session && obj)
        {
            // Purge any existing NORM_TX_WATERMARK_COMPLETED notifications to be safe
            instance->PurgeNotifications(sessionHandle, NORM_TX_WATERMARK_COMPLETED);
            // (segmentId doesn't matter for non-stream)
            if (obj->IsStream())
            {
                NormStreamObject* stream = static_cast<NormStreamObject*>(obj);
                result = session->SenderSetWatermark(stream->GetId(), 
                                                     stream->FlushBlockId(),
                                                     stream->FlushSegmentId(),
                                                     overrideFlush,
                                                     buffer, numBytes);  
            }
            else
            {
                NormBlockId blockId = obj->GetFinalBlockId();
                NormSegmentId segmentId = obj->GetBlockSize(blockId) - 1;
                result = session->SenderSetWatermark(obj->GetId(), 
                                                     blockId,
                                                     segmentId,
                                                     overrideFlush,
                                                     buffer, numBytes);   
            }
        }        
        instance->dispatcher.ResumeThread();
    }
    return result;
}  // end NormSetWatermarkEx()

NORM_API_LINKAGE
bool NormResetWatermark(NormSessionHandle  sessionHandle)
{
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {
        // Purge any existing NORM_TX_WATERMARK_COMPLETED notifications to be safe
        instance->PurgeNotifications(sessionHandle, NORM_TX_WATERMARK_COMPLETED);
        NormSession* session = (NormSession*)sessionHandle;
        session->SenderResetWatermark();
        instance->dispatcher.ResumeThread();
        return true;
    }
    else
    {
        return false;
    }
}  // end NormResetWatermark()

NORM_API_LINKAGE
void NormCancelWatermark(NormSessionHandle sessionHandle)
{
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {
        NormSession* session = (NormSession*)sessionHandle;
        session->SenderCancelWatermark();
        instance->dispatcher.ResumeThread();
    }
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
            result = (NULL != session->SenderAddAckingNode(nodeId));
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
        if (session) session->SenderRemoveAckingNode(nodeId);
        instance->dispatcher.ResumeThread();
    }
}  // end NormRemoveAckingNode()

NORM_API_LINKAGE
NormNodeHandle NormGetAckingNodeHandle(NormSessionHandle  sessionHandle,
                                       NormNodeId         nodeId)
{
	if (NORM_SESSION_INVALID != sessionHandle)
    {
        NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
        if (instance && instance->dispatcher.SuspendThread())
        {
            NormSession* session = (NormSession*)sessionHandle;
            NormAckingNode* acker = session->SenderFindAckingNode(nodeId);
			instance->dispatcher.ResumeThread();
            if (NULL != acker)
                return ((NormNodeHandle)static_cast<NormNode*>(acker));
        }
    }
    return NORM_NODE_INVALID;
}  // end NormGetAckingNodeHandle()

NORM_API_LINKAGE
void NormSetAutoAckingNodes(NormSessionHandle   sessionHandle,
                            NormTrackingStatus  trackingStatus)
{
    NormSession* session = (NormSession*)sessionHandle;
    switch (trackingStatus)
    {
        case NORM_TRACK_NONE:
            session->SenderSetAutoAckingNodes(NormSession::TRACK_NONE);
            break;
        case NORM_TRACK_RECEIVERS:
            session->SenderSetAutoAckingNodes(NormSession::TRACK_RECEIVERS);
            break;
        case NORM_TRACK_SENDERS:
            session->SenderSetAutoAckingNodes(NormSession::TRACK_SENDERS);
            break;
        case NORM_TRACK_ALL:
            session->SenderSetAutoAckingNodes(NormSession::TRACK_ALL);
            break;
        default:
            break;
    }
}  // end NormSetAutoAckingNodes()

NORM_API_LINKAGE
NormAckingStatus NormGetAckingStatus(NormSessionHandle  sessionHandle,
                                     NormNodeId         nodeId)
{
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {
        NormSession* session = (NormSession*)sessionHandle;
        NormAckingStatus status = 
            (NormAckingStatus)session->SenderGetAckingStatus(nodeId);
        instance->dispatcher.ResumeThread();
        return status;
    }
    else
    {
        return NORM_ACK_INVALID;
    }
}  // end NormGetAckingNodeStatus()

NORM_API_LINKAGE 
bool NormGetNextAckingNode(NormSessionHandle    sessionHandle,
                           NormNodeId*          nodeId,   
                           NormAckingStatus*    ackingStatus)
{
    if (NULL == nodeId) return false;
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {
        NormSession* session = (NormSession*)sessionHandle;
        bool result = session->SenderGetNextAckingNode(*nodeId, (NormSession::AckingStatus*)ackingStatus);
        instance->dispatcher.ResumeThread();
        return result;
    }
    else
    {
        return false;
    }
}   // end NormGetNextAckingNode()

NORM_API_LINKAGE
bool NormGetAckEx(NormSessionHandle sessionHandle,
                  NormNodeId        nodeId,   
                  char*             buffer,
                  unsigned int*     buflen)
{
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {
        NormSession* session = (NormSession*)sessionHandle;
        bool result = (NormAckingStatus)session->SenderGetAckEx(nodeId, buffer, buflen);
        instance->dispatcher.ResumeThread();
        return result;
    }
    if (NULL != buflen) *buflen = 0;
    return false;
}  // end NormGetAckEx()


NORM_API_LINKAGE 
bool NormSendCommand(NormSessionHandle  sessionHandle,
                     const char*        cmdBuffer, 
                     unsigned int       cmdLength, 
                     bool               robust)
{
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {
        NormSession* session = (NormSession*)sessionHandle;
        bool result = session->SenderSendCmd(cmdBuffer, cmdLength, robust);
        instance->dispatcher.ResumeThread();
        return result;
    }
    else
    {
        return false;
    }
}  // end NormSendCommand()

NORM_API_LINKAGE 
void NormCancelCommand(NormSessionHandle sessionHandle)
{
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {
        NormSession* session = (NormSession*)sessionHandle;
        session->SenderCancelCmd();
        // we purge in case command was already sent notification posted
        instance->PurgeNotifications(sessionHandle, NORM_TX_CMD_SENT);
        instance->dispatcher.ResumeThread();
    }
}  // end NormCancelCommand()


// This one is not part of the public API (for NormSocket use currently)
NORM_API_LINKAGE
bool NormSendCommandTo(NormSessionHandle  sessionHandle,
                       const char*        cmdBuffer, 
                       unsigned int       cmdLength, 
                       const char*        addr,
                       UINT16             port)
{
    bool result = false;
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {
        NormSession* session = (NormSession*)sessionHandle;
        ProtoAddress dest;
        if (dest.ResolveFromString(addr))
        {
            dest.SetPort(port);
            result = session->SenderSendAppCmd(cmdBuffer, cmdLength, dest);
        }
        instance->dispatcher.ResumeThread();
    }
    return result;
}  // end NormSendCommandTo()


NORM_API_LINKAGE 
void NormSetSynStatus(NormSessionHandle sessionHandle, bool state)
{
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {
        NormSession* session = (NormSession*)sessionHandle;
        session->SenderSetSynStatus(state);
        instance->dispatcher.ResumeThread();
    }
}  // end NormSetSynStatus()


/** NORM Receiver Functions */

NORM_API_LINKAGE
bool NormStartReceiver(NormSessionHandle  sessionHandle,
                       UINT32      bufferSpace)
{
    bool result = false;
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {
        NormSession* session = (NormSession*)sessionHandle;
        result = session->StartReceiver(bufferSpace);
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
        session->StopReceiver();
        instance->dispatcher.ResumeThread();
    }
}  // end NormStopReceiver()


NORM_API_LINKAGE 
void NormSetRxCacheLimit(NormSessionHandle sessionHandle,
                         unsigned short    countMax)
{
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {
        NormSession* session = (NormSession*)sessionHandle;
        if (session) session->SetRxCacheMax(countMax);
        instance->dispatcher.ResumeThread();
    }
}  // end NormSetRxCacheLimit()

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
                           INT32             maxDelay)
{
    NormSession* session = (NormSession*)sessionHandle;
    if (session) 
    {
        session->ReceiverSetSilent(silent);
        session->RcvrSetMaxDelay(maxDelay);
    }
}  // end NormSetSilentReceiver()

NORM_API_LINKAGE
void NormSetDefaultUnicastNack(NormSessionHandle sessionHandle,
                               bool              unicastNacks)
{
    NormSession* session = (NormSession*)sessionHandle;
    if (session) session->ReceiverSetUnicastNacks(unicastNacks);
}  // end NormSetDefaultUnicastNack()

NORM_API_LINKAGE
void NormNodeSetUnicastNack(NormNodeHandle   nodeHandle,
                            bool             unicastNacks)
{
    
    NormNode* node = (NormNode*)nodeHandle;
    if ((NULL != node) && (NormNode::SENDER == node->GetType()))
    {
        NormSenderNode* sender = static_cast<NormSenderNode*>(node);
        sender->SetUnicastNacks(unicastNacks);
    }
}  // end NormNodeSetUnicastNack()

NORM_API_LINKAGE 
void NormSetDefaultSyncPolicy(NormSessionHandle sessionHandle,
                              NormSyncPolicy    syncPolicy)
{
    NormSession* session = (NormSession*)sessionHandle;
    if (session) session->ReceiverSetDefaultSyncPolicy((NormSenderNode::SyncPolicy)syncPolicy);
}  // end NormSetDefaultSyncPolicy()

NORM_API_LINKAGE
void NormSetDefaultNackingMode(NormSessionHandle sessionHandle,
                               NormNackingMode   nackingMode)
{
    NormSession* session = (NormSession*)sessionHandle;
    if (session) session->ReceiverSetDefaultNackingMode((NormObject::NackingMode)nackingMode);
}  // end NormSetDefaultNackingMode()

NORM_API_LINKAGE
void NormNodeSetNackingMode(NormNodeHandle   nodeHandle,
                            NormNackingMode  nackingMode)
{
     NormNode* node = (NormNode*)nodeHandle;
    if ((NULL != node) && (NormNode::SENDER == node->GetType()))
    {
        NormSenderNode* sender = static_cast<NormSenderNode*>(node);
        sender->SetDefaultNackingMode((NormObject::NackingMode)nackingMode);
    }
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
        session->ReceiverSetDefaultRepairBoundary((NormSenderNode::RepairBoundary)repairBoundary);
}  // end NormSetDefaultRepairBoundary()

NORM_API_LINKAGE
void NormNodeSetRepairBoundary(NormNodeHandle     nodeHandle,
                               NormRepairBoundary repairBoundary)
{
    NormNode* node = (NormNode*)nodeHandle;
    if ((NULL != node) && (NormNode::SENDER == node->GetType()))
    {
        NormSenderNode* sender = static_cast<NormSenderNode*>(node);
        sender->SetRepairBoundary((NormSenderNode::RepairBoundary)repairBoundary);
    }
}  // end NormNodeSetRepairBoundary()


NORM_API_LINKAGE 
void NormSetDefaultRxRobustFactor(NormSessionHandle sessionHandle,
                                  int               robustFactor)
{
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {
        NormSession* session = (NormSession*)sessionHandle;
        session->SetRxRobustFactor(robustFactor);
        instance->dispatcher.ResumeThread();
    }
}  // end NormSetDefaultRxRobustFactor()

NORM_API_LINKAGE 
void NormNodeSetRxRobustFactor(NormNodeHandle   nodeHandle,
                               int              robustFactor)
{
    if (NORM_NODE_INVALID != nodeHandle)
    {
        NormInstance* instance = NormInstance::GetInstanceFromNode(nodeHandle);
        if (instance && instance->dispatcher.SuspendThread())
        {
            NormNode* node = (NormNode*)nodeHandle;
            if (NormNode::SENDER == node->GetType())
            {
                NormSenderNode* sender = static_cast<NormSenderNode*>(node);        
                sender->SetRobustFactor(robustFactor);
            }
            instance->dispatcher.ResumeThread();
        }
    }
}  // end NormNodeSetRxRobustFactor()

NORM_API_LINKAGE
bool NormPreallocateRemoteSender(NormSessionHandle  sessionHandle,
                                 unsigned long      bufferSize,
                                 UINT16             segmentSize, 
                                 UINT16             numData, 
                                 UINT16             numParity,
                                 unsigned int       streamBufferSize)
{
    bool result = false;
    NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {
        NormSession* session = (NormSession*)sessionHandle;
        result = session->PreallocateRemoteSender((unsigned int)bufferSize, segmentSize, numData, numParity, streamBufferSize);
        instance->dispatcher.ResumeThread();
    }
    return result;
}  // end NormPreallocateRemoteSender()

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
UINT32 NormStreamGetReadOffset(NormObjectHandle streamHandle)
{
    NormStreamObject* stream = static_cast<NormStreamObject*>((NormObject*)streamHandle);
    if (stream)
        return stream->GetCurrentReadOffset();
    else
        return 0;
}  // end NormStreamGetReadOffset()

NORM_API_LINKAGE 
unsigned int NormStreamGetBufferUsage(NormObjectHandle streamHandle)
{
    unsigned int usage = 0;
    NormInstance* instance = NormInstance::GetInstanceFromObject(streamHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {
        NormStreamObject* stream = 
            static_cast<NormStreamObject*>((NormObject*)streamHandle);
        usage = stream->GetCurrentBufferUsage() ;
        instance->dispatcher.ResumeThread();
    }
    return usage;
}  // end NormStreamGetBufferUsage()


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
UINT16 NormObjectGetInfoLength(NormObjectHandle objectHandle)
{
    return ((NormObject*)objectHandle)->GetInfoLength();
}  // end NormObjectGetInfoLength()

NORM_API_LINKAGE
UINT16 NormObjectGetInfo(NormObjectHandle objectHandle,
                         char*            buffer,
                         UINT16           bufferLen)
{
    UINT16 result = 0;
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
            NormSenderNode* sender = obj->GetSender();
            if (sender)
                sender->DeleteObject(obj); 
            else
                obj->GetSession().DeleteTxObject(obj, false);
            instance->PurgeObjectNotifications(objectHandle);
            instance->dispatcher.ResumeThread();
        }
    }
}  // end NormObjectCancel()

NORM_API_LINKAGE
void NormObjectSetUserData(NormObjectHandle objectHandle, const void* userData)
{
    NormInstance* instance = NormInstance::GetInstanceFromObject(objectHandle);
    if (instance)
    {
        if (instance->dispatcher.SuspendThread())
        {    
            ((NormObject*)objectHandle)->SetUserData(userData);
            instance->dispatcher.ResumeThread();
        }
    }
}  // end NormObjectSetUserData()

NORM_API_LINKAGE
const void* NormObjectGetUserData(NormObjectHandle objectHandle)
{
    return (((NormObject*)objectHandle)->GetUserData());
}  // end NormNormObjectGetUserData()


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
        nameBuffer[bufferLen - 1] = '\0';  // (TBD) should we always do this
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
const char* NormDataAccessData(NormObjectHandle dataHandle) 
{
    NormDataObject* dataObj = 
            static_cast<NormDataObject*>((NormObject*)dataHandle);
    return dataObj->GetData();
}  // end NormDataAccessData()

NORM_API_LINKAGE
char* NormDataDetachData(NormObjectHandle dataHandle) 
{
    char* ptr = NULL;
    NormInstance* instance = NormInstance::GetInstanceFromObject(dataHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {
        NormDataObject* dataObj = static_cast<NormDataObject*>((NormObject*)dataHandle);
        ptr = dataObj->DetachData();
        instance->dispatcher.ResumeThread();
    }
    return ptr;
}  // end NormDataDetachData()

NORM_API_LINKAGE 
char* NormAlloc(size_t numBytes)
{
    return new char[numBytes];
}  // end NormAlloc()

NORM_API_LINKAGE 
void NormFree(char* dataPtr)
{
    if (NULL != dataPtr)
        delete[] dataPtr;
}  // end NormFree()

NORM_API_LINKAGE
NormNodeHandle NormObjectGetSender(NormObjectHandle objectHandle)
{
    return (NormNodeHandle)(((NormObject*)objectHandle)->GetSender());
}  // end NormObjectGetSender()


/** NORM Node Functions */

NORM_API_LINKAGE
NormNodeId NormNodeGetId(NormNodeHandle nodeHandle)
{
    NormNode* node = (NormNode*)nodeHandle;
    if (NULL != node) 
        return node->GetId();
    else
        return NORM_NODE_NONE;
}  // end NormNodeGetId()

NORM_API_LINKAGE
bool NormNodeGetAddress(NormNodeHandle  nodeHandle,
                        char*           addrBuffer, 
                        unsigned int*   bufferLen,
                        UINT16*         port)
{
    bool result = false;
    if (NORM_NODE_INVALID != nodeHandle)
    {
        //NormInstance* instance = NormInstance::GetInstanceFromNode(nodeHandle);
        //if (instance && instance->dispatcher.SuspendThread())
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
            //instance->dispatcher.ResumeThread();  
        }
    }
    return result;
}  // end NormNodeGetAddress()

NORM_API_LINKAGE 
void NormNodeSetUserData(NormNodeHandle nodeHandle, const void* userData)
{
    if (NORM_NODE_INVALID != nodeHandle)
    {
        NormNode* node = (NormNode*)nodeHandle;
        return node->SetUserData(userData);
    }
}

NORM_API_LINKAGE 
const void* NormNodeGetUserData(NormNodeHandle nodeHandle)
{
    NormNode* node = (NormNode*)nodeHandle;
    if (NULL != node) 
        return node->GetUserData();
    else
        return NULL;
}  // end NormNodeGetUserData()

NORM_API_LINKAGE
double NormNodeGetGrtt(NormNodeHandle nodeHandle)
{
    NormNode* node = (NormNode*)nodeHandle;
    if ((NULL != node) && (NormNode::SENDER == node->GetType()))
    {
        NormSenderNode* sender = static_cast<NormSenderNode*>(node);
        sender->ResetGrttNotification();
        return sender->GetGrttEstimate();
    }
    else
    {
        return -1.0;
    }
}  // end NormNodeGetGrtt()

NORM_API_LINKAGE
bool NormNodeGetCommand(NormNodeHandle nodeHandle,
                        char*          cmdBuffer,
                        unsigned int*  cmdLength)
{
    bool result = false;
    if (NORM_NODE_INVALID != nodeHandle)
    {
        NormInstance* instance = NormInstance::GetInstanceFromNode(nodeHandle);
        if (instance && instance->dispatcher.SuspendThread())
        {
            NormNode* node = (NormNode*)nodeHandle;
            if (NormNode::SENDER == node->GetType())
            {
                NormSenderNode* sender = static_cast<NormSenderNode*>(node);
                result = sender->ReadNextCmd(cmdBuffer, cmdLength);
            }
            instance->dispatcher.ResumeThread();  
        }
    }
    return result;
}  // end NormNodeGetCommand()

NORM_API_LINKAGE
bool NormNodeSendAckEx(NormNodeHandle nodeHandle,
                       const char*    buffer,
                       unsigned int   numBytes)
{
    bool result = false;
    if (NORM_NODE_INVALID != nodeHandle)
    {
        NormInstance* instance = NormInstance::GetInstanceFromNode(nodeHandle);
        if (instance && instance->dispatcher.SuspendThread())
        {
            NormNode* node = (NormNode*)nodeHandle;
            if (NormNode::SENDER == node->GetType())
            {
                NormSenderNode* sender = static_cast<NormSenderNode*>(node);
                result = sender->SendAckEx(buffer, numBytes);
            }
            instance->dispatcher.ResumeThread();  
        }
    }
    return result;
}  // end NormNodeSendAckEx()

NORM_API_LINKAGE
bool NormNodeGetWatermarkEx(NormNodeHandle nodeHandle,
                            char*          buffer,
                            unsigned int*  buflen)
{
    bool result = false;
    if (NORM_NODE_INVALID != nodeHandle)
    {
        NormInstance* instance = NormInstance::GetInstanceFromNode(nodeHandle);
        if (instance && instance->dispatcher.SuspendThread())
        {
            NormNode* node = (NormNode*)nodeHandle;
            if (NormNode::SENDER == node->GetType())
            {
                NormSenderNode* sender = static_cast<NormSenderNode*>(node);
                result = sender->GetWatermarkEx(buffer, buflen);
            }
            instance->dispatcher.ResumeThread();  
        }
    }
    return result;
}  // end NormNodeGetWatermarkEx()

NORM_API_LINKAGE
void NormNodeFreeBuffers(NormNodeHandle nodeHandle)
{
    if (NORM_NODE_INVALID != nodeHandle)
    {
        NormInstance* instance = NormInstance::GetInstanceFromNode(nodeHandle);
        if (instance && instance->dispatcher.SuspendThread())
        {
            NormNode* node = (NormNode*)nodeHandle;
            if (NormNode::SENDER == node->GetType())
            {
                NormSenderNode* sender = static_cast<NormSenderNode*>(node);
                sender->FreeBuffers();
                // Since this results in aborted objects, should we purge those object notifications?
                // or let the be delivered since the app may have associate state
            }
            instance->dispatcher.ResumeThread(); 
        }
    }
}  // end NormNodeFreeBuffers()

NORM_API_LINKAGE
void NormNodeDelete(NormNodeHandle nodeHandle)
{
    if (NORM_NODE_INVALID != nodeHandle)
    {
        NormInstance* instance = NormInstance::GetInstanceFromNode(nodeHandle);
        if (instance && instance->dispatcher.SuspendThread())
        {
            NormNode* node = (NormNode*)nodeHandle;
            if (NormNode::SENDER == node->GetType())
            {
                NormSenderNode* sender = static_cast<NormSenderNode*>(node);
                sender->GetSession().DeleteRemoteSender(*sender);
            }
            // else if NormNode::ACKER, should we remove from acking node list???
            instance->PurgeNodeNotifications(nodeHandle);
            instance->dispatcher.ResumeThread(); 
        }
    }
}  // end NormNodeDelete()

NORM_API_LINKAGE
void NormNodeRetain(NormNodeHandle nodeHandle)
{
    if (NORM_NODE_INVALID != nodeHandle)
    {
        NormInstance* instance = NormInstance::GetInstanceFromNode(nodeHandle);
        if (instance && instance->dispatcher.SuspendThread())
        {
            ((NormNode*)nodeHandle)->Retain();
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
            ((NormNode*)nodeHandle)->Release();
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

/*
// Not sure why this function exists.  It just counts the number
// of RX_OBJECT_COMPLETED events currently in notification queue
// which is a temporary and transitory thing???
NORM_API_LINKAGE
UINT32 NormCountCompletedObjects(NormSessionHandle sessionHandle)
{
    UINT32 result = 0;
    NormSession* session = (NormSession*)sessionHandle;
	NormInstance* instance = NormInstance::GetInstanceFromSession(sessionHandle);
    if (instance && instance->dispatcher.SuspendThread())
    {
        result = instance->CountCompletedObjects(session);
        instance->dispatcher.ResumeThread();
    }
	return result;
}  // end NormCountCompletedObjects()
*/
