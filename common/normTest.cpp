////////////////////////////////////////////////////
// This is a test application for experimenting
// with the NORM API implementation during its
// development.  A better-documented and complete
// example of the NORM API usage will be provided
// when the NORM API is more complete.

#include "normApi.h"
#include "protokit.h"  // for protolib debug, stuff, etc

#include <stdio.h>
#ifdef UNIX
#include <unistd.h>  // for "sleep()"
#endif // UNIX
int main(int argc, char* argv[])
{
    printf("normTest starting ...\n");

    SetDebugLevel(2);
    
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
    
    //NormSetMessageTrace(session, true);
    
    NormSetTxLoss(session, 10.0);  // 10% packet loss
    
    NormSetGrttEstimate(session, 0.2);//0.001);  // 1 msec initial grtt
    
    NormSetTransmitRate(session, 1.0e+06);  // in bits/second
    
    NormSetDefaultRepairBoundary(session, NORM_BOUNDARY_BLOCK);  
    
    // Uncomment to receive own traffic
    NormSetLoopback(session, true);     
    
    // Uncomment this line to participate as a receiver
    NormStartReceiver(session, 1024*1024);
    
    // Uncomment the following line to start sender
    NormStartSender(session, 1024*1024, 1024, 64, 0);
    
    NormAddAckingNode(session, NormGetLocalNodeId(session));
    
    NormObjectHandle stream = NORM_OBJECT_INVALID;
    const char* filePath = "/home/adamson/images/art/giger/giger205.jpg";
    const char* fileName = "ferrari.jpg";
    
    // Uncomment this line to send a stream instead of the file
    stream = NormStreamOpen(session, 1024*1024);
    
    
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
    int sendMax = 200;
    NormEvent theEvent;
    while (NormGetNextEvent(instance, &theEvent))
    {
        switch (theEvent.type)
        {
            case NORM_TX_QUEUE_EMPTY:
                if (NORM_OBJECT_INVALID != stream)
                {
                    if (0 == txLen)
                    {
                        // Write a message to the "txBuffer"
                        memset(txBuffer, 'a', 1037);
                        sprintf(txBuffer+1037, "normTest says hello %d ...\n", sendCount);
                        txLen = strlen(txBuffer);
                    }
                    txIndex += NormStreamWrite(stream, txBuffer+txIndex, (txLen - txIndex));
                    if (txIndex == txLen)
                    {
                        // Instead of "NormStreamSetFlushMode(stream, NORM_FLUSH_PASSIVE)" above
                        // and "NormStreamMarkEom()" here, I could have used 
                        // "NormStreamFlush(stream, true)" here to perform explicit flushing 
                        // and EOM marking in one fell swoop.  That would be a better approach 
                        // for apps where big stream messages need to be written with 
                        // multiple calls to "NormStreamWrite()"
                        NormStreamMarkEom(stream);
                        txLen = txIndex = 0;
                        sendCount++;
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
                DMSG(3, "normTest: NORM_TX_OBJECT_PURGED event ...\n");
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
