
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

#include <stdio.h>      // for fprintf()
#include <string.h>     // for memcmp()
#include <map>          // for std::map<>
#include <assert.h>     // more obvious

#include "normSocket.h"
#ifdef WIN32
#include "win32InputHandler.cpp"  // to include full implementation
#include <Winsock2.h>             // for inet_ntoa()
#include <Ws2tcpip.h>             // for inet_ntop()
#else
#include <arpa/inet.h>  // for inet_ntoa
#include <sys/select.h>
#include <fcntl.h>       // for, well, fnctl()
#include <errno.h>       // obvious child
#endif // if/else WIN32/UNIX

void Usage()
{
    fprintf(stderr, "Usage: normServer [listen [<groupAddr>/]<port>][debug <level>][trace]\n");
}

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
        
        const char* GetAddressString();
        void Print(FILE* filePtr);
        
    private:
        UINT8               addr_version;    // 4 or 6
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


const char* ClientInfo::GetAddressString() 
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

void ClientInfo::Print(FILE* filePtr)
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

class Client
{
    public:
        Client(NormSocketHandle clientSocket);
        ~Client();
        
        NormSocketHandle GetSocket() const
            {return client_socket;}
        
        bool GetWriteReady() const
            {return write_ready;}
        void SetWriteReady(bool state)
            {write_ready = state;}
        
        unsigned int GetBytesWritten() const
            {return bytes_written;}
        void SetBytesWritten(unsigned long numBytes)
            {bytes_written = numBytes;}
               
    private:
        NormSocketHandle   client_socket;
        // These are state variables for unicast server -> client communication
        bool                write_ready;
        unsigned int        bytes_written;
        
};  // end class Client

Client::Client(NormSocketHandle clientSocket)
 : client_socket(clientSocket), 
   write_ready(true), bytes_written(0)
{
}

Client::~Client()
{
}

// C++ map used to index client sessions by the client source addr/port
typedef std::map<ClientInfo, Client*> ClientMap;

ClientInfo NormGetClientInfo(NormNodeHandle client)
{
    char addr[16]; // big enough for IPv6
    unsigned int addrLen = 16;
    UINT16 port;
    NormNodeGetAddress(client, addr, &addrLen, &port);
    UINT8 version;
    if (4 == addrLen)
        version = 4;
    else
        version = 6;
    return ClientInfo(version, addr, port);
}  // end NormGetClientInfo(NormNodeHandle)

static ClientInfo NormGetSocketInfo(NormSocketHandle socket)
{
    char addr[16]; // big enough for IPv6
    unsigned int addrLen = 16;
    UINT16 port;
    NormGetPeerName(socket, addr, &addrLen, &port);
    UINT8 version;
    if (4 == addrLen)
        version = 4;
    else
        version = 6;
    return ClientInfo(version, addr, port);
}  // end NormGetSocketInfo(NormSocketHandle)

Client* FindClient(ClientMap& clientMap, const ClientInfo& clientInfo)
{
    ClientMap::iterator it = clientMap.find(clientInfo);
    if (clientMap.end() != it) 
        return it->second;
    else
        return NULL;
}  // end FindClient()

NormSocketHandle FindClientSocket(ClientMap& clientMap, const ClientInfo& clientInfo)
{
    Client* client = FindClient(clientMap, clientInfo);
    if (NULL == client)
        return NORM_SOCKET_INVALID;
    else
        return client->GetSocket();
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
                Usage();
                return -1;
            }
        }
        else if (0 == strncmp(cmd, "interface", len))
        {
            // Note the NormSocket code does not yet expose mcast interface
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
                Usage();
                return -1;
            }
        }
        else
        {
            fprintf(stderr, "normServer error: invalid command \"%s\"\n", cmd);
            Usage();
            return -1;
        }
    }    
    
    NormInstanceHandle instance = NormCreateInstance();
    
    NormSocketHandle serverSocket = NormOpen(instance);
    
    NormListen(serverSocket, serverPort, groupAddrPtr);
    
    if (trace) NormSetMessageTrace(NormGetSocketSession(serverSocket), true);
    if (0 != debugLevel) NormSetDebugLevel(debugLevel);
    
    //NormSetDebugLevel(8);
    //NormSetMessageTrace(NormGetSocketSession(serverSocket), true);
    
    
#ifdef WIN32
    HANDLE hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
    Win32InputHandler inputHandler;
    inputHandler.Open();
    HANDLE handleArray[2];
    handleArray[0] = NormGetDescriptor(instance);
    handleArray[1] = inputHandler.GetEventHandle();
#else    
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
#endif // if/else WIN32/UNIX
    
    bool keepGoing = true;
    bool writeReady = false;
    int inputLength = 0;
    unsigned int bytesWritten = 0;
    const unsigned int BUFFER_LENGTH = 2048;
    char inputBuffer[BUFFER_LENGTH];
    bool inputNeeded = false;  // will be set to "true" upon CONNECT
    bool inputClosed = false;
    unsigned int clientCount = 0;
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
            perror("normServer: MsgWaitForMultipleObjectsEx() error");
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
            perror("normServer: select() error");
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
                inputHandler.Close();
                inputClosed = true;
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
                if (stdin != inputFile)
                {
                    fclose(inputFile);
                    inputFile = NULL;
                }
                inputClosed = true;
                
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
                        perror("normServer: error reading input?!");
                        break;
                }
            }
#endif // if/else WIN32/UNIX
            if (inputClosed)
            {
                inputNeeded = false;
                // Gracefully shutdown any connected clients
                // TBD - set state variable to indicate exit upon all closed?
                if (clientMap.empty())
                {
                    keepGoing = false;
                    continue;
                }
                else
                {
                    ClientMap::iterator it;
                    for (it = clientMap.begin(); it != clientMap.end(); it++)
                    {
                        Client* client = it->second;
                        NormSocketHandle clientSocket = client->GetSocket();
                        NormShutdown(clientSocket);
                    }
                }
            }
        }  // end if inputEventPending
        
        if (normEventPending)
        {

            // There's a NORM event pending
            NormSocketEvent event;
            if (NormGetSocketEvent(instance, &event))
            {
                ClientInfo clientInfo;
                if (NORM_NODE_INVALID != event.sender)
                    clientInfo = NormGetClientInfo(event.sender);
                else
                    clientInfo = NormGetSocketInfo(event.socket);
                switch (event.type)
                {           
                    case NORM_SOCKET_ACCEPT:
                    {
                        if (event.socket == serverSocket)
                        {
                            
                            // TBD - now that the NormSocket code manages its own client_table by remote addr/port
                            // and should eliminate the 'duplicative' connect itself, we can just keep track
                            // of client sockets by their NormSocketHandle
                            
                            // Possibly a new "client" connecting to our "server"
                            // First confirm that this really is a new client.
                            if (NORM_SOCKET_INVALID != FindClientSocket(clientMap, clientInfo))
                            {
                                // We think we're already connected to this client
                                fprintf(stderr, "normServer: duplicative %s from client %s/%hu...\n",
                                        (NORM_REMOTE_SENDER_NEW == event.event.type) ? "new" : "reset",
                                        clientInfo.GetAddressString(), clientInfo.GetPort());
                                continue;
                            }
                            NormSocketHandle clientSocket = NormAccept(serverSocket, event.sender);
                            
                            Client* client = new Client(clientSocket);
                            if (NULL == client)
                            {
                                perror("normServer: new Client() error");
                                NormClose(clientSocket);
                                continue;
                            }

							// TBD - For multicast, if we are sending a tx_stream, we could flush it here to 
                            // bring the new receiver "up to date" ... probably would be best to
                            // do this on some sort of timer-basis in the case of a bunch of receivers
                            // joining in a short window of time ...
                            
                            if (trace)  // note we're already tracing the mcast session
                                NormSetMessageTrace(NormGetSocketSession(clientSocket), true);
                            //NormSetMessageTrace(NormGetSocketSession(clientSocket), true);
                            clientMap[clientInfo] = client;
                            
                            // ACCEPTED is good as CONNECTED, so enable writing right away
                            client->SetWriteReady(true);
                            if (0 == clientCount)
                            {
                                // We have at least one client, so lets serve up some juicy input
                                inputNeeded = true;
                                writeReady = true;
                            }
                            clientCount++;
                            fprintf(stderr, "normServer: ACCEPTED connection from %s/%hu\n",
                                            clientInfo.GetAddressString(), clientInfo.GetPort());
                            // Note that an ACCEPTED socket is essentially CONNECTED, so we could
                            // go ahead and set writeReady to true, etc here
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
                        Client* client = FindClient(clientMap, clientInfo);
                        assert(NULL != client);
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
                                
                                {
#ifdef WIN32
                                    // Use WriteFile() so there is no buffer delay
                                    DWORD dwWritten;
                                    WriteFile(hStdout, buffer, bytesRead, &dwWritten, NULL);
#else
                                    fwrite(buffer, sizeof(char), bytesRead, stdout);
#endif // if/else WIN32
                                }
                                if (bytesRead < 1024) rxReady = false;
                            }
                            // Following lines are test code (to immediately close connection after first reaad)
                            // (tests server stale connection "reject" command)
                            //clientMap.erase(clientInfo);
                            //NormClose(event.socket);
				        break;
                    }      
                    case NORM_SOCKET_WRITE:
                    {
                        if (NULL != groupAddrPtr)
                        {
                            // We are a multicast server
                            writeReady = true;
                        }
                        else
                        {
                            Client* client = FindClient(clientMap, clientInfo);
                            assert(NULL != client);
                            client->SetWriteReady(true);
                        }
                        break;     
                    }
                    case NORM_SOCKET_CLOSING:
                    {
                        fprintf(stderr, "normServer: client %s/%hu CLOSING connection ...\n",
                                            clientInfo.GetAddressString(), clientInfo.GetPort());
                        Client* client = FindClient(clientMap, clientInfo);
                        assert(NULL != client);
                        client->SetWriteReady(false);
                        break;   
                    }
                    case NORM_SOCKET_CLOSE:
                    {
                        fprintf(stderr, "normServer: connection to client %s/%hu CLOSED ...\n",
                                            clientInfo.GetAddressString(), clientInfo.GetPort());
                        clientMap.erase(clientInfo);
                        NormClose(event.socket);
                        if (inputClosed && clientMap.empty())
                            keepGoing = false;
                        break;   
                    }
                    case NORM_SOCKET_NONE:
				        break; 
                }  // end switch(event.type)   
            }
            else
            {
                fprintf(stderr, "normServer: NormGetNextSocketEvent() returned false\n");
            }
        }  // end if FD_ISSET(normfd)
        
        // If the normServer app has unsent data from STDIN, send it to the clients.
        // Note that a _multicast_ server multicasts to all clients at once while a 
        // unicast server sends to each connected client individually
        if ((inputLength > 0) && !inputNeeded)
        {
            // There is inputBuffer data for the server to send to the client(s)
            if (NULL == groupAddrPtr)
            {
                // Unicast the data to each connected client individually by iterating
                // over the clientMap and sending data out to each pending client socket
                // (inputNeeded is reset to "true" when _all_ clients are non-pending)
                bool clientPending = false;
                ClientMap::iterator it;
                for (it = clientMap.begin(); it != clientMap.end(); it++)
                {
                    Client* client = it->second;
                    if (!client->GetWriteReady())
                    {
                        clientPending = true;
                        continue;
                    }
                    unsigned int numBytes = client->GetBytesWritten();
                    if (numBytes < inputLength)
                    {
                        NormSocketHandle clientSocket = client->GetSocket();
                        bytesWritten += NormWrite(clientSocket, inputBuffer + numBytes, inputLength - numBytes);
                        client->SetBytesWritten(numBytes);
                        if (bytesWritten < inputLength)
                        {
                            // Couldn't write whole inputBuffer, need to wait for NORM_SOCKET_WRITE event
                            // for this client socket
                            client->SetWriteReady(false);
                            clientPending = true;
                        }
                        else
                        {
                            // inputBuffer has been completely written to this client
                            NormFlush(clientSocket);
                        }
                    }
                }  
                if (!clientPending)
                {
                    // inputBuffer was sent to _all_ clients, so reset 
                    inputLength = 0;
                    inputNeeded = true;
                    // Reset all client "bytes_written" to zero for next chunk of data
                    for (it = clientMap.begin(); it != clientMap.end(); it++)
                        it->second->SetBytesWritten(0);
                }
            }
            else
            {
                // Multicast the data out the "serverSocket" to all clients
                // (Use the 'writeReady' and 'bytesWritten' state variables)
                NormSocketHandle sendSocket = serverSocket;
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
            }
        }  // end if ((inputLength > 0) && !inputNeeded)
        
        
    }  // end while (keepGoing)
#ifdef WIN32
    inputHandler.Close();
#else
    if ((stdin != inputFile) && (NULL != inputFile))
    {
        fclose(inputFile);
        inputFile = NULL;
    }
#endif // if/else WIN32
    NormClose(serverSocket);
    serverSocket = NORM_SOCKET_INVALID;
}  // end main()
