
// This example NORM application acts as unicast "server". A NORM receive-only session "listens"
// on a "serverPort". When new remote senders are detected, the sender is assigned to a
// newly create "client" NormSession that is "connected" to the remote sender/client addr/port.
// The NormSetRxPortReuse() call provides an option to "connect" the underlying NORM UDP socket
// to the remote sender/client address/port.  Note this is not yet supported for NORM-CCE (ECN)
// congestion control operation.

// By creating "client" NormSessions for each client connection, this enables multiple clients
// from the same host (with the same NormNodeId to connect to the server at the same time.
// Otherwise, clients with the same NormNodeId would "collide" at the server receive session.
// Even the approach here is not perfect as a packet from a another client instance with the 
// same NormNodeId might change the given "remote sender" source addr/port before the
// connected "client" session is created as a sort of race condition.
// A potential enhancement to NORM would be to allow tracking of multiple remote senders
// with the same NormNodeId but different instanceIds ...

// BUILD: (assumes "normApi.h" in "include" ...
// g++ -I../include -o normServer normServer.cpp normSocket.cpp ../lib/libnorm.a ../protolib/lib/libprotokit.a -lresolv


// EXAMPLE NOTES: 
//
// 0) THIS IS A WORK IN PROGRESS AND NOT YET FUNCTIONAL!
//
// 1) Some of the functions here may be added to the NORM API in the future.
//
// 2) The "main()" below is a single-threaded example with a single NormInstance and a
//    single NormGetNextEvent() main loop. Note that the NormAccept() call allows for a
//    new instance (and hence new NormDescriptor) for each accepted client, so multiple
//    threads or event thread-pooling versions of this could be implemented for
//    performance or application design purposes.  A future version of the NORM API and
//    this could even provide for very "socket-like" API calls where each "client"
//    session has its own descriptor independent of threading (e.g. a "normSocket.h" API
//    that is  implemented around the current low level NORM API).  This sort of "NORM
//    socket" approach could be supported for unicast and SSM streams without too much
//    difficulty.

#include "normSocket.h"
#include <arpa/inet.h>  // for inet_ntoa
#include <stdio.h>      // for fprintf()
#include <string.h>     // for memcmp()
#include <map>          // for std::map<>
#include <sys/select.h>
#include <fcntl.h>       // for, well, fnctl()
#include <errno.h>       // obvious child

// Our "server" indexes clients by their source addr/port
class ClientInfo
{
    public:
        ClientInfo(UINT8 ipVersion = 0, const char* theAddr = NULL, UINT16 thePort = 0);
        bool operator < (const ClientInfo& a) const;
        
        int GetAddressFamily() const;
        const char* GetAddress() const
            {return client_addr;}
        UINT16 GetPort() const
            {return client_port;}
        
        const char* GetAddressString() const;
        void Print(FILE* filePtr) const;
        
    private:
        UINT8               addr_version;  // 4 or 6
        char                client_addr[16]; // big enough for IPv6
        UINT16              client_port;
        
};  // end class ClientInfo


ClientInfo::ClientInfo(UINT8 addrVersion, const char* clientAddr, UINT16 clientPort)
 : addr_version(addrVersion), client_port(clientPort)
{
    if (NULL == clientAddr) addrVersion = 0; // forces zero initialization
    switch (addrVersion)
    {
        case 4:
            memcpy(client_addr, clientAddr, 4);
            memset(client_addr+4, 0, 12);
            break;
        case 6:
            memcpy(client_addr, clientAddr, 16);
            break;
        default:
            memset(client_addr, 0, 16);
            break;
    }
}

// returns "true" if "this" less than "a" (used by C++ map)
bool ClientInfo::operator <(const ClientInfo& a) const
{
    if (addr_version != a.addr_version)
        return (addr_version < a.addr_version);
    else if (client_port != a.client_port)
        return (client_port < a.client_port);
    else if (4 == addr_version)
        return (0 > memcmp(client_addr, a.client_addr, 4));
    else
        return (0 > memcmp(client_addr, a.client_addr, 16));
}  // end ClientInfo::operator <()

int ClientInfo::GetAddressFamily() const
{
    if (4 == addr_version)
        return AF_INET;
    else
        return AF_INET6;
}  // end ClientInfo::GetAddressFamily()


const char* ClientInfo::GetAddressString() const
{
    static char text[64];
    text[63] = '\0';    
    int addrFamily;
    if (4 == addr_version)
        addrFamily = AF_INET;
    else
        addrFamily = AF_INET6;
    inet_ntop(addrFamily, client_addr, text, 63);
    return text;
}  // end ClientInfo::GetAddressString() 

void ClientInfo::Print(FILE* filePtr) const
{
    char text[64];
    text[63] = '\0';    
    int addrFamily;
    if (4 == addr_version)
        addrFamily = AF_INET;
    else
        addrFamily = AF_INET6;
    inet_ntop(addrFamily, client_addr, text, 63);
    fprintf(filePtr, "%s/%hu", text, client_port);
}  // end ClientInfo::Print()


// C++ map used to index client sessions by the client source addr/port
typedef std::map<ClientInfo, NormSocketHandle> ClientMap;

ClientInfo NormGetClientInfo(NormNodeHandle client)
{
    char addr[16]; // big enough for IPv6
    unsigned int addrLen = 16;
    UINT16 port;
    NormNodeGetAddress(client, addr, &addrLen, &port);
    int addrFamily;
    UINT8 version;
    if (4 == addrLen)
    {
        addrFamily = AF_INET;
        version = 4;
    }
    else
    {
        addrFamily = AF_INET6;
        version = 6;
    }
    return ClientInfo(version, addr, port);
}  // end NormGetClientInfo()

NormSocketHandle FindClientSocket(ClientMap& clientMap, const ClientInfo& clientInfo)
{
    ClientMap::iterator it = clientMap.find(clientInfo);
    if (clientMap.end() != it) 
        return &(it->second);
    else
        return NORM_SOCKET_INVALID;
}  // end FindClientSocket()



int main(int argc, char* argv[])
{
    ClientMap clientMap;
    
    UINT16 serverPort = 5000;
    UINT16 serverInstanceId = 1;
    char groupAddr[64];
    const char* groupAddrPtr = NULL;
    const char* mcastInterface = NULL;
    
    bool trace = false;
    unsigned int debugLevel = 0;
    
    
    for (int i = 1; i < argc; i++)
    {
        const char* cmd = argv[i];
        unsigned int len = strlen(cmd);
        if (0 == strncmp(cmd, "listen", len))
        {
            // listen [<groupAddr>/]<port>
            const char* val = argv[++i];
            const char* portPtr = strchr(val, '/');
            if (NULL != portPtr)
                portPtr++;
            else
                portPtr = val;
            unsigned int addrTextLen = portPtr - val;
            if (addrTextLen > 0)
            {
                addrTextLen -= 1;
                strncpy(groupAddr, val, addrTextLen);
                groupAddr[addrTextLen] = '\0';
                groupAddrPtr = groupAddr;
            }
            if (1 != sscanf(portPtr, "%hu", &serverPort))
            {
                fprintf(stderr, "normServer error: invalid <port> \"%s\"\n", portPtr);
                return -1;
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
        else
        {
            fprintf(stderr, "normServer error: invalid command \"%s\"\n", cmd);
            return -1;
        }
    }    
    
    // For unicast operation in this demo app, the server only "talks back"
    // to one client on a first come, first serve basis. (Multiple clients
    // can connect and send data _to_ the server, but in this simple example,
    // the server only sends to one at a time. For the multicast server case,
    // the server multicasts to the entire group.
    NormSocketHandle firstClientSocket = NORM_SOCKET_INVALID;
    
    NormInstanceHandle instance = NormCreateInstance();
    
    NormSocketHandle serverSocket = NormListen(instance, serverPort, groupAddrPtr);
    
    if (trace) NormSetMessageTrace(NormGetSession(serverSocket), true);
    if (0 != debugLevel) NormSetDebugLevel(debugLevel);
    
    //NormSetDebugLevel(8);
    //NormSetMessageTrace(NormGetSession(serverSocket), true);
    
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
                // TBD - initiate server shutdown if it's job is
                // only until the input is closed
                // Meanwhile, we just stick around to receive stuff from the clients
                inputNeeded = false;  // TBD - should also fclose(inputFile)??
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
                ClientInfo clientInfo;
                if (NORM_NODE_INVALID != event.sender)
                    clientInfo = NormGetClientInfo(event.sender);
                switch (event.type)
                {           
                    case NORM_SOCKET_ACCEPT:
                    {
                        if (event.socket == serverSocket)
                        {
                            // Possibly a new "client" connecting to our "server"
                            // First confirm that this really is a new client.
                            if (NORM_SOCKET_INVALID != FindClientSocket(clientMap, clientInfo))
                            {
                                // We think we're already connected to this client
                                fprintf(stderr, "normServer: duplicative %s client ...\n",
                                        (NORM_REMOTE_SENDER_NEW == event.event.type) ? "new" : "reset");
                                continue;
                            }
                            NormSocketHandle clientSocket = NormAccept(serverSocket, event.sender);
                            
                            // TBD - For multicast, if we are sending a tx_stream, we could flush it here to 
                            // bring the new receiver "up to date" ... probably would be best to
                            // do this on some sort of timer-basis in the case of a bunch of receivers
                            // joining in a short window of time ...
                            
                            if (trace)  // note we're already tracing the mcast session
                                NormSetMessageTrace(NormGetSession(clientSocket), true);
                            //NormSetMessageTrace(NormGetSession(clientSocket), true);
                            clientMap[clientInfo] = clientSocket;
                            fprintf(stderr, "normServer: ACCEPTED connection from %s/%hu\n",
                                            clientInfo.GetAddressString(), clientInfo.GetPort());
                            // We have at least one client, so lets serve up some juicy input
                            inputNeeded = true;
                            writeReady = true;
                        }
                        else
                        {
                            // shouldn't happen
                        }
				        break;
                    }
                    case NORM_SOCKET_CONNECT:
                    {
                        fprintf(stderr, "normServer: CONNECTED to %s/%hu ...\n",
                                        clientInfo.GetAddressString(), clientInfo.GetPort());
                        if (NORM_SOCKET_INVALID == firstClientSocket)
                                firstClientSocket = event.socket;
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
                                    fprintf(stderr, "normServer: broken stream ...\n");
                                    continue;
                                }
                                if (bytesRead > 0)
                                    fwrite(buffer, sizeof(char), bytesRead, stdout);
                                if (bytesRead < 1024) rxReady = false;
                            }
				        break;
                    }      
                    case NORM_SOCKET_WRITE:
                        // We only demo server data transmission for the multicast server case
                        // (see comment below)
                        if ((NULL != groupAddrPtr) || (event.socket == firstClientSocket)) 
                            writeReady = true;
				        break;     
                    case NORM_SOCKET_CLOSING:
                        fprintf(stderr, "normServer: client %s/%hu CLOSING connection ...\n",
                                            clientInfo.GetAddressString(), clientInfo.GetPort());
                        if (event.socket == firstClientSocket)
                            firstClientSocket = NORM_SOCKET_INVALID;
                        
				        break;   
                    case NORM_SOCKET_CLOSED:
                    {
                        fprintf(stderr, "normServer: connection to client %s/%hu CLOSED ...\n",
                                            clientInfo.GetAddressString(), clientInfo.GetPort());
                        if (event.socket == firstClientSocket)
                            firstClientSocket = NORM_SOCKET_INVALID;
                        clientMap.erase(clientInfo);
				        break;   
                    }
                    case NORM_SOCKET_NONE:
				        break; 
                        break;
                }  // end switch(event.type)   
            }
            else
            {
                fprintf(stderr, "normServer: NormGetNextSocketEvent() returned false\n");
            }
        }  // end if FD_ISSET(normfd)
        
        // For our _multicast_ "normServer" example, the server can send to the group                        
        // (For a unicast "normServer", we would need to do something more complex
        //  to manage sending data to each individual client that connects to us.
        //  So, for the moment, the unicast "normServer" only sends to the "firstClientSocket"
        NormSocketHandle sendSocket = (NULL != groupAddrPtr) ? serverSocket : firstClientSocket;
        if (NORM_SOCKET_INVALID != sendSocket)
        {
            if (writeReady && (inputLength > 0))
            {
                // We have data in our inputBuffer and the NormSocket is "writeReady", so send it
                bytesWritten += NormWrite(sendSocket, inputBuffer + bytesWritten, inputLength - bytesWritten);
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
                    NormFlush(sendSocket);
                }
            }
        }
        
    }  // end while (keepGoing)
}  // end main()
