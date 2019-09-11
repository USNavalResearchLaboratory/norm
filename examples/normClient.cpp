#include "normSocket.h"
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <fcntl.h>       // for, well, fnctl()
#include <errno.h>       // obvious child

// BUILD: (assumes "normApi.h" in "include" ...
// g++ -I../include -o normClient normClient.cpp  normSocket.cpp ../lib/libnorm.a ../protolib/lib/libprotokit.a -lresolv 


// NOTES: 
//   1) THIS IS A WORK IN PROGRESS AND NOT YET FUNCTIONAL!
//   2) Some of the functions here may be added to the NORM API in the future.

// Client initiates connection to "server".  When the "client" gets a NORM_REMOTE_SENDER_NEW
// from the "server", it knows its connection has been accepted.

// To deny a connection, the "server" can send a stream close???  Which means the server will have
// to establish state just to deny a client a connection (yuck!)

const unsigned int MSG_LENGTH_MAX = 64;

int main(int argc, char* argv[])
{
    char serverAddr[64];
    strcpy(serverAddr, "127.0.0.1");
    UINT16 serverPort = 5000;
    char groupAddr[64];
    const char* groupAddrPtr = NULL;
    const char* mcastInterface = NULL;
    NormNodeId clientId = 2;
    bool trace = false;
    unsigned int debugLevel = 0;
    
    for (int i = 1; i < argc; i++)
    {
        const char* cmd = argv[i];
        unsigned int len = strlen(cmd);
        if (0 == strncmp(cmd, "connect", len))
        {
            // connect <serverAddr>/<port>[,<groupAddr>
            const char* val = argv[++i];
            const char* portPtr = strchr(val, '/');
            if (NULL == portPtr)
            {
                fprintf(stderr, "normClient error: missing <port> number\n");
                return -1;
            }
            portPtr++;
            unsigned int addrTextLen = portPtr - val;
            if (addrTextLen > 0)
            {
                addrTextLen -= 1;
                strncpy(serverAddr, val, addrTextLen);
                serverAddr[addrTextLen] = '\0';
            }
            else
            {
                fprintf(stderr, "normClient error: missing <serverAddr>\n");
                return -1;
            }
            char portText[32];
            const char* groupPtr = strchr(portPtr, ',');
            if (NULL == groupPtr)
            {
                strcpy(portText, portPtr);
            }
            else
            {
                groupPtr++;
                unsigned int portTextLen = groupPtr - portPtr - 1;
                strncpy(portText, portPtr, portTextLen);
                portText[portTextLen] = '\0';
            }
            if (1 != sscanf(portText, "%hu", &serverPort))
            {
                fprintf(stderr, "normClient error: invalid <port>\n");
                return -1;
            }
            if (NULL != groupPtr)
            {
                strcpy(groupAddr, groupPtr);
                groupAddrPtr = groupAddr;
            }
        }
        else if (0 == strncmp(cmd, "interface", len))
        {
            mcastInterface = argv[++i];
        }
        else if (0 == strncmp(cmd, "trace", len))
        {
            trace = true;
        }
        else if (0 == strncmp(cmd, "debug", len))
        {
            if (1 != sscanf(argv[++i], "%u", &debugLevel))
            {
                fprintf(stderr, "normServer error: invalid debug level\n");
                return -1;
            }
        }
        else if (0 == strncmp(cmd, "id", len))
        {
            const char* val = argv[++i];
            unsigned long id;
            if (1 != sscanf(val, "%lu", &id))
            {
                fprintf(stderr, "normClient error: invalid 'id' value\n");
                return -1;
            }
            clientId = id;
        }
        else
        {
            fprintf(stderr, "normServer error: invalid command \"%s\"\n", cmd);
            return -1;
        }
    }  
    
    NormInstanceHandle instance = NormCreateInstance();
    
    // Initate connection to server ...
    fprintf(stderr, "normClient: connecting to %s/%hu ...\n", serverAddr, serverPort);
    NormSocketHandle normSocket = NormConnect(instance, serverAddr, serverPort, groupAddrPtr, clientId);
    
    if (trace)
    {
        NormSetMessageTrace(NormGetSession(normSocket), true);
        if (NULL != groupAddrPtr)
            NormSetMessageTrace(NormGetMulticastSession(normSocket), true);
    }
    if (0 != debugLevel) NormSetDebugLevel(debugLevel);
    
    //NormSetDebugLevel(3);
    //NormSetMessageTrace(NormGetSession(normSocket), true);
    
    // We use a select() call to multiplex input reading and NormSocket handling
    fd_set fdset;
    FD_ZERO(&fdset);
    
    // Get our input (STDIN) descriptor and set non-blocking
    FILE* inputFile = stdin;
    int inputfd = fileno(inputFile);
    if (-1 == fcntl(inputfd, F_SETFL, fcntl(inputfd, F_GETFL, 0) | O_NONBLOCK))
        perror("normClient: fcntl(inputfd, O_NONBLOCK) error");
    // Get our NormInstance descriptor
    int normfd = NormGetDescriptor(instance);
    bool keepGoing = true;
    bool writeReady = false;
    unsigned int inputLength = 0;
    unsigned int bytesWritten = 0;
    const unsigned int BUFFER_LENGTH = 2048;
    char inputBuffer[BUFFER_LENGTH];
    bool inputNeeded = false;  // will be set to "true" upon CONNECT
    while (keepGoing)
    {
        FD_SET(normfd, &fdset);
        int maxfd = normfd;
        if (inputNeeded)
        {
            FD_SET(inputfd, &fdset);
            if (inputfd > maxfd) maxfd = inputfd;
        }
        else
        {
            FD_CLR(inputfd, &fdset);
        }
        int result = select(maxfd+1, &fdset, NULL, NULL, NULL);
        if (result <= 0)
        {
            perror("normClient: select() error");
            break;
        }
        if (FD_ISSET(inputfd, &fdset))
        {
            // Read input into our txBuffer
            inputLength = fread(inputBuffer, 1, BUFFER_LENGTH, inputFile);
            if (inputLength > 0)
            {
                // We got our input
                bytesWritten = 0;
                inputNeeded = false;
            }
            else if (feof(inputFile))
            {
                // TBD - initiate client shutdown
                if (NULL == groupAddrPtr)
                {
                    fprintf(stderr, "normClient: CLOSING connection to server ...\n");
                    NormShutdown(normSocket);
                }
                inputNeeded = false;  // TBD -should we also fclose(inputFile)???
                
                // else stick around to receive stuff from the server
            }
            else if (ferror(inputFile))
            {
                switch (errno)
                {
                    case EINTR:
                        // interupted, try again
                        break;
                    case EAGAIN:
                        // input starved, wait for next notification
                        break;
                    default:
                        perror("normClient: error reading input");
                        break;
                }
            }
        }
        if (FD_ISSET(normfd, &fdset))
        {

            // There's a NORM event pending
            NormSocketEvent event;
            if (NormGetSocketEvent(instance, &event))
            {
                switch (event.type)
                {           
                    case NORM_SOCKET_ACCEPT:
                        // shouldn't happen
                        break;

                    case NORM_SOCKET_CONNECT:
                    {
                        fprintf(stderr, "normClient: CONNECTED to server ...\n");
                        inputNeeded = true;
                        writeReady = true;
                        break;   
                    }
                    case NORM_SOCKET_READ:
                    {
                        // This is a cue to try to read data from stream
                        // For our test app here, the data is read and output to STDOUT
                        bool rxReady = true;
                        while (rxReady)
                        {
                            char buffer[1024];
                            ssize_t bytesRead = NormRead(event.socket, buffer, 1024);
                            if (bytesRead < 0)
                            {
                                // This shouldn't happen with ack-based flow control used
                                fprintf(stderr, "normClient: broken stream ...\n");
                                continue;
                            }
                            if (bytesRead > 0)
                                fwrite(buffer, sizeof(char), bytesRead, stdout);
                            if (bytesRead < 1024) rxReady = false;
                        }
				        break;
                    }      
                    case NORM_SOCKET_WRITE:
                        writeReady = true;
				        break;     
                    case NORM_SOCKET_CLOSING:
                        fprintf(stderr, "normClient: server CLOSING connection ...\n");
                        inputNeeded = false;
				        break;   
                    case NORM_SOCKET_CLOSED:
                    {
                        fprintf(stderr, "normClient: connection to server CLOSED.\n");
                        inputNeeded = false;
                        keepGoing = false;
				        break;   
                    }
                    case NORM_SOCKET_NONE:
				        break; 
                }  // end switch(event.type)
            }
            else
            {
                fprintf(stderr, "normClient: NormGetSocketEvent() returned false\n");
            }
        }  // end if FD_ISSET(normfd)
        
        // If we have data in our inputBuffer and the NormSocket is "writeReady", then send it
        if (writeReady && (inputLength > 0))
        {
            bytesWritten += NormWrite(normSocket, inputBuffer + bytesWritten, inputLength - bytesWritten);
            if (bytesWritten < inputLength)
            {
                // Couldn't write whole inputBuffer, need to wait for NORM_SOCKET_WRITE event
                writeReady = false;
            }
            else
            {
                // inputBuffer has been completely written
                inputLength = 0;
                inputNeeded = true;
                NormFlush(normSocket);
            }
        }
    }  // end while(keepGoing)
} 
