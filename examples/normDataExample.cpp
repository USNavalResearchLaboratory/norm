// This example uses the NORM API and demonstrates the use
// of NORM_OBJECT_DATA transmission and reception.
// The "data" sent and received are simple text messages
// of randomly-varying length.

XXX - THIS CODE IS NOT YET A COMPLETE EXAMPLE !!!!!

#include <normApi.h>

// We use some "protolib" functions for
// cross-platform portability
#include <protoDebug.h>
#include <protoTime.h>

#include <stdlib.h>  // for rand(), srand()

void Usage()
{
    fprintf(stderr, "Usage: normDataExample [send][recv]\n");
}




int main(int argc, char* argv[])
{
    bool send = false;
    bool recv = false;
    // Parse any command-line arguments
    int i = 1;
    while (i < argc)
    {
        if (!strcmp("send", argv[i]))
        {
            send = true;
        }
        else if (!strcmp("recv", argv[i]))
        {
            recv = true;
        }
        else
        {
            fprintf(stderr, "normDataExample error: invalid command-line option!\n");
            Usage();
            return -1;
        }  
        return 0;
    }
    
    if (!send && !recv)
    {
        fprintf(stderr, "normDataExample error: not configured as sender or receiver!\n");
        Usage();
        return -1;
    }    
    
    
    // 1) Create a NORM API "NormInstance"
    NormInstanceHandle instance = NormCreateInstance();
    if (NORM_INSTANCE_INVALID == instance)
    {
        fprintf(stderr, "normDataExample error: NormCreateInstance() failure!\n");
        return -1;
    }
    
    
    // 2) Create a NormSession using default "automatic" local node id
    NormSessionHandle session = NormCreateSession(instance,
                                                  "224.1.2.3", 
                                                   6003,
                                                   NORM_NODE_ANY);
    if (NORM_SESSION_INVALID == instance)
    {
        fprintf(stderr, "normDataExample error: NormCreateSession() failure!\n");
        return -1;
    }
    
    // Enable some debugging output here
    //SetDebugLevel(2);
    // Uncomment to turn on debug NORM message tracing
    NormSetMessageTrace(session, true);
    
    // 3) Start receiver operation, if applicable
    if (recv)
    {
        // Start receiver w/ 1MByte buffer per sender
        if (!NormStartReceiver(session, 1024*1024))
        {
            fprintf(stderr, "normDataExample error: NormStartReceiver() failure!\n");
            return -1;
        }        
    }
    
    // 4) Start receiver operation, if applicable
    if (send)
    {
        // a) Pick a random sender "sessionId"
        //    (seed the random generation so we get a new one each time)
        struct timeval currentTime;
        ProtoSystemTime(currentTime);
        srand(currentTime.tv_sec);  // seed random number generator
        NormSessionId sessionId = (NormSessionId)rand();
        
        // b) Set sender rate or congestion control operation
        NormSetTxRate(session, 256.0e+03);  // in bits/second
        
    }
    
    // Enter loop waiting for and handling NormEvents ...
    // (We illustrate usage of a NormDescriptor for this
    //  with our WaitForNormEvent() routine.)
    // When set to "send" the "interval" is used to 
    NormDescriptor normDescriptor = NormGetDescriptor(instance);
    bool keepGoing = true;
    while (keepGoing)
    {
        boolWaitForNormEvent(normDescriptor, timeDelay)
    }  // end while (keepGoing)
    
    
}  // end main()
