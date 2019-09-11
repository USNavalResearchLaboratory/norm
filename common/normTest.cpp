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
    printf("normTest starting (sizeof(NormSize) = %d)...\n", (int)sizeof(NormSize));
    
    NormInstanceHandle instance = NormCreateInstance();

#ifdef WIN32
    const char* cachePath = "C:\\Adamson\\Temp\\cache\\";
#else
    const char* cachePath =  "/tmp/";
#endif // if/else WIN32/UNIX

    NormSetCacheDirectory(instance, cachePath);
    
    // Here's a trick to generate a _hopefully_ unique NormNodeId
    // based on an XOR of the system's IP address and the process id.
    // (We use ProtoAddress::GetEndIdentifier() to get the local
    //  "default" IP address for the system)
    // (Note that passing "NORM_NODE_ANY" to the last arg of 
    //  NormCreateSession() does a similar thing but without
    //  the processId XOR ... perhaps we should add the processId
    //  hack to that default "NORM_NODE_ANY" local NormNodeId picker???
    ProtoAddress localAddr;
    if (!localAddr.ResolveLocalAddress())
    {
        fprintf(stderr, "normTest: error resolving local IP address\n");
        NormDestroyInstance(instance);
        return -1;
    }
    NormNodeId localId = localAddr.EndIdentifier();
#ifdef WIN32
    DWORD processId = GetCurrentProcessId();
#else
    pid_t processId = getpid();
#endif // if/else WIN32/UNIX
    localId ^= (NormNodeId)processId;
    
    // If needed, permutate to a valid, random NormNodeId
    while ((NORM_NODE_ANY == localId) ||
           (NORM_NODE_NONE == localId))
    {
        localId ^= (NormNodeId)rand();
    }
    //localId = 15;  // for testing purposes

    // Create a NORM session instance
    NormSessionHandle session = NormCreateSession(instance,
                                                  "224.1.1.1", 
                                                   6001,
                                                   localId);
    
    //NormSetTOS(session, 0x20);
    
    // NOTE: These are debugging routines available (not for normal app use)
    // (IMPORTANT NOTE: On Win32 builds with Norm.dll, this "SetDebugLevel()" has no
    //                  effect on the Protolib instance in the NORM DLL !!!
    //                  (TBD - provide a "NormSetDebugLevel()" function for this purpose!)
    SetDebugLevel(2);
    // Uncomment to turn on debug NORM message tracing
    //NormSetMessageTrace(session, true);
    // Uncomment to turn on some random packet loss
    //NormSetTxLoss(session, 10.0);  // 10% packet loss
    //NormSetRxLoss(session, 20.0);
    struct timeval currentTime;
    ProtoSystemTime(currentTime);
    // Uncomment to get different packet loss patterns from run to run
    // (and a different sender sessionId)
    srand(currentTime.tv_sec);  // seed random number generator
    
    NormSetGrttEstimate(session, 0.001);  // 1 msec initial grtt
    
    NormSetTransmitRate(session, 1.0e+07);  // in bits/second
    
    // Uncomment to enable TCP-friendly congestion control (overrides NormSetTransmitRate())
    //NormSetCongestionControl(session, true);
    
    //NormSetTransmitRateBounds(session, 1000.0, -1.0);
    
    NormSetDefaultRepairBoundary(session, NORM_BOUNDARY_BLOCK); 
    
    // Uncomment to use a _specific_ transmit port number
    // (Can be the same as session port (rx port), but this
    //  is _not_ recommended when unicast feedback may be
    //  possible!)
    //NormSetTxPort(session, 6001); 
    
    //NormSetDefaultUnicastNack(session, true);
        
    // Uncomment to allow reuse of rx port
    // (This allows multiple "normTest" instances on the same machine
    //  for the same NormSession - note those instances must use
    //  unique local NormNodeIds (see NormCreateSession() above).
    NormSetRxPortReuse(session, true);
    
    // Uncomment to receive your own traffic
    NormSetLoopback(session, true);     
    
    //NormSetSilentReceiver(session, true);
    
    // Uncomment this line to participate as a receiver
    //NormStartReceiver(session, 1024*1024);
    
    // Uncomment to set large rx socket buffer size
    // (might be needed for high rate sessions)
    NormSetRxSocketBuffer(session, 512000);
    
    // We use a random "sessionId"
    NormSessionId sessionId = (NormSessionId)rand();
    
    // Uncomment the following line to start sender
    NormStartSender(session, sessionId, 1024*1024, 1400, 64, 8);
    
    //NormSetAutoParity(session, 6);

    // Uncomment to set large tx socket buffer size
    // (might be needed for high rate sessions)
    //NormSetTxSocketBuffer(session, 512000);
    
    NormAddAckingNode(session, NORM_NODE_NONE); //15); //NormGetLocalNodeId(session));
    
    NormObjectHandle stream = NORM_OBJECT_INVALID;
#ifdef WIN32
    const char* filePath = "C:\\Adamson\\Images\\Art\\giger205.jpg";
#else
    const char* filePath = "/home/adamson/images/art/giger/giger205.jpg";
#endif
    //const char* filePath = "/home/adamson/pkgs/rh73.tgz";
    const char* fileName = "file1.jpg";
    const char* fileName2 = "file2.jpg";
    
    
    // Uncomment this line to send a stream instead of the file
    stream = NormStreamOpen(session, 4*1024*1024);
       
    // NORM_FLUSH_PASSIVE automatically flushes full writes to
    // the stream.
    NormStreamSetAutoFlush(stream, NORM_FLUSH_PASSIVE);   
    
    // Some variable for stream input/output
    char txBuffer[8192], rxBuffer[8192];
    int txIndex = 0;
    int txLen = 0;
    int rxIndex = 0;
    
#define STREAM_MSG_PREFIX_SIZE 1000  // we pad with 'a' character value (the msgPrefix is a prefix)
    char msgPrefix[STREAM_MSG_PREFIX_SIZE];
    memset(msgPrefix, 'a', STREAM_MSG_PREFIX_SIZE);
    bool msgSync = false;
    
    int msgCount = 0;
    int recvCount = -1;  // used to monitor reliable stream reception
    int sendCount = 0;
    int sendMax = -1;//300;//-1;    // -1 means unlimited
    
    int fileMax = 1;
    NormObjectHandle txFile = NORM_OBJECT_INVALID;
    
    NormEvent theEvent;
    
    while (NormGetNextEvent(instance, &theEvent))
    {
        switch (theEvent.type)
        {
            case NORM_TX_QUEUE_VACANCY:
            case NORM_TX_QUEUE_EMPTY:
                /*if (NORM_TX_QUEUE_VACANCY == theEvent.type)
                    TRACE("NORM_TX_QUEUE_VACANCY ...\n");
                else
                    TRACE("NORM_TX_QUEUE_EMPTY ...\n");*/
                if ((sendMax > 0) && (sendCount >= sendMax)) break;
                if (NORM_OBJECT_INVALID != theEvent.object)
                {
                    // We loop here to keep stream buffer full ....
                    bool keepWriting = true;
                    while (keepWriting)
                    {
                        if (0 == txLen)
                        {
                            memset(txBuffer, 'a', STREAM_MSG_PREFIX_SIZE);
                            // Write a message into the "txBuffer" for transmission
                            sprintf(txBuffer+STREAM_MSG_PREFIX_SIZE, " normTest says hello %d ...\n", sendCount);
                            txLen = strlen(txBuffer);
                        }
                        unsigned int want = txLen - txIndex;
                        unsigned int put = NormStreamWrite(stream, txBuffer+txIndex, want);
                        if (put != want) keepWriting = false;
                        txIndex += put;
                        if (txIndex == txLen)
                        {
                            // _Instead_ of "NormStreamSetAutoFlush(stream, NORM_FLUSH_PASSIVE)" above
                            // _and_ "NormStreamMarkEom()" here, I could have used 
                            // "NormStreamFlush(stream, true)" here to perform explicit flushing 
                            // and EOM marking in one fell swoop.  That would be a simpler approach 
                            // for apps where very big stream messages might need to be written with 
                            // multiple calls to "NormStreamWrite()"
                            NormStreamMarkEom(stream);
                            txLen = txIndex = 0;
                            sendCount++;
                            if (sendCount == 15)
                            {
                                //NormSetWatermark(session, stream);   
                            }
                            if ((sendMax > 0) && (sendCount >= sendMax))
                            {
                                // Uncomment to gracefully shut down the stream
                                // after "sendMax" messages
                                NormStreamClose(stream, true);  
                                keepWriting = false; 
                            }                            
                        }
                    }  // end while(keepWriting)
                }
                else if (NORM_OBJECT_INVALID == stream)  // we weren't sending a stream that finished
                {
                    if ((fileMax < 0) || (sendCount < fileMax))
                    {
                        const char* namePtr = (0 == (sendCount & 0x01)) ? fileName : fileName2;
                        txFile = 
                            NormFileEnqueue(session,
                                            filePath,
                                            namePtr,
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
                            if (sendCount < 2) NormSetWatermark(session, txFile);
                        }
                    }
                }
                break;   

            case NORM_TX_FLUSH_COMPLETED:
                DMSG(2, "normTest: NORM_TX_FLUSH_COMPLETED event ...\n");
                // This line of code "requeues" the last file sent
                // (Good for use with silent receivers ... can repeat transmit object this way)
                NormRequeueObject(session, txFile);
                break;  

            case NORM_TX_WATERMARK_COMPLETED:
                DMSG(2, "normTest: NORM_TX_WATERMARK_COMPLETED event ...\n");
                break;
                
            case NORM_TX_OBJECT_SENT:
                DMSG(2, "normTest: NORM_TX_WATERMARK_COMPLETED event ...\n");
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
                            // The '\n' indicates we have received an entire message
                            int parsedLen = 0;
                            bool searching = true;
                            char* startPtr = rxBuffer;
                            while (searching)
                            {
                                char* endPtr = strchr(startPtr, '\n');
                                if (endPtr)
                                {
                                    // Save sub-string message length
                                    parsedLen += (endPtr - startPtr + 1);
                                    // Validate received message string
                                    if (0 == memcmp(rxBuffer, msgPrefix, STREAM_MSG_PREFIX_SIZE))
                                    {
                                        char* ptr = strstr(startPtr, "hello");
                                        if (ptr)
                                        {
                                            int value;
                                            if (1 == sscanf(ptr, "hello %d", &value))
                                            {
                                                msgCount++; // successful recv
                                                if (recvCount >= 0)
                                                {
                                                    if (0 != (value - recvCount))
                                                        TRACE("WARNING! possible break? value:%d recvCount:%d\n",
                                                                value, recvCount);
                                                }
                                                //else
                                                //    TRACE("validated recv msg len:%d\n", len);
                                                recvCount = value+1;   
                                                if (0 == msgCount % 1000)
                                                {
                                                    TRACE("normTest: status> msgCount:%d of total:%d (%lf)\n",
                                                          msgCount, recvCount, 100.0*((double)msgCount)/((double)recvCount));
                           
                                                }
                                            }
                                            else
                                            {
                                                TRACE("couldn't find index!? len:%d in %s\n", len, ptr);
                                                ASSERT(0);
                                            }
                                        }
                                        else
                                        {
                                            TRACE("couldn't find \"hello\"!? len:%d in %s\n", len, rxBuffer);
                                            ASSERT(0);
                                        }
                                        
                                    }
                                    else
                                    {
                                        TRACE("invalid received message!?\n");
                                        ASSERT(0);
                                    }
                                    if (parsedLen >= rxIndex) 
                                    {
                                        rxIndex = 0;
                                        searching = false;
                                    }
                                    else
                                    {
                                        startPtr = rxBuffer + parsedLen;
                                    }
                                }  // end if (endPtr)
                                else if (parsedLen > 0)
                                {
                                    memmove(rxBuffer, rxBuffer+parsedLen, rxIndex - parsedLen);
                                    rxIndex -= parsedLen;
                                    searching = false;
                                }
                                else
                                {
                                    searching = false;
                                }
                            }  // end while(searching)
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
                //NormStopReceiver(session);
                break;

            case NORM_RX_OBJECT_ABORTED:
                TRACE("normTest: NORM_RX_OBJECT_ABORTED event ...\n");
                break;
                
            case NORM_GRTT_UPDATED:
                //TRACE("normTest: NORM_GRTT_UPDATED event ...\n");
                break;

            default:
                TRACE("Got event type: %d\n", theEvent.type); 
        }  // end switch(theEvent.type)
        
        // Uncomment to exit program after sending "sendMax" messages or files
        if ((sendMax > 0) && (sendCount >= sendMax)) break;
        
    }  // end while (NormGetNextEvent())
    
#ifdef WIN32
    Sleep(30000);
#else
    sleep(30);  // allows time for cleanup if we're sending to someone else
#endif // if/else WIN32/UNIX

    //NormStreamClose(stream);  // stream is already closed in loop above 
    NormStopReceiver(session);
    NormStopSender(session);
    NormDestroySession(session);
    NormDestroyInstance(instance);
    
    fprintf(stderr, "normTest: Done.\n");
    return 0;
}  // end main()
