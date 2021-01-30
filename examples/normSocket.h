
#ifndef _NORM_SOCKET
#define _NORM_SOCKET

// IMPORTANT NOTE:  THIS IS A WORK IN PROGRESS. This code, if/when fully developed, will be 
//                  "promoted" from the 'examples' directory to a standard part of the NORM
//                  library if appropriate.

// This provides a higher level API that facilitates a socket-like programming interface
// to use NORM for a specific usage pattern.  The usage pattern uses NORM_OBJECT_STREAM
// (as a byte-stream initially with eventual support for NORM's message-stream capability, too)
// where "clients" explicitly connect to a "server" and bi-directional stream communication is
// established.  This enables ACK-based flow control to/from the server and client(s). This
// is supported for NORM unicast and also (as this is completed) an asymmetric model where the
// server can multicast to the clients while the clients unicast back to the server.


// TODO:
//
// 0) Make "NormInstanceHandle" optional for Listen/Connect/Accept
//    (for eventual blocking usage, it wouldn't be needed)
// 1) Support asymmetric server->multicast, client->unicast model (DONE)
// 2) Provide non-blocking option for read/write calls
// 3) leverage ICMP (e.g., port unreachable) feedback to detect connect() failure (DONE)
// 4) provide means for connect() to time out??? (DONE)
// 5) Separate socket open (i.e. session creation) from connect/accept to allow
//    opportunity for application to set NORM parameters prior to sender/receiver start
// 6) Extend to include message stream, and data/file object sockets
// 7) Option to associate NORM_INFO w/ stream (or objects eventually)


// Here is a list of NORM parameters that need to be set _before_ sender or receiver
// startup.  These will need to be "NormSocket" parameters that can be set after
// NormOpen() but before NormListen(), NormAccept(), or NormConnect():
//
// a) SSM source address (perhaps incorporate into NormListen() and NormConnect()?)
// b) TTL (could be set _after_ connect/accept, but may be something convenient to preset?
// c) TOS (same as for TTL)
// d) Fragmentation (not one to worry much about initially?)
// e) FEC parameters (block size, parity count, segment size, "auto" parity)
// f) Buffer size (sender, stream, receiver)
// g) cache bounds / limits


#ifdef WIN32
#include <BaseTsd.h>    // for SSIZE_T
typedef SSIZE_T ssize_t;
#endif // WIN32

#include "normApi.h"

typedef const void* NormSocketHandle;

extern const NormSocketHandle NORM_SOCKET_INVALID;

extern const double NORM_DEFAULT_CONNECT_TIMEOUT;

#ifndef NULL
#define NULL 0
#endif // !NULL

// Main NormSocket API Functions

NormSocketHandle NormOpen(NormInstanceHandle instance);

bool NormListen(NormSocketHandle    normSocket, 
                UINT16              serverPort, 
                const char*         groupAddr = NULL,
                const char*         serverAddr = NULL);  

bool NormConnect(NormSocketHandle   normSocket, 
                 const char*        serverAddr, 
                 UINT16             serverPort, 
                 UINT16             localPort = 0,
                 const char*        groupAddr = NULL, 
                 NormNodeId         clientId = NORM_NODE_ANY);

NormSocketHandle NormAccept(NormSocketHandle    serverSocket, 
                            NormNodeHandle      clientNode, 
                            NormInstanceHandle  instance = NORM_INSTANCE_INVALID);

void NormReject(NormSocketHandle    serverSocket,
                NormNodeHandle      clientNode);
        
void NormShutdown(NormSocketHandle normSocket);

void NormClose(NormSocketHandle normSocket);

ssize_t NormRead(NormSocketHandle normSocket, void* buf, size_t nbyte);

ssize_t NormWrite(NormSocketHandle normSocket, const void* buf, size_t nbyte);

int NormFlush(NormSocketHandle normSocket);

// NormSocket helper functions

void NormSetSocketUserData(NormSocketHandle normSocket, const void* userData);
const void* NormGetSocketUserData(NormSocketHandle normSocket);
        
NormInstanceHandle NormGetSocketInstance(NormSocketHandle normSocket);
NormSessionHandle NormGetSocketSession(NormSocketHandle normSocket);
NormSessionHandle NormGetSocketMulticastSession(NormSocketHandle normSocket);
void NormGetPeerName(NormSocketHandle normSocket, char* addr, unsigned int* addrLen, UINT16* port);
NormObjectHandle NormGetSocketTxStream(NormSocketHandle normSocket);
NormObjectHandle NormGetSocketRxStream(NormSocketHandle normSocket);

void NormSetSocketFlowControl(NormSocketHandle normSocket, bool enable);
void NormSetSocketTrace(NormSocketHandle normSocket, bool enable);

typedef enum NormSocketEventType
{
    NORM_SOCKET_NONE = 0,       // applications should generally ignore these
    NORM_SOCKET_ACCEPT,         // only issued for listening server sockets
    NORM_SOCKET_CONNECT,        // notification confirming connection to server
    NORM_SOCKET_READ,           // edge-triggered notification that socket is ready for reading
    NORM_SOCKET_WRITE,          // edge-triggered notification that socket is ready for writing
    NORM_SOCKET_CLOSING,        // indicates remote endpoint is closing socket (only read data at this point)
    NORM_SOCKET_CLOSE           // indicates socket is now closed (invalid for further operations)
} NormSocketEventType;
    
    
// Right now, these options MUST be set after NormOpen() 
// but _before_ NormConnect() or NormListen()
// (This will be expanded, perhaps to include parameters 
//  that can be changed after connection setup)
typedef struct 
{
    UINT16          num_data;
    UINT16          num_parity;
    UINT16          num_auto;
    UINT16          segment_size;
    unsigned int    buffer_size;      // used for both FEC and stream buffer sizing
    bool            silent_receiver;  // not yet used  (maybe should be nack_mode instead)
    int             max_delay;        // not yet used
} NormSocketOptions;

void NormGetSocketOptions(NormSocketHandle normSocket, NormSocketOptions* options);
bool NormSetSocketOptions(NormSocketHandle normSocket, NormSocketOptions* options);
    
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
typedef struct
{
    NormSocketEventType type;
    NormSocketHandle    socket;
    // The underlying NormEvent is embedded.  The union
    // here lets us access the NormEvent fields directly.
    // (E.g., *.sender, *.object, etc. as needed)
    union
    {
        NormEvent   event;  // underlying NormEvent that evoked this socketEvent
        struct
        {
            NormEventType       etype;      // underlying NormEventType
            NormSessionHandle   session;    // NormEvent session
            NormNodeHandle      sender;     // NormEvent sender
            NormObjectHandle    object;     // NormEvent object
        };
    };
} NormSocketEvent;
#pragma GCC diagnostic pop
    
bool NormGetSocketEvent(NormInstanceHandle normInstance, NormSocketEvent* event, bool waitForEvent = true);


#endif // !_NORM_SOCKET
