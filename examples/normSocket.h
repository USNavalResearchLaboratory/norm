
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
// 1) Support asymmetric server->multicast, client->unicast model
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

#include "normApi.h"

typedef const void* NormSocketHandle;

extern const NormSocketHandle NORM_SOCKET_INVALID;

extern const double NORM_DEFAULT_CONNECT_TIMEOUT;

#ifndef NULL
#define NULL 0
#endif // !NULL

// Main NormSocket API Functions

NormSocketHandle NormOpen(NormInstanceHandle instance = NORM_INSTANCE_INVALID);

NormSocketHandle NormListen(NormInstanceHandle instance, UINT16 serverPort, const char* groupAddr = NULL);

NormSocketHandle NormConnect(NormInstanceHandle instance, 
                             const char*        serverAddr, 
                              UINT16            serverPort, 
                             const char*        groupAddr = NULL, 
                             NormNodeId         clientId = NORM_NODE_NONE);

NormSocketHandle NormAccept(NormSocketHandle serverSocket, NormNodeHandle clientNode, NormInstanceHandle instance = NORM_INSTANCE_INVALID);
        
void NormShutdown(NormSocketHandle normSocket);

void NormClose(NormSocketHandle normSocket);

ssize_t NormRead(NormSocketHandle normSocket, void *buf, size_t nbyte);

ssize_t NormWrite(NormSocketHandle normSocket, const void *buf, size_t nbyte);

int NormFlush(NormSocketHandle normSocket);

// Helper API functions

NormSessionHandle NormGetSession(NormSocketHandle normSocket);
NormSessionHandle NormGetMulticastSession(NormSocketHandle normSocket);

typedef enum NormSocketEventType
{
    NORM_SOCKET_NONE = 0,
    NORM_SOCKET_ACCEPT,         // only issued for listening server sockets
    NORM_SOCKET_CONNECT,        // notification confirming connection to server
    NORM_SOCKET_READ,           // edge-triggered notification that socket is ready for reading
    NORM_SOCKET_WRITE,          // edge-triggered notification that socket is ready for writing
    NORM_SOCKET_CLOSING,        // indicates remote endpoint is closing socket (only read data at this point)
    NORM_SOCKET_CLOSED          // indicates socket is now closed (invalid for further operations)
} NormSocketEventType;

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
    
bool NormGetSocketEvent(NormInstanceHandle normInstance, NormSocketEvent* event, bool waitForEvent = true);



#endif // !_NORM_SOCKET
