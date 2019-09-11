// This is a test application for experimenting
// with the NORM API implementation during its
// development.  A better-documented and complete
// example of the NORM API usage will be provided
// when the NORM API is more complete.

#include "normApi.h"
#include "protokit.h"  // for protolib debug, stuff, etc

#include <stdio.h>
#include <stdlib.h>  // for srand()

#ifdef UNIX
#include <unistd.h>  // for "sleep()"
#endif // UNIX
int main(int argc, char* argv[])
{
    printf("normTest starting (sizeof(NormSize) = %d)...\n", sizeof(NormSize));
    
    NormInstanceHandle instance = NormCreateInstance();

#ifdef WIN32
    const char* cachePath = "C:\\Adamson\\Temp\\cache\\";
#else
    const char* cachePath =  "/tmp/";
#endif // if/else WIN32/UNIX

    NormSetCacheDirectory(instance, cachePath);

    NormSessionHandle session = NormCreateSession(instance,
                                                  "224.1.2.3", 
                                                   6003,
                                                   NORM_NODE_ANY);
    
    // NOTE: These are debugging routines available (not for normal app use)
    SetDebugLevel(2);
    // Uncomment to turn on debug NORM message tracing
    //NormSetMessageTrace(session, true);
    // Uncomment to turn on some random packet loss
    NormSetTxLoss(session, 10.0);  // 10% packet loss
    struct timeval currentTime;
    ProtoSystemTime(currentTime);
    // Uncomment to get different packet loss patterns from run to run
    // (and a different sender sessionId)
    srand(currentTime.tv_sec);  // seed random number generator
    
    NormSetGrttEstimate(session, 0.001);  // 1 msec initial grtt
    
    NormSetTransmitRate(session, 60.0e+03);  // in bits/second
    
    NormSetDefaultRepairBoundary(session, NORM_BOUNDARY_BLOCK); 
    
    // Uncomment to use a _specific_ transmit port number
    // (Can be the same as session port (rx port), but this
    // is _not_ recommended when unicast feedback may be
    // possible!
    //NormSetTxPort(session, 6001); 
    
    // Uncomment to receive your own traffic
    NormSetLoopback(session, true);     
    
    // Uncomment this line to participate as a receiver
    NormStartReceiver(session, 1024*1024);
    
    // Uncomment to set large rx socket buffer size
    // (might be needed for high rate sessions)
    // NormSetRxSocketBuffer(session, 512000);
    
    // Uncomment to enable TCP-friendly congestion control
    //NormSetCongestionControl(session, true);
    
    // We use a random "sessionId"
    NormSessionId sessionId = (NormSessionId)rand();
    
    
    // Uncomment the following line to start sender
    NormStartSender(session, sessionId, 4096*1024, 1400, 64, 0);

    // Uncomment to set large tx socket buffer size
    // (might be needed for high rate sessions)
    NormSetTxSocketBuffer(session, 512000);
    
    NormAddAckingNode(session, NormGetLocalNodeId(session));
    
    NormObjectHandle stream = NORM_OBJECT_INVALID;
    const char* filePath = "/home/adamson/images/art/giger/giger205.jpg";
    const char* fileName = "ferrari.jpg";
    
    // Uncomment this line to send a stream instead of the file
    //stream = NormStreamOpen(session, 4096*1024);
       
    // NORM_FLUSH_PASSIVE automatically flushes full writes to
    // the stream.
    NormStreamSetAutoFlush(stream, NORM_FLUSH_PASSIVE);   
    
    // Some variable for stream input/output
    char txBuffer[8192], rxBuffer[8192];
    int txIndex = 0;
    int txLen = 0;
    int rxIndex = 0;
    char refBuffer[1037];
    memset(refBuffer, 'a', 1037);
    bool msgSync = false;
    
    int msgCount = 0;
    int recvCount = -1;  // used to monitor reliable stream reception
    int sendCount = 0;
    int sendMax = 20;
    NormEvent theEvent;
    while (NormGetNextEvent(instance, &theEvent))
    {
        switch (theEvent.type)
        {
            case NORM_TX_QUEUE_VACANCY:
            //    TRACE("NORM_TX_QUEUE_VACANCY ...\n");
            case NORM_TX_QUEUE_EMPTY:
                //TRACE("NORM_TX_QUEUE_EMPTY ...\n");
                if (sendCount >= sendMax) break;
                if (NORM_OBJECT_INVALID != stream)
                {
                    // We loop here to keep stream buffer full ....
                    bool keepWriting = true;
                    while (keepWriting)
                    {
                        if (0 == txLen)
                        {
                            // Write a message to the "txBuffer"
                            memset(txBuffer, 'a', 1037);
                            sprintf(txBuffer+1037, "normTest says hello %d ...\n", sendCount);
                            txLen = strlen(txBuffer);
                        }
                        unsigned int want = txLen - txIndex;
                        unsigned int put = NormStreamWrite(stream, txBuffer+txIndex, want);
                        if (put != want) keepWriting = false;
                        txIndex += put;
                        if (txIndex == txLen)
                        {
                            // Instead of "NormStreamSetFlushMode(stream, NORM_FLUSH_PASSIVE)" above
                            // and "NormStreamMarkEom()" here, I could have used 
                            // "NormStreamFlush(stream, true)" here to perform explicit flushing 
                            // and EOM marking in one fell swoop.  That would be a simpler approach 
                            // for apps where big stream messages need to be written with 
                            // multiple calls to "NormStreamWrite()"
                            NormStreamMarkEom(stream);
                            txLen = txIndex = 0;
                            sendCount++;
                            if (sendCount >= sendMax)
                            {
                                // Uncomment to gracefully shut down the stream
                                // after "sendMax" messages
                                //NormStreamClose(stream, true);  
                                //keepWriting = false; 
                            }
                        }
                    }
                }
                else
                {
                    NormObjectHandle txFile = 
                        NormFileEnqueue(session,
                                        filePath,
                                        fileName,
                                        strlen(fileName));
                    // Repeatedly queue our file for sending
                    if (NORM_OBJECT_INVALID == txFile)
                    {
                        DMSG(0, "normTest: error queuing file: %s\n", filePath);
                        break;   
                    }
                    else
                    {
                        TRACE("QUEUED FILE ...\n");
                        sendCount++;
                        NormSetWatermark(session, txFile);
                    }
                }
                break;   

            case NORM_TX_OBJECT_PURGED:
                DMSG(2, "normTest: NORM_TX_OBJECT_PURGED event ...\n");
                break;

            case NORM_RX_OBJECT_NEW:
                DMSG(3, "normTest: NORM_RX_OBJECT_NEW event ...\n");
                break;

            case NORM_RX_OBJECT_INFO:
                // Assume info contains '/' delimited <path/fileName> string
                if (NORM_OBJECT_FILE == NormObjectGetType(theEvent.object))
                {
                    char fileName[PATH_MAX];
                    strcpy(fileName, cachePath);
                    int pathLen = strlen(fileName);
                    unsigned short nameLen = PATH_MAX - pathLen;
                    nameLen = NormObjectGetInfo(theEvent.object, fileName+pathLen, nameLen);
                    fileName[nameLen + pathLen] = '\0';
                    char* ptr = fileName + 5;
                    while ('\0' != *ptr)
                    {
                        if ('/' == *ptr) *ptr = PROTO_PATH_DELIMITER;
                        ptr++;   
                    }
                    if (!NormFileRename(theEvent.object, fileName))
                        TRACE("normTest: NormSetFileName(%s) error\n", fileName);
                    DMSG(3, "normTest: recv'd info for file: %s\n", fileName);   
                }
                break;

            case NORM_RX_OBJECT_UPDATED:
            {
                //TRACE("normTest: NORM_RX_OBJECT_UPDATE event ...\n");
                if (NORM_OBJECT_STREAM != NormObjectGetType(theEvent.object))
                    break;
                unsigned int len;
                do
                {
                    if (!msgSync) 
                        msgSync = NormStreamSeekMsgStart(theEvent.object);
                    if (!msgSync) break;
                    len = 8191 - rxIndex;
                    if (NormStreamRead(theEvent.object, rxBuffer+rxIndex, &len))
                    {
                        rxIndex += len;
                        rxBuffer[rxIndex] = '\0';
                        if (rxIndex > 0)
                        {
                            char* ptr = strchr(rxBuffer, '\n');
                            if (ptr)
                            {
                                // Save sub-string length
                                len = ptr - rxBuffer + 1;
                                
                                // Validate string
                                if (memcmp(rxBuffer, refBuffer, 1037))
                                    ptr = (char*)NULL;
                                else
                                    ptr = strstr(rxBuffer, "hello");
                                if (NULL != ptr)
                                {
                                    int value;
                                    if (1 == sscanf(ptr, "hello %d", &value))
                                    {
                                        if (recvCount >= 0)
                                        {
                                            if (1 != (value - recvCount))
                                                TRACE("WARNING! possible break? value:%d recvCount:%d\n",
                                                        value, recvCount);
                                            else
                                                msgCount++; // successful recv
                                            //else
                                            //    TRACE("validated recv msg len:%d\n", len);
                                        }
                                        recvCount = value;   
                                    }
                                    else
                                    {
                                        TRACE("couldn't find index!?\n");
                                        ASSERT(0);
                                    }
                                    if ((unsigned int)rxIndex > len)
                                    {
                                        memmove(rxBuffer, rxBuffer+len, rxIndex - len);
                                        rxIndex -= len;   
                                    }
                                    else
                                    {
                                        rxIndex = 0;
                                    }
                                }
                                else
                                {
                                    TRACE("invalid string!?\n");
                                    ASSERT(0);
                                }
                            }
                        }
                    }
                    else
                    {
                        TRACE("normTest: error reading stream\n"); 
                        TRACE("status: msgCount:%d of total:%d (%lf)\n",
                                msgCount, recvCount, 100.0*((double)msgCount)/((double)recvCount));
                        msgSync = false;
                        rxIndex = 0;
                        break; 
                    }  
                } while (0 != len);
                break;                 
            }

            case NORM_RX_OBJECT_COMPLETED:
                TRACE("normTest: NORM_RX_OBJECT_COMPLETED event ...\n");
                break;

            case NORM_RX_OBJECT_ABORTED:
                TRACE("normTest: NORM_RX_OBJECT_ABORTED event ...\n");
                break;

            default:
                TRACE("Got event type: %d\n", theEvent.type); 
        }  // end switch(theEvent.type)
        
        // Break after sending "sendMax" messages or files
        //if (sendCount >= sendMax) break;
        
    }  // end while (NormGetNextEvent())
    
    fprintf(stderr, "normTest shutting down in 30 sec ...\n");
#ifdef WIN32
    Sleep(30000);
#else
    sleep(30);  // allows time for cleanup if we're sending to someone else
#endif // if/else WIN32/UNIX

    NormStreamClose(stream);
    NormStopReceiver(session);
    NormStopSender(session);
    NormDestroySession(session);
    NormDestroyInstance(instance);
    
    fprintf(stderr, "normTest: Done.\n");
    return 0;
}  // end main()
