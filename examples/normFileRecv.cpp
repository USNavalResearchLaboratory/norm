/******************************************************************************
 Simple NORM file receiver example app using the NORM API

 USAGE: 
 
 normRecvFile <rxCacheDirectory>

 BUILD (Unix): 
 
g++ -o normFileRecv normFileRecv.cpp -D_FILE_OFFSET_BITS=64 -I../common/ \
     -I../protolib/include ../lib/libnorm.a ../protolib/lib/libProtokit.a \
     -lpthread
         
     (for MacOS/BSD, add "-lresolv")
     (for Solaris, add "-lnsl -lsocket -lresolv")

******************************************************************************/            


// Notes:
//  1) The program exits once a single file has been received ...
//  2) The received file is written to the <rxCacheDirectory>
//  3) The program also will exit on <CTRL-C> from user
//  4) "normFileRecv" should be started before "normFileSend"

#include "normApi.h"     // for NORM API

#include "protoDefs.h"   // for ProtoSystemTime        
#include "protoDebug.h"  // for SetDebugLevel(), etc   

#include <stdio.h>       // for printf(), etc
#include <stdlib.h>      // for srand()
#include <string.h>      // for strrchr()

#ifdef WIN32
const char DIR_DELIMITER = '\\';
#else
const char DIR_DELIMITER = '/';
#endif // if/else WIN32/UNIX

int main(int argc, char* argv[])
{
    // 0) The rxCachePath should be argv[1] ...
    if (argc != 2)
    {
        fprintf(stderr, "normFileRecv error: invalid argument(s)\n");
        fprintf(stderr, "Usage: normRecvFile <rxCachePath>\n");
        return -1;   
    }    
    const char* rxCachePath = argv[1];
    
    // 1) Create a NORM API "NormInstance"
    NormInstanceHandle instance = NormCreateInstance();
    
    // 2) Create a NormSession using default "automatic" local node id
    NormSessionHandle session = NormCreateSession(instance,
                                                  "224.1.2.3", 
                                                   6003,
                                                   NORM_NODE_ANY);
    
    NormSetRxPortReuse(session, true);
    NormSetMulticastLoopback(session, true);
    
    // NOTE: These are debugging routines available 
    //       (not necessary for normal app use)
    // (Need to include "common/protoDebug.h" for this
    //SetDebugLevel(2);
    // Uncomment to turn on debug NORM message tracing
    //NormSetMessageTrace(session, true);
    // Uncomment to turn on some random packet loss for testing
    NormSetRxLoss(session, 10.0);  // 10% packet loss
    struct timeval currentTime;
    ProtoSystemTime(currentTime);
    // Uncomment to get different packet loss patterns from run to run
    srand(currentTime.tv_sec);  // seed random number generator
    
    // 3) Set receiver file cache path (where received files are mstored)
    if (!NormSetCacheDirectory(instance, rxCachePath))
    {
        fprintf(stderr, "normFileReceive: error setting cache directory\n");
        return -1;   
    }
    
    // 4) Start the receiver with 1 Mbyte buffer per sender
    NormStartReceiver(session, 1024*1024);
  
    // 5) Enter NORM event loop
    bool keepGoing = true;
    while (keepGoing)
    {
        NormEvent theEvent;
        if (!NormGetNextEvent(instance, &theEvent)) continue;
        switch (theEvent.type)
        {
           case NORM_RX_OBJECT_NEW:
                fprintf(stderr, "normFileRecv: NORM_RX_OBJECT_NEW event ...\n");
                break;

            case NORM_RX_OBJECT_INFO:
                // Assume info contains '/' delimited <path/fileName> string
                fprintf(stderr, "normFileRecv: NORM_RX_OBJECT_INFO event ...\n");
                if (NORM_OBJECT_FILE == NormObjectGetType(theEvent.object))
                {
                    char fileName[PATH_MAX];
                    strcpy(fileName, rxCachePath);
                    int pathLen = strlen(fileName);
                    if (DIR_DELIMITER != fileName[pathLen-1])
                    {
                        fileName[pathLen++] = DIR_DELIMITER;
                        fileName[pathLen] = '\0';
                    }
                    unsigned short nameLen = PATH_MAX - pathLen;
                    nameLen = NormObjectGetInfo(theEvent.object, fileName+pathLen, nameLen);
                    fileName[nameLen + pathLen] = '\0';
                    char* ptr = fileName + 5;
                    while ('\0' != *ptr)
                    {
                        if ('/' == *ptr) *ptr = DIR_DELIMITER;
                        ptr++;   
                    }
                    if (!NormFileRename(theEvent.object, fileName))
                        fprintf(stderr, "normFileRecv: NormSetFileName(%s) error\n", fileName);
                }
                break;

            case NORM_RX_OBJECT_UPDATED:
            {
                //fprintf(stderr, "normFileRecv: NORM_RX_OBJECT_UPDATE event ...\n");
                // Uncomment this stuff to monitor file receive progress
                // (At high packet rates, you may want to be careful here and
                //  only calculate/post updates occasionally rather than for
                //  each and every RX_OBJECT_UPDATE event)
                NormSize objectSize = NormObjectGetSize(theEvent.object);
                fprintf(stderr, "sizeof(NormSize) = %d\n", (int)sizeof(NormSize));
                NormSize completed = objectSize - NormObjectGetBytesPending(theEvent.object);
                double percentComplete = 100.0 * ((double)completed/(double)objectSize);
                fprintf(stderr, "normFileRecv: completion status %lu/%lu (%3.0lf%%)\n",
                                (unsigned long)completed, (unsigned long)objectSize, percentComplete);
                break;                 
            }

            case NORM_RX_OBJECT_COMPLETED:
                fprintf(stderr, "normFileRecv: NORM_RX_OBJECT_COMPLETED event ...\n");
                keepGoing = false;
                break;

            case NORM_RX_OBJECT_ABORTED:
                fprintf(stderr, "normFileRecv: NORM_RX_OBJECT_ABORTED event ...\n");
                break;

            case NORM_REMOTE_SENDER_NEW:
                fprintf(stderr, "normFileRecv: NORM_REMOTE_SENDER_NEW event ...\n");
                break;

            case NORM_REMOTE_SENDER_ACTIVE:
                fprintf(stderr, "normFileRecv: NORM_REMOTE_SENDER_ACTIVE event ...\n");
                break;

            case NORM_REMOTE_SENDER_INACTIVE:
                fprintf(stderr, "normFileRecv: NORM_REMOTE_SENDER_INACTIVE event ...\n");
                break;

            default:
                fprintf(stderr, "normFileRecv: Got event type: %d\n", theEvent.type); 
        }  // end switch(theEvent.type)
    }
    NormStopReceiver(session);
    NormDestroySession(session);
    NormDestroyInstance(instance);
    
    fprintf(stderr, "normFileRecv: Done.\n");
    return 0;
}  // end main()
