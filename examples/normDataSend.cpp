/******************************************************************************
 Simple NORM_OBJECT_DATA sender example app using the NORM API

 USAGE: 
 
 normSendData

 BUILD (Unix): 
 
 g++ -o normDataSend normDataSend.cpp -D_FILE_OFFSET_BITS=64 -I../common/ \
     -I../protolib/include ../lib/libnorm.a ../protolib/lib/libProtokit.a \
     -lpthread
     
     (for MacOS/BSD, add "-lresolv")
     (for Solaris, add "-lnsl -lsocket -lresolv")

******************************************************************************/            


// Notes:
//  1) A single file is sent.
//  2) The program exits upon NORM_TX_FLUSH_COMPLETED notification (or user <CTRL-C>)
//  3) NORM receiver should be started first (before sender starts)


#include "normApi.h"     // for NORM API

#include "protoDefs.h"   // for ProtoSystemTime()       
#include "protoDebug.h"  // for SetDebugLevel(), etc   
#include "protoAddress.h"  // for ProtoAddress for easy mcast test

#include <stdio.h>       // for printf(), etc
#include <stdlib.h>      // for srand()
#include <string.h>      // for strrchr()



// Usage: normDataSend [addr <addr>/<port>]

void Usage()
{
    fprintf(stderr, "Usage: normDataSend [addr <addr>/<port>]\n");
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
                fprintf(stderr, "normDataSend error: missing \"addr\" arguments\n");
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
            fprintf(stderr, "normDataSend error: invalid command \"%s\"\n", argv[i]);
            Usage();
            return -1;
        }
    }
    
    // Is the session an mcast addr?
    ProtoAddress theAddr;
    theAddr.ResolveFromString(sessionAddr);
    bool isMulticastSession = theAddr.IsMulticast();
    
    
    // 1) Create a NORM API "NormInstance"
    NormInstanceHandle instance = NormCreateInstance();
    
    // 2) Create a NormSession using default "automatic" local node id
    NormSessionHandle session = NormCreateSession(instance,
                                                  sessionAddr, 
                                                  sessionPort,
                                                  NORM_NODE_ANY);
    
    // NOTE: These are some debugging routines available 
    //       (not necessary for normal app use)
    NormSetDebugLevel(3);
    // Uncomment to turn on debug NORM message tracing
    //NormSetMessageTrace(session, true);
    // Uncomment to turn on some random packet loss
    //NormSetTxLoss(session, 10.0);  // 10% packet loss
    struct timeval currentTime;
    ProtoSystemTime(currentTime);
    // Uncomment to get different packet loss patterns from run to run
    // (and a different sender sessionId)
    srand(currentTime.tv_sec);  // seed random number generator
    
    // 3) Set transmission rate
    NormSetTxRate(session, 2.0e+06);  // in bits/second
    
    // Uncomment to enable TCP-friendly congestion control
    //NormSetCongestionControl(session, true);
    
    // Uncomment to use a _specific_ transmit port number
    // (Can be the same as session port (rx port), but this
    // is _not_ recommended for mcast sessions when unicast feedback may be
    // possible! - must be called _before_ NormStartSender())
    if (!isMulticastSession) 
    {
        NormSetTxPort(session, sessionPort+1, true); 
    
        // Uncomment to set the session to only open 
        // a single socket for transmission. 
        // This also "connects" the sender to session (receiver) addr/port
        // (unicast nacking _and_ "txPort == rxPort" MUST be used by receivers)
        NormSetTxOnly(session, true, true);
    }
    
    // Uncomment to allow multiple NORM processes on same session port number
    // Note that port reuse only works well for mcast.
    // if (isMulticastSession) NormSetRxPortReuse(session, true);
    
    // 4) Start the sender using a random "sessionId"
    NormSessionId sessionId = (NormSessionId)rand();
    TRACE("starting NORM sender ...\n");
    NormStartSender(session, sessionId, 1024*1024, 1400, 64, 16);

    // Uncomment to set large tx socket buffer size
    // (might be needed for high rate sessions)
    //NormSetTxSocketBuffer(session, 512000);
    
    
    // 5) Enqueue the first data message
    //    (we enqueue text strings of random length as object content)
    unsigned int MAX_COUNT = 1;     // number of objects to send, zero means unlimited
    const int MIN_LENGTH = 460000;  // min object size (in bytes)
    const int MAX_LENGTH = 460000;  // max object size (in bytes)
    
    unsigned int dataCount = 0;
    int dataLen = MIN_LENGTH + rand() % (MAX_LENGTH - MIN_LENGTH + 1);
    char* dataMsg = new char[dataLen];
    ASSERT(NULL != dataMsg);
    char data = 'a';
    memset(dataMsg, data, dataLen);  // set message content with 'dummy' data
    // Provide some "info" about this message
    char dataInfo[256];
    sprintf(dataInfo, "NORM_OBJECT_DATA count>%d size>%d", dataCount, dataLen);
    // Enqueue the data object
    NormObjectHandle  dataObj = 
        NormDataEnqueue(session, dataMsg, dataLen, dataInfo, strlen(dataInfo));
    ASSERT(NORM_OBJECT_INVALID != dataObj);
    dataCount++;
    
    // 6) Enter NORM event loop
    bool keepGoing = true;
    while (keepGoing)
    {
        NormEvent theEvent;
        if (!NormGetNextEvent(instance, &theEvent)) continue;
        switch (theEvent.type)
        {
            case NORM_TX_QUEUE_VACANCY:
                //fprintf(stderr, "normDataSend: NORM_TX_QUEUE_VACANCY event...\n");
                break;
            case NORM_TX_QUEUE_EMPTY:
            {
                if ((0 == MAX_COUNT) || (dataCount < MAX_COUNT))
                {
                    // Enqueue another data object when norm tx queue goes empty
                    //fprintf(stderr, "normDataSend: NORM_TX_QUEUE_EMPTY event...\n");
                    dataLen = MIN_LENGTH + rand() % (MAX_LENGTH - MIN_LENGTH + 1);
                    dataMsg = new char[dataLen];
                    ASSERT(NULL != dataMsg);
                    memset(dataMsg, data, dataLen);
                    sprintf(dataInfo, "NORM_OBJECT_DATA count>%d size>%d data>%.64s ...", dataCount, dataLen, dataMsg);
                    NormObjectHandle  dataObj = 
                        NormDataEnqueue(session, dataMsg, dataLen, dataInfo, strlen(dataInfo));
                    // Note that flow control timer may have prevented NormDataEnqueue()
                    // from succeeding even though we got a NORM_TX_QUEUE_EMPTY notification
                    // (The underlying NORM code could be tightened up a bit here!)
                    if (NORM_OBJECT_INVALID != dataObj) 
                    {
                        dataCount++;
                        if (++data > 'z') data = 'a';
                    }
                    else
                    {
                        TRACE("normDataSend: FLOW CONTROL CONDITION?\n");
                    } 
                }
                break;
            }
            case NORM_TX_OBJECT_PURGED:
            {
                //fprintf(stderr, "normDataSend: NORM_TX_OBJECT_PURGED event ...\n");
                char* dataPtr = NormDataDetachData(theEvent.object);
                delete[] dataPtr;
                break;
            }   
            case NORM_TX_FLUSH_COMPLETED:
                fprintf(stderr, "normDataSend: NORM_TX_FLUSH_COMPLETED event ...\n");
                break;
                
            default:
                //TRACE("normDataSend: Got event type: %d\n", theEvent.type); 
                break;
        }  // end switch(theEvent.type)
    }  // end while (NormGetNextEvent())
    
    NormStopSender(session);
    NormDestroySession(session);
    NormDestroyInstance(instance);
    
    fprintf(stderr, "normDataSend: Done.\n");
    return 0;
}  // end main()
