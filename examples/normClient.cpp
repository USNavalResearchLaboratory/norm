#include "normSocket.h"
#include <stdio.h>
#include <string.h>

#ifdef WIN32
#include "win32InputHandler.cpp"  // brings in the entire implementation
#else
#include <sys/select.h>
#include <fcntl.h>       // for, well, fnctl()
#include <errno.h>       // obvious child
#endif  // if/else WIN32/UNIX


// BUILD: (assumes "normApi.h" in "include" ...
// g++ -I../include -o normClient normClient.cpp  normSocket.cpp ../lib/libnorm.a ../protolib/lib/libprotokit.a -lresolv 


// NOTES: 
//   1) THIS IS A WORK IN PROGRESS AND NOT YET FUNCTIONAL!
//   2) Some of the functions here may be added to the NORM API in the future.

// Client initiates connection to "server".  When the "client" gets a NORM_REMOTE_SENDER_NEW
// from the "server", it knows its connection has been accepted.

// To deny a connection, the "server" can send a stream close???  Which means the server will have
// to establish state just to deny a client a connection (yuck!)

void Usage()
{
    fprintf(stderr, "Usage: normClient [connect <serverAddr>/<port>[,<groupAddr>]][debug <level>][trace]\n");
}

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
            // connect <serverAddr>/<port>[,<groupAddr>]
            const char* val = argv[++i];
            const char* portPtr = strchr(val, '/');
            if (NULL == portPtr)
            {
                fprintf(stderr, "normClient error: missing <port> number\n");
                Usage();
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
                Usage();
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
                Usage();
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
                fprintf(stderr, "normClient error: invalid debug level\n");
                Usage();
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
                Usage();
                return -1;
            }
            clientId = id;
        }
        else
        {
            fprintf(stderr, "normClient error: invalid command \"%s\"\n", cmd);
                Usage();
            return -1;
        }
    }  
    
    NormInstanceHandle instance = NormCreateInstance();
	NormSocketHandle normSocket = NormOpen(instance);

	if (trace)
	{
		NormSetMessageTrace(NormGetSocketSession(normSocket), true);
		if (NULL != groupAddrPtr)
			NormSetMessageTrace(NormGetSocketMulticastSession(normSocket), true);
	}
	if (0 != debugLevel) NormSetDebugLevel(debugLevel);

	//NormSetDebugLevel(3);
	//NormSetMessageTrace(NormGetSocketSession(normSocket), true);
    
    // Initate connection to server ...
    fprintf(stderr, "normClient: connecting to %s/%hu ...\n", serverAddr, serverPort);        
    // setting 'localPort' param here to zero lets an ephemeral port be picked
    NormConnect(normSocket, serverAddr, serverPort, 0, groupAddrPtr, clientId);
    /* // Optional code to test NormWrite() immediately after NormConnect() call
    // (Note this is _not_ compatible with newer stale connection reject code)
    const char* helloStr = "Hello\n";
    unsigned int helloLen = strlen(helloStr) + 1;
    NormWrite(normSocket, helloStr, helloLen);
    NormFlush(normSocket);*/
    
    
    
#ifdef WIN32
    HANDLE hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
    Win32InputHandler inputHandler;
    inputHandler.Open();
    HANDLE handleArray[2];
    handleArray[0] = NormGetDescriptor(instance);
    handleArray[1] = inputHandler.GetEventHandle();
#else
    // On Unix, use a select() call to multiplex input reading and NormSocket handling
    fd_set fdset;
    FD_ZERO(&fdset);
    // Get our input (STDIN) descriptor and set non-blocking
    FILE* inputFile = stdin;
    int inputfd = fileno(inputFile);
    if (-1 == fcntl(inputfd, F_SETFL, fcntl(inputfd, F_GETFL, 0) | O_NONBLOCK))
        perror("normClient: fcntl(inputfd, O_NONBLOCK) error");
    // Get our NormInstance descriptor
    int normfd = NormGetDescriptor(instance);
# endif // if/else WIN32
    bool keepGoing = true;
    bool writeReady = false;
    int inputLength = 0;
    unsigned int bytesWritten = 0;
    const unsigned int BUFFER_LENGTH = 2048;
    char inputBuffer[BUFFER_LENGTH];
    bool inputNeeded = false;  // will be set to "true" upon CONNECT
    while (keepGoing)
    {
        bool normEventPending = false;
        bool inputEventPending = false;
#ifdef WIN32
        DWORD handleCount = inputNeeded ? 2 : 1;
        DWORD waitStatus =  
            MsgWaitForMultipleObjectsEx(handleCount,   // number of handles in array
                                        handleArray,   // object-handle array
                                        INFINITE,           // time-out interval
                                        QS_ALLINPUT,   // input-event type
                                        0);
        if ((WAIT_OBJECT_0 <= waitStatus) && (waitStatus < (WAIT_OBJECT_0 + handleCount)))
        {
            if (0 == (waitStatus - WAIT_OBJECT_0))
                normEventPending = true;
            else
                inputEventPending = true;
        }
        else if (-1 == waitStatus)
        {
            perror("normClient: MsgWaitForMultipleObjectsEx() error");
            break;
        }
        else
        {
            // TBD - any other status we should handle?
            // (e.g. WAIT_TIMEOUT, WAIT_ABANDONED or WAIT_IO_COMPLETION)
            continue;  // ignore for now
        }
#else
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
            inputEventPending = true;
        if (FD_ISSET(normfd, &fdset))
            normEventPending = true;
#endif  // if/else WIN32/UNIX
        
        if (inputEventPending)
        {
            // Read input into our txBuffer
#ifdef WIN32
			inputLength = inputHandler.ReadData(inputBuffer, BUFFER_LENGTH);
			if (inputLength > 0)
            {
				// We got our input
				bytesWritten = 0;
                inputNeeded = false;
            }
            else if (inputLength < 0)
            {
				// Input stream has likely closed, initiate client shutown
                // TBD - initiate client shutdown
                //if (NULL == groupAddrPtr)
                {
                    fprintf(stderr, "normClient: CLOSING connection to %sserver ...\n",
                                    (NULL != groupAddrPtr) ? : "multicast " : "");
                    NormShutdown(normSocket);
                }
                inputNeeded = false;  // TBD -should we also fclose(inputFile)???
                inputHandler.Close();
            }
			// else zero bytes read, still need input
#else
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
                //if (NULL == groupAddrPtr)
                {
                    fprintf(stderr, "normClient: CLOSING connection to %sserver ...\n",
                                    (NULL != groupAddrPtr) ? "multicast " : "");
                    NormShutdown(normSocket);
                }
                inputNeeded = false;  // TBD -should we also fclose(inputFile)???
                if (stdin != inputFile)
                {
                    fclose(inputFile);
                    inputFile = NULL;
                }
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
                        perror("normClient: error reading input?!");
                        break;
                }
            }
#endif // if/else WIN32/UNIX
        }  // end if inputEventPending
        
        if (normEventPending)
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
						char remoteAddr[16];
						unsigned int addrLen = 16;
						UINT16 remotePort;
						NormGetPeerName(normSocket, remoteAddr, &addrLen, &remotePort);
						fprintf(stderr, "normClient: CONNECTED to %sserver %s/%hu\n",
                                (NULL != groupAddrPtr) ? "multicast " : "", serverAddr, remotePort);
                        inputNeeded = true;
                        writeReady = true;
                        if (trace && (NULL != groupAddrPtr))
                            NormSetMessageTrace(NormGetSocketMulticastSession(normSocket), true);
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
                            {
#ifdef WIN32
                                // Use Win32 WriteFile() so there is no buffer delay
                                DWORD dwWritten;
                                WriteFile(hStdout, buffer, bytesRead, &dwWritten, NULL);
#else
                                fwrite(buffer, sizeof(char), bytesRead, stdout);
#endif // if/else WIN32
                            }
                            // If less bytes read than request then need
                            // to wait for next NORM_SOCKET_READ event
                            if (bytesRead < 1024) rxReady = false;
                        }
				        break;
                    }      
                    case NORM_SOCKET_WRITE:
                        writeReady = true;
				        break;     
                    case NORM_SOCKET_CLOSING:
                        fprintf(stderr, "normClient: %sserver CLOSING connection ...\n",
                                        (NULL != groupAddrPtr) ? "multicast " : "");
                        writeReady = false;
                        inputNeeded = false;
				        break;   
                    case NORM_SOCKET_CLOSE:
                    {
                        fprintf(stderr, "normClient: connection to %sserver CLOSED.\n",
                                        (NULL != groupAddrPtr) ? "multicast " : "");
                        writeReady = false;
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
        }  // end if normEventPending
        
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
    NormClose(normSocket);
#ifdef WIN32
    inputHandler.Close();
#else
    if ((stdin != inputFile) && (NULL != inputFile))
    {
        fclose(inputFile);
        inputFile = NULL;
    }
#endif  // if/else WIN32
	fprintf(stderr, "normClient: Done.\n");
	return 0;
} 
