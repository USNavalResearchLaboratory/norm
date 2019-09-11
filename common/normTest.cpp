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

    SetDebugLevel(4);
    
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
    
    NormSetMessageTrace(session, true);
    
    NormSetTxLoss(session, 10.0);  // 1% packet loss
    
    NormSetGrttEstimate(session, 0.1);//0.001);  // 1 msec initial grtt
    
    NormSetTransmitRate(session, 1.0e+05);  // in bits/second
    
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
    stream = NormOpenStream(session, 1024*1024);
    NormSetStreamFlushMode(stream, NORM_FLUSH_NONE);
    
    int index = -1;  // used to monitor reliable stream reception
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
                    // Write a message to the "stream"
                    char buffer[1024];
                    sprintf(buffer, "normTest says hello %d ...\n", sendCount++);
                    unsigned int len = strlen(buffer);
                    TRACE("writing to stream ...\n");
                    if (len != NormWriteStream(stream, buffer, len))
                        TRACE("incomplete write:%u\n", len);
                    else
                    {
                        NormFlushStream(stream);
                        NormSetWatermark(session, stream);
                    }
                }
                else
                {
                    NormObjectHandle txFile = 
                        NormQueueFile(session,
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
                        NormSetWatermark(session, txFile);
                    }
                    sendCount++;
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
                if (NORM_OBJECT_FILE == NormGetObjectType(theEvent.object))
                {
                    NormObjectTransportId id = NormGetObjectTransportId(theEvent.object);
                    if (0 != (id & 0x01))
                    {
                        //NormCancelObject(theEvent.object);
                        //break;
                    }

                    char fileName[PATH_MAX];
                    strcpy(fileName, cachePath);
                    int pathLen = strlen(fileName);
                    unsigned short nameLen = PATH_MAX - pathLen;
                    NormGetObjectInfo(theEvent.object, fileName+pathLen, &nameLen);
                    fileName[nameLen + pathLen] = '\0';
                    char* ptr = fileName + 5;
                    while ('\0' != *ptr)
                    {
                        if ('/' == *ptr) *ptr = PROTO_PATH_DELIMITER;
                        ptr++;   
                    }
                    if (!NormSetFileName(theEvent.object, fileName))
                        TRACE("normTest: NormSetFileName(%s) error\n", fileName);
                    DMSG(3, "normTest: recv'd info for file: %s\n", fileName);   


                }
                break;

            case NORM_RX_OBJECT_UPDATE:
            {
                //TRACE("normTest: NORM_RX_OBJECT_UPDATE event ...\n");
                if (NORM_OBJECT_STREAM != NormGetObjectType(theEvent.object))
                    break;
                char buffer[1024];
                unsigned int len = 1023;
                do
                {
                    len = 1023;
                    if (NormReadStream(theEvent.object, buffer, &len))
                    {
                        buffer[len] = '\0';
                        if (len)
                        {
                            TRACE("normTest: recvd(%u):\n\"%s\"\n", len, buffer);
                            // This while() loop is cheesy test, looking for broken stream
                            //
                            char* ptr = buffer;
                            while ((ptr = strstr(ptr, "hello")))
                            {   
                                int value;
                                if (1 == sscanf(ptr, "hello %d", &value))
                                {
                                    if (index >= 0)
                                    {
                                        if (1 != (value - index))
                                            TRACE("WARNING! possible break? value:%d index:%d\n",
                                                    value, index);
                                    }
                                    index = value;
                                }
                                else
                                {
                                    TRACE("couldn't find index\n");
                                }
                                ptr += 5;
                            }
                        }
                    }
                    else
                    {
                        TRACE("normTest: error reading stream\n");  
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
        if (sendCount >= sendMax) break;
        
    }  // end while (NormGetNextEvent())
    
    fprintf(stderr, "normTest shutting down in 30 sec ...\n");
#ifdef WIN32
    Sleep(30000);
#else
    sleep(30);  // allows time for cleanup if we're sending to someone else
#endif // if/else WIN32/UNIX

    NormCloseStream(stream);
    NormStopReceiver(session);
    NormStopSender(session);
    NormDestroySession(session);
    NormDestroyInstance(instance);
    
    fprintf(stderr, "normTest: Done.\n");
    return 0;
}  // end main()
