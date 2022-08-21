/******************************************************************************
 Simple NORM_OBJECT_STREAM receiver example app using the NORM API
 (expects NORM message stream of text messages with 2-byte length header (network byte order))

 USAGE: 
 
 normStreamRecv

 BUILD (Unix): 
 
 g++ -o normStreamRecv normStreamRecv.cpp -D_FILE_OFFSET_BITS=64 -I../include/ \
     ./lib/libnorm.a ../protolib/lib/libProtokit.a -lpthread
     
     (for MacOS/BSD, add "-lresolv")
     (for Solaris, add "-lnsl -lsocket -lresolv")

******************************************************************************/            


// Notes:
//  1) The program also will exit on <CTRL-C> from user
//  2) "normStreamRecv" should be started before "normStreamSend" but can join stream in progress.
//  3) This example is designed to receive a single stream from a single sender (could be modified
//     to support multiple streams and/or senders)

#include "normApi.h"     // for NORM API

#include <stdio.h>       // for printf(), etc
#include <stdlib.h>      // for srand()
#include <string.h>      // for strrchr()
#include <sys/time.h>    // for gettimeofday()
#include <arpa/inet.h>   // for ntohs()

int main(int argc, char* argv[])
{
    // 0) Some default params
    const unsigned int MSG_LENGTH_MAX = 5000;
    
    
    // 1) Create a NORM API "NormInstance"
    NormInstanceHandle instance = NormCreateInstance();
    
    // 2) Create a NormSession using default "automatic" local node id
    NormSessionHandle session = NormCreateSession(instance,
                                                  "224.1.2.3", 
                                                   6003,
                                                   NORM_NODE_ANY);
    
    // NOTE: These are debugging routines available 
    //       (not necessary for normal app use)
    // (Need to include "common/protoDebug.h" for this
    NormSetDebugLevel(3);
    // Uncomment to turn on debug NORM message tracing
    NormSetMessageTrace(session, true);
    // Uncomment to write debug output to file "normLog.txt"
    //NormOpenDebugLog(instance, "normLog.txt");
    
    
    // Uncomment to turn on some random packet loss for testing
    //NormSetRxLoss(session, 10.0);  // 10% packet loss
    struct timeval currentTime;
    gettimeofday(&currentTime, NULL);
    // Uncomment to get different packet loss patterns from run to run
    srand(currentTime.tv_sec);  // seed random number generator
    
    
    // Uncomment to enable rx port reuse (this plus unique NormNodeId's enables same-machine send/recv)
    NormSetRxPortReuse(session, true);
    
    // 3) Start the receiver with 1 Mbyte buffer per sender
    NormStartReceiver(session, 8*1024*1024);
    
    NormSetSilentReceiver(session, true);
    
    if (!NormSetRxSocketBuffer(session, 8*1024*1024))
        perror("normStreamRecv error: unable to set requested socket buffer size");
    
    // We use these variables to keep track of our recv stream
    // and to buffer reading from the recv stream
    NormObjectHandle stream = NORM_OBJECT_INVALID;  // we use this to make sure we're handling the correct (only) stream
    bool msgSync = false;
    char msgBuffer[MSG_LENGTH_MAX];
    UINT16 msgLen = 0;
    UINT16 msgIndex = 0;
  
    // 4) Enter NORM event loop
    bool keepGoing = true;
    while (keepGoing)
    {
        NormEvent theEvent;
        if (!NormGetNextEvent(instance, &theEvent)) continue;
        switch (theEvent.type)
        {
           case NORM_RX_OBJECT_NEW:
                fprintf(stderr, "normStreamRecv: NORM_RX_OBJECT_NEW event ...\n");
                if (NORM_OBJECT_INVALID == stream)
                {
                    if (NORM_OBJECT_STREAM == NormObjectGetType(theEvent.object))
                    {
                        stream = theEvent.object;
                        msgLen = msgIndex = 0;  // init stream reading state
                        msgSync = false;
                    }
                    else
                    {
                        fprintf(stderr, "normStreamRecv error: received NORM_RX_OBJECT_NEW for non-stream object?!\n");
                    }
                }
                else
                {
                    fprintf(stderr, "normStreamRecv error: received NORM_RX_OBJECT_NEW while already receiving stream?!\n");
                }                 
                break;

            case NORM_RX_OBJECT_INFO:
            {
                // Assume info contains NULL-terminated string
                //fprintf(stderr, "normStreamRecv: NORM_RX_OBJECT_INFO event ...\n");
                if (theEvent.object != stream)
                {
                    fprintf(stderr, "normStreamRecv error: received NORM_RX_OBJECT_UPDATED for unhandled object?!\n");
                    break;
                }
                char streamInfo[8192];
                unsigned int infoLen = NormObjectGetInfo(theEvent.object, streamInfo, 8191);
                streamInfo[infoLen] = '\0';
                fprintf(stderr, "normStreamRecv: NORM_RX_OBJECT_INFO event, info = \"%s\"\n", streamInfo);
                break;
            }
            case NORM_RX_OBJECT_UPDATED:
            {
                //fprintf(stderr, "normStreamRecv: NORM_RX_OBJECT_UPDATED event ...\n");
                if (theEvent.object != stream)
                {
                    fprintf(stderr, "normStreamRecv error: received NORM_RX_OBJECT_UPDATED for unhandled object?!\n");
                    break;
                }
                while (1)
                {
                    // If we're not "in sync", seek message start
                    if (!msgSync)
                    {
                        msgSync = NormStreamSeekMsgStart(stream);
                        if (!msgSync) break;  // wait for next NORM_RX_OBJECT_UPDATED  to re-sync
                    }
                    if (msgIndex < 2)
                    {
                        // We still need to read the 2-byte message header for the next message
                        unsigned int numBytes = 2 - msgIndex;
                        if (!NormStreamRead(stream, msgBuffer+msgIndex, &numBytes))
                        {
                            fprintf(stderr, "normStreamRecv error: broken stream detected, re-syncing ...\n");
                            msgLen = msgIndex = 0;
                            msgSync = false;
                            continue;  // try to re-sync and read again
                        }
                        msgIndex += numBytes;
                        if (msgIndex < 2) break; // wait for next NORM_RX_OBJECT_UPDATED to read more
                        memcpy(&msgLen, msgBuffer, 2);
                        msgLen = ntohs(msgLen);
                        if ((msgLen < 2) || (msgLen > MSG_LENGTH_MAX))
                        {
                            fprintf(stderr, "normStreamRecv error: message received with invalid length?!\n");
                            msgLen = msgIndex = 0;
                            msgSync = false;
                            continue;  // try to re-sync and read again
                        }
                    }
                    // Read "content" portion of message (note "msgIndex" accounts for length "header"
                    unsigned int numBytes = msgLen - msgIndex;
                    if (!NormStreamRead(stream, msgBuffer+msgIndex, &numBytes))
                    {
                        fprintf(stderr, "normStreamRecv error: broken stream detected, re-syncing ...\n");
                        msgLen = msgIndex = 0;
                        msgSync = false;
                        continue;  // try to re-sync and read again
                    }
                    fprintf(stderr, "read %u bytes from stream ...\n", numBytes);
                    msgIndex += numBytes;
                    if (msgIndex == msgLen)
                    {
                        // Complete message read
                        fprintf(stderr, "normStreamRecv msg: %s\n", msgBuffer+2);
                        msgLen = msgIndex = 0; // reset state variables for next message
                    }
                    else
                    {
                        break; //  wait for next NORM_RX_OBJECT_UPDATED to read more
                    }
                }  // end while(1) (NormStreamRead() loop)
                break;                 
            }
            case NORM_RX_OBJECT_COMPLETED:
            {
                fprintf(stderr, "normStreamRecv: NORM_RX_OBJECT_COMPLETED event ...\n");
                if (stream == theEvent.object)
                {
                    fprintf(stderr, "normStreamRecv: current stream completed ...\n");
                    stream = NORM_OBJECT_INVALID;
                }
                break;
            }
            case NORM_RX_OBJECT_ABORTED:
                fprintf(stderr, "normStreamRecv: NORM_RX_OBJECT_ABORTED event ...\n");
                if (stream == theEvent.object)
                {
                    fprintf(stderr, "normStreamRecv error: current stream aborted ...\n");
                    stream = NORM_OBJECT_INVALID;
                }
                break;

            case NORM_REMOTE_SENDER_NEW:
                fprintf(stderr, "normStreamRecv: NORM_REMOTE_SENDER_NEW event ...\n");
                break;

            case NORM_REMOTE_SENDER_ACTIVE:
                fprintf(stderr, "normStreamRecv: NORM_REMOTE_SENDER_ACTIVE event ...\n");
                break;

            case NORM_REMOTE_SENDER_INACTIVE:
                fprintf(stderr, "normStreamRecv: NORM_REMOTE_SENDER_INACTIVE event ...\n");
                break;
                
            case NORM_GRTT_UPDATED:
                fprintf(stderr, "normStreamRecv: NORM_GRTT_UPDATED event ...\n");
                break;

            default:
                fprintf(stderr, "normStreamRecv: Got event type: %d\n", theEvent.type); 
        }  // end switch(theEvent.type)
    }
    NormStopReceiver(session);
    NormDestroySession(session);
    NormDestroyInstance(instance);
    
    fprintf(stderr, "normStreamRecv: Done.\n");
    return 0;
}  // end main()
