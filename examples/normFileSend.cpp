/******************************************************************************
 Simple NORM file sender example app using the NORM API

 USAGE: 
 
 normSendFile <fileName>

 BUILD (Unix): 
 
 g++ -o normFileSend normFileSend.cpp -D_FILE_OFFSET_BITS=64 -I../common/ \
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
    // 0) The filePath should be argv[1] ...
    if (argc != 2)
    {
        fprintf(stderr, "normFileSend error: invalid argument(s)\n");
        fprintf(stderr, "Usage: normFileSend <file>\n");
        return -1;   
    }    
    const char* filePath = argv[1];
    // Here we determine the "name" portion of the filePath
    // (The file "name" is later used for NORM_INFO content)
    const char* fileName = strrchr(filePath, DIR_DELIMITER);
    if (fileName)
        fileName++;
    else
        fileName = filePath;
    
    
    // 1) Create a NORM API "NormInstance"
    NormInstanceHandle instance = NormCreateInstance();
    
    // 2) Create a NormSession using default "automatic" local node id
    NormSessionHandle session = NormCreateSession(instance,
                                                  "224.1.2.3", 
                                                   6003,
                                                   1);
    
    NormSetRxPortReuse(session, true);
    NormSetMulticastLoopback(session, true);
    
    // NOTE: These are some debugging routines available 
    //       (not necessary for normal app use)
    // (Need to include "common/protoDebug.h" for this
    //SetDebugLevel(2);
    // Uncomment to turn on debug NORM message tracing
    NormSetMessageTrace(session, true);
    // Uncomment to turn on some random packet loss
    //NormSetTxLoss(session, 10.0);  // 10% packet loss
    struct timeval currentTime;
    ProtoSystemTime(currentTime);
    // Uncomment to get different packet loss patterns from run to run
    // (and a different sender sessionId)
    srand(currentTime.tv_sec);  // seed random number generator
    
    // 3) Set transmission rate
    NormSetTxRate(session, 25600.0e+03);  // in bits/second
    
    // Uncomment to use a _specific_ transmit port number
    // (Can be the same as session port (rx port), but this
    // is _not_ recommended when unicast feedback may be
    // possible! - must be called _before_ NormStartSender())
    //NormSetTxPort(session, 6001); 
    
    // Uncomment to enable TCP-friendly congestion control
    //NormSetCongestionControl(session, true);
    
    // 4) Start the sender using a random "sessionId"
    NormSessionId sessionId = (NormSessionId)rand();
    NormStartSender(session, sessionId, 1024*1024, 1400, 64, 16);

    // Uncomment to set large tx socket buffer size
    // (might be needed for high rate sessions)
    //NormSetTxSocketBuffer(session, 512000);
    
    
    // 5) Enqueue the file for transmission
    //    (We use the file _name_ for NORM_INFO)
    //
    NormObjectHandle txFile = 
        NormFileEnqueue(session, filePath,
                        fileName, strlen(fileName));
    
    // 6) Enter NORM event loop
    bool keepGoing = true;
    while (keepGoing)
    {
        NormEvent theEvent;
        if (!NormGetNextEvent(instance, &theEvent)) continue;
        switch (theEvent.type)
        {
            case NORM_TX_QUEUE_VACANCY:
                fprintf(stderr, "normFileSend: NORM_TX_QUEUE_VACANCY event...\n");
                break;
            case NORM_TX_QUEUE_EMPTY:
                fprintf(stderr, "normFileSend: NORM_TX_QUEUE_EMPTY event...\n");
                break;   

            case NORM_TX_OBJECT_PURGED:
                fprintf(stderr, "normFileSend: NORM_TX_OBJECT_PURGED event ...\n");
                break;
                
            case NORM_TX_FLUSH_COMPLETED:
                fprintf(stderr, "normFileSend: NORM_TX_FLUSH_COMPLETED event ...\n");
                keepGoing = false;  // our file has been sent (we think)
                break;
                
            default:
                TRACE("normFileSend: Got event type: %d\n", theEvent.type); 
        }  // end switch(theEvent.type)
    }  // end while (NormGetNextEvent())
    
    NormStopSender(session);
    NormDestroySession(session);
    NormDestroyInstance(instance);
    
    fprintf(stderr, "normFileSend: Done.\n");
    return 0;
}  // end main()
