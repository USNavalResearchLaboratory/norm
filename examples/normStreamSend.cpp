/******************************************************************************
 Simple NORM_OBJECT_STREAM sender example app using the NORM API

 USAGE: 
 
 normSendStream

 BUILD (Unix): 
 
 g++ -o normStreamSend normStreamSend.cpp -D_FILE_OFFSET_BITS=64 -I../include/ \
     ../lib/libnorm.a ../protolib/lib/libProtokit.a -lpthread
     
     (for MacOS/BSD, add "-lresolv")
     (for Solaris, add "-lnsl -lsocket -lresolv")

******************************************************************************/            


// Notes:
//  1) A series of text messages are sent over a stream


#include "normApi.h"     // for NORM API

#include <stdio.h>       // for printf(), etc
#include <stdlib.h>      // for srand()
#include <string.h>      // for strrchr()
#include <sys/time.h>    // for gettimeofday()
#include <arpa/inet.h>   // for htons()

int main(int argc, char* argv[])
{
    // 0) Default parameter values
    const int MSG_COUNT_MAX = 10;
    const unsigned int MSG_LENGTH_MIN = 40;
    const unsigned int MSG_LENGTH_MAX = 40;
    UINT32 streamBufferSize = 4*1024*1024; // 1 Mbyte stream buffer size
    double normRate = 1.0e+07;           // 10 Mbps default NORM tx rate for fixed rate operation (bits/sec units here)
    double msgRate = -1.0; //1e+06;   // 32 kbits/sec default message rate
    
    // 1) Create a NORM API "NormInstance"
    NormInstanceHandle instance = NormCreateInstance();
    
    // 2) Create a NormSession using default "automatic" local node id (based on IP addr)
    //    TBD - add an option to set a specific NormNodeId
    NormSessionHandle session = NormCreateSession(instance,
                                                  "224.1.2.3", 
                                                   6003,
                                                   1);//NORM_NODE_ANY);
    
    // NOTE: These are some debugging routines available 
    //       (not necessary for normal app use)
    NormSetDebugLevel(3);
    // Uncomment to turn on debug NORM message tracing
    NormSetMessageTrace(session, true);
    // Uncomment to turn on some random packet loss
    //NormSetTxLoss(session, 25.0);  // 25% packet loss for testing purposes
    
    struct timeval currentTime;
    gettimeofday(&currentTime, NULL);
    // Uncomment to get different packet loss patterns from run to run
    // (and a different sender sessionId)
    srand(currentTime.tv_sec);  // seed random number generator
    
    // 3) Set transmission rate
    NormSetTxRate(session, normRate); // in bits/second
    
    //NormSetFlowControl(session, 0.0);
    
    // Init GRTT to low value (3 msec)
    //NormSetGrttEstimate(session, 1.0e-03);
    
    // Disable receiver backoffs (for lower latency, high speed performance)
    // (For large group sizes, the default backoff factor is RECOMMENDED)
    NormSetBackoffFactor(session, 2.0);
    
    // Uncomment to use a _specific_ transmit port number
    // (Can be the same as session port (rx port), but this
    // is _not_ recommended when unicast feedback may be
    // possible! - must be called _before_ NormStartSender())
    //NormSetTxPort(session, 6001); 
    
    // Uncomment to enable TCP-friendly congestion control
    //NormSetCongestionControl(session, true);
    
    // Uncomment to enable rx port reuse (this plus unique NormNodeId's enables same-machine send/recv)
    NormSetRxPortReuse(session, true);
    
    // 4) Start the sender using a random "sessionId"
    NormSessionId sessionId = (NormSessionId)rand();
    NormStartSender(session, sessionId, 4*1024*1024, 1300, 64, 16);

    // Uncomment to set large tx socket buffer size
    // (may be needed to achieve very high packet output rates)
    //NormSetTxSocketBuffer(session, 512000);
    
    // 5) Enqueue the NORM_OBJECT_STREAM object
    // Provide some "info" about this stream (the info is OPTIONAL)
    char dataInfo[256];
    sprintf(dataInfo, "NORM_OBJECT_STREAM message stream ...");
    NormObjectHandle stream = NormStreamOpen(session, streamBufferSize, dataInfo, strlen(dataInfo) + 1);
    if (NORM_OBJECT_INVALID == stream)
    {
        fprintf(stderr, "normStreamSend NormStreamOpen() error!\n");
        return -1;
    }
    
    // 6) Write the first stream message
    //    (we enqueue text strings of random length as messages)
    //    ( a 2-byte network byte order length "header" is in each message)
    unsigned int msgCount = 0;
    char data = 'a';
    char msgData[MSG_LENGTH_MAX];
    UINT16 msgLen = MSG_LENGTH_MIN + (rand() % (MSG_LENGTH_MAX - MSG_LENGTH_MIN + 1));
    // set 2 byte message header (length in network byte order)
    UINT16 msgHeader = htons(msgLen);      
    memcpy(msgData, &msgHeader, 2);        // 2-byte message length "header"   
    memset(msgData + 2, data, msgLen - 3); // n-byte message content           
    msgData[msgLen - 1] = '\0';            // 1-byte NULL-termination          
    
    // Write the message (as much as stream buffer will accept)
    unsigned int bytesWritten = NormStreamWrite(stream, msgData, msgLen);
    bool vacancy = (bytesWritten == msgLen);
    
    // Initialize the "delayTime" used in "select()" loop below
    // based on whether message was competely written to stream
    // (i.e. wait according to "msgRate" (configured bytes per second))
    double delayTime;
    if (vacancy)
    {
        // Complete message was written, wait msg interval time
        NormStreamMarkEom(stream);
        msgCount++;  
        delayTime = (msgRate > 0.0) ? ((double)msgLen / msgRate) : 0.0;
    }
    else
    {
        // wait indefinitely for NORM_TX_QUEUE_VACANCY event
        // to finish writing current message to stream
        delayTime = -1.0;
    }
    
    
    // 6) We keep a running "timeAccumulator" value to maintain the proper 
    //    _average_ message transmission rate. (TBD - impose "max" accumulation limit)
    double timeAccumulator = 0.0;
    struct timeval lastTime;
    gettimeofday(&lastTime, NULL);
    
    // 7) We use a "select()" call to wait for NORM events or message interval timeout
    int normfd = NormGetDescriptor(instance);
    fd_set fdset;
    
    struct timeval timeout;
    
    // 6) Enter NORM event loop
    bool keepGoing = true;
    while (keepGoing)
    {
        FD_SET(normfd, &fdset);
        struct timeval* timeoutPtr;
        if (delayTime < 0.0)
        {
            timeoutPtr = NULL;  // wait indefinitely (i.e. for queue vacancy)
        }
        else
        {
            if (delayTime > timeAccumulator)
                delayTime -= timeAccumulator;
            else
                delayTime = 0.0;
            timeout.tv_sec = (unsigned long)delayTime;
            timeout.tv_usec = (unsigned long)(1.0e+06 * (delayTime - (double)timeout.tv_sec));
            timeoutPtr = &timeout;
        }
             
        int result = select(normfd+1, &fdset, NULL, NULL, timeoutPtr);
        
        bool keepSending = true;
        if ((MSG_COUNT_MAX > 0) && (msgCount >= (unsigned int)MSG_COUNT_MAX))
            keepSending = false;
        
        if (result > 0)
        {        
            // Get and handle NORM API event
            NormEvent theEvent;
            if (NormGetNextEvent(instance, &theEvent))
            {
                switch (theEvent.type)
                {
                    case NORM_TX_QUEUE_EMPTY:
                    case NORM_TX_QUEUE_VACANCY:
                    {
                        /*
                        if (NORM_TX_QUEUE_VACANCY == theEvent.type)
                            fprintf(stderr, "normStreamSend: NORM_TX_QUEUE_VACANCY event ...\n");
                        else
                            fprintf(stderr, "normStreamSend: NORM_TX_QUEUE_EMPTY event ...\n");
                        */
                        if (keepSending && (bytesWritten < msgLen))
                        {
                            // Finish writing remaining pending message content (as much as can be written)
                            bytesWritten += NormStreamWrite(stream, msgData + bytesWritten, msgLen - bytesWritten);
                            if (bytesWritten == msgLen)
                            {
                                // Complete message was written, wait msg interval time
                                NormStreamMarkEom(stream);
                                msgCount++;  
                                delayTime = (msgRate > 0.0) ? ((double)msgLen / msgRate) : 0.0;
                                vacancy = true;
                            }
                        }                    
                        break;   
                    }

                    case NORM_TX_OBJECT_PURGED:
                        fprintf(stderr, "normStreamSend: NORM_TX_OBJECT_PURGED event ...\n");
                        break;

                    case NORM_TX_FLUSH_COMPLETED:
                        fprintf(stderr, "normStreamSend: NORM_TX_FLUSH_COMPLETED event ...\n");
                        break;
                
                    case NORM_GRTT_UPDATED:
                        fprintf(stderr, "normStreamSend: NORM_GRTT_UPDATED event ...\n");
                        break;

                    default:
                        fprintf(stderr, "normStreamSend: Got event type: %d\n", theEvent.type); 
                }  // end switch(theEvent.type)
            }  // end if (NormGetNextEvent())
        }
        else if (result < 0)
        {
            // select() error
            perror("normStreamSend: select() error");
            break;
        }
        
        
        // This code writes _new_ message(s) to the stream _if_  there is "vacancy"
        // and it is time based on "msgRate" and how much time has passed since "lastTime" 
        struct timeval currentTime;
        gettimeofday(&currentTime, NULL);
        double timeDelta = (double)(currentTime.tv_sec - lastTime.tv_sec);
        if (currentTime.tv_usec > lastTime.tv_usec)
            timeDelta += 1.0e-06 * (currentTime.tv_usec - lastTime.tv_usec);
        else
            timeDelta -= 1.0e-06 * (lastTime.tv_usec - currentTime.tv_usec);
        timeAccumulator += timeDelta;
        while (keepSending && vacancy && (timeAccumulator > delayTime))
        {
            timeAccumulator -= delayTime;  // subtract last message tx duration from accumulator
            // Fill buffer with new message "data" text character (a-z)
            if (++data > 'z') data = 'a';
            msgLen = MSG_LENGTH_MIN  + (rand() % (MSG_LENGTH_MAX - MSG_LENGTH_MIN + 1));
            // set 2 byte message header (length in network byte order)
            msgHeader = htons(msgLen);
            memcpy(msgData, &msgHeader, 2);      // 2-byte message length "header"
            memset(msgData+2, data, msgLen-3);   // n-byte message content
            msgData[msgLen - 1] = '\0';          // 1-byte NULL-termination
            bytesWritten = NormStreamWrite(stream, msgData, msgLen);
            if (bytesWritten < msgLen)
            {
                // wait indefinitely for NORM_TX_QUEUE_VACANCY event
                // to finish writing current message to stream
                vacancy = false;
                delayTime = -1.0;
                //fprintf(stderr, "norm tx stream buffer full, time accumulator = %lf\n", timeAccumulator);
            }
            else
            {
                // Complete message was written, wait msg interval time
                NormStreamMarkEom(stream);
                msgCount++;  
                delayTime = (msgRate > 0.0) ? ((double)msgLen / msgRate) : 0.0;
                if ((MSG_COUNT_MAX > 0) && ((unsigned int)msgCount >= MSG_COUNT_MAX))
                {
                    fprintf(stderr, "closing stream after %u messages ...\n", msgCount);
                    NormStreamClose(stream, true);  // gracefully close stream
                    keepSending = false;
                }
            }
        }
        if (timeAccumulator <= delayTime) 
        {
            delayTime -= timeAccumulator;
            timeAccumulator = 0.0;
        }
        lastTime = currentTime;
        
        
    }  // end while (keepGoing)
    
    NormStopSender(session);
    NormDestroySession(session);
    NormDestroyInstance(instance);
    
    fprintf(stderr, "normDataSend: Done.\n");
    return 0;
}  // end main()
