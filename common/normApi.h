#ifndef _NORM_API
#define _NORM_API

#ifdef WIN32
#include <winsock2.h>
#include <windows.h>
#else
#include <sys/types.h>  // for "off_t"
#endif // WIN32

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


/** NORM API Data Types and Constants
 *  These are data types and constants defined
 * for the NORM API
 */
 
typedef const void* NormInstanceHandle;
extern const NormInstanceHandle NORM_INSTANCE_INVALID;

typedef const void* NormSessionHandle;
extern const NormSessionHandle NORM_SESSION_INVALID;
typedef unsigned short NormSessionId;

typedef const void* NormNodeHandle;
extern const NormNodeHandle NORM_NODE_INVALID;
typedef unsigned long NormNodeId;
extern const NormNodeId NORM_NODE_NONE;
extern const NormNodeId NORM_NODE_ANY;

typedef const void* NormObjectHandle;
extern const NormObjectHandle NORM_OBJECT_INVALID;
typedef unsigned short NormObjectTransportId;

#ifdef WIN32
typedef __int64 NormSize;
#else
typedef off_t NormSize;
#endif // WIN32

enum NormObjectType
{
    NORM_OBJECT_NONE,
    NORM_OBJECT_DATA,
    NORM_OBJECT_FILE,
    NORM_OBJECT_STREAM   
};
    
enum NormFlushMode
{
    NORM_FLUSH_NONE,
    NORM_FLUSH_PASSIVE,
    NORM_FLUSH_ACTIVE   
};
    

enum NormNackingMode
{
    NORM_NACK_NONE,
    NORM_NACK_INFO_ONLY,
    NORM_NACK_NORMAL  
};
    
enum NormRepairBoundary
{
    NORM_BOUNDARY_BLOCK,
    NORM_BOUNDARY_OBJECT
};
    
enum NormEventType
{
    NORM_EVENT_INVALID = 0,
    NORM_TX_QUEUE_VACANCY,
    NORM_TX_QUEUE_EMPTY,
    NORM_TX_FLUSH_COMPLETED,
    NORM_TX_OBJECT_SENT,
    NORM_TX_OBJECT_PURGED,
    NORM_LOCAL_SERVER_CLOSED,
    NORM_REMOTE_SERVER_NEW,
    NORM_REMOTE_SERVER_ACTIVE,
    NORM_REMOTE_SERVER_INACTIVE,
    NORM_REMOTE_SERVER_PURGED,
    NORM_RX_OBJECT_NEW,
    NORM_RX_OBJECT_INFO,
    NORM_RX_OBJECT_UPDATED,
    NORM_RX_OBJECT_COMPLETED,
    NORM_RX_OBJECT_ABORTED
};

typedef struct
{
    NormEventType       type;
    NormSessionHandle   session;
    NormNodeHandle      sender;
    NormObjectHandle    object;
} NormEvent;
    

/** NORM API General Initialization and Operation Functions */

NormInstanceHandle NormCreateInstance(bool priorityBoost = false);

void NormDestroyInstance(NormInstanceHandle instanceHandle);

// This MUST be set to enable NORM_OBJECT_FILE reception!
bool NormSetCacheDirectory(NormInstanceHandle instanceHandle, 
                           const char*        cachePath);

// This call blocks until the next NormEvent is ready unless asynchronous
// notification is used (see below)
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
extern const NormDescriptor NORM_DESCRIPTOR_INVALID;
NormDescriptor NormGetDescriptor(NormInstanceHandle instanceHandle);

/** NORM Session Creation and Control Functions */

NormSessionHandle NormCreateSession(NormInstanceHandle instanceHandle,
                                    const char*        sessionAddress,
                                    unsigned short     sessionPort,
                                    NormNodeId         localNodeId);

void NormDestroySession(NormSessionHandle sessionHandle);

void NormSetUserData(NormSessionHandle sessionHandle, const void* userData);

const void* NormGetUserData(NormSessionHandle sessionHandle);

NormNodeId NormGetLocalNodeId(NormSessionHandle sessionHandle);

void NormSetTxPort(NormSessionHandle sessionHandle,
                   unsigned short    txPortNumber);

bool NormSetMulticastInterface(NormSessionHandle sessionHandle,
                               const char*       interfaceName);

bool NormSetTTL(NormSessionHandle sessionHandle,
                unsigned char     ttl);

bool NormSetTOS(NormSessionHandle sessionHandle,
                unsigned char     tos);

bool NormSetLoopback(NormSessionHandle sessionHandle,
                     bool              loopback);

// Special functions for debug support
void NormSetMessageTrace(NormSessionHandle sessionHandle, bool state);
void NormSetTxLoss(NormSessionHandle sessionHandle, double percent);
void NormSetRxLoss(NormSessionHandle sessionHandle, double percent);

/** NORM Sender Functions */

bool NormStartSender(NormSessionHandle  sessionHandle,
                     NormSessionId      sessionId,
                     unsigned long      bufferSpace,
                     unsigned short     segmentSize,
                     unsigned char      numData,
                     unsigned char      numParity);

void NormStopSender(NormSessionHandle sessionHandle);

void NormSetTransmitRate(NormSessionHandle sessionHandle,
                         double            bitsPerSecond);

bool NormSetTxSocketBuffer(NormSessionHandle sessionHandle,
                           unsigned int      bufferSize);

void NormSetCongestionControl(NormSessionHandle sessionHandle,
                              bool              state);

void NormSetTransmitRateBounds(NormSessionHandle sessionHandle,
                               double            rateMin,
                               double            rateMax);

void NormSetTransmitCacheBounds(NormSessionHandle sessionHandle,
                                NormSize        sizeMax,
                                unsigned long   countMin,
                                unsigned long   countMax);

void NormSetAutoParity(NormSessionHandle sessionHandle,
                       unsigned char     autoParity);

void NormSetGrttEstimate(NormSessionHandle sessionHandle,
                         double            grttEstimate);

bool NormAddAckingNode(NormSessionHandle  sessionHandle,
                       NormNodeId         nodeId);

void NormRemoveAckingNode(NormSessionHandle  sessionHandle,
                          NormNodeId         nodeId);

NormObjectHandle NormFileEnqueue(NormSessionHandle sessionHandle,
                                 const char*  fileName,
                                 const char*  infoPtr = (const char*)0, 
                                 unsigned int infoLen = 0);

bool NormDataEnqueue(const char*   dataPtr,
                     unsigned long dataLen,
                     const char*   infoPtr = (const char*)0, 
                     unsigned int  infoLen = 0);



NormObjectHandle NormStreamOpen(NormSessionHandle sessionHandle,
                                unsigned long     bufferSize);

void NormStreamClose(NormObjectHandle streamHandle, bool graceful = false);

unsigned int NormStreamWrite(NormObjectHandle streamHandle,
                             const char*      buffer,
                             unsigned int     numBytes);

void NormStreamFlush(NormObjectHandle streamHandle, 
                     bool             eom = false,
                     NormFlushMode    flushMode = NORM_FLUSH_PASSIVE);

void NormStreamSetAutoFlush(NormObjectHandle streamHandle,
                            NormFlushMode    flushMode);

void NormStreamSetPushEnable(NormObjectHandle streamHandle, 
                             bool             pushEnable);


bool NormStreamHasVacancy(NormObjectHandle streamHandle);

void NormStreamMarkEom(NormObjectHandle streamHandle);

bool NormSetWatermark(NormSessionHandle  sessionHandle,
                      NormObjectHandle   objectHandle);

/** NORM Receiver Functions */

bool NormStartReceiver(NormSessionHandle  sessionHandle,
                       unsigned long      bufferSpace);

void NormStopReceiver(NormSessionHandle sessionHandle);

bool NormSetRxSocketBuffer(NormSessionHandle sessionHandle,
                           unsigned int      bufferSize);

void NormSetSilentReceiver(NormSessionHandle sessionHandle,
                           bool              silent);

void NormSetDefaultUnicastNack(NormSessionHandle sessionHandle,
                               bool              unicastNacks);

void NormNodeSetUnicastNack(NormNodeHandle   remoteSender,
                            bool             unicastNacks);

void NormSetDefaultNackingMode(NormSessionHandle sessionHandle,
                               NormNackingMode   nackingMode);
    
void NormNodeSetNackingMode(NormNodeHandle   nodeHandle,
                            NormNackingMode  nackingMode);

void NormObjectSetNackingMode(NormObjectHandle objectHandle,
                              NormNackingMode  nackingMode);    

void NormSetDefaultRepairBoundary(NormSessionHandle  sessionHandle,
                                  NormRepairBoundary repairBoundary); 

void NormNodeSetRepairBoundary(NormNodeHandle     nodeHandle,
                               NormRepairBoundary repairBoundary);

bool NormStreamRead(NormObjectHandle   streamHandle,
                    char*              buffer,
                    unsigned int*      numBytes);

bool NormStreamSeekMsgStart(NormObjectHandle streamHandle);

unsigned long NormStreamGetReadOffset(NormObjectHandle streamHandle);


/** NORM Object Functions */

NormObjectType NormObjectGetType(NormObjectHandle objectHandle);

bool NormObjectHasInfo(NormObjectHandle objectHandle);

unsigned short NormObjectGetInfoLength(NormObjectHandle objectHandle);

unsigned short NormObjectGetInfo(NormObjectHandle objectHandle,
                                  char*           buffer,
                                  unsigned short  bufferLen);

NormSize NormObjectGetSize(NormObjectHandle objectHandle);

NormSize NormObjectGetBytesPending(NormObjectHandle objectHandle);

void NormObjectCancel(NormObjectHandle objectHandle);

void NormObjectRetain(NormObjectHandle objectHandle);

void NormObjectRelease(NormObjectHandle objectHandle);

bool NormFileGetName(NormObjectHandle   fileHandle,
                     char*              nameBuffer,
                     unsigned int       bufferLen);

bool NormFileRename(NormObjectHandle   fileHandle,
                    const char*        fileName);

const char*volatile NormDataAccessData(NormObjectHandle objectHandle);

char* NormDataDetachData(NormObjectHandle objectHandle);

NormNodeHandle NormObjectGetSender(NormObjectHandle objectHandle);


/** NORM Node Functions */

NormNodeId NormNodeGetId(NormNodeHandle nodeHandle);

bool NormNodeGetAddress(NormNodeHandle  nodeHandle,
                        char*           addrBuffer, 
                        unsigned int*   bufferLen,
                        unsigned short* port = (unsigned short*)0);

void NormNodeRetain(NormNodeHandle nodeHandle);

void NormNodeRelease(NormNodeHandle nodeHandle);


#endif // _NORM_API
