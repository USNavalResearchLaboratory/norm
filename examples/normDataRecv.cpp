/******************************************************************************
 Simple NORM_OBJECT_DATA receiver example app using the NORM API

 USAGE: 
 
 normDataRecv

 BUILD (Unix): 
 
 g++ -o normDataRecv normDataRecv.cpp -D_FILE_OFFSET_BITS=64 -I../include/ \
     ../lib/libnorm.a ../protolib/lib/libProtokit.a \
     -lpthread
     
     (for MacOS/BSD, add "-lresolv")
     (for Solaris, add "-lnsl -lsocket -lresolv")

******************************************************************************/            


// Notes:
//  1) The program exits once a single file has been received ...
//  2) The received file is written to the <rxCacheDirectory>
//  3) The program also will exit on <CTRL-C> from user
//  4) "normDataRecv" should be started before "normFileSend"

#include "normApi.h"     // for NORM API

#include "normMessage.h"

#include "protoAddress.h"  // for ProtoAddress for easy mcast test

#include <stdio.h>       // for printf(), etc
#include <stdlib.h>      // for srand()
#include <string.h>      // for strrchr()
#include <sys/time.h>    // for gettimeofday()


// Usage: normDataRecv [addr <addr>/<port>]


#include "protoBitmask.h"

void Usage()
{
    fprintf(stderr, "Usage: normDataRecv [addr <addr>/<port>]\n");
}



int main(int argc, char* argv[])
{
    // Initialize default parameters.
    char sessionAddr[256];
    strcpy(sessionAddr, "224.1.2.3");
    UINT16 sessionPort = 6003;
    
    // Parse command-line for any parameters
    int i = 1;
    while (i < argc)
    {
        if (0 == strcmp("addr", argv[1]))
        {
            i++;
            if (i == argc)
            {
                fprintf(stderr, "normDataRecv error: missing \"addr\" arguments\n");
                Usage();
                return -1;
            }
            strcpy(sessionAddr, argv[i++]);
            char* ptr = strchr(sessionAddr, '/');
            if (NULL != ptr)
            {
                *ptr = '\0';
                sessionPort = atoi(ptr+1);
            }           
        }
        else
        {
            fprintf(stderr, "normDataRecv error: invalid command \"%s\"\n", argv[i]);
            Usage();
            return -1;
        }
    }
    
    // 1) Create a NORM API "NormInstance"
    NormInstanceHandle instance = NormCreateInstance();
    
    // 2) Create a NormSession using default "automatic" local node id
    fprintf(stderr, "joining session at addr/port %s/%hu\n", sessionAddr, sessionPort);
    NormSessionHandle session = NormCreateSession(instance,
                                                  sessionAddr, 
                                                  sessionPort,
                                                  2);//NORM_NODE_ANY);
    
    
    // NOTE: These are debugging routines available 
    //       (not necessary for normal app use)
    NormSetDebugLevel(3);
    // Uncomment to turn on debug NORM message tracing
    //NormSetMessageTrace(session, true);
    // Uncomment to turn on some random packet loss for testing
    //NormSetRxLoss(session, 10.0);  // 10% packet loss
    struct timeval currentTime;
    gettimeofday(&currentTime, NULL);
    // Uncomment to get different packet loss patterns from run to run
    srand(currentTime.tv_sec);  // seed random number generator
    
    // Uncomment if unicast NACKing is desired
    // (We "cheat" here and use the Protolib "ProtoAddress" class
    //  that NORM happens to use under the hood as well)
    ProtoAddress theAddr;
    theAddr.ResolveFromString(sessionAddr);
    if (!theAddr.IsMulticast())
    {
        NormSetDefaultUnicastNack(session, true);
        // Set the tx port equal to session port so sender socket
        // and make it "connected" to the remote sender address (sessionAddr) and port (sessionPort+1)
        // (This assumes the remote sender address/port is known (sessionAddr/sessionPort+1 in this case)
        NormSetTxPort(session, sessionPort);
        NormSetRxPortReuse(session, true, NULL, sessionAddr, sessionPort+1);
        
    }
    else
    {
        // Uncomment to allow multiple NORM processes on same session port number
        //NormSetRxPortReuse(session, true, sessionAddr);
        
        // NormSetDefaultUnicastNack(session, true);
    }
    
    // Set a big rx cache for our current testing
    NormSetRxCacheLimit(session, 4096);
    
    // 3) Start the receiver with 1 Mbyte buffer per sender
    NormStartReceiver(session, 1024*1024);
    
    struct timeval startTime, endTime;  // to measure object transfer time
  
    // 4) Enter NORM event loop
    bool keepGoing = true;
    while (keepGoing)
    {
        NormEvent theEvent;
        if (!NormGetNextEvent(instance, &theEvent)) continue;
        switch (theEvent.type)
        {
           case NORM_RX_OBJECT_NEW:
                gettimeofday(&startTime, NULL);
                //fprintf(stderr, "normDataRecv: NORM_RX_OBJECT_NEW event ...\n");
                break;

            case NORM_RX_OBJECT_INFO:
                // Assume info contains '/' delimited <path/fileName> string
                //fprintf(stderr, "normDataRecv: NORM_RX_OBJECT_INFO event ...\n");
                if (NORM_OBJECT_DATA == NormObjectGetType(theEvent.object))
                {
                    char dataInfo[8192];
                    unsigned int nameLen = NormObjectGetInfo(theEvent.object, dataInfo, 8191);
                    dataInfo[nameLen] = '\0';
                    //fprintf(stderr, "normDataRecv: info = \"%s\"\n", dataInfo);
                }
                break;

            case NORM_RX_OBJECT_UPDATED:
            {
                //fprintf(stderr, "normDataRecv: NORM_RX_OBJECT_UPDATE event ...\n");
                // Uncomment this stuff to monitor file receive progress
                // (At high packet rates, you may want to be careful here and
                //  only calculate/post updates occasionally rather than for
                //  each and every RX_OBJECT_UPDATE event)
                //NormSize objectSize = NormObjectGetSize(theEvent.object);
                //NormSize completed = objectSize - NormObjectGetBytesPending(theEvent.object);
                //double percentComplete = 100.0 * ((double)completed/(double)objectSize);
                //fprintf(stderr, "normDataRecv: object>%p completion status %lu/%lu (%3.0lf%%)\n",
                //                theEvent.object, (unsigned long)completed, (unsigned long)objectSize, percentComplete);
                break;                 
            }

            case NORM_RX_OBJECT_COMPLETED:
            {
                gettimeofday(&endTime, NULL);
                //fprintf(stderr, "normDataRecv: NORM_RX_OBJECT_COMPLETED event ...\n");
                unsigned int objSize = NormObjectGetSize(theEvent.object);
                const char* dataPtr = NormDataAccessData(theEvent.object);
                // next 3 lines are temp for normMsgr testing
                // Validate that the data is complete/accurate
                // a) compare data size against the size embedded in the "INFO"
                char dataInfo[8192];
                unsigned int nameLen = NormObjectGetInfo(theEvent.object, dataInfo, 8191);
                dataInfo[nameLen] = '\0';
                unsigned int dataCount, dataLen;
                if (2 != sscanf(dataInfo, "NORM_OBJECT_DATA count>%u size>%u", &dataCount, &dataLen))
                {
                    fprintf(stderr, "normDataRecv error: received NORM_OBJECT_DATA with invalid INFO?!\n");
                    return -1;
                }                
                if (objSize != dataLen)
                {
                    fprintf(stderr, "normDataRecv error: received NORM_OBJECT_DATA with bad object size?!\n");
                    return -1;
                }
                // b) check the data content
                char data = *dataPtr;
                for (unsigned int i = 0; i < dataLen; i++)
                {
                    if (dataPtr[i] != data)
                    {
                        fprintf(stderr, "normDataRecv error: received bad NORM_OBJECT_DATA!\n");
                        return -1;
                    }
                }
                double transferTime = endTime.tv_sec - startTime.tv_sec;
                if (endTime.tv_usec > startTime.tv_usec)
                    transferTime += 1.0e-06 * (double)(endTime.tv_usec - startTime.tv_usec);
                else
                    transferTime -= 1.0e-06 * (double)(startTime.tv_usec - endTime.tv_usec);
                double transferRate = (8.0/1000.0) * (double)objSize / transferTime;
                fprintf(stderr, "normDataRecv: transfer duration %lf sec at %lf kbps\n", transferTime, transferRate);
                //fprintf(stderr, "normDataRecv: object>%p count>%d size>%u data>%.64s ...\n", 
                //                theEvent.object, dataCount, objSize, dataPtr);
                // NOTE: Since we did not "retain" or "detach data" from this
                // received data object, it (and its data) will be deleted upon
                // the next call to "NormGetNextEvent()".
                //keepGoing = false;
                break;
            }
            case NORM_RX_OBJECT_ABORTED:
                fprintf(stderr, "normDataRecv: NORM_RX_OBJECT_ABORTED event ...\n");
                break;

            case NORM_REMOTE_SENDER_NEW:
                fprintf(stderr, "normDataRecv: NORM_REMOTE_SENDER_NEW event ...\n");
                break;

            case NORM_REMOTE_SENDER_ACTIVE:
                fprintf(stderr, "normDataRecv: NORM_REMOTE_SENDER_ACTIVE event ...\n");
                break;

            case NORM_REMOTE_SENDER_INACTIVE:
                fprintf(stderr, "normDataRecv: NORM_REMOTE_SENDER_INACTIVE event ...\n");
                break;

            default:
                //fprintf(stderr, "normDataRecv: Got event type: %d\n", theEvent.type); 
                break;
        }  // end switch(theEvent.type)
    }
    NormStopReceiver(session);
    NormDestroySession(session);
    NormDestroyInstance(instance);
    
    fprintf(stderr, "normDataRecv: Done.\n");
    return 0;
}  // end main()
