
#include "norp.h"
#include "protoNet.h"
#include "protoDebug.h"


// Note this "PortPool" was a simple helper class
// for a test where we needed to traverse a
// NAT / firewall.  We may _later_ refine this
// as an optional feature where "norp" can be
// configured to use a specific set of port
// numbers for NORM traffic.

// Uncomment this to enable new NORM port handling to improve NAT compatibility
#define NEW_PORT

PortPool::PortPool(UINT16 basePort)
 : base_port(basePort)
{
    memset(port_array, 0, 50*sizeof(bool));
}

UINT16 PortPool::GrabPort()
{
    for (UINT16 i = 0; i < 50; i++)
    {
        if (!port_array[i])
        {
            port_array[i] = true;  
            return (base_port + i);
        }
    }
    return 0;  // no port was available
}  // end PortPool::GrabPort()

void PortPool::ReleasePort(UINT16 thePort)
{
    int index = thePort - base_port;
    if ((index < 50) && (index >=0))
        port_array[index] = false;
}  // end PortPool::ReleasePort()

NorpMsg::NorpMsg(UINT32* bufferPtr, unsigned int numBytes, bool initFromBuffer, bool freeOnDestruct)
 : ProtoPkt(bufferPtr, numBytes, freeOnDestruct)
{    
    if (initFromBuffer) InitFromBuffer(numBytes);
}

NorpMsg::~NorpMsg()
{
}

const double NorpSession::NORP_RTT_MIN = 1.0e-03;
const double NorpSession::NORP_RTT_MAX = 5.0;  // TBD - assess these values (make runtime configurable?)
const double NorpSession::NORP_RTT_DEFAULT = 1.0;//e-01;   // 100 msec default
const double Norp::DEFAULT_TX_RATE = 1.0e+06;
const double Norp::DEFAULT_PERSIST_INTERVAL = 60.0;  // times out unsuccessful NORM delivery attempt after successive ACK_FAILURE


NorpSession::NorpSession(Norp& theController, UINT16 sessionId, NormNodeId originatorId)
 : controller(theController), is_preset(false), socks_state(SOCKS_IDLE),
   socks_client_socket(ProtoSocket::TCP), socks_remote_socket(ProtoSocket::TCP), 
   udp_relay_socket(ProtoSocket::UDP), 
   client_pending(0), client_index(0), remote_pending(0), remote_index(0),
   norm_enable(true), norp_tx_socket(ProtoSocket::UDP), norp_rtt_estimate(NORP_RTT_DEFAULT),
   norm_session(NORM_SESSION_INVALID), norm_tx_stream(NORM_OBJECT_INVALID),
   norm_rx_stream(NORM_OBJECT_INVALID), norm_rx_pending(false), norm_sender_heard(false),
   persist_interval(Norp::DEFAULT_PERSIST_INTERVAL), persist_start_time(0, 0),
   norm_rate_min(-1.0), norm_rate_max(-1.0),
   norm_segment_size(0), norm_stream_buffer_max(0), norm_stream_buffer_count(0),
   norm_stream_bytes_remain(0), norm_watermark_pending(false)
{
    memset(&session_id, 0, sizeof(Moniker));
    session_id.originator = originatorId;
    session_id.identifier = sessionId;
    socks_client_socket.SetNotifier(&controller.GetSocketNotifier());
    //socks_client_socket.SetListener(this, &NorpSession::OnSocksClientEvent);
    socks_remote_socket.SetNotifier(&controller.GetSocketNotifier());
    socks_remote_socket.SetListener(this, &NorpSession::OnSocksRemoteEvent);
    udp_relay_socket.SetNotifier(&controller.GetSocketNotifier());
    udp_relay_socket.SetListener(this, &NorpSession::OnUdpRelayEvent);
    
    norp_tx_socket.SetUserData(this);
    norp_tx_socket.SetNotifier(&controller.GetSocketNotifier());
    norp_tx_socket.SetListener(&controller, &Norp::OnNorpSocketEvent);
    
    norp_msg_timer.SetListener(this, &NorpSession::OnNorpMsgTimeout);
    norp_msg_timer.SetInterval(controller.GetInitialRtt());
    norp_msg_timer.SetRepeat(20);
    
    norp_msg.AttachBuffer(norp_msg_buffer, NORP_BUFFER_SIZE, false);
    
    close_timer.SetListener(this, &NorpSession::OnCloseTimeout);
    close_timer.SetInterval(0.0);
    close_timer.SetRepeat(0);  // one-shot timer
}

NorpSession::~NorpSession()
{
    socks_state = SOCKS_VOID;
    Close();
}

void NorpSession::SetNormRateBounds(double rateMin, double rateMax)
{
    norm_rate_min = rateMin;
    norm_rate_max = rateMax;
    if (NORM_SESSION_INVALID != norm_session)
    {
        NormSetTxRateBounds(norm_session, rateMin, rateMax);
        if (Norp::NORM_FIXED == controller.GetNormCC())
        {
            // cumulative rate limit may override fixed session rate
            // (i.e. to share pipe among multiple flows)
            if ((rateMin >= 0.0) && (rateMin < controller.GetNormTxRate()))
                NormSetTxRate(norm_session, rateMin);
            else
                NormSetTxRate(norm_session, controller.GetNormTxRate()); 
        }
    }
}  // end NorpSession::SetNormRateBounds()

NormNodeId NorpSession::GetNodeId() const
{
    return controller.GetNormNodeId();
}  // end NorpSession::GetNodeId()

void NorpSession::ActivateTimer(ProtoTimer& theTimer)
{
    controller.ActivateTimer(theTimer);
}  // end NorpSession::ActivateTimer()

bool NorpSession::AcceptClientConnection(ProtoSocket& serverSocket, bool normEnable)
{
    ASSERT(socks_client_socket.IsClosed());
    if (!serverSocket.Accept(&socks_client_socket))
    {
        PLOG(PL_ERROR, "NorpSession::AcceptClientConnection() error: server socket accept failure\n");
        return false;
    }
    // Assume the same notifier (for now)
    socks_client_socket.SetListener(this, &NorpSession::OnSocksClientEvent);
    norm_enable = normEnable;
    // Init our socks_state
    socks_state = SOCKS_GET_AUTH_REQ;
    // Init client/remote buffer indices
    client_pending = client_index = 0;
    remote_pending = remote_index = 0;
    return true;
}  // end NorpSession::AcceptClientConnection()

bool NorpSession::AcceptPresetClientConnection(NorpPreset& preset, bool normEnable)
{
    ASSERT(socks_client_socket.IsClosed());
    if (!preset.AccessServerSocket().Accept(&socks_client_socket))
    {
        PLOG(PL_ERROR, "NorpSession::AcceptPresetClientConnection() error: server socket accept failure\n");
        return false;
    }
    is_preset = true;
    // Assume the same notifier (for now)
    socks_client_socket.SetListener(this, &NorpSession::OnSocksClientEvent);
    
    norm_enable = normEnable;
    // Jump-start our socks_state to
    socks_state = SOCKS_GET_REQUEST;
    // Init client/remote buffer indices
    client_pending = client_index = 0;
    remote_pending = remote_index = 0;
    
    if (norm_enable)
    {
        // Make and send SOCKS request to remote peer NORP server (peer will TCP connect to endpoint and reply via NORM)
        ProtoPktSOCKS::Request request(client_buffer, 32, false);
        request.SetVersion(5);
        request.SetCommand(ProtoPktSOCKS::Request::CONNECT);
        request.SetAddress(preset.GetDstAddr());
        return PutRemoteRequest(request, preset.GetNorpAddr());
    }
    else  
    {
        return ConnectToRemote(preset.GetDstAddr());  // do direct TCP connect to remote endpoint
    }
    
    return true;
}  // end NorpSession::AcceptPresetSession()

void NorpSession::Shutdown()
{
    // Initiate hard session shutdown, informing remote
    if (IsRemoteSession())
    {
        norp_msg.Init();
        if (IsRemoteOriginator())
            norp_msg.SetType(NorpMsg::ORIG_END);
        else
            norp_msg.SetType(NorpMsg::CORR_END);
        norp_msg.SetSessionId(GetSessionId());
        norp_msg.SetNodeId(controller.GetNormNodeId());
        // The remaining fields don't matter, so we don't send them!
        norp_remote_addr.SetPort(controller.GetNorpPort());
        if (!controller.SendMessage(norp_msg, norp_remote_addr))
            PLOG(PL_ERROR, "NorpSession::Shutdown() warning: NORP message transmission failed (will try again)!\n");
        // Set timer to repeat until ACK_END is received
        if (norp_msg_timer.IsActive()) norp_msg_timer.Deactivate();
        norp_msg_timer.SetInterval(2.0*norp_rtt_estimate);  // use most recent RTT estimate
        ActivateTimer(norp_msg_timer);
        // Here we use ProtoSocket::Shutdown() (sockets get a DISCONNECT event upon shutdown completion)
        // TBD - we may need to do something with input/output notification here?
        if (socks_client_socket.IsOpen()) socks_client_socket.Close();
        if (socks_remote_socket.IsOpen()) socks_remote_socket.Close();
        socks_state = SOCKS_SHUTDOWN;
    }
    else
    {
        // No remote, so just do a hard Close()
        Close();
    }
}  // end NorpSession::Shutdown()

// This returns "true" when the conditions of shutdown completion are met
bool NorpSession::ShutdownComplete() const
{
    if (socks_remote_socket.IsOpen()) return false;
    if (socks_client_socket.IsOpen()) return false;
    if (NORM_OBJECT_INVALID != norm_tx_stream) return false;
    if (NORM_OBJECT_INVALID != norm_rx_stream) return false;
    return true;
}  // end NorpSession::ShutdownComplete()

void NorpSession::Close()
{
    if (NORM_SESSION_INVALID != norm_session)
    {
        NormDestroySession(norm_session);
        norm_session = NORM_SESSION_INVALID;
    }
    norp_tx_socket.Close();
    socks_client_socket.Close();
    socks_remote_socket.Close();
    udp_relay_socket.Close();
    client_pending = client_index = 0;
    remote_pending = remote_index = 0;
    if (SOCKS_VOID != socks_state)
    {
        // We make the state void here because controller deletes us!
        socks_state = SOCKS_VOID;
        controller.OnSessionClose(*this);    
    }
}  // end NorpSession::Close()

bool NorpSession::OnCloseTimeout(ProtoTimer& /*theTimer*/)
{
    close_timer.Deactivate();
    controller.OnSessionClose(*this);
    return false;  // because we've been deleted
}  // end NorpSession::OnCloseTimeout()


void NorpSession::OnSocksClientEvent(ProtoSocket&       theSocket, 
                                     ProtoSocket::Event theEvent)
{
    PLOG(PL_DETAIL, "NorpSession::OnSocksClientEvent(");
    switch (theEvent)
    {
        case ProtoSocket::INVALID_EVENT:
            PLOG(PL_DETAIL, "INVALID_EVENT) ...\n");
            break;
        case ProtoSocket::CONNECT:
            PLOG(PL_DETAIL, "CONNECT) ...\n");
            break;  
        case ProtoSocket::ACCEPT:
            PLOG(PL_DETAIL, "ACCEPT) ...\n");  // should never happen (was accepted on server socket) unless remote BIND
            break; 
        case ProtoSocket::SEND:
            PLOG(PL_DETAIL, "SEND) ...\n");
            switch (socks_state)
            {
                case SOCKS_VOID:
                case SOCKS_IDLE:
                    ASSERT(0);
                    break;
                case SOCKS_GET_AUTH_REQ:
                    ASSERT(0);
                    break;
                case SOCKS_PUT_AUTH_REP:
                    if (!PutClientAuthReply())
                    {
                        PLOG(PL_ERROR, "NorpSession::OnSocksClientEvent() error: PutClientAuthReply() failure!\n");
                        Close();
                        return;
                    }      
                    break; 
                case SOCKS_GET_REQUEST:
                    ASSERT(0);
                    break;
                case SOCKS_PUT_REQUEST:
                    ASSERT(0);
                    break;
                case SOCKS_CONNECTING:
                    ASSERT(0);
                    break;
                case SOCKS_GET_REPLY:
                    ASSERT(0);
                    break;
                case SOCKS_PUT_REPLY:
                    if (!PutClientReply())
                    {
                        PLOG(PL_ERROR, "NorpSession::OnSocksClientEvent() error: PutClientReply() failure!\n");
                        Shutdown();
                        return;
                    }      
                    break;
                case SOCKS_CONNECTED:
                    if (!PutClientData())
                    {
                        PLOG(PL_ERROR, "NorpSession::OnSocksClientEvent() error: PutClientData() failure!\n");
                        Shutdown();
                        return;
                    }      
                    break;
                case SOCKS_SHUTDOWN:
                    ASSERT(0);
                    break;
            }  // end switch(socks_state)
            break; 
        case ProtoSocket::RECV:
        {
            PLOG(PL_DETAIL, "RECV) ...\n");
            switch (socks_state)
            {
                case SOCKS_VOID:
                case SOCKS_IDLE:          // initial, inactive state (socks_client_socket is closed)
                    ASSERT(0);
                    break;
                case SOCKS_GET_AUTH_REQ:  // first active state, getting auth request bytes from client
                    if (!GetClientAuthRequest())
                    {
                        PLOG(PL_ERROR, "NorpSession::OnSocksClientEvent() error: GetClientAuthRequest() failure!\n");
                        Close();
                        return;
                    }       
                    break;
                case SOCKS_GET_REQUEST:   // next, accumulate SOCKS request bytes from client
                    if (!GetClientRequest())
                    {
                        PLOG(PL_ERROR, "NorpSession::OnSocksClientEvent() error: GetClientRequest() failure!\n");
                        Close();
                        return;
                    }       
                    break;
                case SOCKS_PUT_AUTH_REP:
                case SOCKS_PUT_REQUEST:  
                case SOCKS_CONNECTING:
                case SOCKS_PUT_REPLY:
                case SOCKS_GET_REPLY: 
                case SOCKS_SHUTDOWN:  
                {
                    unsigned int numBytes = SOCKS_BUFFER_SIZE;
                    // do a recv() - probably means client disconnected (prematurely)
                    if (theSocket.Recv((char*)client_buffer, numBytes))
                    {
						if ((SOCKS_SHUTDOWN != socks_state) && (0 != numBytes))
                            PLOG(PL_WARN, "NorpSession::OnSocksClientEvent() warning: unexpectedly received %u bytes from SOCKS client!\n", numBytes);
                    }
                    else
                    {
                        PLOG(PL_ERROR, "NorpSession::OnSocksClientEvent() error: unexpected receive error from SOCKS client!\n");
                    }
                    break;  
                }
                case SOCKS_CONNECTED:     // finally, reading/writing data from/to client
                    if (0 == client_pending)  // we need this because we sometimes get extra notifications with old "protolib"
                    {
                        if (!GetClientData())
                        {
                            PLOG(PL_ERROR, "NorpSession::OnSocksClientEvent() error: GetClientData() failure!\n");
                            Shutdown();
                        }
                    }
                    break;
            }  // end switch(socks_state)
            break; 
        }
        case ProtoSocket::DISCONNECT:
            PLOG(PL_DETAIL, "DISCONNECT) ...\n");
            switch (socks_state)
            {
                case SOCKS_VOID:
                case SOCKS_IDLE:
                    ASSERT(0);  // will never happen
                    break; 
                case SOCKS_GET_AUTH_REQ:
                case SOCKS_PUT_AUTH_REP:
                case SOCKS_GET_REQUEST:
                case SOCKS_PUT_REQUEST:  
                case SOCKS_CONNECTING:
                case SOCKS_PUT_REPLY:
                case SOCKS_GET_REPLY:
                    PLOG(PL_ERROR, "NorpSession::OnSocksClientEvent() error: premature SOCKS client disconnect!\n");
                    Shutdown();  // unexpected, premature client disconnect
                    break;  
                case SOCKS_CONNECTED:
                    // Client finished sending (and receiving) data
                    socks_state = SOCKS_SHUTDOWN;
                    if (0 == client_pending)
                        PutRemoteData();  // to shut down socks_remote_socket or norm_tx_stream
                case SOCKS_SHUTDOWN:
                    theSocket.Close();
                    if (ShutdownComplete())
                        Close();
                    // else more shutdown actions pending
                    break;
            }
            break;   
        
        case ProtoSocket::EXCEPTION:
            PLOG(PL_DETAIL, "EXCEPTION) ...\n");
            ASSERT(0);
            break;  
        
        case ProtoSocket::ERROR_:
            PLOG(PL_DETAIL, "ERROR_) ...\n");
            break; 
    }  // end switch (theEvent)
}  // end NorpSession::OnSocksClientEvent()

bool NorpSession::GetClientAuthRequest()
{
    // This method incrementally receives/processes a SOCKS AuthRequest
    // and updates our "socks_state" and buffer indices accordlingly
    // ("numBytes" is set to how many needed for next socket read)
    ASSERT(SOCKS_GET_AUTH_REQ == socks_state);
    for(;;)  // loops until auth request is read or no more input is ready
    {
        switch (client_index)
        {
            case 0: // need to read VERSION byte first
            {
                client_pending = 1;
                break;
            }
            case 1: // validate VERSION and read NMETHODS byte
            {   
                ProtoPktSOCKS::AuthRequest authReq(client_buffer, 1);
                if (5 !=  authReq.GetVersion())
                {
                    PLOG(PL_ERROR, "NorpSession::GetClientAuthRequest() received incompatible version %d request!\n", authReq.GetVersion());
                    return false;
                }
                client_pending = 2; 
                break;
            }
            case 2: // Check NMETHODS byte to see how much to read.
            {
                ProtoPktSOCKS::AuthRequest authReq(client_buffer, 2);
                UINT8 numBytes = authReq.GetMethodCount();
                if (0 == numBytes)
                {
                    PLOG(PL_ERROR, "NorpSession::GetClientAuthRequest() received AuthRequest with zero methods!\n");
                    return false;
                }   
                client_pending += numBytes;                             
                break;
            }
            default:
            {
                if (client_index == client_pending)
                {
                    PLOG(PL_DETAIL, "NorpSession::GetClientAuthRequest() session %u received SOCKS AuthRequest from client %s/%hu ...\n",
                            GetSessionId(), socks_client_socket.GetDestination().GetHostString(), socks_client_socket.GetDestination().GetPort());
                    // We have received the full SOCKS AuthRequest
                    // Find best authentication "method"  (currently only AUTH_NONE)
                    ProtoPktSOCKS::AuthType authType = ProtoPktSOCKS::AUTH_INVALID;
                    ProtoPktSOCKS::AuthRequest authReq(client_buffer, client_index);
                    for (UINT8 i = 0; i < authReq.GetMethodCount(); i++)
                    {
                        if (ProtoPktSOCKS::AUTH_NONE == authReq.GetMethod(i))
                        {
                            authType = ProtoPktSOCKS::AUTH_NONE;
                            break;
                        }   
                    }
                    if (ProtoPktSOCKS::AUTH_NONE == authType)
                    {
                        // Build our SOCKS AuthReply into "remote_buffer" for transmission to client
                        socks_state = SOCKS_PUT_AUTH_REP;
                        ProtoPktSOCKS::AuthReply authReply(remote_buffer, SOCKS_BUFFER_SIZE, false);
                        authReply.SetVersion(5);
                        authReply.SetMethod(ProtoPktSOCKS::AUTH_NONE);
                        remote_pending = authReply.GetLength();
                        remote_index = 0;
                        client_index = client_pending = 0;
                        // TBD - should we pause client input notification here?
                        return PutClientAuthReply();
                    }
                    else
                    {
                        PLOG(PL_ERROR, "NorpSession::GetClientAuthRequest() received AuthRequest with no compatible method!\n");
                        return false;
                    }
                }
                break;
            }
        }  // end switch(client_rx_index)
        
        // Read next chunk of AuthRequest (as long as socket has data to read)
        unsigned int numBytes = client_pending - client_index;
        if (!socks_client_socket.Recv(((char*)client_buffer)+client_index, numBytes))
        {
            PLOG(PL_ERROR, "NorpSession::OnSocksClientEvent() error: client socket recv() failure!\n");
            return false;
        }
        client_index += numBytes;
        if (client_index < client_pending) break;  // no more to read, so wait until RECV notification for more
    }  // end for(;;)
    return true;
}  // end NorpSession::GetClientAuthRequest()

bool NorpSession::PutClientAuthReply()
{
    ASSERT(SOCKS_PUT_AUTH_REP == socks_state);
    // We have a SOCKS AuthReply in our "remote_buffer" pending transmission to the SOCKS client
    unsigned int numBytes = remote_pending - remote_index;
    if (socks_client_socket.Send(((char*)remote_buffer)+remote_index, numBytes))
    {
        remote_index += numBytes;
        if (remote_index < remote_pending)
        {
            // Only partially sent, cue output notification to complete
            socks_client_socket.StartOutputNotification();
        }
        else
        {
            // AuthReply was fully sent
            socks_client_socket.StopOutputNotification();
            socks_state = SOCKS_GET_REQUEST;
            remote_pending = remote_index = 0;
            client_pending = 1;  // To get VERSION byte of impending SOCKS Request
            client_index = 0;
        }
        return true;
    }
    else
    {
        PLOG(PL_ERROR, "NorpSession::PutClientAuthReply() error: auth reply send failure!\n");
        return false;
    }
}  // end NorpSession::PutClientAuthReply()

bool NorpSession::GetClientRequest()
{
    ASSERT(SOCKS_GET_REQUEST == socks_state);
    // SOCKS5 request:   VERSION + CMD + RESV + ADDR_TYP + DST.ADDR + DST.PORT
    //          bytes:      1    +  1  +  1   +    1     +    N     +    2   
    for(;;)  // loops until client request is read or no more input is ready
    {
        switch (client_index)
        {
            case 0: // need to read VERSION byte first
            {
                client_pending = 1;
                break;
            }
            case 1:  // Validate VERSION, and get REQ, if applicable
            {   
                ProtoPktSOCKS::Request req(client_buffer, 1);
                if (5 !=  req.GetVersion())
                {
                    PLOG(PL_ERROR, "NorpSession::GetClientRequest() received incompatible version %d request!\n", req.GetVersion());
                    return false;
                }
                client_pending = 4;  // ask for bytes up through ADDR_TYP
                break;
            }
            case 2:  // Handle request based on command type
            case 3:
            {
                // no change, just CMD and/or RESV byte coming in
                ASSERT(4 == client_pending);
                break;
            }
            case 4:  // Process ADDR_TYP to see how many more to ask for
            {
                ProtoPktSOCKS::Request req(client_buffer, 4);
                switch (req.GetAddressType())
                {
                    case ProtoPktSOCKS::IPv4:
                        client_pending = 4 + (4 + 2);  // IPv4 addr + port
                        break;
                    case ProtoPktSOCKS::NAME:
                        client_pending = 4 + 1;  // ask for name length byte
                        break;
                    case ProtoPktSOCKS::IPv6:
                        client_pending = 4 + (16 + 2);  // IPv6 addr + port
                        break;
                    default:
                        PLOG(PL_ERROR, "NorpSession::GetClientRequest() error: invalid address type!\n");
                        return false;
                }            
                break;
            }
            case 5:  // Process NAME length byte
            {
                ProtoPktSOCKS::Request req(client_buffer, 5);
                if (ProtoPktSOCKS::NAME == req.GetAddressType())
                    client_pending = 5 + (req.GetAddressLength() + 2);  // DNS name + port
                break;
            }
            default:
            {
                if (client_index == client_pending)
                {
                    // Entire SOCKS Request from local client has been received, so handle it ...
                    ProtoPktSOCKS::Request request(client_buffer, client_index);
                    client_pending = client_index = 0;  // finished with client buffer for moment after handing request here
                    return OnClientRequest(request, socks_client_socket.GetDestination());
                }
                break;
            }
        }
        // Read next chunk of client Request (as long as socket has data to read)
        unsigned int numBytes = client_pending - client_index;
        if (!socks_client_socket.Recv(((char*)client_buffer)+client_index, numBytes))
        {
            PLOG(PL_ERROR, "NorpSession::OnSocksClientEvent() error: client socket recv() failure!\n");
            return false;
        }
        client_index += numBytes;
        if (client_index < client_pending) break;  // no more to read, so wait until RECV notification for more
    }  // end for(;;;)
    return true;
}  // end NorpSession::GetClientRequest()


bool NorpSession::OnClientRequest(const ProtoPktSOCKS::Request request, const ProtoAddress& srcAddr)
{
    ProtoAddress destAddr;
    if (!request.GetAddress(destAddr))
    {
        PLOG(PL_ERROR, "NorpSession::HandleSocksRequest() error: request with invalid destination address!\n");
        return false;
    }
    switch (request.GetCommand())
    {
        case ProtoPktSOCKS::Request::CONNECT:
            PLOG(PL_INFO, "norp: session %u recvd CONNECT request dst addr/port = %s/%hu\n", GetSessionId(), destAddr.GetHostString(), destAddr.GetPort());
            if (norm_enable)
            {
                // TBD - Determine "route" for "destAddr" to potentially separate remote "norp" peer
                //       (for now we assume "norp" remote is collocated on "destAddr" endpoints unless
                //        the controller has been configured for a specific remote 'correspondent')
                ProtoAddress norpAddr;
                controller.GetRemoteNorpAddress(destAddr, norpAddr); 
                // Relay SOCKS request to remote peer NORP server (peer will TCP connect to endpoint and reply via NORM)
                return PutRemoteRequest(request, norpAddr);
            }
            else  
            {
                return ConnectToRemote(destAddr);  // do direct TCP connect to remote endpoint
            }
        case ProtoPktSOCKS::Request::BIND:
            PLOG(PL_INFO, "norp: session %u recvd BIND request dst addr/port = %s/%hu\n", GetSessionId(), destAddr.GetHostString(), destAddr.GetPort());
            return BindRemote(destAddr.GetPort());
        case ProtoPktSOCKS::Request::UDP_ASSOC:
            PLOG(PL_INFO, "norp: session %u recvd UDP_ASSOC for %s/%hu\n", GetSessionId(), destAddr.GetHostString(), destAddr.GetPort());
            // Save our UDP client's address (use the control socket addr if unspecified in request)
            if (destAddr.IsUnspecified())
            {
                udp_client_addr = srcAddr;
                udp_client_addr.SetPort(destAddr.GetPort());
            }
            else
            {
                udp_client_addr = destAddr;
            }
            return OpenUdpRelay();
        default:
            PLOG(PL_ERROR, "NorpSession::OnClientRequest() error: invalid request command!\n");
            return false;
    }
}  // end NorpSession::OnClientRequest()

bool NorpSession::PutRemoteRequest(const ProtoPktSOCKS::Request& request, const ProtoAddress& remoteAddr)
{
    // The "request" here sits in our "client_buffer".  We know this buffer has room to append our
    // "session_id.identifier" to the end.  This "identifier" gives the command a unique key in
    // the context of the NorpSession "originator" NormNodeId to recognize if it is a repeated
    // request command, etc.
    if (NORM_SESSION_INVALID == norm_session)
    {
        // This creates the client-side NORM session for a connection.
        // (TBD) Create single-socket NORM session using an ephemeral port and connect() to the remote
        //       NORP server NORM port. (Use NormChangeDestination() after NormStartReceiver() to 
        //       configure this newly-create NormSession as needed)
#ifdef NEW_PORT
        // We use port zero to get session that will bound to an ephemeral port (single port for tx and rx) upon NormStartReceiver
        norm_session = NormCreateSession(controller.GetNormInstance(), remoteAddr.GetHostString(), 0, controller.GetNormNodeId());
#else 
        norm_session = NormCreateSession(controller.GetNormInstance(), remoteAddr.GetHostString(), controller.GetNormPort(), controller.GetNormNodeId());
#endif
        if (NORM_SESSION_INVALID == norm_session)
        {
            PLOG(PL_ERROR, "NorpSession::PutRemoteRequest() NormCreateSession() failure!\n");
            return false;
        }
        NormSetMessageTrace(norm_session, controller.GetNormTrace());
        NormSetUserData(norm_session, this); 
        NormSetDefaultSyncPolicy(norm_session, NORM_SYNC_STREAM);
        
        // We use a 2-step sender/receiver startup  process by first opening an idle, "tx-only" NormSession to
        // send the Request command to the remote "controller" NormSession (The remote will get our "tx port" 
        // info as part of this request sent). Our controller will get the REQ_ACK and/or SOCKS_REPLY message, 
        // then inform us, and so we will get the remote "tx port" info so we can then start our own receiver  
        // and "connect" our NORM rx_socket to the remote tx_socket.  The sender here is made "idle" by disabling
        // disabling the normal NORM GRTT probing.  We don't re-activate that until we know the remote end has 
        // a receiver running (This is done upon REQ_ACK or SOCKS_REPLY reception when we also start our own NORM 
        // receiver).
        
        char ifaceName[64];
        ifaceName[63] = '\0';
        if (ProtoNet::GetInterfaceName(controller.GetProxyAddress(), ifaceName, 63))
            NormSetMulticastInterface(norm_session, ifaceName);
        else
            PLOG(PL_ERROR, "NorpSession::PutRemoteRequest() warning: unable to get interface name for address %s\n", controller.GetProxyAddress().GetHostString());
        switch (controller.GetNormCC())
        {
            case Norp::NORM_CC:  // default TCP-friendly congestion control
                // do nothing, this is NORM's default mode
                break;
            case Norp::NORM_CCE: // "wireless-ready" ECN-only congestion control
                NormSetEcnSupport(norm_session, true, true);
                break;
            case Norp::NORM_CCL: // "loss tolerant", non-ECN congestion control
                NormSetEcnSupport(norm_session, false, false, true);
                break;
            case Norp::NORM_FIXED:  // fixed-rate operation
                // if cumulative rate limit has been imposed, it can override the fixed rate
                // (i.e. to share pipe among multiple flows)
                if ((norm_rate_min >= 0) && (norm_rate_min < controller.GetNormTxRate()))
                    NormSetTxRate(norm_session, norm_rate_min);                    
                else
                    NormSetTxRate(norm_session, controller.GetNormTxRate());
                break;
        }
        if ((norm_rate_min >= 0.0) || (norm_rate_max >= 0.0))
            NormSetTxRateBounds(norm_session, norm_rate_min, norm_rate_max);
        
        // Even though we call "NormStartSender()" to get a "tx port" value, we disable GRTT probing and thus
        // defer "real" sender startup until we get an ACK to our request (see "OriginatorStartNorm()")
        NormSetTxOnly(norm_session, true);
        NormSetGrttProbingMode(norm_session, NORM_PROBE_NONE);  // no probing until remote receiver is started
        if (!NormStartSender(norm_session, GetSessionId(), NORM_BUFFER_SIZE, controller.GetNormSegmentSize(), 
                             controller.GetNormBlockSize(), controller.GetNormParityCount()))
        {
            PLOG(PL_ERROR, "NorpSession::PutRemoteRequest() error: NormStartSender() failure!\n");
            return false;
        }
#ifdef NEW_PORT
        // Now call NormChangeDestination() to set proper destination port and "connect" to it
        NormChangeDestination(norm_session, remoteAddr.GetHostString(), controller.GetNormPort(), true);
#endif // NEW_PORT
        //NormSetTxSocketBuffer(norm_session, 4096);
        NormSetFlowControl(norm_session, 0.0);  // disable timer-based flow control since we are ACK-limiting writes to stream
        norm_segment_size = controller.GetNormSegmentSize();
        norm_stream_buffer_max = ComputeNormStreamBufferSegmentCount(NORM_BUFFER_SIZE, norm_segment_size, controller.GetNormBlockSize());
        norm_stream_buffer_max -= controller.GetNormBlockSize();  // a little safety margin
        norm_stream_buffer_count = 0;
        norm_stream_bytes_remain = 0;
        norm_watermark_pending = false;
        
        PLOG(PL_INFO, "norp: originator %lu created NORM session with dest %s/%hu (srcPort %hu)\n", controller.GetNormNodeId(), 
                remoteAddr.GetHostString(), controller.GetNormPort(), NormGetTxPort(norm_session));
        
        // We open a UDP socket for NORP signaling here and "connect() it so we can get ICMP error messages
        // (I.e., an ICMP "port unreachable" indicates the remote destination is not NORP-enabled)
        if (!norp_tx_socket.Open())
        {
            PLOG(PL_ERROR, "NorpSession::PutRemoteRequest() error: unable to open norp_tx_socket!\n");
            return false;
        }
        norp_remote_addr = remoteAddr;
        norp_remote_addr.SetPort(controller.GetNorpPort());
        if (!norp_tx_socket.Connect(norp_remote_addr))
        {
            PLOG(PL_ERROR, "NorpSession::PutRemoteRequest() error: unable to open and connect norp_tx_socket!\n");
            return false;
        }
    }
    socks_client_socket.StopInputNotification();  // don't accept more from client until connected
    PLOG(PL_INFO, "norp: node %u relaying SOCKS request to NORP destination %s/%hu\n", controller.GetNormNodeId(), 
          remoteAddr.GetHostString(), controller.GetNorpPort());
    // Put the SOCKS request into a NorpMsg and relay it to remote NORP server
    // (Note that the request is "robustly" transmitted via periodic repetition until a request acknowledgment (REQ_ACK) is received  
    ASSERT(!norp_msg_timer.IsActive());
    norp_msg.SetType(NorpMsg::SOCKS_REQ);
    norp_msg.SetSessionId(GetSessionId());
    norp_msg.SetSourcePort(NormGetTxPort(norm_session));      // Our NORM tx port number (so remote can "connect()"
    norp_msg.SetDestinationPort(NormGetRxPort(norm_session)); // Our NORM rx port (for info)
    norp_msg.SetNodeId(controller.GetNormNodeId());           // our NORM node id
    norp_msg.SetContent((const char*)request.GetBuffer(), request.GetLength());
    
    struct timeval currentTime;
    ProtoSystemTime(currentTime);
    norp_msg.SetTimestamp(currentTime);
    if (!SendMessage(norp_msg))
        PLOG(PL_ERROR, "NorpSession::PutRemoteRequest() warning: NORP message transmission failed (will try again)!\n");
    norp_msg_timer.SetInterval(2.0 * norp_rtt_estimate);
    ActivateTimer(norp_msg_timer);
    socks_state = SOCKS_PUT_REQUEST;
    return true;
}  // end NorpSession::PutRemoteRequest()

bool NorpSession::MakeDirectConnect()
{
    TRACE("MAKING DIRECT CONNECTION ...");
    // This assumes our "norp_msg" previously sent in attempt to connect
    // to a remote "norp" peer is cached (per PutRemoveRequest()
    if (NORM_SESSION_INVALID != norm_session)
    {
        NormDestroySession(norm_session);
        norm_session = NORM_SESSION_INVALID;
    }
    if (norp_msg_timer.IsActive()) norp_msg_timer.Deactivate();
    if (NorpMsg::SOCKS_REQ != norp_msg.GetType())
    {
        PLOG(PL_ERROR, "NorpSession::MakeDirectConnect() error: invalid norp_msg enqueued!\n");
        return false;
    }
    
    ProtoPktSOCKS::Request req(norp_msg.AccessContentPtr(), norp_msg.GetContentLength(), true);
    ProtoAddress destAddr;
    if (!req.GetAddress(destAddr))
    {
        PLOG(PL_ERROR, "NorpSession::MakeDirectConnect() error: norp_msg SOCKS_REQ with invalid destination address enqueued!\n");
        return false;
    }
    return ConnectToRemote(destAddr);
}  // end NorpSession::MakeDirectConnect()

bool NorpSession::OnRemoteRequestAcknowledgment(const NorpMsg& theMsg, const ProtoAddress& senderAddr)
{
    // Note we update our RTT estimate on ACKs received
    ProtoTime recvTime;
    recvTime.GetCurrentTime();
    ProtoTime sentTime;
    theMsg.GetTimestamp(sentTime);
    if (SOCKS_PUT_REQUEST == socks_state)
    {
        norp_rtt_estimate = ProtoTime::Delta(recvTime, sentTime);  // initial rtt received
        if (!OriginatorStartNorm(senderAddr, theMsg.GetNodeId(), theMsg.GetSourcePort(), theMsg.GetDestinationPort()))
        {
            PLOG(PL_ERROR, "NorpSession::OnRemoteRequestAcknowledgment() error: OriginatorStartNormSender() failure!\n");
            return false;
        }
        // Note that successful OriginatorStartNorm() sets socks_state to SOCKS_CONNECTING
    }
    else
    {
        // Just use duplicative ACKs to update RTT estimate via EWMA
        ASSERT(norp_rtt_estimate > 0.0);
        double rttNew = ProtoTime::Delta(recvTime, sentTime);
        norp_rtt_estimate = 0.5*(norp_rtt_estimate + rttNew);  // TBD - is this a reasonable "smoothing" factor
    }
    // Note it's OK if a bona fide measured RTT is less than NORP_RTT_MIN
    if (norp_rtt_estimate <= 0.0) 
    {
        norp_rtt_estimate = NORP_RTT_MIN;
    }
    else if (norp_rtt_estimate > NORP_RTT_MAX)
    {
        PLOG(PL_WARN, "NorpSession::OnRemoteRequestAcknowledgment() warning: measured RTT exceeds max value of %lf sec.\n", NORP_RTT_MAX);
        norp_rtt_estimate = NORP_RTT_MAX;
    }
    return true;
}  // end NorpSession::OnRemoteRequestAcknowledgment()

bool NorpSession::OnRemoteReply(const NorpMsg& theMsg, const ProtoAddress& senderAddr)
{
    ProtoPktSOCKS::Reply reply(theMsg.AccessContentPtr(), theMsg.GetContentLength());
    PLOG(PL_INFO, "NorpSession::OnRemoteReply() session %hu reply type %d\n", GetSessionId(), reply.GetType());
    if (ProtoPktSOCKS::Reply::SUCCESS == reply.GetType())
    {
        if (SOCKS_PUT_REQUEST == socks_state)
        {
            // Somehow we missed the NORP REQUEST_ACK and the reply is being received first
            // (TBD - should we init RTT here like above???)
            if (!OriginatorStartNorm(senderAddr, theMsg.GetNodeId(), theMsg.GetSourcePort(), theMsg.GetDestinationPort()))
            {
                PLOG(PL_ERROR, "NorpSession::OnRemoteReply() error: OriginatorStartNormSender() failure!\n");
                return false;
            }
            // Note that successful OriginatorStartNorm() sets socks_state to SOCKS_CONNECTING
        }
        if (is_preset)
        {
            // Go straight to connected state and start
            // accepting recv data from client or remote
            client_index = client_pending = 0;
            socks_client_socket.StartInputNotification(); 
            remote_index = remote_pending = 0;
            socks_state = SOCKS_CONNECTED;
        }
        else if (SOCKS_CONNECTING == socks_state)
        {
            // TBD - we may want to put the reply in the client_buffer instead???
            // (i.e. in case we start receiving remote data before reply is fully sent?)
            socks_state = SOCKS_PUT_REPLY;
            memcpy(remote_buffer, (char*)reply.GetBuffer(), reply.GetLength());
            remote_pending = reply.GetLength();
            remote_index = 0;
            PLOG(PL_INFO, "NorpSession::OnRemoteReply() originator relaying reply to SOCKS client ...\n");
            if (!PutClientReply())
            {
                PLOG(PL_ERROR, "NorpSession::OnRemoteReply() error: unable to relay SOCKS reply to client!\n");
                return false;
            }
        }
        // Send an ACK to the received reply 
        UINT32 buffer[32/4];
        NorpMsg ack(buffer, 32, false);
        ack.SetType(NorpMsg::REP_ACK);
        ack.SetSessionId(GetSessionId());                    // our session id (note it was set by request originator)
        ack.SetSourcePort(NormGetTxPort(norm_session));      // our NORM tx port number (so remote can "connect()"
        ack.SetDestinationPort(NormGetRxPort(norm_session)); // our NORM rx port (for info)
        struct timeval sentTime;
        theMsg.GetTimestamp(sentTime);
        ack.SetTimestamp(sentTime);                         // echo the sender's timestamp for their RTT measurement
        ack.SetNodeId(controller.GetNormNodeId());          // our NORM node id
        if (!controller.SendMessage(ack, senderAddr))
            PLOG(PL_ERROR, "NorpSession::OnRemoteReply() warning: unsable to transmit NORP REP_ACK message!\n");
    }
    else
    {
        PLOG(PL_ERROR, "NorpSession::OnRemoteReply() received negative reply ...\n");
        return false;
    }
    return true;
}  // end NorpSession::OnRemoteReply()

// Note the "socks_state" MUST be SOCKS_PUT_REQUEST when this is invoked
// (starts associated NORM session upon receipt of request ack or reply)
bool NorpSession::OriginatorStartNorm(const ProtoAddress& senderAddr, 
                                      NormNodeId          corrNormId, 
                                      UINT16              normSrcPort, 
                                      UINT16              normDstPort)
{
    ASSERT(SOCKS_PUT_REQUEST == socks_state);
    if (norp_msg_timer.IsActive()) norp_msg_timer.Deactivate();  // cancels ongoing request retransmission, if applicable
    // TBD - log the norp_msg_timer repeat count?
    PLOG(PL_INFO, "norp: originator %u starting NORM sender probing (and receiver), connected to remote NORM source port %hu ...\n", 
                    controller.GetNormNodeId(), normSrcPort);
    NormSetGrttEstimate(norm_session, norp_rtt_estimate);  // init NORM with value learned from NORP setup handshake
    NormSetRxPortReuse(norm_session, true, NULL, senderAddr.GetHostString(), normSrcPort);
    if (!NormStartReceiver(norm_session, NORM_BUFFER_SIZE))
    {
        PLOG(PL_ERROR, "NorpSession::OriginatorStartNorm() error: NormStartReceiver() failure!\n");
        return false;
    }       
    // The "NormChangeDestination()" call here makes sure our NORM session sends its packets
    // to the destination that the remote correspondent server wants us to use for NORM
    NormChangeDestination(norm_session, senderAddr.GetHostString(), normDstPort);
    if (Norp::NORM_FIXED != controller.GetNormCC())
        NormSetCongestionControl(norm_session, true);  // Note this also re-enables GRTT probing that was disabled at session creation
    else
        NormSetGrttProbingMode(norm_session, NORM_PROBE_ACTIVE);  // re-enables previously suspended GRTT probing
    if (NORM_OBJECT_INVALID == (norm_tx_stream = NormStreamOpen(norm_session, NORM_BUFFER_SIZE)))
    {
        PLOG(PL_ERROR, "NorpSession::OriginatorStartNorm() NormStreamOpen() failure!\n");
        return false;
    }
    if (!NormAddAckingNode(norm_session, corrNormId))
    {
        PLOG(PL_ERROR, "NorpSession::OriginatorStartNorm() NormAddAckingNode() failure!\n");
        return false;
    }
    socks_state = SOCKS_CONNECTING;
    return true;
}  // end NorpSession::OriginatorStartNorm()


bool NorpSession::OnRemoteRequest(const NorpMsg& theMsg, const ProtoAddress& senderAddr)
{
    ProtoPktSOCKS::Request request(theMsg.AccessContentPtr(), theMsg.GetContentLength(), true);
    PLOG(PL_INFO, "NorpSession::OnRemoteRequest() session %hu request type %d (source port %hu)\n", 
                  GetSessionId(), request.GetCommand(), theMsg.GetSourcePort());
    if (NORM_SESSION_INVALID == norm_session)
    {
        // 0) Store the sender address to transmit REPLY to when connected to remote
        norp_remote_addr = senderAddr;
        // 1) Create a NormSession with request sender as session (destination) address
        UINT16 normSessionPort = controller.GetNormPort();
        norm_session = NormCreateSession(controller.GetNormInstance(), senderAddr.GetHostString(), normSessionPort, controller.GetNormNodeId());
#ifdef NEW_PORT
        NormSetTxPort(norm_session, normSessionPort); // single port
#endif // NEW_PORT
        if (NORM_SESSION_INVALID == norm_session)
        {
            PLOG(PL_ERROR, "NorpSession::OnRemoteRequest() NormCreateSession() failure!\n");
            return false;
        }
        NormSetMessageTrace(norm_session, controller.GetNormTrace());
        // Set the NormSession user data so NORM API events can be directed to this session
        NormSetUserData(norm_session, this);  
        NormSetDefaultSyncPolicy(norm_session, NORM_SYNC_STREAM);
        
        // Start our NORM session as a receiver (we start the sender later when we get a REPLY_ACK, or NORM_DATA)
        // (First, enable port reuse and "connect()" to the sender's NORM source addr/port)
        char ifaceName[64];
        ifaceName[63] = '\0';
        if (ProtoNet::GetInterfaceName(controller.GetProxyAddress(), ifaceName, 63))
            NormSetMulticastInterface(norm_session, ifaceName);
        else
            PLOG(PL_ERROR, "NorpSession::OnRemoteRequest() warning: unable to get interface name for address %s\n", controller.GetProxyAddress().GetHostString());
        switch (controller.GetNormCC())
        {
            case Norp::NORM_CC:  // default TCP-friendly congestion control
                // do nothing, this is NORM's default mode
                break;
            case Norp::NORM_CCE: // "wireless-ready" ECN-only congestion control
                NormSetEcnSupport(norm_session, true, true);
                break;
            case Norp::NORM_CCL: // "loss tolerant", non-ECN congestion control
                NormSetEcnSupport(norm_session, false, false, true);
                break;
            case Norp::NORM_FIXED:  // fixed-rate transmission (no congestion control)
                // if cumulative rate limit has been imposed, it can override the fixed rate
                // (i.e. to share pipe among multiple flows)
                if ((norm_rate_min >= 0) && (norm_rate_min < controller.GetNormTxRate()))
                    NormSetTxRate(norm_session, norm_rate_min);                    
                else
                    NormSetTxRate(norm_session, controller.GetNormTxRate());
                break;
        }
        if ((norm_rate_min >= 0.0) || (norm_rate_max >= 0.0))
            NormSetTxRateBounds(norm_session, norm_rate_min, norm_rate_max);
        // The "NormSetRxPortReuse()" call here "connects" us to the remote NORM sender source addr/port
        // (this creates a proper binding since we reuse the same port number for multiple NORM sessions)
#ifdef NEW_PORT
        NormSetRxPortReuse(norm_session, true);  // defer connecting to remote sender until to REMOTE_SENDER_NEW to get correct port
#else
        NormSetRxPortReuse(norm_session, true, NULL, senderAddr.GetHostString(), theMsg.GetSourcePort());
#endif // if/else NEW_PORT
        if (!NormStartReceiver(norm_session, NORM_BUFFER_SIZE))
        {
            PLOG(PL_ERROR, "NorpSession::OnRemoteRequest() error: NormStartReceiver() failure!\n");
            return false;
        }
        // This call makes sure we send our NORM packets to the originator's NORM port (i.e, if different than ours)
        NormChangeDestination(norm_session, senderAddr.GetHostString(), theMsg.GetDestinationPort()); 
        if (!NormAddAckingNode(norm_session, theMsg.GetNodeId()))
        {
            PLOG(PL_ERROR, "NorpSession::OnRemoteRequest() NormAddAckingNode() failure!\n");
            return false;
        }
        PLOG(PL_INFO, "norp: correspondent %lu created NORM session with dest %s/%hu (srcPort %hu) connected to NORM source port %hu\n", 
              controller.GetNormNodeId(), senderAddr.GetHostString(), controller.GetNormPort(), NormGetTxPort(norm_session), theMsg.GetSourcePort());
        
        // 2) Initiate TCP connection to indicated endpoint address/port (should we do this first before setting up NORM?)
        ProtoAddress destAddr;
        if (!request.GetAddress(destAddr))
        {
            PLOG(PL_ERROR, "NorpSession::OnRemoteRequest() error: request with invalid address!\n");
            return false;
        }
        switch (request.GetCommand())
        {
            case ProtoPktSOCKS::Request::CONNECT:
                PLOG(PL_INFO, "norp: session %u recvd remote CONNECT request dst add/port = %s/%hu\n", GetSessionId(), destAddr.GetHostString(), destAddr.GetPort());
                if (!ConnectToRemote(destAddr))
                {
                    PLOG(PL_ERROR, "NorpSession::OnRemoteRequest() error: ConnectToRemote() failure\n");
                    // TBD - send failure reply
                    return false;
                }
                break;
            case ProtoPktSOCKS::Request::BIND:
                PLOG(PL_ERROR, "NorpSession::OnRemoteRequest() error: remote SOCKS BIND not supported\n");
                // TBD - send negative reply 
                return false;
            case ProtoPktSOCKS::Request::UDP_ASSOC:
                PLOG(PL_ERROR, "NorpSession::OnRemoteRequest() error: remote SOCKS UDP_ASSOC not supported\n");
                // TBD - send negative reply 
                return false;
            default:
                PLOG(PL_ERROR, "NorpSession::OnRemoteRequest() error: invalid request command!\n");
                return false;
        }
    }  // end if (NORM_SESSION_INVALID == norm_session)
    else
    {
        // TBD - verify this request matches our existing session info
    }
    // Note the "ConnectToRemote()" call above transitioned us to SOCKS_CONNECTING state
    if (SOCKS_CONNECTING == socks_state)
    {
        ASSERT(!norp_msg_timer.IsActive());
        // 3) Send a NormCmd request acknowledged (REQ_ACK) to "squelch" further request resends from remote
        //    (This is sent as a one-shot in response to NorpMsg::SOCKS_REQ messages)
        UINT32 buffer[32/4];
        NorpMsg ack(buffer, 32, false);
        ack.SetType(NorpMsg::REQ_ACK);
        ack.SetSessionId(GetSessionId());
        ack.SetNodeId(controller.GetNormNodeId());           // our NORM node id
        ack.SetSourcePort(NormGetTxPort(norm_session));      // Our NORM tx port number (so remote can "connect()"
        ack.SetDestinationPort(NormGetRxPort(norm_session)); // Our NORM rx port (let's remote where to send NORM pkts)
        struct timeval sentTime;
        theMsg.GetTimestamp(sentTime);
        ack.SetTimestamp(sentTime);
        if (!controller.SendMessage(ack, senderAddr))
            PLOG(PL_ERROR, "NorpSession::OnRemoteRequest() warning: NORP REQ_ACK message transmission failed (will try again)!\n");
    }
    else if (SOCKS_PUT_REPLY == socks_state)
    {
        // The reply already sent upon connect was evidently not
        // received (nor the REQ_ACK message(s) that were sent)
        // Note calling this also resets the reply robust retransmission process
        if (!PutRemoteReply())
        {
            PLOG(PL_ERROR, "NorpSession::OnRemoteRequest() error: unable to send SOCKS_REPLY\n");
            return false;
        }
    }
    else if (SOCKS_CONNECTED == socks_state)
    {
        // Evidently a late arriving request, so we just ignore it
        PLOG(PL_INFO, "NorpSession::OnRemoteRequest() late arriving remote SOCK_REQUEST\n");
    }
    return true;
}  // end NorpSession::OnRemoteRequest()

bool NorpSession::PutRemoteReply()
{
    // Send SOCKS reply to remote NORP server via NORM command
    ASSERT(IsRemoteSession());
    norp_msg.Init();
    norp_msg.SetType(NorpMsg::SOCKS_REP);
    norp_msg.SetSessionId(GetSessionId());                    // our session id (note was selected by request originator)
    norp_msg.SetSourcePort(NormGetTxPort(norm_session));      // our NORM tx port number (so remote can "connect()"
    norp_msg.SetDestinationPort(NormGetRxPort(norm_session)); // our NORM rx port (for info)
    norp_msg.SetNodeId(controller.GetNormNodeId());           // our NORM node id
    ProtoPktSOCKS::Reply reply(norp_msg.AccessContentPtr(), norp_msg.GetBufferLength() - norp_msg.GetLength(), false);
    reply.SetVersion(5);
    reply.SetType(ProtoPktSOCKS::Reply::SUCCESS);
    reply.SetAddress(socks_remote_socket.GetSourceAddr());
    norp_msg.SetContentLength(reply.GetLength());
    
    // If an existing ack or reply cmd is pending, we cancel it. This
    // resets the robust transmission of the reply
    if (norp_msg_timer.IsActive()) norp_msg_timer.Deactivate();
    struct timeval currentTime;
    ProtoSystemTime(currentTime);
    norp_msg.SetTimestamp(currentTime);
    if (!controller.SendMessage(norp_msg, norp_remote_addr))
         PLOG(PL_ERROR, "NorpSession::OnRemoteRequest() warning: NORP REQ_ACK message transmission failed (will try again)!\n");
    norp_msg_timer.SetInterval(2.0 * norp_rtt_estimate);
    ActivateTimer(norp_msg_timer);
    socks_state = SOCKS_PUT_REPLY;
    return true;
}  // end NorpSession::PutRemoteReply()

bool NorpSession::OnRemoteReplyAcknowledgment(const NorpMsg& theMsg)
{
    // Note we update our RTT estimate in either case
    ProtoTime recvTime;
    recvTime.GetCurrentTime();
    ProtoTime sentTime;
    theMsg.GetTimestamp(sentTime);
    
    if (SOCKS_PUT_REPLY == socks_state)
    {
        // Initial rtt measurement
        norp_rtt_estimate = ProtoTime::Delta(recvTime, sentTime);
        PLOG(PL_INFO, "norp node %u starting sender probing (and sender) ...\n", controller.GetNormNodeId());
        if (Norp::NORM_FIXED != controller.GetNormCC())
            NormSetCongestionControl(norm_session, true);
        if (!NormStartSender(norm_session, GetSessionId(), NORM_BUFFER_SIZE, controller.GetNormSegmentSize(), 
                             controller.GetNormBlockSize(), controller.GetNormParityCount()))
        {
            PLOG(PL_ERROR, "NorpSession::OnRemoteReplyAcknowledgment() NormStartSender() failure!\n");
            return false;
        }
        //NormSetTxSocketBuffer(norm_session, 4096);
        NormSetFlowControl(norm_session, 0.0);  // disable timer-based flow control since we are ACK-limiting writes to stream
        norm_segment_size = controller.GetNormSegmentSize();
        norm_stream_buffer_max = ComputeNormStreamBufferSegmentCount(NORM_BUFFER_SIZE, norm_segment_size, controller.GetNormBlockSize());
        norm_stream_buffer_max -= controller.GetNormBlockSize();  // a little safety margin
        norm_stream_buffer_count = 0;
        norm_stream_bytes_remain = 0;
        norm_watermark_pending = false;
        if (NORM_OBJECT_INVALID == (norm_tx_stream = NormStreamOpen(norm_session, NORM_BUFFER_SIZE)))
        {
            PLOG(PL_ERROR, "NorpSession::OnRemoteReplyAcknowledgment() NormStreamOpen() failure!\n");
            return false;
        }
        if (norp_msg_timer.IsActive()) norp_msg_timer.Deactivate();  // This cancels the robust (repeated) remote reply transmission
        socks_remote_socket.StartInputNotification();                // enable flow of data from remote TCP socket
        socks_state = SOCKS_CONNECTED;
        remote_index = remote_pending = 0;
    }
    else
    {
        ASSERT(norp_rtt_estimate > 0.0);             
        // Only use duplicative ACKs to update RTT estimate via EWMA
        double rttNew = ProtoTime::Delta(recvTime, sentTime);
        norp_rtt_estimate = 0.5*(norp_rtt_estimate + rttNew);  // TBD - is this a reasonable "smoothing" factor
    }
    // Note it's OK if a bona fide measured RTT is less than NORP_RTT_MIN
    if (norp_rtt_estimate <= 0.0) 
    {
        norp_rtt_estimate = NORP_RTT_MIN;
    }
    else if (norp_rtt_estimate > NORP_RTT_MAX)
    {
        PLOG(PL_WARN, "NorpSession::OnRemoteReplyAcknowledgment() warning: measured RTT exceeds max value of %lf sec.\n", NORP_RTT_MAX);
        norp_rtt_estimate = NORP_RTT_MAX;
    }
    return true;
}  // end NorpSession::OnRemoteReplyAcknowledgment()

bool NorpSession::ConnectToRemote(const ProtoAddress& destAddr)
{
    // Make a direct TCP connection to remote destAddr
    ASSERT(socks_remote_socket.IsClosed());
    if (!socks_remote_socket.Connect(destAddr))
    {
        PLOG(PL_ERROR, "NorpSession::ConnectToRemote() error: remote socket connect() failure!\n");
        return false;
    }
    ASSERT(!socks_remote_socket.IsConnected());  // TBD - handle immediate connection case???
    PLOG(PL_DETAIL, "NorpSession::ConnectToRemote() connection to %s/%hu initiated (connected:%d)...\n",
		destAddr.GetHostString(), destAddr.GetPort(), socks_remote_socket.IsConnected());
    if (socks_client_socket.IsOpen())
        socks_client_socket.StopInputNotification();  // don't accept more from client until connected
    socks_state = SOCKS_CONNECTING;
    return true;
}  // end NorpSession::ConnectToRemote()

bool NorpSession::BindRemote(UINT16 bindPort)
{
    // Open our remote socket in unbound state
    if (!socks_remote_socket.Open(0, proxy_addr.GetType(), false))
    {
        PLOG(PL_ERROR, "NorpSession::BindRemote() error: unable to open socket for remote connection\n");
        return false;
    }
    // Typically, the BIND request should be for port zero (any available port)
    // But, in the case that it is not, we set port reuse for when the norp server
    // is set up as a "local, loopback" server
    if (0 != bindPort) socks_remote_socket.SetReuse(true);
    bool result = socks_remote_socket.Bind(bindPort, &proxy_addr);
    if (!result) result = socks_remote_socket.Bind(0, &proxy_addr);
    if (!result)
    {
        PLOG(PL_ERROR, "NorpSession::BindRemote() error: unable to bind for remote connection\n");
        return false;
    }
    if (!socks_remote_socket.Listen())
    {
        PLOG(PL_ERROR, "NorpSession::BindRemote() error: unable to listen for remote connection\n");
        return false;
    }
    // Construct the first reply to inform client of bind addr   
    ProtoPktSOCKS::Reply reply(remote_buffer, SOCKS_BUFFER_SIZE, false);
    reply.SetVersion(5);
    reply.SetType(ProtoPktSOCKS::Reply::SUCCESS);
    ProtoAddress bindAddr = proxy_addr;
    bindAddr.SetPort(socks_remote_socket.GetPort());
    PLOG(PL_INFO,"norp proxy listening on addr/port = %s/%hu\n", bindAddr.GetHostString(), bindAddr.GetPort());
    reply.SetAddress(bindAddr);
    remote_pending = reply.GetLength();
    remote_index = 0;
    socks_state = SOCKS_PUT_REPLY;
    return PutClientReply();
}  // NorpSession::BindRemote()

bool NorpSession::OpenUdpRelay()
{
    // 1) open udp_relay_socket 
    if (!udp_relay_socket.Open())
    {
        PLOG(PL_ERROR, "NorpSession::OpenUdpRelay() error: udp_relay_socket open failed!\n");
        return false;
    }
    
    // 2) Build the SOCKS reply into the remote_buffer ...
    ProtoPktSOCKS::Reply reply(remote_buffer, SOCKS_BUFFER_SIZE, false);
    reply.SetVersion(5);
    reply.SetType(ProtoPktSOCKS::Reply::SUCCESS);
    // We use the bind (local source) addr of our client socket as the SOCKS reply BIND.ADDR
    // and the port number of our udp relay socket as the SOCKS reply BIND.PORT
    ProtoAddress bindAddr = socks_client_socket.GetSourceAddr();
    bindAddr.SetPort(udp_relay_socket.GetPort());
    reply.SetAddress(bindAddr);
    PLOG(PL_INFO, "udp relay bind addr = %s/%hu\n", bindAddr.GetHostString(), bindAddr.GetPort());
    remote_pending = reply.GetLength();
    remote_index = 0;
    socks_state = SOCKS_PUT_REPLY;
    return PutClientReply();
    
}  // end NorpSession::OpenUdpRelay()

bool NorpSession::PutClientReply()
{
    // Transmit SOCKS reply that's in "remote_buffer" to local SOCKS client
    // (This buffering and indexing allows for possibly multiple TCP sockets 
    // writes to send the reply, even though that is not very likely given a 
    // SOCKS reply is smaller than the minimum IP MTU of 512 bytes)
    ASSERT(SOCKS_PUT_REPLY == socks_state);
    unsigned int numBytes = remote_pending - remote_index;
    if (socks_client_socket.Send(((char*)remote_buffer)+remote_index, numBytes))
    {
        remote_index += numBytes;
        if (remote_index < remote_pending)
        {
            // Still more to send, keep output notification going
            socks_client_socket.StartOutputNotification();
        }
        else
        {
            // Reply fully sent, now transition to SOCKS_CONNECTED state
            PLOG(PL_DETAIL, "NorpSession::PutClientReply() session %hu:%08x SOCKS reply sent to client ...\n", 
                             GetSessionId(), GetOriginatorId());
            remote_index = remote_pending = 0;
            socks_client_socket.StopOutputNotification();
            // Start accepting recv data from client or remote
            client_index = client_pending = 0;
            socks_client_socket.StartInputNotification(); 
            if (socks_remote_socket.IsOpen())
                socks_remote_socket.StartInputNotification();
            socks_state = SOCKS_CONNECTED;
        }
    }
    else
    {
        PLOG(PL_ERROR, "NorpSession::PutClientReply() error: remote socket send failure\n");
        return false;
    }
    return true;
}  // end NorpSession::PutClientReply()

bool NorpSession::GetClientData()
{
    if (0 != client_pending) return true;
    // Get whatever data is ready from client and start sending to remote
    unsigned int numBytes = SOCKS_BUFFER_SIZE;
    if (IsRemoteSession() && !IsRemoteOriginator())
    {
        // We're the remote correspondent, so get client data from remote originator (via NORM)
        if (norm_rx_pending)
        {
            ASSERT(0 == client_pending);
            if (NormStreamRead(norm_rx_stream, (char*)client_buffer, &numBytes))
            {
                client_pending = numBytes;
                client_index = 0;
                if (numBytes < SOCKS_BUFFER_SIZE) norm_rx_pending = false;
                PLOG(PL_DETAIL, "NorpSession::GetClientData() correspondent read %lu bytes from NORM stream ..\n", numBytes);
                if (0 != numBytes)
                    return PutRemoteData();
                // else wait for NORM_RX_OBJECT_UPDATED notification
            }
            else
            {
                PLOG(PL_ERROR, "NorpSession::GetClientData() NormStreamRead() error: break in stream!\n");
                return false;
            }
        }
        // else wait for NORM_RX_OBJECT_UPDATED notification
    }
    else
    {
        // Originator server so get data from SOCKS client via TCP
        ASSERT(0 == client_pending);
        if (socks_client_socket.Recv((char*)client_buffer, numBytes))
        {
			client_pending = numBytes;
			client_index = 0;
			if (0 != numBytes)
			{
				PLOG(PL_DETAIL, "NorpSession::GetClientData() originator read %lu bytes from SOCKS client ...\n", numBytes);
				socks_client_socket.StopInputNotification();
				return PutRemoteData();
			}
			// else socket will close itself when appropriate
        }
        else
        {
            PLOG(PL_ERROR, "NorpSession::GetClientData() client socket recv failure!\n");
            return false;
        }
    }
    return true;
}  // end NorpSession::GetClientData()

bool NorpSession::PutClientData()
{
    // Transmit data that's in "remote_buffer" to client
    unsigned int numBytes = remote_pending - remote_index;
    if (IsRemoteSession() && !IsRemoteOriginator())
    {
        if (0 != numBytes)
        {
             numBytes = WriteToNormStream(((char*)remote_buffer)+remote_index, numBytes);
        }
        PLOG(PL_DETAIL, "correspondent wrote %u bytes to NORM stream ...\n", numBytes);
        remote_index += numBytes;
        if (remote_index == remote_pending)
        {
            // All pending client data has been enqueued to remote
            remote_index = remote_pending = 0;
            if (SOCKS_CONNECTED == socks_state)
            {
                FlushNormStream(false, NORM_FLUSH_ACTIVE);
                socks_remote_socket.StartInputNotification();
            }
            else
            {
                ASSERT(SOCKS_SHUTDOWN == socks_state);
                // No more data to put, so gracefully close norm_tx_stream
                NormStreamClose(norm_tx_stream, true);  // Note our "trick" here to do a graceful close, _then_ watermark
                NormSetWatermark(norm_session, norm_tx_stream, true);  // future NORM API will add "bool watermark" option to graceful close
            }
        }
        // else wait for NORM_TX_QUEUE_VACANCY / EMPTY or watermark acknowledgemnt completion
    }
    else
    {
        // Relay data to local SOCKS client via TCP
        if (0 != numBytes)
        {
            if (!socks_client_socket.Send(((char*)remote_buffer)+remote_index, numBytes))
            {
                if (SOCKS_CONNECTED == socks_state)
                {
                    PLOG(PL_ERROR, "NorpSession::PutClientData() error: remote socket send failure\n");
                    return false;
                }
                else  // SOCKS_SHUTDOWN == socks_state
                {
                    PLOG(PL_WARN, "NorpSession::PutClientData() warning: remote socket send failure\n");
                    return true;
                }
            }
        }
        PLOG(PL_DETAIL, "originator wrote %lu bytes to client TCP socket ...\n", numBytes);
        remote_index += numBytes;
        if (remote_index < remote_pending)
        {
            // Still more data to send, keep output notification going
            socks_client_socket.StartOutputNotification();
        }
        else
        {
            // Data fully sent, re-enable remote_socket input notifcation
            remote_index = remote_pending = 0;
            socks_client_socket.StopOutputNotification();
            if (SOCKS_CONNECTED == socks_state)
            {
                if (IsRemoteSession())
                {
                    // We're the remote session originator, so get more data if ready
                    if (norm_rx_pending) 
                        return GetRemoteData();
                    // else wait for NORM_RX_OBJECT_UPDATED notification
                }
                else
                {
                    // Restart accepting recv data from remote
                   socks_remote_socket.StartInputNotification();
                }
            }
            else  // SOCKS_SHUTDOWN == socks_state
            {
                if (socks_client_socket.IsConnected())
                {
                    socks_client_socket.Shutdown();
                }
                else
                {
                    socks_client_socket.Close();
                    if (ShutdownComplete()) Shutdown();
                }
            }
        }
    }
    return true;
}  // end NorpSession::PutClientData()

void NorpSession::OnSocksRemoteEvent(ProtoSocket&       theSocket, 
                                     ProtoSocket::Event theEvent)
{
    PLOG(PL_DETAIL, "NorpSession::OnSocksRemoteEvent(");
    switch (theEvent)
    {
        case ProtoSocket::INVALID_EVENT:
        {
            // should never happen, just for completeness of switch cases
            PLOG(PL_DETAIL, "INVALID_EVENT) ...\n");
            break;
        }
        case ProtoSocket::CONNECT:
        {
            PLOG(PL_DETAIL, "CONNECT) ...\n");
            ASSERT(SOCKS_CONNECTING == socks_state);
            PLOG(PL_DETAIL, "NorpSession::OnSocksRemoteEvent(CONNECT) session %hu:%08x connected to destination %s/%hu ...\n",
                             GetSessionId(), GetOriginatorId(), 
                             socks_remote_socket.GetDestination().GetHostString(), 
                             socks_remote_socket.GetDestination().GetPort());
            if (is_preset)
            {
                // Go straight to connected state and start
                // accepting recv data from client or remote
                client_index = client_pending = 0;
                socks_client_socket.StartInputNotification(); 
                remote_index = remote_pending = 0;
                socks_state = SOCKS_CONNECTED;
            }
            else
            {
                // Don't get data from remote until reply sent to client
                socks_remote_socket.StopInputNotification();
                socks_state = SOCKS_PUT_REPLY;
                if (IsRemoteSession())
                {
                    if (!PutRemoteReply())
                    {
                        PLOG(PL_ERROR, "NorpSession::OnSocksRemoteEvent(CONNECT) error: SOCKS CONNECT remote reply send failure!\n");
                        Shutdown();
                    }
                }
                else
                {
                    // Build reply in "remote_buffer" to send to client
                    ProtoPktSOCKS::Reply reply(remote_buffer, SOCKS_BUFFER_SIZE, false);
                    reply.SetVersion(5);
                    reply.SetType(ProtoPktSOCKS::Reply::SUCCESS);
                    reply.SetAddress(socks_remote_socket.GetSourceAddr());
                    remote_pending = reply.GetLength();
                    remote_index = 0;
                    // Send SOCKS reply to locally connected client
                    if (!PutClientReply())
                    {
                        PLOG(PL_ERROR, "NorpSession::OnSocksRemoteEvent(CONNECT) error: SOCKS CONNECT local reply send failure!\n");
                        Shutdown();
                    }
                }
            }
            break;  
        }
        case ProtoSocket::ACCEPT:
        {
            PLOG(PL_DETAIL, "ACCEPT) ...\n");
            // This will happen for a listening socket put in place for BIND request
            // (we need to send a second reply indicating the remote connection accepted)
            ASSERT(SOCKS_CONNECTED == socks_state);
            if (!socks_remote_socket.Accept())
            {
                PLOG(PL_ERROR, "NorpSession::OnSocksRemoteEvent(ACCEPT) error: SOCKS BIND accept failure!\n");
                Shutdown();
                break;
            }
            // TBD - should check that the accepted remote dst addr/port is what it's supposed to be
            ProtoPktSOCKS::Reply reply(remote_buffer, SOCKS_BUFFER_SIZE, false);
            reply.SetVersion(5);
            reply.SetType(ProtoPktSOCKS::Reply::SUCCESS);
            reply.SetAddress(socks_remote_socket.GetDestination());
            remote_pending = reply.GetLength();
            remote_index = 0;
            socks_state = SOCKS_PUT_REPLY;
            if (!PutClientReply())
            {
                PLOG(PL_ERROR, "NorpSession::OnSocksRemoteEvent(ACCEPT) error: SOCKS BIND reply send failure!\n");
                Shutdown();
            }
            break; 
        }
        case ProtoSocket::SEND:
            PLOG(PL_DETAIL, "SEND) ...\n");
            // write pending data from client_buffer to remote
			if (SOCKS_PUT_REPLY == socks_state)
			{
				if (!PutRemoteReply())
				{
					PLOG(PL_ERROR, "NorpSession::OnSocksRemoteEvent() error: PutRemoteReply() failure!\n");
					Shutdown();
				}
			}
            else if (!PutRemoteData())
            {
                PLOG(PL_ERROR, "NorpSession::OnSocksRemoteEvent() error: PutRemoteData() failure!\n");
                Shutdown();
            }
            break; 
        case ProtoSocket::RECV:
        {
            PLOG(PL_DETAIL, "RECV) ...\n");
            if (0 == remote_pending)  // we need this check because of occasional extra notifications with old "protolib"
            {
                if (!GetRemoteData())
                {
                    PLOG(PL_ERROR, "NorpSession::OnSocksRemoteEvent(SEND) error: GetRemoteData() failure!\n");
                    Shutdown();
                }
            }
            break; 
        }
        case ProtoSocket::DISCONNECT:
            PLOG(PL_DETAIL, "DISCONNECT) ...\n");
            switch (socks_state)
            {
                case SOCKS_VOID:
                case SOCKS_IDLE:
                case SOCKS_GET_AUTH_REQ:
                case SOCKS_PUT_AUTH_REP:
                case SOCKS_GET_REQUEST:
                case SOCKS_PUT_REQUEST:
                    ASSERT(0);
                    break;  
                case SOCKS_CONNECTING:
                case SOCKS_PUT_REPLY:
                case SOCKS_GET_REPLY:
                    PLOG(PL_ERROR, "NorpSession::OnSocksRemoteEvent() error: premature SOCKS client disconnect!\n");
                    Shutdown();  // unexpected, premature client disconnect
                    break;  
                case SOCKS_CONNECTED:
                    socks_state = SOCKS_SHUTDOWN;
                    if (0 == remote_pending)
                        PutClientData();  // to shutdown NORM stream or socks_client_socket
                case SOCKS_SHUTDOWN:
                    // Client finished sending (and receiving) data
                    theSocket.Close();
                    if (ShutdownComplete()) 
                        Close();
                    // else further shutdown actions pending
                    break;
            }
            break;   
        
        case ProtoSocket::EXCEPTION:
            PLOG(PL_DETAIL, "EXCEPTION) ...\n");  // shouldn't happen
            break;  
        
        case ProtoSocket::ERROR_:
            PLOG(PL_DETAIL, "ERROR_) ...\n");
            Shutdown();
            break; 
    }
}  // end NorpSession::OnSocksRemoteEvent()   

bool NorpSession::GetRemoteData()
{
    // Receive data from remote socket into remote_buffer
    // Get some data from remote and start sending it to the client
    ASSERT(0 == remote_pending);
    unsigned int numBytes = SOCKS_BUFFER_SIZE;
    if (IsRemoteSession() && IsRemoteOriginator())
    {
        // We're the remote correspondent (i.e. not the remote session originator)
        if (norm_rx_pending)
        {
            ASSERT(0 == remote_pending);
            if (NormStreamRead(norm_rx_stream, (char*)remote_buffer, &numBytes))
            {
                remote_pending = numBytes;
                remote_index = 0;
                if (numBytes < SOCKS_BUFFER_SIZE) norm_rx_pending = false;
                if (0 != numBytes)
                    return PutClientData();
                // else wait for NORM_RX_OBJECT_UPDATED notification
            }
            else
            {
                PLOG(PL_ERROR, "NorpSession::GetRemoteData() NormStreamRead() error: break in stream!\n");
                return false;
            }
        }
        // else wait for NORM_RX_OBJECT_UPDATED notification
    }
    else
    {
        // Remote correspondent or direct-connect, so use TCP
        ASSERT(0 == remote_pending);
        if (socks_remote_socket.Recv((char*)remote_buffer, numBytes))
        {
			remote_pending = numBytes;
			remote_index = 0;
			if (0 != numBytes)
			{
				PLOG(PL_DETAIL, "NorpSession::GetClientData() originator read %lu bytes from remote ...\n", numBytes);
				socks_remote_socket.StopInputNotification();
				return PutClientData();
			}
            // else socket will disconnect itself and session will close
        }
        else
        {
            PLOG(PL_ERROR, "NorpSession::GetRemoteData() error: remote socket recv failure!\n");
            return false;
        }
    }
    return true;
}  // end NorpSession::GetRemoteData()

bool NorpSession::PutRemoteData()
{
    // Transmit data in "client_buffer" to remote
    unsigned int numBytes = client_pending - client_index;
    if (IsRemoteSession() && IsRemoteOriginator())
    {
        if (0 != numBytes)
            numBytes = WriteToNormStream(((char*)client_buffer)+client_index, numBytes);
        PLOG(PL_DETAIL, "originator wrote %u bytes to NORM stream ...\n", numBytes);
        client_index += numBytes;
        if (client_index == client_pending)
        {
            // All pending client data has been enqueued to remote
            client_index = client_pending = 0;
            if (SOCKS_CONNECTED == socks_state)
            {
                FlushNormStream(false, NORM_FLUSH_ACTIVE);
                socks_client_socket.StartInputNotification();  // to get more data
            }
            else
            {
                ASSERT(SOCKS_SHUTDOWN == socks_state);
                // gracefully close our tx stream
                NormStreamClose(norm_tx_stream, true);  // Note our "trick" here to do a graceful close, _then_ watermark
                NormSetWatermark(norm_session, norm_tx_stream, true);  // future NORM API will add "bool watermark" option to graceful close
            }
        }
        // else wait for NORM_TX_QUEUE_VACANCY / EMPTY or watermark acknowledgment completion
    }
    else
    {
        // Use TCP to send data directly to remote endpoint 
        if (0 != numBytes)
        {
            if (!socks_remote_socket.Send(((char*)client_buffer)+client_index, numBytes))
            {
                PLOG(PL_ERROR, "NorpSession::PutRemoteData() error: remote TCP socket send failure\n");
                return false;
            }
        }
        PLOG(PL_DETAIL, "%s wrote %lu bytes to remote TCP socket ...\n", IsRemoteSession() ? "correspondent" : "server", numBytes);
        client_index += numBytes;
        if (client_index < client_pending)
        {
            socks_remote_socket.StartOutputNotification();
        }
        else
        {
            // All pending client data has been sent to remote
            socks_remote_socket.StopOutputNotification();
            client_index = client_pending = 0;
            if (SOCKS_CONNECTED == socks_state)
            {
                if (IsRemoteSession())
                {
                    // We're the remote correspondent (i.e. not the remote session originator)
                    if (norm_rx_pending)
                        return GetClientData();
                    // else wait for NORM_RX_OBJECT_UPDATED notification
                }
                else
                {
                    socks_client_socket.StartInputNotification();  // prompt for more from client
                }
            }
            else
            {
				ASSERT(SOCKS_SHUTDOWN == socks_state);
                if (socks_remote_socket.IsConnected())
                {
                    socks_remote_socket.Shutdown();
                }
                else
                {
                    socks_remote_socket.Close();
                    if (ShutdownComplete()) Shutdown();
                }
            }
        }
    }
    return true;
}  // end NorpSession::PutRemoteData()


void NorpSession::OnUdpRelayEvent(ProtoSocket&       theSocket, 
                                  ProtoSocket::Event theEvent)
{
    switch (theEvent)
    {
        case ProtoSocket::RECV:
        {
            UINT32 buffer[8192/4];
            ProtoAddress srcAddr;
            char* bufPtr = ((char*)buffer) + 22; // allows room for IPv6 header to be applied
            unsigned int numBytes = (8192 - 22);
            while (udp_relay_socket.RecvFrom(bufPtr, numBytes, srcAddr))
            {
                if (0 == numBytes) break;  // no more data to read
                if (srcAddr.IsEqual(udp_client_addr))
                {
                    
                    // Packet is from client, so relay it ...
                    // (First need to parse SOCKS header to get destination, etc
                    // The cast up to UINT32* here is safe because UdpRequest uses byte boundaries
                    ProtoPktSOCKS::UdpRequest udpReq((UINT32*)bufPtr, numBytes);
                    if (0 != udpReq.GetFrag())
                    {
                        PLOG(PL_ERROR, "NorpSession::OnUdpRelayEvent() received fragmented packet from client!"
                                       " (fragmentation not yet supported.)\n");
                        numBytes = (8192 - 22);   // reset "numBytes"
                        continue;  // get next packet
                    }
                    ProtoAddress dstAddr;
                    if (!udpReq.GetAddress(dstAddr))
                    {
                        PLOG(PL_ERROR, "NorpSession::OnUdpRelayEvent() received packet with bad destination address from client!\n");
                        numBytes = 8192;
                        continue;  // get next packet
                    }
                    // TBD - for NORM relaying we need to establish a new child NorpSession to hand 
                    //       the packet stream off to for each unique UDP destination.  Since UDP is 
                    //       connectionless, we will need to have some rules of motion regarding
                    //       timeout, etc of the child session?  We will have to buffer incoming UDP
                    //       packets while the child NorpSession is being setup. We could use the new
                    //       NormSocket APIs to signal setup with a SOCKS handshake relayed in-band
                    //       at the NORM connection startup ....
                    
                    
                    // Relay the packet (TBD - add buffering, etc?)
                    unsigned int bytesSent = udpReq.GetDataLength();
                    if (!udp_relay_socket.SendTo(udpReq.GetDataPtr(), bytesSent, dstAddr) || (0 == bytesSent))
                    {
                        PLOG(PL_ERROR, "NorpSession::OnUdpRelayEvent() error: unable to send packet to %s/%hu!\n",
                                       dstAddr.GetHostString(), dstAddr.GetPort());
                        numBytes = (8192 - 22);   // reset "numBytes"
                        continue;  // get next packet
                    }   
                }
                else
                {
                    // TBD - should we make sure the dest addr was for our client?
                    // Relay inbound packet to SOCKS client
                    // Note we left room for our SOCKS UdpRequest header to be applied in front of payload data
                    unsigned int headerOffset = 0;
                    if (ProtoAddress::IPv4 == srcAddr.GetType())
                        headerOffset = 12;  // IPv4 addr is 12 bytes shorter than IPv6
                    else if (ProtoAddress::IPv6 != srcAddr.GetType())
                        ASSERT(0);  // can only receive IPv4 or IPv6 packets1
                    char* reqPtr = ((char*)buffer) + headerOffset;
                    // The cast up to UINT32* here is safe because UdpRequest uses byte boundaries
                    ProtoPktSOCKS::UdpRequest udpReq((UINT32*)reqPtr, numBytes + 22 - headerOffset, false);
                    udpReq.SetFragment(0);
                    udpReq.SetAddress(srcAddr);
                    udpReq.SetDataLength(numBytes);
                    // Send to client
                    unsigned int bytesSent = udpReq.GetLength();
                    if (!udp_relay_socket.SendTo(reqPtr, bytesSent, udp_client_addr) || (0 == bytesSent))
                    {
                        PLOG(PL_ERROR, "NorpSession::OnUdpRelayEvent() error: unable to relay inbound packet to %s/%hu!\n",
                                       udp_client_addr.GetHostString(), udp_client_addr.GetPort());
                        numBytes = (8192 - 22);
                        continue;  // get next packet
                    }   
                }
                numBytes = (8192 - 22);  // reset "numBytes"
            }
            break;
        }   
        default:
            PLOG(PL_DETAIL, "NorpSession::OnUdpRelayEvent() unhandled event type %d\n", theEvent);
            break;
    }  // end switch(theEvent)
    
}  // end NorpSession::OnUdpRelayEvent()

bool NorpSession::GetNormNodeAddress(NormNodeHandle nodeHandle, ProtoAddress& theAddr)
{
    char addrBuffer[16];
    unsigned int addrLen = 16;
    UINT16 portNum;
    if (NormNodeGetAddress(nodeHandle, addrBuffer, &addrLen, &portNum))
    {
        if (4 == addrLen)
            theAddr.SetRawHostAddress(ProtoAddress::IPv4, addrBuffer, 4);
        else 
            theAddr.SetRawHostAddress(ProtoAddress::IPv6, addrBuffer, 16);
        theAddr.SetPort(portNum);
        return true;
    }
    else
    {
        PLOG(PL_ERROR, "NorpSession::GetNormNodeAddress() error: NormNodeGetAddress() failed!\n");
        theAddr.Invalidate();
        return false;
    }
}  // end NorpSession::GetNormNodeAddress()

unsigned int NorpSession::ComputeNormStreamBufferSegmentCount(unsigned int bufferBytes, UINT16 segmentSize, UINT16 blockSize)
{
    // This same computation is performed in NormStreamObject::Open() in "normObject.cpp"
    unsigned int numBlocks = bufferBytes / (blockSize * segmentSize);
    if (numBlocks < 2) numBlocks = 2; // NORM enforces a 2-block minimum buffer size
    return (numBlocks * blockSize);
}  // end NorpSession::ComputeNormStreamBufferSegmentCount()

unsigned int NorpSession::WriteToNormStream(const char*         buffer,
                                            unsigned int        numBytes)
{
    // This method uses NormStreamWrite(), but limits writes by explicit ACK-based flow control status
    if (norm_stream_buffer_count < norm_stream_buffer_max)
    {
        // 1) How many buffer bytes are available?
        unsigned int bytesAvailable = norm_segment_size * (norm_stream_buffer_max - norm_stream_buffer_count);
        bytesAvailable -= norm_stream_bytes_remain;  // unflushed segment portiomn
        if (numBytes <= bytesAvailable) 
        {
            unsigned int totalBytes = numBytes + norm_stream_bytes_remain;
            unsigned int numSegments = totalBytes / norm_segment_size;
            norm_stream_bytes_remain = totalBytes % norm_segment_size;
            norm_stream_buffer_count += numSegments;
        }
        else
        {
            numBytes = bytesAvailable;
            norm_stream_buffer_count = norm_stream_buffer_max;        
        }
        // 2) Write to the stream
        unsigned int bytesWritten = NormStreamWrite(norm_tx_stream, buffer, numBytes);
        //ASSERT(bytesWritten == numBytes);  // this could happen if timer-based flow control is left enabled
	                                     // or if stream is closing ... ???
        // We had a "stream is closing" case here somehow?  Need to make sure we don't try
        // to write to norm stream
        
        // 3) Do we need to issue a watermark ACK request?
        if (!norm_watermark_pending && (norm_stream_buffer_count >= (norm_stream_buffer_max >> 1)))
        {
            //TRACE("NorpSession::WriteToNormStream() initiating watermark ACK request (buffer count:%lu max:%lu usage:%u)...\n",
            //            norm_stream_buffer_count, norm_stream_buffer_max, NormStreamGetBufferUsage(norm_tx_stream));
            NormSetWatermark(norm_session, norm_tx_stream);
            norm_watermark_pending = true;
        }
        return bytesWritten;
    }
    else
    {
        PLOG(PL_DETAIL, "NorpSession::WriteToNormStream() is blocked pending acknowledgment from receiver\n");
        return 0;
    }
}  // end NorpSession::WriteToNormStream()

void NorpSession::FlushNormStream(bool eom, NormFlushMode flushMode)
{
    // NormStreamFlush always will transmit pending runt segments, if applicable
    // (thus we need to manage our buffer counting accordingly if pending bytes remain)
    NormStreamFlush(norm_tx_stream, eom, flushMode);
    if (0 != norm_stream_bytes_remain)
    {
        norm_stream_buffer_count++;
        norm_stream_bytes_remain = 0;
        if (!norm_watermark_pending && (norm_stream_buffer_count >= (norm_stream_buffer_max >> 1)))
        {
            //TRACE("NorpSession::FlushNormStream() initiating watermark ACK request (buffer count:%lu max:%lu usage:%u)...\n",
            //       norm_stream_buffer_count, norm_stream_buffer_max, NormStreamGetBufferUsage(norm_tx_stream));
			NormSetWatermark(norm_session, norm_tx_stream, true); 
            norm_watermark_pending = true;
        }
    } 
}  // end NorpSession::FlushNormStream()

bool NorpSession::OnNorpMsgTimeout(ProtoTimer& theTimer)
{
    PLOG(PL_DETAIL, "NorpSession::OnNorpMsgTimeout() repeat count = %d\n", theTimer.GetRepeatCount());
    if (0 != theTimer.GetRepeatCount())
    {
        struct timeval currentTime;
        ProtoSystemTime(currentTime);
        norp_msg.SetTimestamp(currentTime);
        if (SOCKS_PUT_REQUEST == socks_state)
        {
            if (!SendMessage(norp_msg))
                PLOG(PL_ERROR, "NorpSession::OnNorpMsgTimeout() warning: unable to send message (retries remaining %d)\n", theTimer.GetRepeatCount());
        }
        else
        {
            if (!controller.SendMessage(norp_msg, norp_remote_addr))
                PLOG(PL_ERROR, "NorpSession::OnNorpMsgTimeout() warning: unable to send message (retries remaining %d)\n", theTimer.GetRepeatCount());
        }
    }
    else
    {
        PLOG(PL_ERROR, "NorpSession::OnNorpMsgTimeout() error: message retransmission timeout with no reply\n");
        switch (socks_state)
        {
            case SOCKS_PUT_REQUEST:
	    	    if (!MakeDirectConnect())
                {
                    PLOG(PL_ERROR, "NorpSession::OnNorpMsgTimeout() error: also unable to make direct TCP connection\n");
                    Shutdown();
                }
		        return false;  // MakeDirectConnect() kills timer
                break;
            case SOCKS_PUT_REPLY:
	    	    Shutdown();
                break;
            case SOCKS_SHUTDOWN:
	    	    Close();
                return false;
            default:
                 ASSERT(0);  // shouldn't be messaging in any other states
                 break;
        }     
    }
    return true;
}  // end NorpSession::OnNorpMsgTimeout()

void NorpSession::OnNormEvent(NormEvent& theEvent)
{
    PLOG(PL_DETAIL, "NorpSession::OnNormEvent(");
    switch (theEvent.type)
    {
        case NORM_TX_QUEUE_VACANCY:
            PLOG(PL_DETAIL, "NORM_TX_QUEUE_VACANCY)\n");
		case NORM_TX_QUEUE_EMPTY:
            if (NORM_TX_QUEUE_EMPTY == theEvent.type) PLOG(PL_DETAIL, "NORM_TX_QUEUE_EMPTY)\n");
            if (IsRemoteOriginator())
            {
                // If there's pending data from the SOCKS client, try to send it
                if (0 != client_pending) 
                {
                    if (!PutRemoteData())
                    {
                        PLOG(PL_ERROR, "NorpSession::OnNormEvent(TX_VACANCY) error: PutRemoteData() failure!\n");
                        Shutdown();
                    }
                }
            }
            else
            {
                if (0 != remote_pending) 
                {
                    if (!PutClientData())
                    {
                        PLOG(PL_ERROR, "NorpSession::OnNormEvent(TX_VACANCY) error: PutClientData() failure!\n");
                        Shutdown();
                    }
                }
            }
            break;
		case NORM_TX_FLUSH_COMPLETED:
            PLOG(PL_DETAIL, "NORM_TX_FLUSH_COMPLETED)\n");
            break;
		case NORM_TX_WATERMARK_COMPLETED:
            PLOG(PL_DETAIL, "NORM_TX_WATERMARK_COMPLETED)\n");
            if (NORM_ACK_SUCCESS == NormGetAckingStatus(norm_session))
            {
                PLOG(PL_DETAIL, "   acking status: SUCCESS\n");
                //TRACE("successful watermark ACK (buffer count:%lu max:%lu)\n", norm_stream_buffer_count, norm_stream_buffer_max);
                norm_watermark_pending = false;
                norm_stream_buffer_count -= (norm_stream_buffer_max >> 1);
                //TRACE("   (count reduced to %lu)\n", norm_stream_buffer_count);
                // Use as prompt to send pending data to NORM stream
                if (IsRemoteOriginator())
                {
                    // If there's pending data from the SOCKS client, try to send it
                    if (0 != client_pending) 
                    {
                        if (!PutRemoteData())
                        {
                            PLOG(PL_ERROR, "NorpSession::OnNormEvent(TX_VACANCY) error: PutRemoteData() failure!\n");
                            Shutdown();
                        }
                    }
                }
                else
                {
                    if (0 != remote_pending) 
                    {
                        if (!PutClientData())
                        {
                            PLOG(PL_ERROR, "NorpSession::OnNormEvent(TX_VACANCY) error: PutClientData() failure!\n");
                            Shutdown();
                        }
                    }
                }
                if (!persist_start_time.IsZero())
                    persist_start_time.Zeroize();  // resets persistence timeout
            }
            else if (NORM_ACK_FAILURE == NormGetAckingStatus(norm_session))
            {
                PLOG(PL_DETAIL, "   acking status: FAILURE\n");
                if (persist_interval >= 0.0)
                {
                    // Finite persistence will be observed
                    if (persist_start_time.IsZero())
                    {
                        // Initialize persistence timeout
                        persist_start_time.GetCurrentTime();
                    }
                    else
                    {
                        ProtoTime currentTime;
                        currentTime.GetCurrentTime();
                        if ((currentTime - persist_start_time) > persist_interval)
                        {
                            NormStreamClose(norm_tx_stream);
                            norm_tx_stream = NORM_OBJECT_INVALID;
                            Shutdown();
                            break;
                        }
                    }
                }
                NormResetWatermark(norm_session);
            }
            break;
		case NORM_TX_CMD_SENT:
            PLOG(PL_DETAIL, "NORM_TX_CMD_SENT)\n");
            break;
		case NORM_TX_OBJECT_SENT:
            PLOG(PL_DETAIL, "NORM_TX_OBJECT_SENT)\n");
            break;
		case NORM_TX_OBJECT_PURGED:
            PLOG(PL_DETAIL, "NORM_TX_OBJECT_PURGED)\n");
            if (theEvent.object == norm_tx_stream)
            {
                ASSERT(SOCKS_SHUTDOWN == socks_state);
                // do we need to call NormStreamClose() here?
                NormStreamClose(norm_tx_stream);
                norm_tx_stream = NORM_OBJECT_INVALID;
                if (ShutdownComplete()) Shutdown();
            }
            break;
		case NORM_TX_RATE_CHANGED:
            PLOG(PL_DETAIL, "NORM_TX_RATE_CHANGED)\n");
            break;
		case NORM_LOCAL_SENDER_CLOSED:
            PLOG(PL_DETAIL, "NORM_LOCAL_SENDER_CLOSED)\n");
            break;
		case NORM_REMOTE_SENDER_NEW:
        {
            PLOG(PL_DETAIL, "NORM_REMOTE_SENDER_NEW)\n");
            char addrBuffer[16];
            unsigned int addrLen = 16;
            UINT16 sndrPort;
            NormNodeGetAddress(theEvent.sender, addrBuffer, &addrLen, &sndrPort);  
            ProtoAddress sndrAddr;
            switch (addrLen)
            {
                case 4:
                    sndrAddr.SetRawHostAddress(ProtoAddress::IPv4, addrBuffer, 4);
                    break;
                case 16:
                    sndrAddr.SetRawHostAddress(ProtoAddress::IPv6, addrBuffer, 16);
                    break;
                default:
                    TRACE("invalid remote sender address ?!\n");
                    break;
            }
#ifdef NEW_PORT
            TRACE("new remote sender address %s/%hu (orig:%d) ...\n", sndrAddr.GetHostString(), sndrPort, IsRemoteOriginator());  
            if (!IsRemoteOriginator() && !norm_sender_heard)
            {
                NormChangeDestination(theEvent.session, sndrAddr.GetHostString(), sndrPort, true);
                norm_sender_heard = true;
                //NormSetRxPortReuse(theEvent.session, true, NULL, sndrAddr.GetHostString(), sndrPort);
            }
#endif   // NEW_PORT            
            break;
        }
		case NORM_REMOTE_SENDER_ACTIVE:
            PLOG(PL_DETAIL, "NORM_REMOTE_SENDER_ACTIVE)\n");
            break;
		case NORM_REMOTE_SENDER_INACTIVE:
            PLOG(PL_DETAIL, "NORM_REMOTE_SENDER_INACTIVE)\n");
            break;
		case NORM_REMOTE_SENDER_PURGED:
            PLOG(PL_DETAIL, "NORM_REMOTE_SENDER_PURGED)\n");
            break;
		case NORM_RX_CMD_NEW:
            PLOG(PL_DETAIL, "NORM_RX_CMD_NEW)\n");
            break;
		case NORM_RX_OBJECT_NEW:
            PLOG(PL_DETAIL, "NORM_RX_OBJECT_NEW)\n");
            // TBD - confirm it's a stream, etc?
            norm_rx_stream = theEvent.object;
            break;
		case NORM_RX_OBJECT_INFO:
            PLOG(PL_DETAIL, "NORM_RX_OBJECT_INFO)\n");
            break;
		case NORM_RX_OBJECT_UPDATED:
            PLOG(PL_DETAIL, "NORM_RX_OBJECT_UPDATED)\n");
            ASSERT(theEvent.object == norm_rx_stream);
            norm_rx_pending = true;  // signals there is NORM rx data ready for reading
            if (IsRemoteOriginator())
            {
                // we're the remote session originator (read data from NORM and put to local SOCKS client)
                if (0 == remote_pending)
                {
                    // try to read more data from NORM into "remote_buffer" and put to SOCKS client
                    if (!GetRemoteData())
                    {
                        PLOG(PL_ERROR, "NorpSession::OnNormEvent(RX_UPDATE) error: GetRemoteData() failure!\n");
                        Shutdown();  // TBD - shut down more gracefully (informing remote)
                    }
                }
                // else there's still data in remote_buffer pending relay to SOCKS client
                // TBD - should we go ahead and append more data to remote_buffer?
                // (for now, PutClientData() will call GetRemoteData() for more when relay to client completes
            }
            else  
            {
                // we're the remote correspondent (read data from NORM and put to remote)
                if ((0 == client_pending) && ((SOCKS_CONNECTED == socks_state) || (SOCKS_SHUTDOWN == socks_state)))
                {
                    // try to read more data from NORM into "client_buffer" and put to SOCKS destination
                    if (!GetClientData())
                    {
                        PLOG(PL_ERROR, "NorpSession::OnNormEvent(RX_UPDATE) error: GetRemoteData() failure!\n");
                        Shutdown();  // TBD - shut down more gracefully (informing remote)
                    }
                }
                // else there's still data in client_buffer pending relay to TCP destination
                // TBD - should we go ahead and append more data to client_buffer?
                // (for now, PutRemoteData() will call GetClientData() for more when relay to remote completes
            }
            break;
		case NORM_RX_OBJECT_COMPLETED:
            PLOG(PL_DETAIL, "NORM_RX_OBJECT_COMPLETED)\n");
            if (IsRemoteOriginator())
            {
                if (SOCKS_CONNECTED == socks_state)
                {
                    socks_state = SOCKS_SHUTDOWN;
                    // Make sure our socks_client_socket gets shut down
                    if (0 == remote_pending)
                        PutClientData(); // will shutdown our socks_client_socket
                    // else socks_client_socket output notification will call PutClientData()
                    
                    // TBD - if the remote obj is completed, then there's no use in pushing more data their way?
                    // Gracefully shut down our norm_tx_stream (requesting an ACK from the correspondent)
                    NormStreamClose(norm_tx_stream, true);  // Note our "trick" here to do a graceful close, _then_ watermark
                    NormSetWatermark(norm_session, norm_tx_stream, true);  // future NORM API will add "bool watermark" option to graceful close
                }
                else
                {
                    ASSERT(!socks_client_socket.IsOpen());
                }
            }
            else
            {
                if (SOCKS_CONNECTED == socks_state)
                {
                    socks_state = SOCKS_SHUTDOWN;
                    if (0 == client_pending)
                        PutRemoteData(); // will shutdown our socks_remote_socket
                    // else socks_remote_socket output notification will call PutClientData()
                    
                    // Gracefully shut down our norm_tx_stream (requesting an ACK from the originator)
                    NormStreamClose(norm_tx_stream, true);  // Note our "trick" here to do a graceful close, _then_ watermark
                    NormSetWatermark(norm_session, norm_tx_stream, true);  // future NORM API will add "bool watermark" option to graceful close
                }
                else
                {
                    ASSERT(!socks_remote_socket.IsOpen());
                }
            }
            norm_rx_stream = NORM_OBJECT_INVALID;
            norm_rx_pending = false;
            if (ShutdownComplete()) Close();
            break;
        case NORM_SEND_ERROR:
            PLOG(PL_ERROR, "NorpSession::OnNormEvent(NORM_SEND_ERROR) error ...\n");//: remote session unexpectedly closed!\n");
            Close();
            break;
            
		case NORM_RX_OBJECT_ABORTED:
            PLOG(PL_DETAIL, "NORM_RX_OBJECT_ABORTED)\n");
            break;
		case NORM_GRTT_UPDATED:
            PLOG(PL_DETAIL, "NORM_GRTT_UPDATED)\n");
            break;
		case NORM_CC_ACTIVE:
            PLOG(PL_DETAIL, "NORM_CC_ACTIVE)\n");
            break;
		case NORM_CC_INACTIVE:
            PLOG(PL_DETAIL, "NORM_CC_INACTIVE)\n");
            break;
		case NORM_EVENT_INVALID:
            PLOG(PL_DETAIL, "NORM_EVENT_INVALID)\n");
            break;
        default:
            PLOG(PL_DETAIL, "UNKNOWN event %d)\n", theEvent.type);
            break;
    }
}  // end NorpSession::OnNormEvent()

NorpPreset::NorpPreset(Norp& norpController, UINT16 tcpPort, const ProtoAddress& dstAddr, const ProtoAddress& norpAddr)
  : controller(norpController), tcp_port(tcpPort), dst_addr(dstAddr), norp_addr(norpAddr), server_socket(ProtoSocket::TCP)
{
    server_socket.SetNotifier(&(controller.GetSocketNotifier()));
    server_socket.SetListener(this, &NorpPreset::OnSocketEvent);        
            
}

NorpPreset::~NorpPreset()
{
    if (server_socket.IsOpen()) server_socket.Close();
}

bool NorpPreset::Listen()
{
    if (!server_socket.Listen(tcp_port))
    {
        PLOG(PL_ERROR, "NorpPreset::Listen() error: unable to establish listening TCP socket on port %hu\n", tcp_port);
        return false;
    }
    return true;
}  // end NorpPreset::Listen();

void NorpPreset::OnSocketEvent(ProtoSocket& theSocket, ProtoSocket::Event theEvent)
{
    // TBD - dispatch ACCEPT events up to controller to create a corresponding 
    //       jump-started NorpSession
    
    PLOG(PL_DETAIL, "NorpPreset::OnSocketEvent(");
    switch (theEvent)
    {
        case ProtoSocket::INVALID_EVENT:
            PLOG(PL_DETAIL, "INVALID_EVENT) ...\n");
            break;
        case ProtoSocket::CONNECT:
            PLOG(PL_DETAIL, "CONNECT) ...\n");
            break;  
        case ProtoSocket::ACCEPT:
        {
            PLOG(PL_DETAIL, "ACCEPT) ...\n");
            if (!controller.AcceptPresetClientConnection(*this))
                PLOG(PL_ERROR, "Norp::OnSocketEvent(ACCEPT) error: unable to create new NorpSession\n");
            break; 
        }
        case ProtoSocket::SEND:
            PLOG(PL_DETAIL, "SEND) ...\n");
            break; 
        case ProtoSocket::RECV:
        {
            PLOG(PL_DETAIL, "RECV) ...\n");
            break; 
        }
        case ProtoSocket::DISCONNECT:
            PLOG(PL_DETAIL, "DISCONNECT) ...\n");
            break;   
        
        case ProtoSocket::EXCEPTION:
            PLOG(PL_DETAIL, "EXCEPTION) ...\n");
            break;  
        
        case ProtoSocket::ERROR_:
            PLOG(PL_DETAIL, "ERROR_) ...\n");
            break;  
    }
    
}  // end NorpPreset::OnSocketEvent()

Norp::Norp(ProtoDispatcher& theDispatcher)
 : dispatcher(theDispatcher), socks_server_socket(ProtoSocket::TCP), socks_port(DEFAULT_SOCKS_PORT), 
   next_session_id(1), session_count(0), norp_rx_socket(ProtoSocket::UDP), norp_local_port(DEFAULT_NORP_PORT), 
   norp_remote_port(DEFAULT_NORP_PORT), norp_rtt_init(NorpSession::NORP_RTT_DEFAULT),
   norm_enable(true), norm_instance(NORM_INSTANCE_INVALID), norm_node_id(NORM_NODE_ANY), 
   norm_port(DEFAULT_NORM_PORT), norm_cc_mode(NORM_CC), norm_tx_rate(DEFAULT_TX_RATE), norm_tx_limit(-1.0),
   norm_segment_size(1400), norm_block_size(64), norm_parity_count(0), norm_parity_auto(0), norm_trace(false)
//   ,port_pool(9000)
{
    socks_server_socket.SetNotifier(&GetSocketNotifier());
    socks_server_socket.SetListener(this, &Norp::OnSocksServerEvent);
    norp_rx_socket.SetNotifier(&GetSocketNotifier());
    norp_rx_socket.SetListener(this, &Norp::OnNorpSocketEvent);
}

Norp::~Norp()
{
    StopServer();
}

bool Norp::SendMessage(const NorpMsg& msg, const ProtoAddress& dstAddr)
{
    unsigned int numBytes = msg.GetLength();
    if (norp_rx_socket.SendTo((const char*)msg.GetBuffer(), numBytes, dstAddr))
    {
        if (0 == numBytes)
        {
            PLOG(PL_WARN, "Norp::SendMessage() norp_rx_socket.SendTo() error: %s\n", GetErrorString());    
            return false;  
        }
    }
    else
    {
        PLOG(PL_ERROR, "Norp::SendMessage() norp_rx_socket.SendTo() error: %s\n", GetErrorString());
        return false;
    }
    return true;
}  // end Norp::SendMessage()

bool Norp::StartServer(bool normEnable)
{
    if (!socks_server_socket.Listen(socks_port))
    {
        PLOG(PL_ERROR, "Norp::StartServer() error: socks server socket listen(%d) error\n", socks_port);
        return false;
    }
    norm_enable = normEnable;
    if (normEnable)
    {
        norm_instance = NormCreateInstance();
        if (NORM_INSTANCE_INVALID == norm_instance)
        {
            PLOG(PL_ERROR, "Norp::StartServer() error: NormCreateInstance() failure!\n");
            StopServer();
            return false;
        }
        NormDescriptor normDescriptor = NormGetDescriptor(norm_instance);
        if (!dispatcher.InstallGenericInput(normDescriptor, NormEventCallback, this))
        {
            PLOG(PL_ERROR, "Norp::StartServer() error: unable to install NORM event notification!\n");
            StopServer();
            return false;
        }
        if (!norp_rx_socket.Open(norp_local_port))
        {
            PLOG(PL_ERROR, "Norp::StartServer() error: unable to open NORP signaling socket!\n");
            StopServer();
            return false;
        }
        if (NORM_NODE_ANY == norm_node_id)
            norm_node_id = proxy_addr.GetEndIdentifier(); // TBD - make sure proxy_addr.IsValid() ???
    }
    else
    {
        norm_node_id = NORM_NODE_NONE;
    }
    PLOG(PL_INFO, "norp: created controller on NORP port %hu with local NORM node id: %08x\n", norp_local_port, norm_node_id);
    return true;
}  // end Norp::StartServer()

void Norp::StopServer()
{
    norp_rx_socket.Close();
    socks_server_socket.Close();
    session_list.Destroy();
    preset_list.Destroy();
    if (NORM_INSTANCE_INVALID != norm_instance)
    {
        dispatcher.RemoveGenericInput(NormGetDescriptor(norm_instance));
        NormDestroyInstance(norm_instance);
        norm_instance = NORM_INSTANCE_INVALID;
    }
}  // end Norp::StopServer()

void Norp::OnSessionClose(NorpSession& theSession)
{
    RemoveSession(theSession);
    PLOG(PL_INFO, "norp: deleting closed session %u\n", theSession.GetSessionId());
    delete &theSession;
}  // end Norp::OnSessionClose();

bool Norp::AcceptPresetClientConnection(NorpPreset& preset)
{
    // Create a new NorpSession for this prest client connection
    NorpSession* norpSession = new NorpSession(*this, next_session_id++);
    if (NULL == norpSession)
    {
        PLOG(PL_ERROR, "Norp::AcceptPresetClientConnection() new NorpSession error: %s\n", GetErrorString());
        return false;
    }
    if (!norpSession->AcceptPresetClientConnection(preset, norm_enable))
    {
        PLOG(PL_ERROR, "Norp::AcceptPresetClientConnection() error: unable to accept connection\n");
        delete norpSession;
        return false;
    }
    norpSession->SetProxyAddress(proxy_addr);
    AddSession(*norpSession);
    PLOG(PL_INFO, "norp: session %hu accepted preset connection from %s\n", norpSession->GetSessionId(), norpSession->GetClientAddress().GetHostString());
    return true;
}  // end Norp::AcceptPresetClientConnection()

void Norp::OnSocksServerEvent(ProtoSocket&       theSocket, 
                              ProtoSocket::Event theEvent)
{
    PLOG(PL_DETAIL, "Norp::OnServerSocketEvent(");
    switch (theEvent)
    {
        case ProtoSocket::INVALID_EVENT:
            PLOG(PL_DETAIL, "INVALID_EVENT) ...\n");
            break;
        case ProtoSocket::CONNECT:
            PLOG(PL_DETAIL, "CONNECT) ...\n");
            break;  
        case ProtoSocket::ACCEPT:
        {
            PLOG(PL_DETAIL, "ACCEPT) ...\n");
            // Create a new NorpSession for this potential client
            NorpSession* norpSession = new NorpSession(*this, next_session_id++);
            if (NULL == norpSession)
            {
                PLOG(PL_ERROR, "Norp::OnSocksServerEvent() new NorpSession error: %s\n", GetErrorString());
                break;
            }
            if (!norpSession->AcceptClientConnection(socks_server_socket, norm_enable))
            // Accept the socket connection into this session's "socks_client_socket"
            //if (!socks_server_socket.Accept(&norpSession->AccessSocksClientSocket()))
            {
                PLOG(PL_ERROR, "Norp::OnSocksServerEvent(ACCEPT) error accepting connection\n");
                delete norpSession;
                break;
            }
            norpSession->SetProxyAddress(proxy_addr);
            AddSession(*norpSession);
            PLOG(PL_INFO, "norp: session %hu accepted connection from %s\n", norpSession->GetSessionId(), norpSession->GetClientAddress().GetHostString());
            break; 
        }
        case ProtoSocket::SEND:
            PLOG(PL_DETAIL, "SEND) ...\n");
            break; 
        case ProtoSocket::RECV:
        {
            PLOG(PL_DETAIL, "RECV) ...\n");
            break; 
        }
        case ProtoSocket::DISCONNECT:
            PLOG(PL_DETAIL, "DISCONNECT) ...\n");
            break;   
        
        case ProtoSocket::EXCEPTION:
            PLOG(PL_DETAIL, "EXCEPTION) ...\n");
            break;  
        
        case ProtoSocket::ERROR_:
            PLOG(PL_DETAIL, "ERROR_) ...\n");
            break;  
    }
}  // end Norp::OnSocksServerEvent() 

void Norp::AddSession(NorpSession& session)
{
    session_list.Insert(session);
    session_count++;
    if (norm_tx_limit >= 0.0)
    {
        double lowerLimit = 0.9 * (norm_tx_limit / (double)session_count);
        NorpSessionList::Iterator iterator(session_list);
        NorpSession* nextSession;
        while (NULL != (nextSession = iterator.GetNextItem()))
            nextSession->SetNormRateBounds(lowerLimit, norm_tx_limit);
    }
}  // end Norp::AddSession()

void Norp::RemoveSession(NorpSession& session)    
{
    session_list.Remove(session);
    session_count--;
    if ((session_count > 0) && (norm_tx_limit >= 0.0))
    {
       double lowerLimit = 0.9 * (norm_tx_limit / (double)session_count);
       NorpSessionList::Iterator iterator(session_list);
       NorpSession* nextSession;
       while (NULL != (nextSession = iterator.GetNextItem()))
           nextSession->SetNormRateBounds(lowerLimit, norm_tx_limit);
    }
}  // end Norp::RemoveSession()   


bool Norp::AddPreset(UINT16 tcpPort, const ProtoAddress& dstAddr, const ProtoAddress& norpAddr)
{
    NorpPreset* preset = new NorpPreset(*this, tcpPort, dstAddr, norpAddr);
    if (NULL == preset)
    {
        PLOG(PL_ERROR, "Norp::AddPreset() new NorpPreset error: %s\n", GetErrorString());
        return false;
    }
    if (!preset->Listen())
    {
        PLOG(PL_ERROR, "Norp::AddPreset() error: unable to create listening TCP socket\n");
        delete preset;
        return false;
    }
    preset_list.Insert(*preset);
    return true;
}  // end Norp::AddPreset()

void Norp::OnNorpSocketEvent(ProtoSocket&       theSocket, 
                             ProtoSocket::Event theEvent)
{
    if (ProtoSocket::RECV == theEvent)
    {
        UINT32 buffer[1024/4];
        ProtoAddress senderAddr;
        for(;;)
        {
            unsigned int numBytes = 1024;
            if (!theSocket.RecvFrom((char*)buffer, numBytes, senderAddr))
            {
                NorpSession* theSession = (NorpSession*)(theSocket.GetUserData());
                if ((NULL != theSession) && (NorpSession::SOCKS_PUT_REQUEST == theSession->GetSocksState()))
                {
                    // Assume it is an ICMP "port unreachable" error in response to NORP request 
                    PLOG(PL_INFO, "Norp::OnNorpSocketEvent() remote NORP server port unreachable?\n");
                    if (!theSession->MakeDirectConnect())
                    {
                        PLOG(PL_ERROR, "Norp::OnNorpSocketEvent(RECV) error: unable to make direct connection\n"); 
                        theSession->Shutdown();
                    }
                    break;
                }
            }
            if (0 == numBytes) break;  // nothing left to read
            // TBD - make sure received command is at least 20 bytes (NORP header)
            NorpMsg msg(buffer, numBytes);
            switch (msg.GetType())
            {
                case NorpMsg::SOCKS_REQ:
                {
                    PLOG(PL_INFO, "Norp::OnNorpSocketEvent(RECV) received SOCKS_REQ session %d:%hu\n", msg.GetNodeId(), msg.GetSessionId());
                    OnRemoteRequest(msg, senderAddr);
                    break;
                }
                case NorpMsg::REQ_ACK:
                {
                    PLOG(PL_INFO, "Norp::OnNorpSocketEvent(RECV) received REQ_ACK session %d:%hu\n", msg.GetNodeId(), msg.GetSessionId());
                    NorpSession* session = session_list.FindSession(msg.GetSessionId());
                    // TBD - confirm the session found is for the given node id, etc
                    if (NULL != session)
                    {
                        if (!session->OnRemoteRequestAcknowledgment(msg, senderAddr)) 
                            session->Shutdown();
                    }
                    else
                    {
                        PLOG(PL_ERROR, "Norp::OnNorpSocketEvent(RECV) warning: received remote REQ_ACK for unknown session\n");
                    }
                    break;
                }
                case NorpMsg::SOCKS_REP:
                {
                    PLOG(PL_INFO, "Norp::OnNorpSocketEvent(RECV) received SOCKS_REP session %d:%hu (length:%d)\n", msg.GetNodeId(), msg.GetSessionId(), msg.GetLength());
                    NorpSession* session = session_list.FindSession(msg.GetSessionId());
                    if (NULL != session)
                    {
                        // TBD - confirm the session found is for the given node id, etc
                        if (!session->OnRemoteReply(msg, senderAddr))
                            session->Shutdown();
                    }
                    else
                    {
                        PLOG(PL_ERROR, "Norp::OnNorpSocketEvent(RECV) warning: received remote SOCKS_REPLY for unknown session\n");
                    }
                    break;
                }
                case NorpMsg::REP_ACK:
                {
                    PLOG(PL_INFO, "Norp::OnNorpSocketEvent(RECV) received REP_ACK session %d:%hu\n", msg.GetNodeId(), msg.GetSessionId());
                    NorpSession* session = session_list.FindSession(msg.GetSessionId(), msg.GetNodeId());
                    if (NULL != session)
                    {
                        if (!session->OnRemoteReplyAcknowledgment(msg))
                            session->Shutdown();
                    }
                    else
                    {
                        PLOG(PL_ERROR, "Norp::OnNorpSocketEvent(RECV) warning: received remote REP_ACK for unknown session\n");
                    }
                    break;
                }
                case NorpMsg::ORIG_END:
                {
                 
                    PLOG(PL_INFO, "Norp::OnNorpSocketEvent(RECV) received ORIG_END session %d:%hu\n", msg.GetNodeId(), msg.GetSessionId());
                    NorpSession* session = session_list.FindSession(msg.GetSessionId(), msg.GetNodeId());
                    if (NULL != session)
                    {
                        // TBD - mroe gently end things (e.g. let socket shutdowns commence)???
                        session->Close();  // the session will make an upcall and get itself removed/deleted
                    }   
                    UINT32 buffer[20/sizeof(UINT32)];
                    NorpMsg ack(buffer, 20, false);
                    ack.SetType(NorpMsg::ACK_END);
                    ack.SetSessionId(msg.GetSessionId());
                    ack.SetNodeId(NORM_NODE_NONE);
                    if (!SendMessage(ack, senderAddr))
                        PLOG(PL_ERROR, "Norp::OnNorpSocketEvent(RECV) warning: unable to send ACK_END message to originator\n");
                    break;
                }
                case NorpMsg::CORR_END:
                {
                    PLOG(PL_INFO, "Norp::OnNorpSocketEvent(RECV) received CORR_END session %d:%hu\n", msg.GetNodeId(), msg.GetSessionId());
                    NorpSession* session = session_list.FindSession(msg.GetSessionId(), NORM_NODE_NONE);
                    if (NULL != session)
                    {
                        // TBD - mroe gently end things (e.g. let socket shutdowns commence)???
                        session->Close();  // the session will make an upcall and get itself removed/deleted
                    }   
                    UINT32 buffer[20/sizeof(UINT32)];
                    NorpMsg ack(buffer, 20, false);
                    ack.SetType(NorpMsg::ACK_END);
                    ack.SetSessionId(msg.GetSessionId());
                    ack.SetNodeId(GetNormNodeId());
                    if (!SendMessage(ack, senderAddr))
                        PLOG(PL_ERROR, "Norp::OnNorpSocketEvent(RECV) warning: unable to send ACK_END message to correspondent\n");
                    break;
                }
                case NorpMsg::ACK_END:
                {
                    PLOG(PL_INFO, "Norp::OnNorpSocketEvent(RECV) received ACK_END session %d:%hu\n", msg.GetNodeId(), msg.GetSessionId());
                    NorpSession* session = session_list.FindSession(msg.GetSessionId(), msg.GetNodeId());
                    if (NULL != session) session->Close();  // the session will make an upcall and get itself removed/deleted
                    break;
                }
                default:
                {
                    PLOG(PL_ERROR, "Norp::OnNormEvent(NORM_RX_CMD_NEW) error: received invalid NORP command: %d\n", msg.GetType());
                    break;
                }
            }  // end switch(msg.GetType())
            numBytes = 1024;
        }  // end while(theSocket.RecvFrom())
    }  // end if (ProtoSocket::RECV == theEvent)
    else
    {
        PLOG(PL_DETAIL, "unhandled NORP socket event!\n");
    }
}  // end Norp::OnNorpSocketEvent()

void Norp::OnRemoteRequest(const NorpMsg& msg, const ProtoAddress& senderAddr)
{
    NorpSession* session = session_list.FindSession(msg.GetSessionId(), msg.GetNodeId());
    if (NULL == session)
    {
        PLOG(PL_INFO, "norp: new remote session id %hu:%08x\n", msg.GetSessionId(), msg.GetNodeId());
        // It's a new remote session
        session = new NorpSession(*this, msg.GetSessionId(), msg.GetNodeId());
        if (NULL == session)
        {
            PLOG(PL_ERROR, "Norp::OnRemoteRequest() new NorpSession error: %s\n", GetErrorString());
            // TBD - should we somehow signal the remote of our failure?
            // (just let it time out for now)
            return;
        }
        AddSession(*session);
    }
    if (!session->OnRemoteRequest(msg, senderAddr))
    {
        PLOG(PL_ERROR, "Norp::OnRemoteRequest() error: unable to initiate new remote NorpSession\n");
        // TBD - signal failure to remote
        RemoveSession(*session);
        delete session;
    }
}  // end Norp::OnRemoteRequest()


void Norp::NormEventCallback(ProtoDispatcher::Descriptor descriptor, 
                             ProtoDispatcher::Event      theEvent, 
                             const void*                 userData)
{
    if (ProtoDispatcher::EVENT_INPUT == theEvent)
    {
        Norp* norp = (Norp*)(userData);
        norp->OnNormEvent();
    }
    else
    {
        ASSERT(0);  // should never happen
    }
}  // end Norp::NormEventCallback()

void Norp::OnNormEvent()
{
    NormEvent theEvent;
    // We make a looping, non-blocking call to NormGetNextEvent() here
    // to consume all available NORM notification events
    while (NormGetNextEvent(norm_instance, &theEvent, false))
    {
        PLOG(PL_DETAIL, "Norp::OnNormEvent(");
        // See if this event is for a "child" NorpSession
	    if (NORM_SESSION_INVALID != theEvent.session)
        {
            NorpSession* norpSession = (NorpSession*)NormGetUserData(theEvent.session);
            if (NULL != norpSession)
            {
                PLOG(PL_DETAIL, "session %hu event)\n", norpSession->GetSessionId());
                norpSession->OnNormEvent(theEvent);
                continue;
            }
        }
        else
        {
            //ASSERT(0);  // should never happen
        }
    }
}  // end Norp::OnNormEvent()



