#ifndef _NORM_API
#define _NORM_API

/*******************************************************************************
 * NOTE: To use NORM as a DLL on Win32 platforms, the macro "NORM_USE_DLL" must be 
 *       defined.  Otherwise, the static library "NormLib.lib" is built and should 
 *       be used for your code compilation and linking.
 ********************************************************************************/

#ifdef WIN32
#include <winsock2.h>
#include <windows.h>
#include <basetsd.h>  // for UINT32/INT32, etc types
#ifdef NORM_USE_DLL
#ifdef _NORM_API_BUILD
#define NORM_API_LINKAGE __declspec(dllexport)  // to support building of "Norm.dll"
#else
#define NORM_API_LINKAGE __declspec(dllimport)  // to let apps use "Norm.dll" functions
#endif // if/else _NORM_API_BUILD
#else
#define NORM_API_LINKAGE
#endif // if/else NORM_USE_DLL
#else
#include <sys/types.h>  // for "off_t"
#include <stdint.h>     // for proper uint32_t, etc definitions
typedef int8_t INT8;
typedef int16_t INT16;
#ifdef _USING_X11
typedef long int INT32;
#else
typedef int32_t INT32;
#endif // if/else _USING_X11
typedef uint8_t UINT8;
typedef uint16_t UINT16;
typedef uint32_t UINT32;
#define NORM_API_LINKAGE 
#endif // if/else WIN32/UNIX

////////////////////////////////////////////////////////////
// IMPORTANT NOTICE
//  The NORM API is _very_ much in a developmental phase
//  right now.  So, although this code is available in
//  source code distribution, it is very much subject
//  to change in future revisions.  The goal of the NORM
//  API _will_ be to provide a stable base of function calls
//  for applications to use, even as the underlying NORM
//  C++ code continues to evolve.  But, until this notice
//  is removed, the API shouldn't be considered final.

/** NORM API Types */
typedef const void* NormInstanceHandle;
extern NORM_API_LINKAGE
const NormInstanceHandle NORM_INSTANCE_INVALID;

typedef const void* NormSessionHandle;
extern NORM_API_LINKAGE
const NormSessionHandle NORM_SESSION_INVALID;
typedef UINT16 NormSessionId;

typedef const void* NormNodeHandle;
extern NORM_API_LINKAGE 
const NormNodeHandle NORM_NODE_INVALID;
typedef UINT32 NormNodeId;
extern NORM_API_LINKAGE
const NormNodeId NORM_NODE_NONE;
extern NORM_API_LINKAGE
const NormNodeId NORM_NODE_ANY;

typedef const void* NormObjectHandle;
extern NORM_API_LINKAGE
const NormObjectHandle NORM_OBJECT_INVALID;
typedef UINT16 NormObjectTransportId;

#ifdef WIN32
typedef __int64 NormSize;
#else
typedef off_t NormSize;
#endif // WIN32

NORM_API_LINKAGE
enum NormObjectType
{
    NORM_OBJECT_NONE,
    NORM_OBJECT_DATA,
    NORM_OBJECT_FILE,
    NORM_OBJECT_STREAM   
};
  
NORM_API_LINKAGE
enum NormFlushMode
{
    NORM_FLUSH_NONE,
    NORM_FLUSH_PASSIVE,
    NORM_FLUSH_ACTIVE   
};
    
NORM_API_LINKAGE
enum NormNackingMode
{
    NORM_NACK_NONE,
    NORM_NACK_INFO_ONLY,
    NORM_NACK_NORMAL  
};

NORM_API_LINKAGE
enum NormAckingStatus
{
    NORM_ACK_INVALID,
    NORM_ACK_FAILURE,
    NORM_ACK_PENDING,
    NORM_ACK_SUCCESS  
};

NORM_API_LINKAGE
enum NormProbingMode
{
    NORM_PROBE_NONE,
    NORM_PROBE_PASSIVE,
    NORM_PROBE_ACTIVE  
};
    
NORM_API_LINKAGE
enum NormRepairBoundary
{
    NORM_BOUNDARY_BLOCK,
    NORM_BOUNDARY_OBJECT
};
    
NORM_API_LINKAGE
enum NormEventType
{
    NORM_EVENT_INVALID = 0,
    NORM_TX_QUEUE_VACANCY,
    NORM_TX_QUEUE_EMPTY,
    NORM_TX_FLUSH_COMPLETED,
    NORM_TX_WATERMARK_COMPLETED,
    NORM_TX_OBJECT_SENT,
    NORM_TX_OBJECT_PURGED,
    NORM_LOCAL_SENDER_CLOSED,
    NORM_REMOTE_SENDER_NEW,
    NORM_REMOTE_SENDER_ACTIVE,
    NORM_REMOTE_SENDER_INACTIVE,
    NORM_REMOTE_SENDER_PURGED,
    NORM_RX_OBJECT_NEW,
    NORM_RX_OBJECT_INFO,
    NORM_RX_OBJECT_UPDATED,
    NORM_RX_OBJECT_COMPLETED,
    NORM_RX_OBJECT_ABORTED,
    NORM_GRTT_UPDATED,
    NORM_CC_ACTIVE,
    NORM_CC_INACTIVE
};

typedef struct
{
    NormEventType       type;
    NormSessionHandle   session;
    NormNodeHandle      sender;
    NormObjectHandle    object;
} NormEvent;
    

/** NORM API General Initialization and Operation Functions */
NORM_API_LINKAGE 
NormInstanceHandle NormCreateInstance(bool priorityBoost = false);

NORM_API_LINKAGE 
void NormDestroyInstance(NormInstanceHandle instanceHandle);

NORM_API_LINKAGE 
void NormStopInstance(NormInstanceHandle instanceHandle);

NORM_API_LINKAGE 
bool NormRestartInstance(NormInstanceHandle instanceHandle);

NORM_API_LINKAGE 
bool NormSuspendInstance(NormInstanceHandle instanceHandle);

NORM_API_LINKAGE 
void NormResumeInstance(NormInstanceHandle instanceHandle);



// This MUST be set to enable NORM_OBJECT_FILE reception!
// (otherwise received files are ignored)
NORM_API_LINKAGE
bool NormSetCacheDirectory(NormInstanceHandle instanceHandle, 
                           const char*        cachePath);

// This call blocks until the next NormEvent is ready unless asynchronous
// notification is used (see below)
NORM_API_LINKAGE 
bool NormGetNextEvent(NormInstanceHandle instanceHandle, NormEvent* theEvent);

// The "NormGetDescriptor()" function returns a HANDLE (WIN32) or
// a file descriptor (UNIX) which can be used for async notification
// of pending NORM events. On WIN32, the returned HANDLE can be used 
// with system calls like "WaitForSingleEvent()", and on Unix the
// returned descriptor can be used in a "select()" call.  If this type
// of asynchronous notification is used, calls to "NormGetNextEvent()" will
// not block when the notification is posted.

#ifdef WIN32
typedef HANDLE NormDescriptor;
#else
typedef int NormDescriptor;
#endif // if/else WIN32/UNIX
extern NORM_API_LINKAGE
const NormDescriptor NORM_DESCRIPTOR_INVALID;
NORM_API_LINKAGE 
NormDescriptor NormGetDescriptor(NormInstanceHandle instanceHandle);

/** NORM Session Creation and Control Functions */

NORM_API_LINKAGE 
NormSessionHandle NormCreateSession(NormInstanceHandle instanceHandle,
                                    const char*        sessionAddress,
                                    UINT16     sessionPort,
                                    NormNodeId         localNodeId);

NORM_API_LINKAGE 
void NormDestroySession(NormSessionHandle sessionHandle);

NORM_API_LINKAGE 
void NormSetUserData(NormSessionHandle sessionHandle, const void* userData);

NORM_API_LINKAGE 
const void* NormGetUserData(NormSessionHandle sessionHandle);

NORM_API_LINKAGE 
NormNodeId NormGetLocalNodeId(NormSessionHandle sessionHandle);

NORM_API_LINKAGE 
void NormSetTxPort(NormSessionHandle sessionHandle,
                   UINT16    txPortNumber);

NORM_API_LINKAGE 
void NormSetRxPortReuse(NormSessionHandle sessionHandle,
                        bool              enable,
                        bool              bindToSessionAddress = true);

NORM_API_LINKAGE 
bool NormSetMulticastInterface(NormSessionHandle sessionHandle,
                               const char*       interfaceName);

NORM_API_LINKAGE 
bool NormSetTTL(NormSessionHandle sessionHandle,
                unsigned char     ttl);

NORM_API_LINKAGE 
bool NormSetTOS(NormSessionHandle sessionHandle,
                unsigned char     tos);

NORM_API_LINKAGE 
bool NormSetLoopback(NormSessionHandle sessionHandle,
                     bool              loopback);

// Special functions for debug support
NORM_API_LINKAGE 
void NormSetMessageTrace(NormSessionHandle sessionHandle, bool state);
NORM_API_LINKAGE 
void NormSetTxLoss(NormSessionHandle sessionHandle, double percent);
NORM_API_LINKAGE 
void NormSetRxLoss(NormSessionHandle sessionHandle, double percent);

/** NORM Sender Functions */

NORM_API_LINKAGE 
bool NormStartSender(NormSessionHandle  sessionHandle,
                     NormSessionId      sessionId,
                     UINT32      bufferSpace,
                     UINT16     segmentSize,
                     unsigned char      numData,
                     unsigned char      numParity);

NORM_API_LINKAGE 
void NormStopSender(NormSessionHandle sessionHandle);

NORM_API_LINKAGE 
double NormGetTransmitRate(NormSessionHandle sessionHandle);

NORM_API_LINKAGE 
void NormSetTransmitRate(NormSessionHandle sessionHandle,
                         double            bitsPerSecond);

NORM_API_LINKAGE 
bool NormSetTxSocketBuffer(NormSessionHandle sessionHandle,
                           unsigned int      bufferSize);

NORM_API_LINKAGE 
void NormSetCongestionControl(NormSessionHandle sessionHandle,
                              bool              state);

NORM_API_LINKAGE 
void NormSetTransmitRateBounds(NormSessionHandle sessionHandle,
                               double            rateMin,
                               double            rateMax);

NORM_API_LINKAGE 
void NormSetTransmitCacheBounds(NormSessionHandle sessionHandle,
                                NormSize          sizeMax,
                                UINT32            countMin,
                                UINT32            countMax);

NORM_API_LINKAGE 
void NormSetAutoParity(NormSessionHandle sessionHandle,
                       unsigned char     autoParity);

NORM_API_LINKAGE 
void NormSetGrttEstimate(NormSessionHandle sessionHandle,
                         double            grttEstimate);

NORM_API_LINKAGE
double NormGetGrttEstimate(NormSessionHandle sessionHandle);

NORM_API_LINKAGE 
void NormSetGrttMax(NormSessionHandle sessionHandle,
                    double            grttMax);

NORM_API_LINKAGE 
void NormSetGrttProbingMode(NormSessionHandle sessionHandle,
                            NormProbingMode   probingMode);

NORM_API_LINKAGE 
void NormSetGrttProbingInterval(NormSessionHandle sessionHandle,
                                double            intervalMin,
                                double            intervalMax);

NORM_API_LINKAGE 
void NormSetBackoffFactor(NormSessionHandle sessionHandle,
                          double            backoffFactor);

NORM_API_LINKAGE 
void NormSetGroupSize(NormSessionHandle sessionHandle,
                      unsigned int      groupSize);

NORM_API_LINKAGE 
void NormSetTxRobustFactor(NormSessionHandle sessionHandle,
                           int               robustFactor);

NORM_API_LINKAGE 
NormObjectHandle NormFileEnqueue(NormSessionHandle sessionHandle,
                                 const char*  fileName,
                                 const char*  infoPtr = (const char*)0, 
                                 unsigned int infoLen = 0);

NORM_API_LINKAGE 
NormObjectHandle NormDataEnqueue(NormSessionHandle sessionHandle,
                                 const char*       dataPtr,
                                 UINT32     dataLen,
                                 const char*       infoPtr = (const char*)0, 
                                 unsigned int      infoLen = 0);

NORM_API_LINKAGE 
bool NormRequeueObject(NormSessionHandle sessionHandle, NormObjectHandle objectHandle);
                                     
NORM_API_LINKAGE 
NormObjectHandle NormStreamOpen(NormSessionHandle sessionHandle,
                                UINT32     bufferSize);

NORM_API_LINKAGE 
void NormStreamClose(NormObjectHandle streamHandle, bool graceful = false);

NORM_API_LINKAGE 
unsigned int NormStreamWrite(NormObjectHandle streamHandle,
                             const char*      buffer,
                             unsigned int     numBytes);

NORM_API_LINKAGE 
void NormStreamFlush(NormObjectHandle streamHandle, 
                     bool             eom = false,
                     NormFlushMode    flushMode = NORM_FLUSH_PASSIVE);

NORM_API_LINKAGE 
void NormStreamSetAutoFlush(NormObjectHandle streamHandle,
                            NormFlushMode    flushMode);

NORM_API_LINKAGE 
void NormStreamSetPushEnable(NormObjectHandle streamHandle, 
                             bool             pushEnable);

NORM_API_LINKAGE 
bool NormStreamHasVacancy(NormObjectHandle streamHandle);

NORM_API_LINKAGE 
void NormStreamMarkEom(NormObjectHandle streamHandle);

NORM_API_LINKAGE 
bool NormSetWatermark(NormSessionHandle  sessionHandle,
                      NormObjectHandle   objectHandle);

NORM_API_LINKAGE 
void NormCancelWatermark(NormSessionHandle sessionHandle);

NORM_API_LINKAGE 
bool NormAddAckingNode(NormSessionHandle  sessionHandle,
                       NormNodeId         nodeId);

NORM_API_LINKAGE 
void NormRemoveAckingNode(NormSessionHandle  sessionHandle,
                          NormNodeId         nodeId);

NORM_API_LINKAGE 
NormAckingStatus NormGetAckingStatus(NormSessionHandle  sessionHandle,
                                     NormNodeId         nodeId = NORM_NODE_ANY);

/* NORM Receiver Functions */

NORM_API_LINKAGE 
bool NormStartReceiver(NormSessionHandle  sessionHandle,
                       UINT32      bufferSpace);

NORM_API_LINKAGE 
void NormStopReceiver(NormSessionHandle sessionHandle);

NORM_API_LINKAGE 
bool NormSetRxSocketBuffer(NormSessionHandle sessionHandle,
                           unsigned int      bufferSize);

NORM_API_LINKAGE 
void NormSetSilentReceiver(NormSessionHandle sessionHandle,
                           bool              silent,
                           int               maxDelay = -1);

NORM_API_LINKAGE 
void NormSetDefaultUnicastNack(NormSessionHandle sessionHandle,
                               bool              unicastNacks);

NORM_API_LINKAGE 
void NormNodeSetUnicastNack(NormNodeHandle   remoteSender,
                            bool             unicastNacks);

NORM_API_LINKAGE 
void NormSetDefaultNackingMode(NormSessionHandle sessionHandle,
                               NormNackingMode   nackingMode);
    
NORM_API_LINKAGE 
void NormNodeSetNackingMode(NormNodeHandle   nodeHandle,
                            NormNackingMode  nackingMode);

NORM_API_LINKAGE 
void NormObjectSetNackingMode(NormObjectHandle objectHandle,
                              NormNackingMode  nackingMode);    

NORM_API_LINKAGE 
void NormSetDefaultRepairBoundary(NormSessionHandle  sessionHandle,
                                  NormRepairBoundary repairBoundary); 

NORM_API_LINKAGE 
void NormNodeSetRepairBoundary(NormNodeHandle     nodeHandle,
                               NormRepairBoundary repairBoundary);

NORM_API_LINKAGE 
void NormSetDefaultRxRobustFactor(NormSessionHandle sessionHandle,
                                  int               robustFactor);

NORM_API_LINKAGE 
void NormNodeSetRxRobustFactor(NormNodeHandle   nodeHandle,
                               int              robustFactor);

NORM_API_LINKAGE 
bool NormStreamRead(NormObjectHandle   streamHandle,
                    char*              buffer,
                    unsigned int*      numBytes);

NORM_API_LINKAGE 
bool NormStreamSeekMsgStart(NormObjectHandle streamHandle);

NORM_API_LINKAGE 
UINT32 NormStreamGetReadOffset(NormObjectHandle streamHandle);


/** NORM Object Functions */

NORM_API_LINKAGE 
NormObjectType NormObjectGetType(NormObjectHandle objectHandle);

NORM_API_LINKAGE 
bool NormObjectHasInfo(NormObjectHandle objectHandle);

NORM_API_LINKAGE 
UINT16 NormObjectGetInfoLength(NormObjectHandle objectHandle);

NORM_API_LINKAGE 
UINT16 NormObjectGetInfo(NormObjectHandle objectHandle,
                                  char*           buffer,
                                  UINT16  bufferLen);

NORM_API_LINKAGE 
NormSize NormObjectGetSize(NormObjectHandle objectHandle);

NORM_API_LINKAGE 
NormSize NormObjectGetBytesPending(NormObjectHandle objectHandle);

NORM_API_LINKAGE 
void NormObjectCancel(NormObjectHandle objectHandle);

NORM_API_LINKAGE 
void NormObjectRetain(NormObjectHandle objectHandle);

NORM_API_LINKAGE 
void NormObjectRelease(NormObjectHandle objectHandle);

NORM_API_LINKAGE 
bool NormFileGetName(NormObjectHandle   fileHandle,
                     char*              nameBuffer,
                     unsigned int       bufferLen);

NORM_API_LINKAGE 
bool NormFileRename(NormObjectHandle   fileHandle,
                    const char*        fileName);

NORM_API_LINKAGE 
const char*volatile NormDataAccessData(NormObjectHandle objectHandle);

NORM_API_LINKAGE 
char* NormDataDetachData(NormObjectHandle objectHandle);

NORM_API_LINKAGE 
NormNodeHandle NormObjectGetSender(NormObjectHandle objectHandle);

/** NORM Node Functions */

NORM_API_LINKAGE 
NormNodeId NormNodeGetId(NormNodeHandle nodeHandle);

NORM_API_LINKAGE 
bool NormNodeGetAddress(NormNodeHandle  nodeHandle,
                        char*           addrBuffer, 
                        unsigned int*   bufferLen,
                        UINT16*         port = (UINT16*)0);

NORM_API_LINKAGE
double NormNodeGetGrtt(NormNodeHandle nodeHandle);

NORM_API_LINKAGE
void NormNodeFreeBuffers(NormNodeHandle nodeHandle);

// The next 4 functions have not yet been implemented
// (work in progress)
NORM_API_LINKAGE
void NormNodeSetAutoDelete(NormNodeHandle nodeHandle,
                           bool           autoDelete);

NORM_API_LINKAGE
void NormNodeDeleteSender(NormNodeHandle nodeHandle);

NORM_API_LINKAGE
bool NormNodeAllowSender(NormNodeId senderId);

NORM_API_LINKAGE
bool NormNodeDenySender(NormNodeId senderId);        


NORM_API_LINKAGE 
void NormNodeRetain(NormNodeHandle nodeHandle);

NORM_API_LINKAGE 
void NormNodeRelease(NormNodeHandle nodeHandle);

/** Some experimental functions */

NORM_API_LINKAGE 
UINT32 NormCountCompletedObjects(NormSessionHandle sessionHandle);

#endif // _NORM_API
