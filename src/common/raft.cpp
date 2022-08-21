
#include "protokit.h"
#include <stdio.h>  // for sscanf()

#ifdef UNIX
#include <unistd.h>
#include <fcntl.h>
#endif // UNIX


class RaftApp : public ProtoApp
{
    public:
        RaftApp();
        ~RaftApp();
        
        // Overrides from ProtoApp base
        bool OnStartup(int argc, const char*const* argv);
        bool ProcessCommands(int argc, const char*const* argv);
        void OnShutdown();
        
        bool OnCommand(const char* cmd, const char* arg = NULL);
            
    private:
        static void Usage()
        {
            fprintf(stderr, "Usage: raft [listen [<groupAddr>/]<port>][dest <addr>/<port>][ipv4|ipv6]\n");
        }
        enum CmdType {CMD_INVALID, CMD_NOARG, CMD_ARG};
        static CmdType GetCommandType(const char* cmd);    
        static const char* const CMD_LIST[];
        
        // Tunnel related members ...
        void OnRxSocketEvent(ProtoSocket&       theSocket,
                             ProtoSocket::Event theEvent);
        static void DoInputReady(ProtoDispatcher::Descriptor descriptor,
                                 ProtoDispatcher::Event      event,
                                 const void*                 userData);
        void OnInputReady();
        
        ProtoSocket     rx_socket;
        ProtoSocket     tx_socket;
        ProtoAddress    tx_address;
        char            tx_msg_buffer[8194];
        UINT16          tx_msg_length;
        UINT16          tx_msg_index;
        
        // RTSP proxy related members ...
        void OnProxySocketEvent(ProtoSocket&       theSocket,
                                ProtoSocket::Event theEvent);
        void OnClientSocketEvent(ProtoSocket&       theSocket,
                                 ProtoSocket::Event theEvent);
    
        ProtoSocket     rtsp_proxy_socket; 
        char*           rtsp_url;
        ProtoAddress    rtsp_server_address;
        ProtoSocket     rtsp_client_socket;
                        
		enum IpvType { IPV4, IPV6 };
		IpvType			rcv_socket_type;
		ProtoAddress	rcv_group_addr;
		UINT16			rcv_port;
};  // end class RaftApp

const char* const RaftApp::CMD_LIST[] = 
{
    "+debug",        // debug <level>
    "+listen",       // recv [<mcastAddr>/]<port>
    "+dest",         // send <addr>/<port>
    "+rtspProxy",    // rtsp <rtspUrl>
	"-ipv4",         // rcv socket is IPv4 (default)
	"-ipv6",         // rcv socket is IPv6
    NULL        
};

RaftApp::RaftApp()
 : rx_socket(ProtoSocket::UDP),
   tx_socket(ProtoSocket::UDP), tx_msg_length(0), tx_msg_index(0),
   rtsp_proxy_socket(ProtoSocket::TCP), rtsp_url(NULL),
   rtsp_client_socket(ProtoSocket::TCP), rcv_socket_type(IPV4),
   rcv_port(0)
{
    rx_socket.SetNotifier(&GetSocketNotifier());
    rx_socket.SetListener(this, &RaftApp::OnRxSocketEvent);
    
    rtsp_proxy_socket.SetNotifier(&GetSocketNotifier());
    rtsp_proxy_socket.SetListener(this, &RaftApp::OnProxySocketEvent);
    rtsp_client_socket.SetNotifier(&GetSocketNotifier());
    rtsp_client_socket.SetListener(this, &RaftApp::OnClientSocketEvent);
}

RaftApp::~RaftApp()
{
    if (rtsp_proxy_socket.IsOpen()) 
        rtsp_proxy_socket.Close();
    if (rtsp_client_socket.IsOpen()) 
        rtsp_client_socket.Close();
    if (rtsp_url) 
    {
        delete rtsp_url;
        rtsp_url = NULL;  
    }
}

bool RaftApp::OnStartup(int argc, const char*const* argv)
{
    bool result = ProcessCommands(argc, argv);
	if (rcv_port > 0)  // need to open rcv port after PC()
	{
		if (rcv_socket_type == IPV4)
		{
			if (!rx_socket.Open(rcv_port, ProtoAddress::IPv4))
			{
				PLOG(PL_FATAL, "Raft::OnCommand() rx_socket.Open() error\n");
				return false;   
			}
		}
		else  // IPV6
		{
			if (!rx_socket.Open(rcv_port, ProtoAddress::IPv6))
			{
				PLOG(PL_FATAL, "Raft::OnCommand() rx_socket.Open() error\n");
				return false;   
			}
		}

		if (rcv_group_addr.IsValid())  // need to join group after PC()
		{
			if (!rx_socket.JoinGroup(rcv_group_addr))
			{
				PLOG(PL_FATAL, "Raft::OnStartup() rx_socket.JoinGroup() error\n");
				return false;
			}
		}
	}

    if (result && !dispatcher.IsPending())
    {
        Usage();
        OnShutdown();
        return false;   
    }
    return result;
}  // end RaftApp::OnStartup()

void RaftApp::OnShutdown()
{
    if (rtsp_proxy_socket.IsOpen()) 
        rtsp_proxy_socket.Close();
    if (rtsp_client_socket.IsOpen()) 
        rtsp_client_socket.Close();
    if (rtsp_url) 
    {
        delete rtsp_url;
        rtsp_url = NULL;   
    }
}  // end RaftApp::OnShutdown()

bool RaftApp::OnCommand(const char* cmd, const char* arg)
{
    if (!strncmp(cmd, "dest", strlen(cmd)))
    {
        char host[256];
        char* ptr = (char*) strchr(arg, '/');
        if (ptr)
        {
            unsigned int len = ptr - arg;
            strncpy(host, arg, len);
            host[len] = '\0';
            ptr++;
        }
        else
        {
            PLOG(PL_FATAL, "Raft::OnCommand() invalid \"dest\" command\n");
            return false;   
        }  
        if (!tx_address.ResolveFromString(host))
        {
            PLOG(PL_FATAL, "Raft::OnCommand() invalid dest address\n");
            return false;   
        }
        UINT16 port;
        if (1 != sscanf(ptr, "%hu", &port))
        {
            PLOG(PL_FATAL, "Raft::OnCommand() invalid dest port\n"); 
            return false;  
        }
        tx_address.SetPort(port);
        int fd = fileno(stdin);
        if(-1 == fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK))
        {
           PLOG(PL_FATAL, "Raft::OnCommand() warning: fcntl(stdout, F_SETFL(O_NONBLOCK)) error: %s",
                    strerror(errno));
        }
        tx_msg_length = tx_msg_index = 0;
        dispatcher.InstallGenericInput(fileno(stdin), RaftApp::DoInputReady, this);
    }
    else if (!strncmp(cmd, "listen", strlen(cmd)))
    {
        const char* ptr = strchr(arg, '/');
        if (!ptr)
			ptr = arg;  // no group address, just a port #
		else  // extract the group address
        {
            char group[256];
            unsigned int len = ptr - arg;
            ptr++;    // point to port #
            strncpy(group, arg, len);
            group[len] = '\0';     
            if (!rcv_group_addr.ResolveFromString(group) ||
			    !rcv_group_addr.IsMulticast() )
            {
                PLOG(PL_FATAL, "Raft::OnCommand() invalid recv multicast address\n");
                return false;    
            }   
        }   

        if (1 != sscanf(ptr, "%hu", &rcv_port))
        {
            PLOG(PL_FATAL, "Raft::OnCommand() invalid recv port\n"); 
            return false;  
        }

    }
    else if (!strncmp(cmd, "rtspProxy", strlen(cmd)))
    {
        if (!rtsp_proxy_socket.Listen(554))
        {
            PLOG(PL_FATAL, "RaftApp::OnCommand() error: rtsp_proxy_socket.Listen() failed\n");
            return false;   
        }
        if (rtsp_url) delete rtsp_url;
        if (!(rtsp_url = new char[strlen(arg) + 1]))
        {
            PLOG(PL_FATAL, "RaftApp::OnCommand() new rtsp_url error: %s\n",
                    strerror(errno));
            rtsp_proxy_socket.Close();
            return false;        
        }
        strcpy(rtsp_url, arg);
        char* ptr = strstr(rtsp_url, "rtsp://");
        if (!ptr)
        {
            PLOG(PL_FATAL, "RaftApp::OnCommand() error: invalid rtsp_url\n");
            rtsp_proxy_socket.Close();
            return false;  
        }
        ptr += 7;
        char hostName[256];
        if (1 != sscanf(ptr, "%255s", hostName))
        {
            PLOG(PL_FATAL, "RaftApp::OnCommand() error: no rtsp_url hostname\n");
            rtsp_proxy_socket.Close();
            return false;  
        }
        char* sptr = strchr(hostName, '/');
        if (sptr) *sptr = '\0';
        if (!rtsp_server_address.ResolveFromString(hostName))
        {
            PLOG(PL_FATAL, "RaftApp::OnCommand() error: invalid rtsp_url hostname:%s\n",
                    hostName);
            rtsp_proxy_socket.Close();
            return false;  
        }
        rtsp_server_address.SetPort(554);
    }
	else if (!strncmp(cmd, "ipv6", strlen(cmd)))
		rcv_socket_type = IPV6;
	else if (!strncmp(cmd, "ipv4", strlen(cmd)))
		rcv_socket_type = IPV4;   // the default
    else
    {
        PLOG(PL_FATAL, "RaftApp::OnCommand() error: invalid command!\n");
        return false;    
    }        
    return true;
}  // end RaftApp::OnCommand()

bool RaftApp::ProcessCommands(int argc, const char*const* argv)
{
    int i = 1;
    while (i < argc)
    {
        switch (GetCommandType(argv[i]))
        {
            case CMD_ARG:
                if (!OnCommand(argv[i], argv[i+1]))
                {
                    PLOG(PL_FATAL, "RaftApp::ProcessCommands() %s command error\n", argv[i]);
                    return false;  
                }
                i += 2;
                break;
            case CMD_NOARG:
                if (!OnCommand(argv[i]))
                {
                    PLOG(PL_FATAL, "RaftApp::ProcessCommands() %s command error\n", argv[i]);
                    return false;  
                }
                i++;
                break;
            default:
                PLOG(PL_FATAL, "RaftApp::ProcessCommands() invalid command\n");
                return false;   
        }
    }  
    return true;
}  // end RaftApp::ProcessCommands()

void RaftApp::DoInputReady(ProtoDispatcher::Descriptor /*descriptor*/,
                           ProtoDispatcher::Event      /*event*/,
                           const void*                 userData)
{
    ((RaftApp*)userData)->OnInputReady();   
}  // end RaftApp::DoInputReady()

void RaftApp::OnInputReady()
{
    // Read from stdin ...
    unsigned int want = tx_msg_length ? tx_msg_length - tx_msg_index : 2 - tx_msg_index;
    if (want)
    {
        int result = fread(tx_msg_buffer+tx_msg_index, 1, want, stdin);
        if (result > 0)
        {
            tx_msg_index += result;
        }
        else if (ferror(stdin))
        {
            switch (errno)
            {
                case EINTR:
                case EAGAIN:
                    break;
                default:
                    PLOG(PL_ERROR, "raft: input error:%s\n", strerror(errno));
                    break;   
            }
            clearerr(stdin);
        }
        else if (feof(stdin))
        {
            PLOG(PL_ERROR, "raft: input end-of-file\n");
            dispatcher.RemoveGenericInput(fileno(stdin));
            return;
        }
    } 
    if (0 == tx_msg_length)
    {
        if (2 == tx_msg_index)
        {
            memcpy(&tx_msg_length, tx_msg_buffer, 2);
            tx_msg_length = ntohs(tx_msg_length);
            if ((tx_msg_length < 2) || (tx_msg_length > 8194))
            {
                PLOG(PL_ERROR, "raft: input error: invalid tx_msg_length: %u\n", tx_msg_length);
                dispatcher.RemoveGenericInput(fileno(stdin));
                return;
            }
        }   
        OnInputReady();
    }
    else if (tx_msg_index == tx_msg_length)
    {
        unsigned int bytesSent = tx_msg_length-2;
        if (!tx_socket.SendTo(tx_msg_buffer+2, bytesSent, tx_address))
        {
            PLOG(PL_ERROR, "raft: tx_socket.SendTo() error: %s\n", GetErrorString());
            return;   
        }
        else if (0 == bytesSent)
        {
            PLOG(PL_WARN, "raft: tx_socket.SendTo() error: %s\n", GetErrorString());
            return;  
        }
        tx_msg_index = tx_msg_length = 0;
    }
    
}  // end RaftApp::OnInputReady()

void RaftApp::OnRxSocketEvent(ProtoSocket&       /*theSocket*/,
                              ProtoSocket::Event theEvent)
{
    char buffer[8194];
    unsigned int numBytes = 8192;
    ProtoAddress srcAddr;
    if (!rx_socket.RecvFrom(buffer+2, numBytes, srcAddr))
    {
        PLOG(PL_ERROR, "RaftApp::OnRxSocketEvent() rx_socket.RecvFrom() error\n");
        return;
    }
    numBytes += 2;
    UINT16 msgLength = htons((UINT16)numBytes);
    memcpy(buffer, &msgLength, 2);
    unsigned int put = 0;
    while (put < numBytes)
    {
        size_t result = fwrite(buffer+put, 1, numBytes-put, stdout);
        if (result > 0)
        {
            put += result;
        }
        else if (EINTR != errno)
        {
            PLOG(PL_ERROR, "RaftApp::OnRxSocketEvent() fwrite() error: %s\n",
                    strerror(errno));
        }          
    }
    fflush(stdout);
}  // end RaftApp::OnRxSocketEvent()

void RaftApp::OnProxySocketEvent(ProtoSocket& /*theSocket*/,
                                 ProtoSocket::Event theEvent)
{
    //TRACE("RaftApp::OnProxySocketEvent() ...\n");
    switch (theEvent)
    {
        case ProtoSocket::ACCEPT:
        {
            if (!rtsp_proxy_socket.Accept())
            {
                PLOG(PL_ERROR, "RaftApp::OnProxySocketEvent() rtsp_proxy_socket.Accept() error\n");
                rtsp_proxy_socket.Close();
                if (!rtsp_proxy_socket.Listen(554))
                    PLOG(PL_ERROR, "raft: rtsp_proxy_socket.Listen() error\n");
            }
            TRACE("calling rtsp_client_socket.Connect() ...\n");
            if (!rtsp_client_socket.Connect(rtsp_server_address))
            {
                PLOG(PL_ERROR, "RaftApp::OnProxySocketEvent() rtsp_client_socket.Connect() error\n");
                rtsp_proxy_socket.Close();
            }    
            TRACE("   rtsp_client_socket.Connect() complete.\n");        
            break;
        }
        case ProtoSocket::RECV:
        {
            //TRACE("rtsp_proxy_socket RECV event (connected:%d)...\n",
            //        rtsp_client_socket.IsConnected());
            if (rtsp_client_socket.IsConnected())
            {
                char buffer[1024];
                unsigned int buflen = 1023;
                while (rtsp_proxy_socket.Recv(buffer, buflen))
                {
                    unsigned int put = 0;
                    while (put < buflen)
                    {
                        unsigned int numBytes = buflen - put;
                        if (!rtsp_client_socket.Send(buffer+put, numBytes))
                        {
                            PLOG(PL_ERROR, "rtsp_client_socket.Send() error\n");
                            rtsp_client_socket.Close();
                            rtsp_proxy_socket.Close();
                            if (!rtsp_proxy_socket.Listen(554))
                                PLOG(PL_ERROR, "raft: rtsp_proxy_socket.Listen() error\n");
                        }
                        put += numBytes;
                    }
                    TRACE("rtsp_client sent %u bytes ....\n", put);
                    buflen = 1023;
                }
                TRACE("proxy socket RECV completed.\n");
            }
            break;   
        } 
        case ProtoSocket::DISCONNECT:
        {
            TRACE("rtsp_proxy_socket DISCONNECT event ...\n");
            rtsp_proxy_socket.Close();
            rtsp_client_socket.Close();
            if (!rtsp_proxy_socket.Listen(554))
                PLOG(PL_ERROR, "raft: rtsp_proxy_socket.Listen() error\n");
            break;
        }
        default:
            TRACE("rtsp_proxy_socket UNKNOWN event ...\n");
            break;
    }
}  // end RaftApp::OnProxySocketEvent()

void RaftApp::OnClientSocketEvent(ProtoSocket& /*theSocket*/,
                                  ProtoSocket::Event theEvent)
{
    TRACE("RaftApp::OnClientSocketEvent() ...\n");
    switch (theEvent)
    {
        case ProtoSocket::CONNECT:
        {
            TRACE("RaftApp::OnClientSocketEvent() CONNECT ...\n");
            break;
        }
        case ProtoSocket::RECV:
        {
            TRACE("rtsp_client_socket RECV event ..\n");
            char buffer[1024];
            unsigned int buflen = 1023;
            while (rtsp_client_socket.Recv(buffer, buflen))
            {
                buffer[buflen] = '\0';
                TRACE("%s", buffer);
            }
            TRACE("\n");
            break;   
        } 
        case ProtoSocket::DISCONNECT:
        {
            TRACE("rtsp_client_socket DISCONNECT event ...\n");
            rtsp_client_socket.Close();
            rtsp_proxy_socket.Close();
            if (!rtsp_proxy_socket.Listen(554))
                PLOG(PL_ERROR, "raft: rtsp_proxy_socket.Listen() error\n");
            break;
        }
        default:
            TRACE("rtsp_client_socket UNKNOWN event ...\n");
            break;
    }
}  // end RaftApp::OnClientSocketEvent()

RaftApp::CmdType RaftApp::GetCommandType(const char* cmd)
{
    if (!cmd) return CMD_INVALID;
    unsigned int len = strlen(cmd);
    bool matched = false;
    CmdType type = CMD_INVALID;
    const char* const* nextCmd = CMD_LIST;
    while (*nextCmd)
    {
        if (!strncmp(cmd, *nextCmd+1, len))
        {
            if (matched)
            {
                // ambiguous command (command should match only once)
                return CMD_INVALID;
            }
            else
            {
                matched = true;   
                if ('+' == *nextCmd[0])
                    type = CMD_ARG;
                else
                    type = CMD_NOARG;
            }
        }
        nextCmd++;
    }
    return type;
}  // end RaftApp::GetCommandType()

PROTO_INSTANTIATE_APP(RaftApp)

