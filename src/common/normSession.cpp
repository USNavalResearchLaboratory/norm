#include "normSession.h"

#include "normEncoderMDP.h"  // "legacy" MDP Reed-Solomon encoder
#include "normEncoderRS8.h"  // 8-bit Reed-Solomon encoder of RFC 5510
#include "normEncoderRS16.h" // 16-bit Reed-Solomon encoder of RFC 5510

#include <time.h> // for gmtime() in NormTrace()

#include "protoPktETH.h"
#include "protoPktIP.h"
#include "protoNet.h"

const UINT8 NormSession::DEFAULT_TTL = 255;
const double NormSession::DEFAULT_TRANSMIT_RATE = 64000.0;  // bits/sec
const double NormSession::DEFAULT_GRTT_INTERVAL_MIN = 1.0;  // sec
const double NormSession::DEFAULT_GRTT_INTERVAL_MAX = 30.0; // sec
const double NormSession::DEFAULT_GRTT_ESTIMATE = 0.25;     // sec
const double NormSession::DEFAULT_GRTT_MAX = 10.0;          // sec
const unsigned int NormSession::DEFAULT_GRTT_DECREASE_DELAY = 3;
const double NormSession::DEFAULT_BACKOFF_FACTOR = 4.0;
const double NormSession::DEFAULT_GSIZE_ESTIMATE = 1000.0;
const UINT16 NormSession::DEFAULT_NDATA = 64;
const UINT16 NormSession::DEFAULT_NPARITY = 8;
const UINT16 NormSession::DEFAULT_TX_CACHE_MIN = 8;
const UINT16 NormSession::DEFAULT_TX_CACHE_MAX = 256;
const UINT32 NormSession::DEFAULT_TX_CACHE_SIZE = (UINT32)20 * 1024 * 1024;
const double NormSession::DEFAULT_FLOW_CONTROL_FACTOR = 2.0;
const UINT16 NormSession::DEFAULT_RX_CACHE_MAX = 256;

const int NormSession::DEFAULT_ROBUST_FACTOR = 20; // default robust factor

// This is extra stuff defined for NormSocket API extension purposes.  As the NormSocket
// extension is finalized, these may be refined/relocated
enum
{
    NORM_SOCKET_VERSION = 1
};
enum NormSocketCommand
{
    NORM_SOCKET_CMD_NULL = 0, // reserved, invalid/null command
    NORM_SOCKET_CMD_REJECT,   // sent by server-listener to reject invalid connection messages
    NORM_SOCKET_CMD_ALIVE     // TBD - for NormSocket "keep-alive" option?
};

NormSession::NormSession(NormSessionMgr &sessionMgr, NormNodeId localNodeId)
    : session_mgr(sessionMgr), notify_pending(false), tx_port(0), tx_port_reuse(false),
      tx_socket_actual(ProtoSocket::UDP), tx_socket(&tx_socket_actual),
      rx_socket(ProtoSocket::UDP),
#ifdef ECN_SUPPORT 
      proto_cap(NULL), 
#endif // ECN_SUPPORT
      rx_port_reuse(false), local_node_id(localNodeId),
      ttl(DEFAULT_TTL), tos(0), loopback(false), mcast_loopback(false), fragmentation(false), ecn_enabled(false),
      tx_rate(DEFAULT_TRANSMIT_RATE / 8.0), tx_rate_min(-1.0), tx_rate_max(-1.0), tx_residual(0),
      backoff_factor(DEFAULT_BACKOFF_FACTOR), is_sender(false),
      tx_robust_factor(DEFAULT_ROBUST_FACTOR), instance_id(0),
      ndata(DEFAULT_NDATA), nparity(DEFAULT_NPARITY), auto_parity(0), extra_parity(0),
      sndr_emcon(false), tx_only(false), tx_connect(false), fti_mode(FTI_ALWAYS), encoder(NULL),
      next_tx_object_id(0),
      tx_cache_count_min(DEFAULT_TX_CACHE_MIN),
      tx_cache_count_max(DEFAULT_TX_CACHE_MAX),
      tx_cache_size_max(DEFAULT_TX_CACHE_SIZE),
      posted_tx_queue_empty(false), posted_tx_rate_changed(false), posted_send_error(false),
      acking_node_count(0), acking_auto_populate(TRACK_NONE), watermark_pending(false), watermark_flushes(false),
      tx_repair_pending(false), advertise_repairs(false),
      suppress_nonconfirmed(false), suppress_rate(-1.0), suppress_rtt(-1.0),
      probe_proactive(true), probe_pending(false), probe_reset(true), probe_data_check(false),
      probe_tos(0), grtt_interval(0.5), grtt_interval_min(DEFAULT_GRTT_INTERVAL_MIN),
      grtt_interval_max(DEFAULT_GRTT_INTERVAL_MAX),
      grtt_max(DEFAULT_GRTT_MAX),
      grtt_decrease_delay_count(DEFAULT_GRTT_DECREASE_DELAY),
      grtt_response(false), grtt_current_peak(0.0), grtt_age(0.0), probe_count(1),
      cc_enable(false), cc_adjust(true), cc_sequence(0), cc_slow_start(true), cc_active(false),
      flow_control_factor(DEFAULT_FLOW_CONTROL_FACTOR),
      cmd_count(0), cmd_buffer(NULL), cmd_length(0), syn_status(false),
      ack_ex_buffer(NULL), ack_ex_length(0),
      is_receiver(false), rx_robust_factor(DEFAULT_ROBUST_FACTOR), preset_sender(NULL), unicast_nacks(false),
      receiver_silent(false), rcvr_ignore_info(false), rcvr_max_delay(-1), rcvr_realtime(false),
      default_repair_boundary(NormSenderNode::BLOCK_BOUNDARY),
      default_nacking_mode(NormObject::NACK_NORMAL), default_sync_policy(NormSenderNode::SYNC_CURRENT),
      rx_cache_count_max(DEFAULT_RX_CACHE_MAX), is_server_listener(false), notify_on_grtt_update(true),
      ecn_ignore_loss(false),
      trace(false), tx_loss_rate(0.0), rx_loss_rate(0.0),
      user_data(NULL), next(NULL)
{
    interface_name[0] = '\0';
    tx_socket_actual.SetNotifier(&sessionMgr.GetSocketNotifier());
    tx_socket_actual.SetListener(this, &NormSession::TxSocketRecvHandler);
    tx_address.Invalidate();

    rx_socket.SetNotifier(&sessionMgr.GetSocketNotifier());
    rx_socket.SetListener(this, &NormSession::RxSocketRecvHandler);

    tx_timer.SetListener(this, &NormSession::OnTxTimeout);
    tx_timer.SetInterval(0.0);
    tx_timer.SetRepeat(-1);

    repair_timer.SetListener(this, &NormSession::OnRepairTimeout);
    repair_timer.SetInterval(0.0);
    repair_timer.SetRepeat(1);

    flush_timer.SetListener(this, &NormSession::OnFlushTimeout);
    flush_timer.SetInterval(0.0);
    flush_timer.SetRepeat(0);

    flow_control_timer.SetListener(this, &NormSession::OnFlowControlTimeout);
    flow_control_timer.SetInterval(0.0);
    flow_control_timer.SetRepeat(0);

    cmd_timer.SetListener(this, &NormSession::OnCmdTimeout);
    cmd_timer.SetInterval(0.0);
    cmd_timer.SetRepeat(0);

    probe_timer.SetListener(this, &NormSession::OnProbeTimeout);
    probe_timer.SetInterval(0.0);
    probe_timer.SetRepeat(-1);
    probe_time_last.tv_sec = probe_time_last.tv_usec = 0;

    grtt_quantized = NormQuantizeRtt(DEFAULT_GRTT_ESTIMATE);
    grtt_measured = grtt_advertised = NormUnquantizeRtt(grtt_quantized);

    gsize_measured = DEFAULT_GSIZE_ESTIMATE;
    gsize_quantized = NormQuantizeGroupSize(DEFAULT_GSIZE_ESTIMATE);
    gsize_advertised = NormUnquantizeGroupSize(gsize_quantized);

    // This timer is for printing out occasional status reports
    // (It may be used to trigger transmission of report messages
    //  in the future for debugging, etc
    report_timer.SetListener(this, &NormSession::OnReportTimeout);
    report_timer.SetInterval(10.0);
    report_timer.SetRepeat(-1);

    user_timer.SetListener(this, &NormSession::OnUserTimeout);
    user_timer.SetInterval(0.0);
    user_timer.SetRepeat(0);
}

NormSession::~NormSession()
{
    if (user_timer.IsActive())
        user_timer.Deactivate();
    if (NULL != preset_sender)
    {
        delete preset_sender;
        preset_sender = NULL;
    }
    Close();
}

bool NormSession::Open()
{
    ASSERT(address.IsValid());
    if (!tx_socket->IsOpen())
    {
        // Make sure user wants a separate tx_socket
        if ((address.GetPort() != tx_port) ||
            (tx_address.IsValid() && !address.HostIsEqual(tx_address)))
        {
            if (!tx_socket->Open(tx_port, address.GetType(), false))
            {
                PLOG(PL_FATAL, "NormSession::Open() tx_socket::Open() error\n");
                return false;
            }
            if (tx_port_reuse)
            {
                if (!tx_socket->SetReuse(true))
                {
                    PLOG(PL_FATAL, "NormSession::Open() tx_socket::SetReuse() error\n");
                    Close();
                    return false;
                }
            }
            ProtoAddress *txBindAddress = tx_address.IsValid() ? &tx_address : NULL;
            if (!tx_socket->Bind(tx_port, txBindAddress))
            {
                PLOG(PL_FATAL, "NormSession::Open() tx_socket::Bind() error\n");
                Close();
                return false;
            }
            // Connecting unicast sockets can help us get unicast NACKs
            // when tx_port_reuse and tx_only is requested
            if (tx_connect && !address.IsMulticast())
            {
                if (!tx_socket->Connect(address))
                {
                    PLOG(PL_FATAL, "NormSession::Open() tx_socket::Connect() error\n");
                    Close();
                    return false;
                }
            }
        }
        else
        {
            tx_socket = &rx_socket;
        }
        tx_port = tx_socket->GetPort();
            
    }
    if (!rx_socket.IsOpen() && (!tx_only || (&rx_socket == tx_socket)))
    {
        if (!rx_socket.Open(0, address.GetType(), false))
        {
            PLOG(PL_FATAL, "NormSession::Open() rx_socket.Open() error\n");
            Close();
            return false;
        }
        rx_socket.EnableRecvDstAddr();
        if (rx_port_reuse)
        {
            // Enable port/addr reuse and bind socket to destination address
            if (!rx_socket.SetReuse(true))
            {
                PLOG(PL_FATAL, "NormSession::Open() rx_socket::SetReuse() error\n");
                Close();
                return false;
            }
        }
        const ProtoAddress *bindAddr = NULL;
        if (rx_bind_addr.IsValid())
        {
#ifdef WIN32
            if (rx_bind_addr.IsMulticast()) // Win32 doesn't like to bind() mcast addr??
                PLOG(PL_WARN, "NormSession::Open() warning: WIN32 multicast bind() issue!\n");
            else
#endif
                bindAddr = &rx_bind_addr;
        }
        if (!rx_socket.Bind(address.GetPort(), bindAddr))
        {
            PLOG(PL_FATAL, "NormSession::Open() error: rx_socket.Bind() error\n");
            Close();
            return false;
        }
        if (rx_connect_addr.IsValid() && (0 != rx_connect_addr.GetPort()))
        {
            // For unicast, we use the "connect()" call to effectively
            // uniquely "bind" our rx_socket to the remote addr.
            // (it _may_ be the case that "tx_port" == "address.GetPort()"
            //  for this to work?)
            if (!rx_socket.Connect(rx_connect_addr))
            {
                PLOG(PL_FATAL, "NormSession::Open() rx_socket.Connect() error\n");
                Close();
                return false;
            }
        }
    }
    if (ecn_enabled)
    {
        if (!tx_socket->SetEcnCapable(true))
        {
            PLOG(PL_WARN, "NormSession::Open() warning: tx_socket.SetEcnEnable() error\n");
        }
    }

    if (0 != tos)
    {
        if (!tx_socket->SetTOS(tos))
            PLOG(PL_WARN, "NormSession::Open() warning: tx_socket.SetTOS() error\n");
    }

    if (!tx_socket->SetFragmentation(fragmentation))
        PLOG(PL_WARN, "NormSession::Open() warning: tx_socket.SetFragmentation() error\n");

    if (address.IsMulticast())
    {
        if (!tx_socket->SetTTL(ttl))
        {
            PLOG(PL_FATAL, "NormSession::Open() tx_socket.SetTTL() error\n");
            Close();
            return false;
        }
        if (!tx_socket->SetLoopback(mcast_loopback))
        {
            // TBD - Should this be set on the rx_socket instead???
            PLOG(PL_FATAL, "NormSession::Open() tx_socket.SetLoopback() error\n");
            Close();
            return false;
        }
        const char *interfaceName = NULL;
        if ('\0' != interface_name[0])
        {
            bool result = tx_only ? true : rx_socket.SetMulticastInterface(interface_name);
            result &= tx_socket->SetMulticastInterface(interface_name);
            if (!result)
            {
                PLOG(PL_FATAL, "NormSession::Open() tx_socket::SetMulticastInterface() error\n");
                Close();
                return false;
            }
            interfaceName = interface_name;
        }
        if (!tx_only)
        {
            if (!rx_socket.JoinGroup(address, interfaceName, ssm_source_addr.IsValid() ? &ssm_source_addr : NULL))
            {
                PLOG(PL_FATAL, "NormSession::Open() rx_socket.JoinGroup error\n");
                Close();
                return false;
            }
        }
    }

#ifdef ECN_SUPPORT
    // TBD - do this via UDP socket recvmsg() instead of raw packet capture
    // If raw packet capture is enabled, create/open ProtoCap device to do it
    if ((ecn_enabled && !tx_only) || (0 != probe_tos))
    {
        if (!OpenProtoCap())
        {
            PLOG(PL_FATAL, "NormSession::Open() error: unable to create ProtoCap device!\n");
            Close();
            return false;
        }
        if (ecn_enabled)
        {
            rx_socket.StopInputNotification(); // Disable rx_socket (keep open so mcast JOIN holds)
            proto_cap->StartInputNotification();
        }
        else
        {
            proto_cap->StopInputNotification();
        }
    }
#endif // ECN_SUPPORT
    if (message_pool.IsEmpty())
    {
        for (unsigned int i = 0; i < DEFAULT_MESSAGE_POOL_DEPTH; i++)
        {
            NormMsg *msg = new NormMsg();
            if (msg)
            {
                message_pool.Append(msg);
            }
            else
            {
                PLOG(PL_FATAL, "NormSession::Open() new message error: %s\n", GetErrorString());
                Close();
                return false;
            }
        }
    }
    if (!report_timer.IsActive())
        ActivateTimer(report_timer);

    return true;
} // end NormSession::Open()

void NormSession::Close()
{
    if (report_timer.IsActive())
        report_timer.Deactivate();
    if (is_sender)
        StopSender();
    if (is_receiver)
        StopReceiver();
    if (tx_timer.IsActive())
        tx_timer.Deactivate();
    message_queue.Destroy();
    message_pool.Destroy();
    if (tx_socket->IsOpen())
        tx_socket->Close();
    if (rx_socket.IsOpen())
    {
        if (address.IsMulticast())
        {
            const char *interfaceName = ('\0' != interface_name[0]) ? interface_name : NULL;
            rx_socket.LeaveGroup(address, interfaceName, ssm_source_addr.IsValid() ? &ssm_source_addr : NULL);
        }
        rx_socket.Close();
    }
#ifdef ECN_SUPPORT
    CloseProtoCap();
#endif // ECN_SUPPORT
} // end NormSession::Close()

#ifdef ECN_SUPPORT
bool NormSession::OpenProtoCap()
{
    if (NULL == proto_cap)
    {
        if (NULL == (proto_cap = ProtoCap::Create()))
        {
            PLOG(PL_FATAL, "NormSession::OpenProtoCap() error: unable to create ProtoCap device!\n");
            return false;
        }
        proto_cap->SetListener(this, &NormSession::OnPktCapture);
        proto_cap->SetNotifier(session_mgr.GetChannelNotifier());
        if (!proto_cap->Open(('\0' != interface_name[0]) ? interface_name : NULL))
        {
            PLOG(PL_FATAL, "NormSession::OpenProtoCap() error: unable to open ProtoCap device '%s'!\n", (('\0' != interface_name[0]) ? interface_name : "(null)"));
            return false;
        }
        // Populate "dst_addr_list" with potential valid dst addrs for this host
        dst_addr_list.Destroy();
        if (rx_bind_addr.IsValid())
        {
            if (!dst_addr_list.Insert(rx_bind_addr))
            {
                PLOG(PL_FATAL, "NormSession::OpenProtoCap() error: unable to add rx_bind_addr to dst_addr_list!!\n");
                return false;
            }
        }
        else
        {
            // Put all local unicast addrs in list
            if (!ProtoSocket::GetHostAddressList(ProtoAddress::IPv4, dst_addr_list))
                PLOG(PL_WARN, "NormSession::OpenProtoCap() warning: incomplete IPv4 host address list\n");
            if (!ProtoSocket::GetHostAddressList(ProtoAddress::IPv6, dst_addr_list))
                PLOG(PL_WARN, "NormSession::OpenProtoCap() warning: incomplete IPv6 host address list\n");
        }
        if (address.IsMulticast() && !address.HostIsEqual(rx_bind_addr))
        {
            if (!dst_addr_list.Insert(address))
            {
                PLOG(PL_FATAL, "NormSession::OpenProtoCap() error: unable to add session addr to dst_addr_list!!\n");
                return false;
            }
        }
        
        // Get the interface source address for pcap message transmission
        if (!ProtoNet::GetInterfaceAddress(proto_cap->GetInterfaceIndex(),
                                            address.GetType(),
                                            src_addr))
        {
            PLOG(PL_WARN, "NormSession::OpenProtoCap() warning: unable to get interface source address\n");
        }
        
        if (dst_addr_list.IsEmpty())
        {
            PLOG(PL_FATAL, "NormSession::OpenProtoCap() error: unable to add any addresses to dst_addr_list!!\n");
            return false;
        }
    }
    return true;
}  // end NormSession::OpenProtoCap()

void NormSession::CloseProtoCap()
{
    if (NULL != proto_cap)
    {
        proto_cap->Close();
        delete proto_cap;
        proto_cap = NULL;
    }
}  // end NormSession::CloseProtoCap()

#endif  // ECN_SUPPORT

bool NormSession::SetMulticastInterface(const char *interfaceName)
{
    if (NULL != interfaceName)
    {
        bool result = true;
        if (rx_socket.IsOpen())
            result &= rx_socket.SetMulticastInterface(interfaceName);
        if (tx_socket->IsOpen())
            result &= tx_socket->SetMulticastInterface(interfaceName);
        strncpy(interface_name, interfaceName, IFACE_NAME_MAX);
        interface_name[IFACE_NAME_MAX] = '\0';
        return result;
    }
    else
    {
        interface_name[0] = '\0';
        return true;
    }
} // end NormSession::SetMulticastInterface()

bool NormSession::SetSSM(const char *sourceAddress)
{
    if (NULL != sourceAddress)
    {
        if (ssm_source_addr.ResolveFromString(sourceAddress))
        {
            return true;
        }
        else
        {
            PLOG(PL_ERROR, "NormSession::SetSSM() error: invalid source address\n");
            return false;
        }
    }
    else
    {
        ssm_source_addr.Invalidate();
        return true;
    }
} // end NormSession::SetSSM()

// This must be called _before_ sender or receiver is started
// (i.e., before socket(s) are opened)
bool NormSession::SetRxPortReuse(bool enableReuse,
                                 const char *rxBindAddress, // bind() to <rxBindAddress>/<sessionPort>
                                 const char *senderAddress, // connect() to <senderAddress>/<senderPort>
                                 UINT16 senderPort)
{
    rx_port_reuse = enableReuse; // allow sessionPort reuse when true
    bool result;
    if (NULL != rxBindAddress)
    {
        result = rx_bind_addr.ResolveFromString(rxBindAddress);
    }
    else
    {
        rx_bind_addr.Invalidate();
        result = true;
    }
    if (NULL != senderAddress)
    {
        // TBD - if open, connect() to sender?
        if (rx_connect_addr.ResolveFromString(senderAddress))
        {
            rx_connect_addr.SetPort(senderPort);
            result &= true;
        }
        else
        {
            result = false;
        }
    }
    else
    {
        rx_connect_addr.Invalidate();
    }
    // TBD - if rx_socket.IsOpen(), should we do a Close()/Open() to rebind socket???
    return result;
} // end NormSession::SetRxPortReuse()

bool NormSession::SetTxPort(UINT16 txPort, bool enableReuse, const char *txAddress)
{
    tx_port = txPort;
    tx_port_reuse = enableReuse;
    bool result;
    if (NULL != txAddress)
    {
        result = tx_address.ResolveFromString(txAddress);
        // Automatically set port reuse if same tx/rx port nums but diff addr bindings
        if (result)
        {
            if ((tx_port == GetRxPort()) && !tx_address.HostIsEqual(address))
                tx_port_reuse = rx_port_reuse = true;
        }
    }
    else
    {
        tx_address.Invalidate();
        result = true;
    }
    return result;
} // end NormSession::SetTxPort()

UINT16 NormSession::GetTxPort() const
{
    if (0 != tx_port)
        return tx_port;
    else if (tx_socket->IsOpen())
        return tx_socket->GetPort();
    else
        return 0;
} // end NormSession::GetTxPort()

UINT16 NormSession::GetRxPort() const
{
    // Note that rx port _can_ be different than session port
    // if the session destination address was changed via
    // NormSession::SetAddress()
    if (rx_socket.IsOpen())
        return rx_socket.GetPort();
    else
        return address.GetPort();
} // end NormSession::GetRxPort()

void NormSession::SetTxOnly(bool txOnly, bool connectToSessionAddress)
{
    tx_only = txOnly;
    tx_connect = connectToSessionAddress;
    if (IsOpen())
    {
        if (txOnly)
        {
            if (IsReceiver())
                StopReceiver();
            if (rx_socket.IsOpen())
                rx_socket.Close();
#ifdef ECN_SUPPORT
            if (NULL != proto_cap)
                proto_cap->StopInputNotification();
#endif // ECN_SUPPORT
        }
        // We connect tx_only session sockets when tx port
        // reuse is set _and_ it is a unicast session
        // (This makes sure unicast NACKs get back to the right tx_socket!)
        if (connectToSessionAddress && !address.IsMulticast())
        {
            if (!tx_socket->Connect(address))
                PLOG(PL_WARN, "NormSession::SetTxOnly() tx_socket connect() error: %s\n", ProtoSocket::GetErrorString());
        }
    }
} // end NormSession::SetTxOnly()

double NormSession::GetTxRate()
{
    posted_tx_rate_changed = false;
    if (cc_enable && !cc_adjust)
    {
        // Return rate of CLR
        const NormCCNode *clr = static_cast<const NormCCNode *>(cc_node_list.Head());
        return ((NULL != clr) ? 8.0 * clr->GetRate() : 0.0);
    }
    else
    {
        return (8.0 * tx_rate);
    }
} // end NormSession::GetTxRate()

/*
// This hack can be uncommented give us a tx rate interval that is POISSON instead of PERIODIC
static double PoissonRand(double mean)
{
    return(-log(((double)rand())/((double)RAND_MAX))*mean);
}
*/

static inline double GetTxInterval(unsigned int msgSize, double txRate)
{
    double interval = (double)msgSize / txRate;

    //double jitterMax = 0.05*interval;
    //interval += UniformRand(jitterMax) - jitterMax/2.0;
    return interval; // PERIODIC interval based on rate
    //return PoissonRand(interval);
}

void NormSession::SetTxRateInternal(double txRate)
{
    if (!is_sender)
    {
        tx_rate = txRate;
        return;
    }
    if (txRate < 0.0)
    {
        PLOG(PL_FATAL, "NormSession::SetTxRateInternal() invalid transmit rate!\n");
        return;
    }
    if (tx_timer.IsActive())
    {
        if (txRate > 0.0)
        {
            double adjustInterval = (tx_rate / txRate) * tx_timer.GetTimeRemaining();
            //adjustInterval = PoissonRand(adjustInterval);
            if (adjustInterval > NORM_TICK_MIN)
            {
                tx_timer.SetInterval(adjustInterval);
                tx_timer.Reschedule();
            }
        }
        else
        {
            tx_timer.Deactivate();
        }
    }
    else if ((0.0 == tx_rate) && IsOpen())
    {
        tx_timer.SetInterval(0.0);
        if (txRate > 0.0)
            ActivateTimer(tx_timer);
    }
    tx_rate = txRate;
    if (tx_rate > 0.0)
    {
        unsigned char grttQuantizedOld = grtt_quantized;
        double pktInterval = (double)(44 + segment_size) / txRate;
        if (grtt_measured < pktInterval)
            grtt_quantized = NormQuantizeRtt(pktInterval);
        else
            grtt_quantized = NormQuantizeRtt(grtt_measured);
        grtt_advertised = NormUnquantizeRtt(grtt_quantized);

        // What do we do when "pktInterval" > "grtt_max"?
        // We will take our lumps with some extra activity timeout NACKs when they happen?
        if (grtt_advertised > grtt_max)
        {
            grtt_quantized = NormQuantizeRtt(grtt_max);
            grtt_advertised = NormUnquantizeRtt(grtt_quantized);
        }
        if (grttQuantizedOld != grtt_quantized)
        {
            PLOG(PL_DEBUG, "NormSession::SetTxRateInternal() node>%lu %s to new grtt to: %lf sec\n",
                 (unsigned long)LocalNodeId(),
                 (grttQuantizedOld < grtt_quantized) ? "increased" : "decreased",
                 grtt_advertised);
            if (notify_on_grtt_update)
            {
                notify_on_grtt_update = false;
                Notify(NormController::GRTT_UPDATED, (NormSenderNode *)NULL, (NormObject *)NULL);
            }
        }
        // wakeup grtt/cc probing if necessary
        if (probe_reset)
        {
            probe_reset = false;
            OnProbeTimeout(probe_timer);
            if (!probe_timer.IsActive())
                ActivateTimer(probe_timer);
        }
    }
} // end NormSession::SetTxRateInternal()

void NormSession::SetTxRateBounds(double rateMin, double rateMax)
{
    posted_tx_rate_changed = false;
    // Make sure min <= max
    if ((rateMin >= 0.0) && (rateMax >= 0.0))
    {
        if (rateMin > rateMax)
        {
            double temp = rateMin;
            rateMin = rateMax;
            rateMax = temp;
        }
    }
    if (rateMin < 0.0)
        tx_rate_min = -1.0;
    else if (rateMin < 8.0)
        tx_rate_min = 1.0; // one byte/second absolute minimum
    else
        tx_rate_min = rateMin / 8.0; // convert to bytes/second
    if (rateMax < 0.0)
        tx_rate_max = -1.0;
    else
        tx_rate_max = rateMax / 8.0; // convert to bytes/second
    if (cc_enable)
    {
        double txRate = tx_rate;
        if ((tx_rate_min > 0.0) && (txRate < tx_rate_min))
            txRate = tx_rate_min;
        if ((tx_rate_max >= 0.0) && (txRate > tx_rate_max))
            txRate = tx_rate_max;
        if (txRate != tx_rate)
            SetTxRateInternal(txRate);
    }
} // end NormSession::SetTxRateBounds()

void NormSession::SetUserTimer(double seconds)
{
    if (user_timer.IsActive())
        user_timer.Deactivate();
    if (seconds >= 0.0)
    {
        user_timer.SetInterval(seconds);
        ActivateTimer(user_timer);
    }
} // end NormSession::SetUserTimer()

bool NormSession::StartSender(UINT16 instanceId,
                              UINT32 bufferSpace,
                              UINT16 segmentSize,
                              UINT16 numData,
                              UINT16 numParity,
                              UINT8 fecId)
{
    UINT16 blockSize = numData + numParity;
    if (blockSize <= 255)
        fec_m = 8;
    else
        fec_m = 16;
    if (preset_fti.IsValid())
    {
        if ((preset_fti.GetSegmentSize() != segmentSize) ||
            (preset_fti.GetFecMaxBlockLen() != numData) ||
            (preset_fti.GetFecNumParity() != numParity) ||
            (preset_fti.GetFecFieldSize() != fec_m))
        {
            PLOG(PL_FATAL, "NormSession::StartSender() preset FTI mismatch error!\n");
            return false;
        }
    }
    if (!IsOpen())
    {
        if (!Open())
            return false;
    }
    if (!tx_table.Init(tx_cache_count_max))
    {
        PLOG(PL_FATAL, "NormSession::StartSender() tx_table.Init() error!\n");
        StopSender();
        return false;
    }
    if (!tx_pending_mask.Init(tx_cache_count_max, 0x0000ffff))
    {
        PLOG(PL_FATAL, "NormSession::StartSender() tx_pending_mask.Init() error!\n");
        StopSender();
        return false;
    }
    if (!tx_repair_mask.Init(tx_cache_count_max, 0x0000ffff))
    {
        PLOG(PL_FATAL, "NormSession::StartSender() tx_repair_mask.Init() error!\n");
        StopSender();
        return false;
    }

    // Calculate how much memory each buffered block will require
    unsigned long maskSize = blockSize >> 3;
    if (0 != (blockSize & 0x07))
        maskSize++;
    unsigned long blockSpace = sizeof(NormBlock) +
                               blockSize * sizeof(char *) +
                               2 * maskSize +
                               numParity * (segmentSize + NormDataMsg::GetStreamPayloadHeaderLength());

    unsigned long numBlocks = bufferSpace / blockSpace;
    if (bufferSpace > (numBlocks * blockSpace))
        numBlocks++;
    if (numBlocks < 2)
        numBlocks = 2;
    unsigned long numSegments = numBlocks * numParity;

    if (!block_pool.Init((UINT32)numBlocks, blockSize))
    {
        PLOG(PL_FATAL, "NormSession::StartSender() block_pool init error\n");
        StopSender();
        return false;
    }

    if (!segment_pool.Init((unsigned int)numSegments, segmentSize + NormDataMsg::GetStreamPayloadHeaderLength()))
    {
        PLOG(PL_FATAL, "NormSession::StartSender() segment_pool init error\n");
        StopSender();
        return false;
    }

    if (numParity)
    {
        if (NULL != encoder)
            delete encoder;

        if (blockSize <= 255)
        {
#ifdef ASSUME_MDP_FEC
            if (NULL == (encoder = new NormEncoderMDP))
            {
                PLOG(PL_FATAL, "NormSession::StartSender() new NormEncoderMDP error: %s\n", GetErrorString());
                StopSender();
                return false;
            }
            fec_id = 129;
            fec_m = 8;
#else
            if (NULL == (encoder = new NormEncoderRS8))
            {
                PLOG(PL_FATAL, "NormSession::StartSender() new NormEncoderRS8 error: %s\n", GetErrorString());
                StopSender();
                return false;
            }
            if (0 != fecId)
                fec_id = fecId;
            else
                fec_id = 5;
            fec_m = 8;
#endif
        }
        else //if (blockSize <= 65535)
        {
            if (NULL == (encoder = new NormEncoderRS16))
            {
                PLOG(PL_FATAL, "NormSession::StartSender() new NormEncoderRS16 error: %s\n", GetErrorString());
                StopSender();
                return false;
            }
            // TBD - Investigate if fec_id == 129 can also support 16-bit Reed Solomon
            fec_id = 2;
            fec_m = 16;
        }
        /*else
        {
            PLOG(PL_FATAL, "NormSession::StartSender() error: invalid FEC block size\n");
            StopSender();
            return false;
        }*/

        if (!encoder->Init(numData, numParity, segmentSize + NormDataMsg::GetStreamPayloadHeaderLength()))
        {
            PLOG(PL_FATAL, "NormSession::StartSender() encoder init error\n");
            StopSender();
            return false;
        }
    }
    else
    {
        // for now use RS8 fec_id with no parity (TBD - support "compact" null FEC type)
        if (0 != fecId)
            fec_id = fecId;
        else
            fec_id = 5;
        fec_m = 8;
    }

    fec_block_mask = NormPayloadId::GetFecBlockMask(fec_id, fec_m);

    // Initialize optional app-defined command state
    cmd_count = cmd_length = 0;
    if (NULL == (cmd_buffer = new char[segmentSize]))
    {
        PLOG(PL_FATAL, "NormSession::StartSender() error: unable to allocate cmd_buffer: %s\n", GetErrorString());
        StopSender();
        return false;
    }

    instance_id = instanceId;
    segment_size = segmentSize;
    sent_accumulator.Reset();
    nominal_packet_size = (double)segmentSize;
    data_active = false;
    ndata = numData;
    nparity = numParity;
    is_sender = true;

    flush_count = (GetTxRobustFactor() < 0) ? 0 : (GetTxRobustFactor() + 1);

    if (cc_enable && cc_adjust)
    {
        double txRate;
        if (tx_rate_min > 0.0)
        {
            txRate = tx_rate_min;
        }
        else
        {
            // Don't let txRate below MIN(one segment per grtt, one segment per second)
            txRate = ((double)segment_size) / grtt_measured;
            if (txRate > ((double)(segment_size)))
                txRate = (double)(segment_size);
        }
        if ((tx_rate_max >= 0.0) && (tx_rate > tx_rate_max))
            txRate = tx_rate_max;
        SetTxRateInternal(txRate); // adjusts grtt_advertised as needed
    }
    else
    {
        SetTxRateInternal(tx_rate); // takes segment size into account, etc on sender start
    }
    cc_slow_start = true;
    cc_active = false;

    grtt_age = 0.0;
    probe_pending = false;
    probe_data_check = false;
    if (probe_reset)
    {
        probe_reset = false;
        OnProbeTimeout(probe_timer);
        if (!probe_timer.IsActive())
            ActivateTimer(probe_timer);
    }
    return true;
} // end NormSession::StartSender()

void NormSession::StopSender()
{
    if (probe_timer.IsActive())
    {
        probe_timer.Deactivate();
        probe_reset = true;
    }
    if (repair_timer.IsActive())
    {
        repair_timer.Deactivate();
        tx_repair_pending = false;
    }
    if (flush_timer.IsActive())
        flush_timer.Deactivate();
    if (cmd_timer.IsActive())
        cmd_timer.Deactivate();
    if (flow_control_timer.IsActive())
        flow_control_timer.Deactivate();

    if (NULL != ack_ex_buffer)
    {
        delete[] ack_ex_buffer;
        ack_ex_buffer = NULL;
        ack_ex_length = 0;
    }

    if (NULL != cmd_buffer)
    {
        delete[] cmd_buffer;
        cmd_buffer = NULL;
        cmd_length = 0;
    }

    if (NULL != encoder)
    {
        encoder->Destroy();
        delete encoder;
        encoder = NULL;
    }
    acking_node_tree.Destroy();
    cc_node_list.Destroy();
    // Iterate tx_table and release objects
    while (!tx_table.IsEmpty())
    {
        NormObject *obj = tx_table.Find(tx_table.RangeLo());
        ASSERT(NULL != obj);
        tx_table.Remove(obj);
        obj->Close();
        obj->Release();
    }
    // Then destroy table
    tx_table.Destroy();
    block_pool.Destroy();
    segment_pool.Destroy();
    tx_repair_mask.Destroy();
    tx_pending_mask.Destroy();
    is_sender = false;
    if (!IsReceiver())
        Close();
} // end NormSession::StopSender()

bool NormSession::StartReceiver(unsigned long bufferSize)
{
    //if (tx_only) return false;
    tx_only = false;
    if (!rx_socket.IsOpen())
    {
        if (!Open())
            return false;
    }
    is_receiver = true;
    remote_sender_buffer_size = bufferSize;
    return true;
} // end NormSession::StartReceiver()

void NormSession::StopReceiver()
{
    // Iterate sender_tree and close/release sender nodes
    if (IsServerListener())
    {
        client_tree.Destroy();
    }
    else
    {
        NormSenderNode *senderNode =
            static_cast<NormSenderNode *>(sender_tree.GetRoot());
        while (NULL != senderNode)
        {
            sender_tree.DetachNode(senderNode);
            senderNode->Close();
            senderNode->Release();
            senderNode = static_cast<NormSenderNode *>(sender_tree.GetRoot());
        }
    }
    is_receiver = false;
    if (!is_sender)
        Close();
} // end NormSession::StopReceiver()

void NormSession::DeleteRemoteSender(NormSenderNode &senderNode)
{
    // TBD - confirm that "senderNode" is valid???
    if (IsServerListener())
        client_tree.RemoveNode(senderNode);
    else
        sender_tree.DetachNode(&senderNode);
    senderNode.Close();
    senderNode.Release();
} // end NormSession::DeleteRemoteSender()

bool NormSession::PreallocateRemoteSender(unsigned int bufferSpace,
                                          UINT16 segmentSize,
                                          UINT16 numData,
                                          UINT16 numParity,
                                          unsigned int streamBufferSize)
{
    if (NULL != preset_sender)
        delete preset_sender;
    preset_sender = new NormSenderNode(*this, NORM_NODE_ANY);
    if (!preset_sender->Open(0))
    {
        PLOG(PL_ERROR, "NormSession::PreallocateRemoteSender() error: NormSenderNode::Open() failure!\n");
        delete preset_sender;
        preset_sender = NULL;
        return false;
    }
    UINT16 blockSize = numData + numParity;
    UINT8 fecId;
    UINT8 fecM = 8;
    if (blockSize > 255)
    {
        fecId = 2;
        fecM = 16;
    }
    else
    {
        fecId = 5;
    }
    if (!preset_sender->AllocateBuffers(bufferSpace, fecId, 0, fecM, segmentSize, numData, numParity))
    {
        PLOG(PL_ERROR, "NormSession::PreallocateRemoteSender() error: buffer allocation failure!\n");
        delete preset_sender;
        preset_sender = NULL;
        return false;
    }
    if (0 != streamBufferSize)
    {
        if (!preset_sender->PreallocateRxStream(streamBufferSize, segmentSize, numData, numParity))
        {
            PLOG(PL_ERROR, "NormSession::PreallocateRemoteSender() error: preset_stream allocation failure!\n");
            delete preset_sender;
            preset_sender = NULL;
            return false;
        }
    }
    return true;
} // end NormSession::PreallocateRemoteSender()

bool NormSession::SetPresetFtiData(unsigned int objectSize,
                                   UINT16 segmentSize,
                                   UINT16 numData,
                                   UINT16 numParity)
{
    UINT16 blockSize = numData + numParity;
    UINT8 fecM;
    if (blockSize > 255)
        fecM = 16;
    else
        fecM = 8;
    if (IsSender())
    {
        if ((segmentSize != segment_size) ||
            (numData != ndata) ||
            (numParity != nparity) ||
            (fecM != fec_m))
        {
            PLOG(PL_FATAL, "NormSession::SetPresetFtiData() sender FTI mismatch error!\n");
            return false;
        }
    }
    preset_fti.SetObjectSize(NormObjectSize(objectSize));
    preset_fti.SetSegmentSize(segmentSize);
    preset_fti.SetFecMaxBlockLen(numData);
    preset_fti.SetFecNumParity(numParity);
    preset_fti.SetFecFieldSize(fecM);
    preset_fti.SetFecInstanceId(0);
    return true;
} // end NormSession::SetPresetFtiData()

void NormSession::Serve()
{
    // Only send new data when no other messages are queued for transmission
    if (!message_queue.IsEmpty())
    {
        ASSERT(tx_timer.IsActive());
        return;
    }

    // Queue next sender message
    NormObjectId objectId;
    NormObject *obj = NULL;
    if (SenderGetFirstPending(objectId))
    {
        obj = tx_table.Find(objectId);
        ASSERT(NULL != obj);
    }

    // If any app-defined command is pending, enqueue it for transmission
    if ((0 != cmd_count) && !(cmd_timer.IsActive()))
    {
        // If command is enqueued
        if (SenderQueueAppCmd())
            return;
    }

    bool watermarkJustCompleted = false;
    if (watermark_pending && !flush_timer.IsActive())
    {
        PLOG(PL_DEBUG, "NormSession::Serve() watermark status check ...\n");
        // Determine next message (objectId::blockId::segmentId) to be sent
        NormObject *nextObj;
        NormObjectId nextObjectId = next_tx_object_id;
        NormBlockId nextBlockId = 0;
        NormSegmentId nextSegmentId = 0;
        if (obj)
        {
            // Get index (objectId::blockId::segmentId) of next transmit pending segment
            nextObj = obj;
            nextObjectId = objectId;
            if (nextObj->IsPending())
            {
                if (nextObj->GetFirstPending(nextBlockId))
                {
                    NormBlock *block = nextObj->FindBlock(nextBlockId);
                    if (block)
                    {
                        block->GetFirstPending(nextSegmentId);
                        // Adjust so watermark segmentId < block length
                        UINT16 nextBlockSize = nextObj->GetBlockSize(nextBlockId);
                        if (nextSegmentId >= nextBlockSize)
                            nextSegmentId = nextBlockSize - 1;
                    }
                }
                else
                {
                    // info only pending; so blockId = segmentId = 0 (as inited)
                }
            }
            else
            {
                // Must be an active, but non-pending stream object
                ASSERT(nextObj->IsStream());
                nextBlockId = static_cast<NormStreamObject *>(nextObj)->GetNextBlockId();
                nextSegmentId = static_cast<NormStreamObject *>(nextObj)->GetNextSegmentId();
            }
        }
        PLOG(PL_DEBUG, "   nextPending index>%hu:%lu:%hu\n",
             (UINT16)nextObjectId,
             (unsigned long)nextBlockId.GetValue(),
             (UINT16)nextSegmentId);

        if (tx_repair_pending)
        {

            PLOG(PL_DEBUG, "   tx_repair index>%hu:%lu:%hu\n",
                 (UINT16)tx_repair_object_min,
                 (unsigned long)tx_repair_block_min.GetValue(),
                 (UINT16)tx_repair_segment_min);
            if ((tx_repair_object_min < nextObjectId) ||
                ((tx_repair_object_min == nextObjectId) &&
                 //((tx_repair_block_min < nextBlockId) ||
                 ((Compare(tx_repair_block_min, nextBlockId) < 0) ||
                  ((tx_repair_block_min == nextBlockId) &&
                   (tx_repair_segment_min < nextSegmentId)))))
            {
                nextObjectId = tx_repair_object_min;
                nextBlockId = tx_repair_block_min;
                nextSegmentId = tx_repair_segment_min;
                PLOG(PL_DEBUG, "   updated nextPending index>%hu:%lu:%hu\n",
                     (UINT16)nextObjectId,
                     (unsigned long)nextBlockId.GetValue(),
                     (UINT16)nextSegmentId);
            }
        } // end if (tx_repair_pending)

        ASSERT(nextBlockId.GetValue() <= (UINT32)0x00ffffff);

        PLOG(PL_DEBUG, "   watermark>%hu:%lu:%hu check against next pending index>%hu:%lu:%hu\n",
             (UINT16)watermark_object_id, (unsigned long)watermark_block_id.GetValue(), (UINT16)watermark_segment_id,
             (UINT16)nextObjectId, (unsigned long)nextBlockId.GetValue(), (UINT16)nextSegmentId);
        if ((nextObjectId > watermark_object_id) ||
            ((nextObjectId == watermark_object_id) &&
             //((nextBlockId > watermark_block_id) ||
             ((Compare(nextBlockId, watermark_block_id) > 0) ||
              ((nextBlockId == watermark_block_id) &&
               (nextSegmentId > watermark_segment_id)))))
        {
            PLOG(PL_DEBUG, "   calling SenderQueueWatermarkFlush() ...\n");
            // The sender tx position is > watermark
            if (SenderQueueWatermarkFlush())
            {
                watermark_active = true;
                return;
            }
            else
            {
                // (TBD) optionally return here to have ack collection temporarily
                // suspend forward progress of data transmission
                //return;

                // If the app has set the property to "truncated_flushing" because
                // there is explicit positive acknowledge from everyone in the group
                // (or unicast destination), we can safely provide an _early_
                // termination of flushing at this point iff:
                //
                // "(false == watermark_pending) &&
                //  (NULL == obj) &&
                //  (watermark_object_id == last_tx_object_id)
                //
                // These conditions indicate that the watermark ack has completed,
                // there is no more data to send, _and_ the watermark was the
                // ordinally highest object that has been enqueued ....
                if (!watermark_pending)
                {
                    // watermark flush just completed
                    if (watermark_flushes)
                        flush_count = GetTxRobustFactor();
                    watermarkJustCompleted = true;
                }
            }
        }
        else
        {
            // The sender tx position is < watermark
            // Reset non-acked acking nodes since sender has rewound
            // TBD - notify application that watermark ack has been reset ???
            if (watermark_active)
            {
                watermark_active = false;
                NormNodeTreeIterator iterator(acking_node_tree);
                NormAckingNode *next;
                while ((next = static_cast<NormAckingNode *>(iterator.GetNextNode())))
                    next->ResetReqCount(GetTxRobustFactor());
            }
        }
    } // end if (watermark_pending && !flush_timer.IsActive())

    if (NULL != obj)
    {
        NormObjectMsg *msg = (NormObjectMsg *)GetMessageFromPool();
        if (msg)
        {
            if (obj->NextSenderMsg(msg))
            {
                if (cc_enable && !data_active)
                {
                    data_active = true;
                    if (probe_timer.IsActive())
                    {
                        double elapsed = probe_timer.GetInterval() - probe_timer.GetTimeRemaining();
                        double probeInterval = GetProbeInterval();
                        if (elapsed > probeInterval)
                            probe_timer.SetInterval(0.0);
                        else
                            probe_timer.SetInterval(probeInterval - elapsed);
                        probe_timer.Reschedule();
                    }
                }
                msg->SetDestination(address);
                msg->SetGrtt(grtt_quantized);
                msg->SetBackoffFactor((unsigned char)backoff_factor);
                msg->SetGroupSize(gsize_quantized);
                QueueMessage(msg);
                flush_count = 0;
                // (TBD) ??? should streams every allowed to be non-pending?
                //       we _could_ re-architect streams a little bit and allow
                //       for this by having NormStreamObject::Write() control
                //       stream advancement ... I think it would be cleaner.
                //       (mod NormStreamObject::StreamAdvance() to depend upon
                //        what has been written and conversely set some pending
                //        state as calls to NormStreamObject::Write() are made.
                if (!obj->IsPending() && !obj->IsStream())
                {
                    tx_pending_mask.Unset(obj->GetId());
                    if (!tx_pending_mask.IsSet() && !posted_tx_queue_empty)
                    {
                        // Tell the app we would like to send more data ...
                        posted_tx_queue_empty = true;
                        Notify(NormController::TX_QUEUE_EMPTY, (NormSenderNode *)NULL, (NormObject *)NULL);
                        // (TBD) Was session deleted?
                    }
                }
            }
            else
            {
                ReturnMessageToPool(msg);
                if (obj->IsStream())
                {
                    NormStreamObject *stream = static_cast<NormStreamObject *>(obj);
                    if (stream->IsFlushPending() || stream->IsClosing())
                    {
                        // Queue flush message
                        if (!flush_timer.IsActive())
                        {
                            if ((GetTxRobustFactor() < 0) || (flush_count < GetTxRobustFactor()))
                            {
                                SenderQueueFlush();
                            }
                            else if (GetTxRobustFactor() == flush_count)
                            {

                                PLOG(PL_TRACE, "NormSession::Serve() node>%lu sender stream flush complete ...\n",
                                     (unsigned long)LocalNodeId());
                                Notify(NormController::TX_FLUSH_COMPLETED, (NormSenderNode *)NULL, stream);
                                flush_count++;
                                data_active = false;
                                if (stream->IsClosing())
                                {
                                    // If the stream just failed end-of-stream watermark flush, we don't
                                    // close the stream yet, but instead give app chance to reset watermark
                                    bool watermarkFailed = watermarkJustCompleted && (obj->GetId() == watermark_object_id) &&
                                                           (ACK_FAILURE == SenderGetAckingStatus(NORM_NODE_ANY));
                                    if (!watermarkFailed)
                                    {
                                        // end of stream was successfully acknowledged
                                        stream->Close();
                                        DeleteTxObject(stream, true);
                                        obj = NULL;
                                    }
                                }
                            }
                        }
                    }
                    //ASSERT(stream->IsPending() || stream->IsRepairPending() || stream->IsClosing());
                    if (!posted_tx_queue_empty && !stream->IsClosing() && stream->IsPending())
                    // post if pending || !repair_timer.IsActive() || (repair_timer.GetRepeatCount() == 0) ???
                    {
                        //data_active = false;
                        posted_tx_queue_empty = true;
                        Notify(NormController::TX_QUEUE_EMPTY, (NormSenderNode *)NULL, obj);
                        // (TBD) Was session deleted?
                        return;
                    }
                }
                else
                {
                    PLOG(PL_ERROR, "NormSession::Serve() pending non-stream obj, no message?.\n");
                    //ASSERT(repair_timer.IsActive());
                }
            }
        }
        else
        {
            PLOG(PL_ERROR, "NormSession::Serve() node>%lu Warning! message_pool empty.\n",
                 (unsigned long)LocalNodeId());
        }
    }
    else
    {
        // No pending objects or positive acknowledgement request
        if ((GetTxRobustFactor() < 0) || (flush_count < GetTxRobustFactor()))
        {
            // Queue flush message
            if (!tx_repair_pending) // don't queue flush if repair pending
                SenderQueueFlush();
            else
                PLOG(PL_DETAIL, "NormSession::Serve() node>%lu NORM_CMD(FLUSH) deferred by pending repairs ...\n",
                     (unsigned long)LocalNodeId());
        }
        else if (GetTxRobustFactor() == flush_count)
        {
            PLOG(PL_TRACE, "NormSession::Serve() node>%lu sender flush complete ...\n",
                 (unsigned long)LocalNodeId());
            Notify(NormController::TX_FLUSH_COMPLETED,
                   (NormSenderNode *)NULL,
                   (NormObject *)NULL);
            flush_count++;
            data_active = false;
        }
    }
} // end NormSession::Serve()

bool NormSession::SenderSetWatermark(NormObjectId objectId,
                                     NormBlockId blockId,
                                     NormSegmentId segmentId,
                                     bool overrideFlush,
                                     const char *appAckReq,
                                     unsigned int appAckReqLen)
{
    PLOG(PL_DEBUG, "NormSession::SenderSetWatermark() watermark>%hu:%lu:%hu\n",
         (UINT16)objectId, (unsigned long)blockId.GetValue(), (UINT16)segmentId);
    watermark_flushes = overrideFlush;
    watermark_pending = true;
    watermark_active = false;
    watermark_object_id = objectId;
    watermark_block_id = blockId;
    watermark_segment_id = segmentId;
    acking_success_count = 0;
    // Reset acking_node_list
    NormNodeTreeIterator iterator(acking_node_tree);
    NormNode *next;
    int robustFactor = GetTxRobustFactor();
    while ((next = iterator.GetNextNode()))
        static_cast<NormAckingNode *>(next)->Reset(robustFactor);

    if (NULL != appAckReq)
    {
        if (appAckReqLen != ack_ex_length)
        {
            if (NULL != ack_ex_buffer)
            {
                delete[] ack_ex_buffer;
                ack_ex_buffer = NULL;
                ack_ex_length = 0;
            }
            // Make sure there is room left  for at least one acker NormNodeId in
            if ((NormHeaderExtension::GetContentOffset() + appAckReqLen) > (segment_size - sizeof(NormNodeId)))
            {
                PLOG(PL_ERROR, "NormSession::SenderSetWatermark() error: application-defined ACK_REQ content too large!\n");
                watermark_pending = false;
                return false;
            }
            else if (NULL == (ack_ex_buffer = new char[appAckReqLen]))
            {
                PLOG(PL_ERROR, "NormSession::SenderSetWatermark() new app_req_buffer error: %s\n", GetErrorString());
                watermark_pending = false;
                return false;
            }
        }
        memcpy(ack_ex_buffer, appAckReq, appAckReqLen);
        ack_ex_length = appAckReqLen;
    }
    else if (NULL != ack_ex_buffer)
    {
        delete[] ack_ex_buffer;
        ack_ex_buffer = NULL;
        ack_ex_length = 0;
    }
    PromptSender();
    return true;
} // end NormSession::SenderSetWatermark()

void NormSession::SenderResetWatermark()
{
    NormNodeTreeIterator iterator(acking_node_tree);
    NormNode *next;
    int robustFactor = GetTxRobustFactor();
    while ((next = iterator.GetNextNode()))
    {
        NormAckingNode *node = static_cast<NormAckingNode *>(next);
        if ((NORM_NODE_NONE == node->GetId()) || (!node->AckReceived()))
        {
            node->Reset(robustFactor);
            watermark_pending = true;
            watermark_active = false;
        }
    }
    PromptSender();
} // end NormSession::SenderResetWatermark()

void NormSession::SenderCancelWatermark()
{
    watermark_pending = false;
} // end NormSession::SenderCancelWatermark()

NormAckingNode *NormSession::SenderAddAckingNode(NormNodeId nodeId, const ProtoAddress *srcAddress)
{
    NormAckingNode *theNode = static_cast<NormAckingNode *>(acking_node_tree.FindNodeById(nodeId));
    if (NULL == theNode)
    {
        theNode = new NormAckingNode(*this, nodeId);
        if (NULL != theNode)
        {
            theNode->Reset(GetTxRobustFactor());
            acking_node_tree.AttachNode(theNode);
            acking_node_count++;
        }
        else
        {
            PLOG(PL_ERROR, "NormSession::SenderAddAckingNode() new NormAckingNode error: %s\n", GetErrorString());
            return NULL;
        }
    }
    else
    {
        PLOG(PL_WARN, "NormSession::SenderAddAckingNode() warning: node already in list!?\n");
    }
    if (NULL != srcAddress)
        theNode->SetAddress(*srcAddress);
    return theNode;
} // end NormSession::AddAckingNode(NormNodeId nodeId)

void NormSession::SenderRemoveAckingNode(NormNodeId nodeId)
{
    NormAckingNode *theNode =
        static_cast<NormAckingNode *>(acking_node_tree.FindNodeById(nodeId));
    if (NULL != theNode)
    {
        acking_node_tree.DetachNode(theNode);
        theNode->Release();
        // TBD - if a watermark was pending and this is the only
        //       non-pending acker, can we immediately issue WATERMARK_COMPLETED?
        acking_node_count--;
    }
} // end NormSession::RemoveAckingNode()

NormSession::AckingStatus NormSession::SenderGetAckingStatus(NormNodeId nodeId)
{
    if (NORM_NODE_ANY == nodeId)
    {
        // Return result based on overall success of acking process
        if (watermark_pending)
        {
            return ACK_PENDING;
        }
        else
        {
            if (acking_success_count < acking_node_count)
                return ACK_FAILURE;
            else
                return ACK_SUCCESS;
        }
    }
    else
    {
        NormAckingNode *theNode =
            static_cast<NormAckingNode *>(acking_node_tree.FindNodeById(nodeId));
        if (NULL != theNode)
        {
            if (theNode->IsPending())
                return ACK_PENDING;
            else if (NORM_NODE_NONE == theNode->GetId())
                return ACK_SUCCESS;
            else if (theNode->AckReceived())
                return ACK_SUCCESS;
            else
                return ACK_FAILURE;
        }
        else
        {
            return ACK_INVALID;
        }
    }
} // end NormSession::SenderGetAckingStatus()

bool NormSession::SenderGetNextAckingNode(NormNodeId &prevNodeId, AckingStatus *ackingStatus)
{
    NormNode *prevNode = NULL;
    if (NORM_NODE_NONE != prevNodeId)
        prevNode = acking_node_tree.FindNodeById(prevNodeId);
    NormNodeTreeIterator iterator(acking_node_tree, prevNode);
    NormAckingNode *nextNode = static_cast<NormAckingNode *>(iterator.GetNextNode());
    // Note we skip NORM_NODE_NONE even though it may be in the tree
    // (This method only returns the id / status of _actual_ nodes)
    // TBD - we could return NORM_NODE_ANY as a proxy id for a NORM_NODE_NONE entry
    if ((NULL != nextNode) && (NORM_NODE_NONE == nextNode->GetId()))
        nextNode = static_cast<NormAckingNode *>(iterator.GetNextNode());
    if (NULL != nextNode)
    {
        prevNodeId = nextNode->GetId();
        if (NULL != ackingStatus)
        {
            if (nextNode->IsPending())
                *ackingStatus = ACK_PENDING;
            else if (NORM_NODE_NONE == nextNode->GetId())
                *ackingStatus = ACK_SUCCESS;
            else if (nextNode->AckReceived())
                *ackingStatus = ACK_SUCCESS;
            else
                *ackingStatus = ACK_FAILURE;
        }
        return true;
    }
    else
    {
        prevNodeId = NORM_NODE_NONE;
        if (NULL != ackingStatus)
            *ackingStatus = ACK_INVALID;
        return false;
    }
} // end NormSession::SenderGetNextAckingNode()

bool NormSession::SenderGetAckEx(NormNodeId nodeId, char *buffer, unsigned int *buflen)
{
    NormAckingNode *theNode =
        static_cast<NormAckingNode *>(acking_node_tree.FindNodeById(nodeId));
    if (NULL != theNode)
    {
        return theNode->GetAckEx(buffer, buflen);
    }
    else
    {
        if (NULL != buflen)
            *buflen = 0;
        return false;
    }
} // end NormSession::SenderGetAckEx()

bool NormSession::SenderQueueWatermarkFlush()
{
    if (flush_timer.IsActive())
        return false;
    NormCmdFlushMsg *flush = static_cast<NormCmdFlushMsg *>(GetMessageFromPool());
    if (flush)
    {
        flush->Init();

        flush->SetDestination(address);
        flush->SetGrtt(grtt_quantized);
        flush->SetBackoffFactor((unsigned char)backoff_factor);
        flush->SetGroupSize(gsize_quantized);
        flush->SetObjectId(watermark_object_id);
        // _Attempt_ to set the fec_payload_id source block length field appropriately
        UINT16 blockLen;
        NormObject *obj = tx_table.Find(watermark_object_id);
        if (NULL != obj)
            blockLen = obj->GetBlockSize(watermark_block_id);
        else if (watermark_segment_id < ndata)
            blockLen = ndata;
        else
            blockLen = watermark_segment_id;
        flush->SetFecPayloadId(fec_id, watermark_block_id.GetValue(), watermark_segment_id, blockLen, fec_m);

        if (0 != ack_ex_length)
        {
            NormAppAckExtension ext;
            flush->AttachExtension(ext);
            ext.SetContent(ack_ex_buffer, ack_ex_length);
            flush->PackExtension(ext);
        }

        NormNodeTreeIterator iterator(acking_node_tree);
        NormAckingNode *next;
        watermark_pending = false;
        NormAckingNode *nodeNone = NULL;
        acking_success_count = 0;
        while (NULL != (next = static_cast<NormAckingNode *>(iterator.GetNextNode())))
        {
            // Save NORM_NODE_NONE for last
            if (NORM_NODE_NONE == next->GetId())
            {
                if (next->IsPending())
                    nodeNone = next;
                else
                    acking_success_count++; // implicit success for NORM_NODE_NONE
                continue;
            }
            if (next->AckReceived())
            {
                acking_success_count++; // ACK was received for this node
            }
            else if (next->IsPending())
            {
                // Add node to list
                if (flush->AppendAckingNode(next->GetId(), segment_size))
                {
                    next->DecrementReqCount();
                    watermark_pending = true;
                }
                else
                {
                    PLOG(PL_FATAL, "NormSession::ServeQueueWatermarkFlush() full cmd ...\n");
                    nodeNone = NULL;
                    break;
                }
            }
        }
        if (NULL != nodeNone)
        {
            if (flush->AppendAckingNode(NORM_NODE_NONE, segment_size))
            {
                nodeNone->DecrementReqCount();
                watermark_pending = true;
            }
            else
            {
                PLOG(PL_DEBUG, "NormSession::ServeQueueWatermarkFlush() full cmd ...\n");
            }
        }
        if (watermark_pending)
        {

            // (TBD) we should increment the "flush_count" here only iff the watermark
            // corresponds to our "last_tx_object_id", etc
            //if ((GetTxRobustFactor() < 0) || (flush_count < GetTxRobustFactor()))
            //    flush_count++;
            QueueMessage(flush);
            PLOG(PL_DEBUG, "NormSession::ServeQueueWatermarkFlush() node>%lu cmd queued ...\n",
                 (unsigned long)LocalNodeId());
        }
        else if (NULL != acking_node_tree.GetRoot())
        {
            ReturnMessageToPool(flush);
            PLOG(PL_DEBUG, "NormSession::ServeQueueWatermarkFlush() node>%lu watermark ack finished.\n",
                 (unsigned long)LocalNodeId());
            Notify(NormController::TX_WATERMARK_COMPLETED, (NormSenderNode *)NULL, (NormObject *)NULL);
            return false;
        }
        else
        {
            ReturnMessageToPool(flush);
            PLOG(PL_INFO, "NormSession::ServeQueueWatermarkFlush() node>%lu no acking nodes specified?!\n");
            return false;
        }
    }
    else
    {
        PLOG(PL_ERROR, "NormSession::SenderQueueWatermarkRequest() node>%lu message_pool exhausted! (couldn't req)\n",
             (unsigned long)LocalNodeId());
    }
    PLOG(PL_DEBUG, "NormSession::SenderQueueWatermarkFlush() starting flush timeout: %lf sec ....\n", 2 * grtt_advertised);
    flush_timer.SetInterval(2 * grtt_advertised);
    ActivateTimer(flush_timer);
    return true;
} // end NormSession::SenderQueueWatermarkFlush()

void NormSession::SenderQueueFlush()
{
    // (TBD) Don't enqueue a new flush if there is already one in our tx_queue!
    if (flush_timer.IsActive())
        return;
    NormObject *obj = tx_table.Find(tx_table.RangeHi());
    NormObjectId objectId;
    NormBlockId blockId;
    NormSegmentId segmentId;
    if (obj)
    {
        if (obj->IsStream())
        {
            NormStreamObject *stream = (NormStreamObject *)obj;
            objectId = stream->GetId();
            blockId = stream->FlushBlockId();
            segmentId = stream->FlushSegmentId();
        }
        else
        {
            objectId = obj->GetId();
            blockId = obj->GetFinalBlockId();
            segmentId = obj->GetBlockSize(blockId) - 1;
        }
        NormCmdFlushMsg *flush = (NormCmdFlushMsg *)GetMessageFromPool();
        if (flush)
        {
            flush->Init();
            flush->SetDestination(address);
            flush->SetGrtt(grtt_quantized);
            flush->SetBackoffFactor((unsigned char)backoff_factor);
            flush->SetGroupSize(gsize_quantized);
            flush->SetObjectId(objectId);

            flush->SetFecPayloadId(fec_id, blockId.GetValue(), segmentId, obj->GetBlockSize(blockId), fec_m);

            QueueMessage(flush);
            if ((GetTxRobustFactor() < 0) || (flush_count < GetTxRobustFactor()))
                flush_count++;
            PLOG(PL_DEBUG, "NormSession::SenderQueueFlush() node>%lu, flush queued (flush_count:%u)...\n",
                 (unsigned long)LocalNodeId(), flush_count);
        }
        else
        {
            PLOG(PL_ERROR, "NormSession::SenderQueueFlush() node>%lu message_pool exhausted! (couldn't flush)\n",
                 (unsigned long)LocalNodeId());
        }
    }
    else
    {
        // Why did I do this? - Brian // Because a squelch keeps the receivers from NACKing in futility
        // (TBD) send NORM_CMD(EOT) instead? - no
        // Perhaps I should send a flush anyway w/ (next_tx_object_id - 1) and squelch accordingly?
        // This condition shouldn't occur if we have state on the most recent object ... we should
        // unless the app does bad things like "cancel" all of its tx objects ...
        // Maybe we shouldn't send anything if we have no pending tx objects? No need to flush, etc
        // if all tx object state is gone ...
        if (SenderQueueSquelch(next_tx_object_id))
        {
            if ((GetTxRobustFactor() < 0) || (flush_count < GetTxRobustFactor()))
                flush_count++;
            PLOG(PL_DEBUG, "NormSession::SenderQueueFlush() node>%lu squelch queued (flush_count:%u)...\n",
                 (unsigned long)LocalNodeId(), flush_count);
        }
        else
        {
            PLOG(PL_ERROR, "NormSession::SenderQueueFlush() warning: node>%lu unable to queue squelch\n",
                 (unsigned long)LocalNodeId());
        }
    }
    PLOG(PL_DEBUG, "NormSession::SenderQueueFlush() starting flush timeout: %lf sec ....\n", 2 * grtt_advertised);
    flush_timer.SetInterval(2 * grtt_advertised);
    ActivateTimer(flush_timer);
} // end NormSession::SenderQueueFlush()

bool NormSession::OnFlushTimeout(ProtoTimer & /*theTimer*/)
{
    PLOG(PL_DEBUG, "NormSession::OnFlushTimeout() deactivating flush_timer ....\n");
    flush_timer.Deactivate();
    PromptSender();
    return false;
} // NormSession::OnFlushTimeout()

void NormSession::QueueMessage(NormMsg *msg)
{

    /* A little test jig
        static struct timeval lastTime = {0,0};
    struct timeval currentTime;
    ProtoSystemTime(currentTime);
    if (0 != lastTime.tv_sec)
    {
        double delta = currentTime.tv_sec - lastTime.tv_sec;
        delta += (((double)currentTime.tv_usec)*1.0e-06 -  
                  ((double)lastTime.tv_usec)*1.0e-06);
        DMSG(0, "NormSession::QueueMessage() deltaT:%lf\n", delta);
    }
    lastTime = currentTime;
*/
    // (TBD) if (0.0 == tx_rate), should we just dump the
    // message rather than queueing it?
    if (!tx_timer.IsActive() && (tx_rate > 0.0))
    {
        tx_timer.SetInterval(0.0);
        ActivateTimer(tx_timer);
    }
    if (NULL != msg)
        message_queue.Append(msg);
} // end NormSesssion::QueueMessage(NormMsg& msg)

NormFileObject *NormSession::QueueTxFile(const char *path,
                                         const char *infoPtr,
                                         UINT16 infoLen)
{
    if (!IsSender())
    {
        PLOG(PL_FATAL, "NormSession::QueueTxFile() Error: sender is closed\n");
        return NULL;
    }
    NormFileObject *file = new NormFileObject(*this, (NormSenderNode *)NULL, next_tx_object_id);
    if (NULL == file)
    {
        PLOG(PL_FATAL, "NormSession::QueueTxFile() new file object error: %s\n",
             GetErrorString());
        return NULL;
    }
    if (!file->Open(path, infoPtr, infoLen))
    {
        PLOG(PL_FATAL, "NormSession::QueueTxFile() file open error\n");
        file->Release();
        return NULL;
    }
    if (QueueTxObject(file))
    {
        return file;
    }
    else
    {
        file->Close();
        file->Release();
        return NULL;
    }
} // end NormSession::QueueTxFile()

NormDataObject *NormSession::QueueTxData(const char *dataPtr,
                                         UINT32 dataLen,
                                         const char *infoPtr,
                                         UINT16 infoLen)
{
    if (!IsSender())
    {
        PLOG(PL_FATAL, "NormSession::QueueTxData() Error: sender is closed\n");
        return NULL;
    }
    NormDataObject *obj = new NormDataObject(*this, (NormSenderNode *)NULL, next_tx_object_id, session_mgr.GetDataFreeFunction());
    if (!obj)
    {
        PLOG(PL_FATAL, "NormSession::QueueTxData() new data object error: %s\n",
             GetErrorString());
        return NULL;
    }
    if (!obj->Open((char *)dataPtr, dataLen, false, infoPtr, infoLen))
    {
        PLOG(PL_FATAL, "NormSession::QueueTxData() object open error\n");
        obj->Release();
        return NULL;
    }
    if (QueueTxObject(obj))
    {
        return obj;
    }
    else
    {
        obj->Close();
        obj->Release();
        return NULL;
    }
} // end NormSession::QueueTxData()

NormStreamObject *NormSession::QueueTxStream(UINT32 bufferSize,
                                             bool doubleBuffer,
                                             const char *infoPtr,
                                             UINT16 infoLen)
{
    if (!IsSender())
    {
        PLOG(PL_FATAL, "NormSession::QueueTxStream() Error: sender is closed\n");
        return NULL;
    }
    NormStreamObject *stream = new NormStreamObject(*this, (NormSenderNode *)NULL, next_tx_object_id);
    if (!stream)
    {
        PLOG(PL_FATAL, "NormSession::QueueTxStream() new stream object error: %s\n",
             GetErrorString());
        return NULL;
    }
    if (!stream->Open(bufferSize, doubleBuffer, infoPtr, infoLen))
    {
        PLOG(PL_FATAL, "NormSession::QueueTxStream() stream open error\n");
        stream->Release();
        return NULL;
    }
    if (QueueTxObject(stream))
    {
        // (???: stream has nothing pending until user writes to it???)
        //stream->Reset();
        return stream;
    }
    else
    {
        stream->Close();
        stream->Release();
        return NULL;
    }
} // end NormSession::QueueTxStream()

#ifdef SIMULATE
NormSimObject *NormSession::QueueTxSim(unsigned long objectSize)
{
    if (!IsSender())
    {
        PLOG(PL_FATAL, "NormSession::QueueTxSim() Error: sender is closed\n");
        return NULL;
    }
    NormSimObject *simObject = new NormSimObject(*this, NULL, next_tx_object_id);
    if (!simObject)
    {
        PLOG(PL_FATAL, "NormSession::QueueTxSim() new sim object error: %s\n",
             GetErrorString());
        return NULL;
    }

    if (!simObject->Open(objectSize))
    {
        PLOG(PL_FATAL, "NormSession::QueueTxSim() open error\n");
        simObject->Release();
        return NULL;
    }
    if (QueueTxObject(simObject))
    {
        return simObject;
    }
    else
    {
        simObject->Release();
        return NULL;
    }
} // end NormSession::QueueTxSim()
#endif // SIMULATE

bool NormSession::QueueTxObject(NormObject *obj)
{
    if (!IsSender())
    {
        PLOG(PL_FATAL, "NormSession::QueueTxObject() non-sender session error!?\n");
        return false;
    }

    if (preset_fti.IsValid() && (obj->GetSize() != preset_fti.GetObjectSize()))
    {
        PLOG(PL_FATAL, "NormSession::QueueTxObject() preset object info mismatch!\n");
        return false;
    }

    // Manage tx_table min/max count and max size bounds
    // Depending on tx cache bounds _and_ what has been
    // enqueued/dequeued, we may need to prune the
    // "tx_table" a little
    // The cases when pruning is needed include:
    //
    // 1) When the cache bounds dictate:
    //      i.e., ((count > count_min) && ((count > count_max) || (size > size_max))), or
    // 2) When the "tx_table" state (from insert/remove history) doesn't allow
    //      i.e., !tx_table.CanInsert(obj)
    unsigned long newCount = tx_table.GetCount() + 1;
    while (!tx_table.CanInsert(obj->GetId()) ||
           ((newCount > tx_cache_count_min) &&
            ((newCount > tx_cache_count_max) ||
             ((tx_table.GetSize() + obj->GetSize()) > tx_cache_size_max))))
    {
        // Remove oldest non-pending
        NormObject *oldest = tx_table.Find(tx_table.RangeLo());
        if (oldest->IsRepairPending() || oldest->IsPending())
        {
            PLOG(PL_DEBUG, "NormSession::QueueTxObject() all held objects repair pending:%d (repair active:%d) pending:%d\n",
                 oldest->IsRepairPending(), repair_timer.IsActive(), oldest->IsPending());
            posted_tx_queue_empty = false;
            return false;
        }
        else
        {
            double delay = GetFlowControlDelay() - oldest->GetNackAge();
            if (delay < 1.0e-06)
            {
                if (FlowControlIsActive())
                    DeactivateFlowControl();
                DeleteTxObject(oldest, true);
            }
            else
            {
                PLOG(PL_DEBUG, "NormSession::QueueTxObject() asserting flow control for object delay:%lf sec\n", delay);
                // TBD - flow control as should allow for TX_QUEUE_VACANCY posting for session
                ActivateFlowControl(delay, oldest->GetId(), NormController::TX_QUEUE_EMPTY);
                posted_tx_queue_empty = false;
                return false;
            }
        }
        newCount = tx_table.GetCount() + 1;
    }
    // Attempt to queue the object (note it gets "retained" by the tx_table)
    if (!tx_table.Insert(obj))
    {
        PLOG(PL_FATAL, "NormSession::QueueTxObject() tx_table insert error\n");
        ASSERT(0);
        return false;
    }
    tx_pending_mask.Set(obj->GetId());
    ASSERT(tx_pending_mask.Test(obj->GetId()));
    next_tx_object_id++;
    TouchSender();
    return true;
} // end NormSession::QueueTxObject()

bool NormSession::RequeueTxObject(NormObject *obj)
{
    ASSERT(NULL != obj);
    if (obj->IsStream())
    {
        // (TBD) allow buffered stream to be reset?
        PLOG(PL_FATAL, "NormSession::RequeueTxObject() error: can't requeue NORM_OBJECT_STREAM\n");
        return false;
    }
    NormObjectId objectId = obj->GetId();
    if (tx_table.Find(objectId) == obj)
    {
        if (tx_pending_mask.Set(objectId))
        {
            obj->TxReset(0, true);
            TouchSender();
            return true;
        }
        else
        {
            PLOG(PL_FATAL, "NormSession::RequeueTxObject() error: couldn't set object as pending\n");
            return false;
        }
    }
    else
    {
        PLOG(PL_FATAL, "NormSession::RequeueTxObject() error: couldn't find object\n");
        return false;
    }
} // end NormSession::RequeueTxObject()

void NormSession::DeleteTxObject(NormObject *obj, bool notify)
{
    ASSERT(NULL != obj);
    if (tx_table.Remove(obj))
    {
        Notify(NormController::TX_OBJECT_PURGED, (NormSenderNode *)NULL, obj);
        NormObjectId objectId = obj->GetId();
        tx_pending_mask.Unset(objectId);
        tx_repair_mask.Unset(objectId);
        obj->Close();
        obj->Release();
    }
} // end NormSession::DeleteTxObject()

bool NormSession::SetTxCacheBounds(NormObjectSize sizeMax,
                                   unsigned long countMin,
                                   unsigned long countMax)
{
    bool result = true;
    tx_cache_size_max = sizeMax;
    tx_cache_count_min = (unsigned int)((countMin < countMax) ? countMin : countMax);
    if (tx_cache_count_min < 1)
        tx_cache_count_min = 1;
    tx_cache_count_max = (unsigned int)((countMax > countMin) ? countMax : countMin);
    if (tx_cache_count_max < 1)
        tx_cache_count_max = 1;

    tx_cache_count_min &= 0x00007fff; // limited to one-half of 16-bit NormObjectId space
    tx_cache_count_max &= 0x00007fff;

    if (IsSender())
    {
        // Trim/resize the tx_table and tx masks as needed
        unsigned long count = tx_table.GetCount();
        while ((count >= tx_cache_count_min) &&
               ((count > tx_cache_count_max) ||
                (tx_table.GetSize() > tx_cache_size_max)))
        {
            // Remove oldest (hopefully non-pending ) object
            NormObject *oldest = tx_table.Find(tx_table.RangeLo());
            ASSERT(NULL != oldest);
            DeleteTxObject(oldest, true);
            count = tx_table.GetCount();
        }
        if (tx_cache_count_max < DEFAULT_TX_CACHE_MAX)
            countMax = DEFAULT_TX_CACHE_MAX;
        else
            countMax = tx_cache_count_max;
        if (countMax != tx_table.GetRangeMax())
        {
            tx_table.SetRangeMax((UINT16)countMax);
            result = tx_pending_mask.Resize((UINT32)countMax);
            result &= tx_repair_mask.Resize((UINT32)countMax);
            if (!result)
            {
                countMax = tx_pending_mask.GetSize();
                if (tx_repair_mask.GetSize() < countMax)
                    countMax = tx_repair_mask.GetSize();
                if (tx_cache_count_max > countMax)
                    tx_cache_count_max = (unsigned int)countMax;
                if (tx_cache_count_min > tx_cache_count_max)
                    tx_cache_count_min = tx_cache_count_max;
            }
        }
    }
    return result;
} // end NormSession::SetTxCacheBounds()

NormBlock *NormSession::SenderGetFreeBlock(NormObjectId objectId,
                                           NormBlockId blockId)
{
    // First, try to get one from our block pool
    NormBlock *b = block_pool.Get();
    // Second, try to steal oldest non-pending block
    if (!b)
    {
        NormObjectTable::Iterator iterator(tx_table);
        NormObject *obj;
        while ((obj = iterator.GetNextObject()))
        {
            if (obj->GetId() == objectId)
                b = obj->StealNonPendingBlock(true, blockId);
            else
                b = obj->StealNonPendingBlock(false);
            if (b)
            {
                b->EmptyToPool(segment_pool);
                break;
            }
        }
    }
    // Finally, try to steal newer pending block
    if (!b)
    {
        // reverse iteration to find newest object with resources
        NormObjectTable::Iterator iterator(tx_table);
        NormObject *obj;
        while ((obj = iterator.GetPrevObject()))
        {
            if (obj->GetId() < objectId)
            {
                break;
            }
            else
            {
                if (obj->GetId() > objectId)
                    b = obj->StealNewestBlock(false);
                else
                    b = obj->StealNewestBlock(true, blockId);
                if (b)
                {
                    b->EmptyToPool(segment_pool);
                    break;
                }
            }
        }
    }
    return b;
} // end NormSession::SenderGetFreeBlock()

char *NormSession::SenderGetFreeSegment(NormObjectId objectId,
                                        NormBlockId blockId)
{
    while (segment_pool.IsEmpty())
    {
        NormBlock *b = SenderGetFreeBlock(objectId, blockId);
        if (b)
            block_pool.Put(b);
        else
            return NULL;
    }
    return segment_pool.Get();
} // end NormSession::SenderGetFreeSegment()

void NormSession::TxSocketRecvHandler(ProtoSocket &theSocket,
                                      ProtoSocket::Event theEvent)
{
    if (ProtoSocket::RECV == theEvent)
    {
        NormMsg msg;
        unsigned int msgLength = NormMsg::MAX_SIZE;
        while (true)
        {

            if (theSocket.RecvFrom(msg.AccessBuffer(),
                                   msgLength,
                                   msg.AccessAddress()))
            {
                if (0 == msgLength)
                    break; // no more data to read
                if (msg.InitFromBuffer(msgLength))
                {
                    // Since it arrived on the tx_socket, we know it was unicast
                    HandleReceiveMessage(msg, true);
                    msgLength = NormMsg::MAX_SIZE;
                }
                else
                {
                    PLOG(PL_ERROR, "NormSession::TxSocketRecvHandler() warning: received bad message\n");
                }
            }
            else
            {
                // Probably an ICMP "port unreachable" error
                // Note we purposefull do _not_ set the "posted_send_error"
                // status here because we do not want this notification
                // cleared due to SEND_OK status since it's receiver driven
                if (Address().IsUnicast())
                    Notify(NormController::SEND_ERROR, NULL, NULL);
                break;
            }
        }
    }
    else if (ProtoSocket::SEND == theEvent)
    {
        // This is a little cheesy, but ...
        theSocket.StopOutputNotification();
        if (tx_timer.IsActive())
            tx_timer.Deactivate();
        if (OnTxTimeout(tx_timer))
        {
            if (!tx_timer.IsActive())
                ActivateTimer(tx_timer);
        }
    }
} // end NormSession::TxSocketRecvHandler()

//#define RX_MEASURE_ONLY
#ifdef RX_MEASURE_ONLY
static bool rxMeasureInit = true;
struct timeval rxMeasureRefTime;
unsigned int rxMeasurePktCount = 0;
unsigned int rxMeasurePktTotal = 0;
unsigned int rxMeasureByteTotal = 0;
UINT16 rxMeasureSeqPrev = 0;
int rxMeasureGapMax = 0;
#endif // RX_MEASURE_ONLY

void NormSession::RxSocketRecvHandler(ProtoSocket &theSocket,
                                      ProtoSocket::Event theEvent)
{
    if (ProtoSocket::RECV == theEvent)
    {
        unsigned int recvCount = 0;
        NormMsg msg;
        unsigned int msgLength = NormMsg::MAX_SIZE;
        while (true)
        {
            ProtoAddress destAddr; // we get the pkt destAddr to determine unicast/multicast
            if (theSocket.RecvFrom(msg.AccessBuffer(),
                                   msgLength,
                                   msg.AccessAddress(),
                                   destAddr))
            {
                if (0 == msgLength)
                    break;
                if (msg.InitFromBuffer(msgLength))
                {
#ifdef RX_MEASURE_ONLY
                    // Measure rx rate / loss stats only
                    struct timeval currentTime;
                    ProtoSystemTime(currentTime);
                    UINT16 seq = msg.GetSequence();
                    if (rxMeasureInit)
                    {
                        rxMeasureRefTime = currentTime;
                        rxMeasureSeqPrev = seq;
                        rxMeasurePktCount = rxMeasurePktTotal = 1;
                        rxMeasureByteTotal = msgLength;
                        rxMeasureInit = false;
                        return;
                    }
                    int seqDelta = (int)seq - (int)rxMeasureSeqPrev;
                    ASSERT(seqDelta > 0);

                    rxMeasurePktTotal += seqDelta; // total should have received.
                    rxMeasurePktCount++;           // total actually received
                    rxMeasureByteTotal += msgLength;

                    if (seqDelta > rxMeasureGapMax)
                        rxMeasureGapMax = seqDelta;

                    int deltaSec = currentTime.tv_sec - rxMeasureRefTime.tv_sec;
                    if (deltaSec >= 10)
                    {
                        if (currentTime.tv_usec > rxMeasureRefTime.tv_usec)
                            deltaSec += 1.0e-06 * (double)(currentTime.tv_usec - rxMeasureRefTime.tv_usec);
                        else
                            deltaSec -= 1.0e-06 * (double)(rxMeasureRefTime.tv_usec - currentTime.tv_usec);
                        double rxRate = (8.0 / 1000.0) * (double)rxMeasureByteTotal / (double)deltaSec;
                        double rxLoss = 100.0 * (1.0 - (double)rxMeasurePktCount / (double)rxMeasurePktTotal);

                        rxMeasureRefTime = currentTime;
                        rxMeasureByteTotal = rxMeasurePktCount = rxMeasurePktTotal = rxMeasureGapMax = 0;
                    }
                    rxMeasureSeqPrev = seq;
                    return;
#endif // RX_MEASURE_ONLY
                    bool ecnStatus = false;
#ifdef SIMULATE
                    ecnStatus = theSocket.GetEcnStatus();
#endif // SIMULATE
                    bool wasUnicast;
                    if (destAddr.IsValid())
                        wasUnicast = destAddr.IsUnicast();
                    else
                        wasUnicast = false;
                    HandleReceiveMessage(msg, wasUnicast, ecnStatus);
                    msgLength = NormMsg::MAX_SIZE;
                }
                else
                {
                    PLOG(PL_ERROR, "NormSession::RxSocketRecvHandler() warning: received bad message\n");
                }
                // If our system gets very busy reading sockets, we should occasionally
                // execute any timeouts to keep protocol operation smooth (i.e., sending feedback)
                // TBD - perhaps this should be time based
                if (++recvCount >= 100)
                {
                    break;
                    //session_mgr.DoSystemTimeout();
                    //recvCount = 0;
                }
            }
            else
            {
                // Probably an ICMP "port unreachable" error
                // Note we purposefull do _not_ set the "posted_send_error"
                // status here because we do not want this notification
                // cleared due to SEND_OK status since it's receiver driven
                if (Address().IsUnicast())
                {
                    Notify(NormController::SEND_ERROR, NULL, NULL);
                }
                break;
            }
        }
    }
    else if (ProtoSocket::SEND == theEvent)
    {
        // This is a little cheesy, but ...
        theSocket.StopOutputNotification();
        if (tx_timer.IsActive())
            tx_timer.Deactivate();
        if (OnTxTimeout(tx_timer))
        {
            if (!tx_timer.IsActive())
                ActivateTimer(tx_timer);
        }
    } // end if/else (theEvent == RECV/SEND)
} // end NormSession::RxSocketRecvHandler()

#ifdef ECN_SUPPORT
#ifndef SIMULATE
void NormSession::OnPktCapture(ProtoChannel &theChannel,
                               ProtoChannel::Notification notifyType)
{
    // We only care about NOTIFY_INPUT events (all we should get anyway)
    if (ProtoChannel::NOTIFY_INPUT != notifyType)
        return;
    while (1)
    {
        ProtoCap::Direction direction;
        // Note: We offset the buffer by 2 bytes since Ethernet header is 14 bytes
        //       (i.e. not a multiple of 4 (sizeof(UINT32))
        //       This gives us a properly aligned buffer for 32-bit aligned IP packets
        //      (The 256*sizeof(UINT32) bytes are for potential "smfPkt" message header use)
        const int BUFFER_MAX = 4096;
        UINT32 alignedBuffer[BUFFER_MAX / sizeof(UINT32)];
        UINT16 *ethBuffer = ((UINT16 *)alignedBuffer) + 1; // offset by 2-bytes so IP content is 32-bit aligned
        UINT32 *ipBuffer = alignedBuffer + 4;              // offset by ETHER header size + 2 bytes
        unsigned int numBytes = (sizeof(UINT32) * (BUFFER_MAX / sizeof(UINT32))) - 2;

        ProtoCap &cap = static_cast<ProtoCap &>(theChannel);

        if (!cap.Recv((char *)ethBuffer, numBytes, &direction))
        {
            PLOG(PL_ERROR, "NormSession::OnPktCapture() ProtoCap::Recv() error\n");
            break;
        }
        if (numBytes == 0)
            break; // no more packets to receive

        // Map ProtoPktETH instance into buffer and init for processing
        ProtoPktETH ethPkt((UINT32 *)((void *)ethBuffer), BUFFER_MAX - 2);
        if (!ethPkt.InitFromBuffer(numBytes))
        {
            PLOG(PL_ERROR, "NormSession::OnPktCapture() error: bad Ether frame\n");
            continue;
        }
        // Only process IP packets (skip others)
        UINT16 ethType = ethPkt.GetType();
        if ((ethType != 0x0800) && (ethType != 0x86dd))
            continue; // go read next packet
        // Map ProtoPktIP instance into buffer and init for processing.
        ProtoPktIP ipPkt(ipBuffer, BUFFER_MAX - 16);
        if (!ipPkt.InitFromBuffer(ethPkt.GetPayloadLength()))
        {
            PLOG(PL_ERROR, "NormSession::OnPktCapture() error: bad IP packet\n");
            continue;
        }

        // Does this packet match any of our valid destination addrs?
        ProtoAddress dstIp;
        ProtoAddress srcIp;
        ProtoSocket::EcnStatus ecnStatus = ProtoSocket::ECN_NONE;
        switch (ipPkt.GetVersion())
        {
        case 4:
        {
            ProtoPktIPv4 ip4Pkt(ipPkt);
            ip4Pkt.GetDstAddr(dstIp);
            ip4Pkt.GetSrcAddr(srcIp);
            ecnStatus = (ProtoSocket::EcnStatus)(ip4Pkt.GetTOS() & ProtoSocket::ECN_CE);
            break;
        }
        case 6:
        {
            ProtoPktIPv6 ip6Pkt(ipPkt);
            ip6Pkt.GetDstAddr(dstIp);
            ip6Pkt.GetSrcAddr(srcIp);
            ecnStatus = (ProtoSocket::EcnStatus)(ip6Pkt.GetTrafficClass() & ProtoSocket::ECN_CE);
            break;
        }
        default:
            PLOG(PL_ERROR, "NormSession::OnPktCapture() error: recvd IP packet w/ bad version number\n");
            continue; // go read next packet
        }
        if (!dst_addr_list.Contains(dstIp))
            continue; // not a matching dst addr, go read next packet
        // Is this a UDP packet for our session dst port?
        int dstPort = -1;
        ProtoPktUDP udpPkt;
        if (udpPkt.InitFromPacket(ipPkt))
            dstPort = udpPkt.GetDstPort();
        //if (dstPort != address.GetPort())
        if (dstPort != rx_socket.GetPort())
            continue; // not a UDP packet for our session, go read next packet
        // If our rx_socket is "connected", make sure source addr/port matches
        srcIp.SetPort(udpPkt.GetSrcPort());
        // if socket is connected, validate that the packet's from the specified source addr
        if (rx_connect_addr.IsValid())
        {
            if (0 != rx_connect_addr.GetPort())
            {
                // check host addr component only for match
                if (!rx_connect_addr.HostIsEqual(srcIp))
                    continue;
            }
            else
            {
                // check for addr _and_ port match
                if (!rx_connect_addr.IsEqual(srcIp))
                    continue;
            }
        }
        // if we are using SSM multicast make sure it's the right source addr
        if (ssm_source_addr.IsValid() && !ssm_source_addr.HostIsEqual(srcIp))
            continue;

        // IMPORTANT NOTE:  We ignore the checksum for OUTBOUND packets since these
        // are often computed by the Ethernet hardware these days
        if ((ProtoCap::INBOUND == direction) && !udpPkt.ChecksumIsValid(ipPkt))
        {
            PLOG(PL_WARN, "NormSession::OnPktCapture() error: recvd UDP packet w/ bad checksum: %04x (computed: %04x)\n",
                 (UINT16)udpPkt.GetChecksum(), udpPkt.ComputeChecksum(ipPkt));
            continue; // go read next packet
        }

        // TBD - we can avoid this copy
        NormMsg msg;
        if (msg.CopyFromBuffer((const char *)udpPkt.GetPayload(), udpPkt.GetPayloadLength()))
        {

            msg.AccessAddress() = srcIp;
            HandleReceiveMessage(msg, dstIp.IsUnicast(), (ProtoSocket::ECN_CE == ecnStatus));
        }
        else
        {
            PLOG(PL_WARN, "NormSession::OnPktCapture() error: recvd bad NORM packet?!\n");
        }
    } // end while(1)
} // end NormSession::OnPktCapture()
#endif // !SIMULATE
#endif // ECN_SUPPORT

// TBD - move this to its own cpp file???
void NormTrace(const struct timeval &currentTime,
               NormNodeId localId,
               const NormMsg &msg,
               bool sent,
               UINT8 fecM,
               UINT16 instId)
{
    static const char *MSG_NAME[] =
        {
            "INVALID",
            "INFO",
            "DATA",
            "CMD",
            "NACK",
            "ACK",
            "REPORT"};
    static const char *CMD_NAME[] =
        {
            "CMD(INVALID)",
            "CMD(FLUSH)",
            "CMD(EOT)",
            "CMD(SQUELCH)",
            "CMD(CC)",
            "CMD(REPAIR_ADV)",
            "CMD(ACK_REQ)",
            "CMD(APP)"};
    static const char *REQ_NAME[] =
        {
            "INVALID",
            "WATERMARK",
            "RTT",
            "APP"};

    NormMsg::Type msgType = msg.GetType();
    UINT16 length = msg.GetLength();
    const char *status = sent ? "dst" : "src";
    const ProtoAddress &addr = sent ? msg.GetDestination() : msg.GetSource();

    UINT16 seq = msg.GetSequence();

#ifdef _WIN32_WCE
    struct tm timeStruct;
    timeStruct.tm_hour = currentTime.tv_sec / 3600;
    unsigned long hourSecs = 3600 * timeStruct.tm_hour;
    timeStruct.tm_min = (currentTime.tv_sec - (hourSecs)) / 60;
    timeStruct.tm_sec = currentTime.tv_sec - (hourSecs) - (60 * timeStruct.tm_min);
    timeStruct.tm_hour = timeStruct.tm_hour % 24;
    struct tm *ct = &timeStruct;
#else
    time_t secs = (time_t)currentTime.tv_sec;
    struct tm *ct = gmtime(&secs);
#endif // if/else _WIN32_WCE
    PLOG(PL_ALWAYS, "trace>%02d:%02d:%02d.%06lu ",
         (int)ct->tm_hour, (int)ct->tm_min, (int)ct->tm_sec, (unsigned int)currentTime.tv_usec);
    PLOG(PL_ALWAYS, "node>%lu %s>%s/%hu ", (unsigned long)localId, status, addr.GetHostString(), addr.GetPort());

    bool clrFlag = false;
    switch (msgType)
    {
    case NormMsg::INFO:
    {
        const NormInfoMsg &info = (const NormInfoMsg &)msg;
        PLOG(PL_ALWAYS, "inst>%hu seq>%hu INFO obj>%hu ",
             info.GetInstanceId(), seq, (UINT16)info.GetObjectId());
        break;
    }
    case NormMsg::DATA:
    {
        const NormDataMsg &data = (const NormDataMsg &)msg;
        PLOG(PL_ALWAYS, "inst>%hu seq>%hu DATA obj>%hu blk>%lu seg>%hu ",
             data.GetInstanceId(),
             seq,
             //data.IsData() ? "DATA" : "PRTY",
             (UINT16)data.GetObjectId(),
             (unsigned long)data.GetFecBlockId(fecM).GetValue(),
             (UINT16)data.GetFecSymbolId(fecM));

        if (data.IsStream())
        {
            UINT32 offset = NormDataMsg::ReadStreamPayloadOffset(data.GetPayload());
            PLOG(PL_ALWAYS, "offset>%lu ", (unsigned long)offset);

            /*if (data.GetFecSymbolId(fecM) < 32)
                {
                    //if (NormDataMsg::StreamPayloadFlagIsSet(data.GetPayload(), NormDataMsg::FLAG_MSG_START))
                    UINT16 msgStartOffset = NormDataMsg::ReadStreamPayloadMsgStart(data.GetPayload());
                    if (0 != msgStartOffset)
                    {
                        PLOG(PL_ALWAYS, "start word>%hu ", msgStartOffset - 1);
                    }
                    //if (NormDataMsg::StreamPayloadFlagIsSet(data.GetPayload(), NormDataMsg::FLAG_STREAM_END))
                    if (0 == NormDataMsg::ReadStreamPayloadLength(data.GetPayload()))
                        PLOG(PL_ALWAYS, "(stream end) ");
                }
                */
        }
        break;
    }
    case NormMsg::CMD:
    {
        const NormCmdMsg &cmd = static_cast<const NormCmdMsg &>(msg);
        NormCmdMsg::Flavor flavor = cmd.GetFlavor();
        PLOG(PL_ALWAYS, "inst>%hu seq>%hu %s ", cmd.GetInstanceId(), seq, CMD_NAME[flavor]);
        switch (flavor)
        {
        case NormCmdMsg::ACK_REQ:
        {
            int index = ((const NormCmdAckReqMsg &)msg).GetAckType();
            index = MIN(index, 3);
            PLOG(PL_ALWAYS, "(%s) ", REQ_NAME[index]);
            break;
        }
        case NormCmdMsg::SQUELCH:
        {
            const NormCmdSquelchMsg &squelch =
                static_cast<const NormCmdSquelchMsg &>(msg);
            PLOG(PL_ALWAYS, " obj>%hu blk>%lu seg>%hu ",
                 (UINT16)squelch.GetObjectId(),
                 (unsigned long)squelch.GetFecBlockId(fecM).GetValue(),
                 (UINT16)squelch.GetFecSymbolId(fecM));
            break;
        }
        case NormCmdMsg::FLUSH:
        {
            const NormCmdFlushMsg &flush =
                static_cast<const NormCmdFlushMsg &>(msg);
            PLOG(PL_ALWAYS, " obj>%hu blk>%lu seg>%hu ",
                 (UINT16)flush.GetObjectId(),
                 (unsigned long)flush.GetFecBlockId(fecM).GetValue(),
                 (UINT16)flush.GetFecSymbolId(fecM));

            if (0 != flush.GetAckingNodeCount())
                PLOG(PL_ALWAYS, "(WATERMARK) "); // ACK requested
            break;
        }
        case NormCmdMsg::CC:
        {
            const NormCmdCCMsg &cc = static_cast<const NormCmdCCMsg &>(msg);
            PLOG(PL_ALWAYS, " seq>%u ", cc.GetCCSequence());
            NormHeaderExtension ext;
            while (cc.GetNextExtension(ext))
            {
                if (NormHeaderExtension::CC_RATE == ext.GetType())
                {
                    UINT16 sendRate = ((NormCCRateExtension &)ext).GetSendRate();
                    PLOG(PL_ALWAYS, " rate>%f ", 8.0e-03 * NormUnquantizeRate(sendRate));
                    break;
                }
            }
            break;
        }
        default:
            break;
        }
        break;
    }

    case NormMsg::ACK:
    case NormMsg::NACK:
    {
        PLOG(PL_ALWAYS, "inst>%hu ", instId);
        // look for NormCCFeedback extension
        NormHeaderExtension ext;
        while (msg.GetNextExtension(ext))
        {
            if (NormHeaderExtension::CC_FEEDBACK == ext.GetType())
            {
                clrFlag = ((NormCCFeedbackExtension &)ext).CCFlagIsSet(NormCC::CLR);
                break;
            }
        }
        if (NormMsg::ACK == msgType)
        {
            const NormAckMsg &ack = static_cast<const NormAckMsg &>(msg);
            if (NormAck::FLUSH == ack.GetAckType())
            {
                const NormAckFlushMsg &flushAck = static_cast<const NormAckFlushMsg &>(ack);
                PLOG(PL_ALWAYS, "ACK(FLUSH) obj>%hu blk>%lu seg>%hu ",
                     (UINT16)flushAck.GetObjectId(),
                     (unsigned long)flushAck.GetFecBlockId(fecM).GetValue(),
                     (UINT16)flushAck.GetFecSymbolId(fecM));
            }
            else if (NormAck::CC == ack.GetAckType())
            {
                PLOG(PL_ALWAYS, "ACK(CC) ");
            }
            else
            {
                PLOG(PL_ALWAYS, "ACK(ZZZ) ");
            }
        }
        else
        {
            PLOG(PL_ALWAYS, "NACK ");
            // TBD - provide deeper NACK inspection?
        }
        break;
    }

    default:
        PLOG(PL_ALWAYS, "%s ", MSG_NAME[msgType]);
        break;
    } // end switch (msgType)
    PLOG(PL_ALWAYS, "len>%hu %s\n", length, clrFlag ? "(CLR)" : "");
} // end NormTrace();

void NormSession::HandleReceiveMessage(NormMsg &msg, bool wasUnicast, bool ecnStatus)
{
    // Ignore messages from ourself unless "loopback" is enabled
    if ((msg.GetSourceId() == LocalNodeId()) && !loopback)
        return;
    // Drop some rx messages for testing
    if ((rx_loss_rate > 0) && (UniformRand(100.0) < rx_loss_rate))
        return;

    struct timeval currentTime;
    ::ProtoSystemTime(currentTime);

    if (trace)
    {
        // Initially assume it's a message we generated (or similarly configured sender)
        UINT8 fecM = fec_m;
        UINT16 instId = instance_id;
        NormNodeId senderId;
        switch (msg.GetType())
        {
        case NormMsg::ACK:
            senderId = static_cast<NormAckMsg &>(msg).GetSenderId();
            break;
        case NormMsg::NACK:
            senderId = static_cast<NormAckMsg &>(msg).GetSenderId();
            break;
        default:
            senderId = msg.GetSourceId();
            break;
        }
        if (IsReceiver() && (senderId != LocalNodeId()))
        {
            // Use our receiver state to look up sender if possible
            NormSenderNode *sender;
            if (IsServerListener())
                sender = client_tree.FindNodeByAddress(msg.GetSource());
            else
                sender = static_cast<NormSenderNode *>(sender_tree.FindNodeById(senderId));
            if (NULL != sender)
            {
                fecM = sender->GetFecFieldSize();
                instId = sender->GetInstanceId();
            }
            else
            {
                fecM = 16; // reasonable assumption
                instId = 0;
            }
        }
        NormTrace(currentTime, LocalNodeId(), msg, false, fecM, instId); // TBD don't assume m == 16 (i.e. for fec_id == 2)
    }                                                                    // end if (trace)

    NormMsg::Type msgType = msg.GetType();

    if (IsServerListener())
    {
        // Only pay attention to packets with FLAG_SYN set
        // (NORM_CMD(CC), NORM_INFO, or NORM_DATA messages
        // (Note FLAG_SYN is not part of RFC 5740)
        bool syn = false;
        bool senderMsg = true;
        switch (msgType)
        {
        case NormMsg::CMD:
        {
            NormCmdMsg &cmd = static_cast<NormCmdMsg &>(msg);
            if ((NormCmdMsg::CC == cmd.GetFlavor()) &&
                static_cast<NormCmdCCMsg &>(cmd).SynIsSet())
            {
                syn = true;
            }
            break;
        }
        case NormMsg::INFO:
        case NormMsg::DATA:
            if (static_cast<NormObjectMsg &>(msg).FlagIsSet(NormObjectMsg::FLAG_SYN))
            {
                syn = true;
            }
            break;
        default:
            senderMsg = false;
            // Receiver messages are ignored by unicast server-listener,
            // but multicast server needs to process ACKS/NACKS from client receivers
            if (!Address().IsMulticast())
                return;
        }
        if (senderMsg)
        {
            if (!syn)
            {
                // Send "reject" command to source
                char buffer[2];
                buffer[0] = NORM_SOCKET_VERSION;
                buffer[1] = NORM_SOCKET_CMD_REJECT;
                SenderSendAppCmd(buffer, 2, msg.GetSource());
                return;
            }
        }
    }

    // Add newly detected nodes to acking list _before_ processing message
    if (IsSender() && (TRACK_NONE != acking_auto_populate))
    {
        bool addNode = false;
        switch (acking_auto_populate)
        {
        case TRACK_ALL:
            addNode = true;
            break;
        case TRACK_RECEIVERS:
            addNode = (NormMsg::NACK == msgType) || (NormMsg::ACK == msgType);
            break;
        case TRACK_SENDERS:
            addNode = (NormMsg::NACK != msgType) && (NormMsg::ACK != msgType);
            break;
        default:
            break;
        }
        if (addNode)
        {
            NormNodeId sourceId = msg.GetSourceId();
            if (NULL == acking_node_tree.FindNodeById(sourceId))
            {
                if (!SenderAddAckingNode(msg.GetSourceId(), &msg.GetSource()))
                    PLOG(PL_ERROR, "NormSession::HandleReceiveMessage() error: unable to add acking node!\n");
                NormAckingNode *acker = (NormAckingNode *)acking_node_tree.FindNodeById(sourceId);
                Notify(NormController::ACKING_NODE_NEW, acker, NULL);
            }
        }
    }

    switch (msg.GetType())
    {
    case NormMsg::INFO:
        //DMSG(0, "NormSession::HandleReceiveMessage(NormMsg::INFO)\n");
        if (IsReceiver())
            ReceiverHandleObjectMessage(currentTime, (NormObjectMsg &)msg, ecnStatus);
        break;
    case NormMsg::DATA:
        //DMSG(0, "NormSession::HandleReceiveMessage(NormMsg::DATA) ...\n");
        if (IsReceiver())
            ReceiverHandleObjectMessage(currentTime, (NormObjectMsg &)msg, ecnStatus);
        break;
    case NormMsg::CMD:
        //DMSG(0, "NormSession::HandleReceiveMessage(NormMsg::CMD) ...\n");
        if (IsReceiver())
            ReceiverHandleCommand(currentTime, (NormCmdMsg &)msg, ecnStatus);
        break;
    case NormMsg::NACK:
        if (IsSender() && (((NormNackMsg &)msg).GetSenderId() == LocalNodeId()))
        {
            SenderHandleNackMessage(currentTime, (NormNackMsg &)msg);
            if (wasUnicast && (backoff_factor > 0.5) && Address().IsMulticast())
            {
                // for suppression of unicast nack feedback
                advertise_repairs = true;
                QueueMessage(NULL); // to prompt transmit timeout
            }
        }
        if (IsReceiver())
            ReceiverHandleNackMessage((NormNackMsg &)msg);
        break;
    case NormMsg::ACK:
        if (IsSender() && (((NormAckMsg &)msg).GetSenderId() == LocalNodeId()))
            SenderHandleAckMessage(currentTime, (NormAckMsg &)msg, wasUnicast);
        if (IsReceiver())
            ReceiverHandleAckMessage((NormAckMsg &)msg);
        break;

    case NormMsg::REPORT:
    case NormMsg::INVALID:
        PLOG(PL_ERROR, "NormSession::HandleReceiveMessage(NormMsg::INVALID)\n");
        break;
    }
} // end NormSession::HandleReceiveMessage()

void NormSession::ReceiverHandleObjectMessage(const struct timeval &currentTime,
                                              const NormObjectMsg &msg,
                                              bool ecnStatus)
{
    // Do common updates for senders we already know.
    NormNodeId sourceId = msg.GetSourceId();
    NormSenderNode *theSender;
    if (IsServerListener())
        theSender = client_tree.FindNodeByAddress(msg.GetSource());
    else
        theSender = (NormSenderNode *)sender_tree.FindNodeById(sourceId);
    if (theSender)
    {
        if (msg.GetInstanceId() != theSender->GetInstanceId())
        {
            PLOG(PL_INFO, "NormSession::ReceiverHandleObjectMessage() node>%lu sender>%lu instanceId change - resyncing.\n",
                 (unsigned long)LocalNodeId(), (unsigned long)theSender->GetId());
            theSender->Close();
            Notify(NormController::REMOTE_SENDER_RESET, theSender, NULL);
            if (!theSender->Open(msg.GetInstanceId()))
            {
                PLOG(PL_ERROR, "NormSession::ReceiverHandleObjectMessage() node>%lu error re-opening NormSenderNode\n",
                     (unsigned long)LocalNodeId());
                // (TBD) notify application of error
                return;
            }
        }
    }
    else
    {
        if (NULL != preset_sender)
        {
            theSender = preset_sender;
            preset_sender = NULL;
            theSender->SetId(msg.GetSourceId());
            theSender->SetInstanceId(msg.GetInstanceId());
            theSender->SetAddress(msg.GetSource());
            if (IsServerListener())
                client_tree.InsertNode(*theSender);
            else
                sender_tree.AttachNode(theSender);
            PLOG(PL_DEBUG, "NormSession::ReceiverHandleObjectMessage() node>%lu new remote sender:%lu ...\n",
                 (unsigned long)LocalNodeId(), (unsigned long)msg.GetSourceId());
            Notify(NormController::REMOTE_SENDER_NEW, theSender, NULL);
        }
        else if (NULL != (theSender = new NormSenderNode(*this, msg.GetSourceId())))
        {
            theSender->SetAddress(msg.GetSource());
            if (theSender->Open(msg.GetInstanceId()))
            {
                if (IsServerListener())
                    client_tree.InsertNode(*theSender);
                else
                    sender_tree.AttachNode(theSender);
                PLOG(PL_DEBUG, "NormSession::ReceiverHandleObjectMessage() node>%lu new remote sender:%lu ...\n",
                     (unsigned long)LocalNodeId(), (unsigned long)msg.GetSourceId());
            }
            else
            {
                PLOG(PL_FATAL, "NormSession::ReceiverHandleObjectMessage() node>%lu error opening NormSenderNode\n",
                     (unsigned long)LocalNodeId());
                // (TBD) notify application of error
                return;
            }
            Notify(NormController::REMOTE_SENDER_NEW, theSender, NULL);
        }
        else
        {
            PLOG(PL_ERROR, "NormSession::ReceiverHandleObjectMessage() new NormSenderNode error: %s\n",
                 GetErrorString());
            // (TBD) notify application of error
            return;
        }
    }
    theSender->Activate(true);
    if (!theSender->GetAddress().IsEqual(msg.GetSource()))
    {
        // sender source address has changed
        theSender->SetAddress(msg.GetSource());
        Notify(NormController::REMOTE_SENDER_ADDRESS, theSender, NULL);
    }
    theSender->UpdateRecvRate(currentTime, msg.GetLength());
    theSender->UpdateLossEstimate(currentTime, msg.GetSequence(), ecnStatus);
    theSender->IncrementRecvTotal(msg.GetLength()); // for statistics only (TBD) #ifdef NORM_DEBUG
    theSender->HandleObjectMessage(msg);
    theSender->CheckCCFeedback(); // this cues immediate CLR cc feedback if loss was detected
                                  // and cc feedback was not provided in response otherwise

} // end NormSession::ReceiverHandleObjectMessage()

void NormSession::ReceiverHandleCommand(const struct timeval &currentTime,
                                        const NormCmdMsg &cmd,
                                        bool ecnStatus)
{
    // Do common updates for senders we already know.
    NormNodeId sourceId = cmd.GetSourceId();
    NormSenderNode *theSender;
    if (IsServerListener())
        theSender = client_tree.FindNodeByAddress(cmd.GetSource());
    else
        theSender = (NormSenderNode *)sender_tree.FindNodeById(sourceId);
    if (NULL != theSender)
    {
        if (cmd.GetInstanceId() != theSender->GetInstanceId())
        {
            PLOG(PL_INFO, "NormSession::ReceiverHandleCommand() node>%lu sender>%lu instanceId change - resyncing.\n",
                 (unsigned long)LocalNodeId(), theSender->GetId());
            theSender->Close();
            Notify(NormController::REMOTE_SENDER_RESET, theSender, NULL);
            if (!theSender->Open(cmd.GetInstanceId()))
            {
                PLOG(PL_ERROR, "NormSession::ReceiverHandleCommand() node>%lu error re-opening NormSenderNode\n",
                     (unsigned long)LocalNodeId());
                // (TBD) notify application of error
                return;
            }
        }
    }
    else
    {
        //DMSG(0, "NormSession::ReceiverHandleCommand() node>%lu recvd command from unknown sender ...\n",
        //          (unsigned long)LocalNodeId());
        if (NULL != preset_sender)
        {
            theSender = preset_sender;
            preset_sender = NULL;
            theSender->SetId(cmd.GetSourceId());
            theSender->SetInstanceId(cmd.GetInstanceId());
            theSender->SetAddress(cmd.GetSource());
            if (IsServerListener())
                client_tree.InsertNode(*theSender);
            else
                sender_tree.AttachNode(theSender);
            PLOG(PL_DEBUG, "NormSession::ReceiverHandleCommand() node>%lu new remote sender:%lu ...\n",
                 (unsigned long)LocalNodeId(), (unsigned long)cmd.GetSourceId());
            Notify(NormController::REMOTE_SENDER_NEW, theSender, NULL);
        }
        else if ((theSender = new NormSenderNode(*this, cmd.GetSourceId())))
        {
            theSender->SetAddress(cmd.GetSource());
            if (theSender->Open(cmd.GetInstanceId()))
            {
                if (IsServerListener())
                    client_tree.InsertNode(*theSender);
                else
                    sender_tree.AttachNode(theSender);
                PLOG(PL_DEBUG, "NormSession::ReceiverHandleCommand() node>%lu new remote sender:%lu ...\n",
                     (unsigned long)LocalNodeId(), (unsigned long)cmd.GetSourceId());
            }
            else
            {
                PLOG(PL_ERROR, "NormSession::ReceiverHandleCommand() node>%lu error opening NormSenderNode\n");
                // (TBD) notify application of error
                return;
            }
            Notify(NormController::REMOTE_SENDER_NEW, theSender, NULL);
        }
        else
        {
            PLOG(PL_ERROR, "NormSession::ReceiverHandleCommand() new NormSenderNode node>%lu error: %s\n",
                 (unsigned long)LocalNodeId(), GetErrorString());
            // (TBD) notify application of error
            return;
        }
    }
    // We should "re-activate" senders on NORM_CMD(FLUSH)
    if (NormCmdMsg::FLUSH == cmd.GetFlavor())
        theSender->Activate(true);
    else
        theSender->Activate(false);
    if (!theSender->GetAddress().IsEqual(cmd.GetSource()))
    {
        // sender source address has changed
        theSender->SetAddress(cmd.GetSource());
        Notify(NormController::REMOTE_SENDER_ADDRESS, theSender, NULL);
    }
    theSender->UpdateRecvRate(currentTime, cmd.GetLength());
    theSender->UpdateLossEstimate(currentTime, cmd.GetSequence(), ecnStatus);
    theSender->IncrementRecvTotal(cmd.GetLength()); // for statistics only (TBD) #ifdef NORM_DEBUG
    theSender->HandleCommand(currentTime, cmd);
    theSender->CheckCCFeedback(); // this cues immediate CLR cc feedback if loss was detected
                                  // and cc feedback was not provided in response otherwise
} // end NormSession::ReceiverHandleCommand()

bool NormSession::InsertRemoteSender(NormSenderNode &sender)
{
    // Build a NORM_CMD(CC) message with information from
    // a "sender" being inserted from another NormSession
    // (supports NormSocket server operations)
    if (!IsReceiver())
        return false;
    NormCmdCCMsg cmd;
    cmd.Init();
    cmd.SetSequence(sender.GetCurrentSequence());
    cmd.SetSourceId(sender.GetId());
    cmd.SetDestination(sender.GetAddress());
    cmd.SetInstanceId(sender.GetInstanceId());
    cmd.SetGrtt(sender.GetGrttQuantized());
    cmd.SetBackoffFactor(sender.GetBackoffFactor());
    cmd.SetGroupSize(sender.GetGroupSizeQuantized());
    cmd.SetCCSequence(sender.GetCCSequence());
    // Adjust send time for any current hold time
    // since it will be "rehandled"
    struct timeval adjustedSendTime;
    struct timeval currentTime;
    ::ProtoSystemTime(currentTime);
    sender.CalculateGrttResponse(currentTime, adjustedSendTime);
    cmd.SetSendTime(adjustedSendTime);

    // Insert NORM-CC header extension, if applicable
    // (Note we set the extension "rate" _after_ AdjustRate() done below)
    NormCCRateExtension ext;
    cmd.AttachExtension(ext);
    ext.SetSendRate(NormQuantizeRate(sender.GetSendRate()));

    HandleReceiveMessage(cmd, false);

    return true; // TBD - confirm the node was added

} // end NormSession::InsertRemoteSender()

double NormSession::CalculateRtt(const struct timeval &currentTime,
                                 const struct timeval &grttResponse)
{
    if (grttResponse.tv_sec || grttResponse.tv_usec)
    {
        double rcvrRtt;
        // Calculate rtt estimate for this receiver and process the response
        if (currentTime.tv_usec < grttResponse.tv_usec)
        {
            rcvrRtt =
                (double)(currentTime.tv_sec - grttResponse.tv_sec - 1);
            rcvrRtt +=
                ((double)(1000000 - (grttResponse.tv_usec - currentTime.tv_usec))) / 1.0e06;
        }
        else
        {
            rcvrRtt =
                (double)(currentTime.tv_sec - grttResponse.tv_sec);
            rcvrRtt +=
                ((double)(currentTime.tv_usec - grttResponse.tv_usec)) / 1.0e06;
        }
        // Lower limit on RTT (because of coarse timer resolution on some systems,
        // this can sometimes actually end up a negative value!)
        // (TBD) this should be system clock granularity?
        return (rcvrRtt < 1.0e-06) ? 1.0e-06 : rcvrRtt;
    }
    else
    {
        return -1.0;
    }
} // end NormSession::CalculateRtt()

void NormSession::SenderUpdateGrttEstimate(double receiverRtt)
{
    grtt_response = true;
    if ((receiverRtt > grtt_measured) || !address.IsMulticast())
    {
        // Immediately incorporate bigger RTT's
        grtt_decrease_delay_count = DEFAULT_GRTT_DECREASE_DELAY;
        grtt_measured = 0.25 * grtt_measured + 0.75 * receiverRtt;
        //grtt_measured = 0.9 * grtt_measured + 0.1 * receiverRtt;
        if (grtt_measured > grtt_max)
            grtt_measured = grtt_max;
        UINT8 grttQuantizedOld = grtt_quantized;
        double pktInterval = ((double)(44 + segment_size)) / tx_rate;
        if (grtt_measured < pktInterval)
            grtt_quantized = NormQuantizeRtt(pktInterval);
        else
            grtt_quantized = NormQuantizeRtt(grtt_measured);
        // Calculate grtt_advertised since quantization rounds upward
        grtt_advertised = NormUnquantizeRtt(grtt_quantized);
        if (grtt_advertised > grtt_max)
        {
            grtt_quantized = NormQuantizeRtt(grtt_max);
            grtt_advertised = NormUnquantizeRtt(grtt_quantized);
        }
        grtt_current_peak = grtt_measured;
        if (grttQuantizedOld != grtt_quantized)
        {
            if (notify_on_grtt_update)
            {
                notify_on_grtt_update = false;
                Notify(NormController::GRTT_UPDATED, (NormSenderNode *)NULL, (NormObject *)NULL);
            }
            Notify(NormController::GRTT_UPDATED, (NormSenderNode *)NULL, (NormObject *)NULL);
            PLOG(PL_DEBUG, "NormSession::SenderUpdateGrttEstimate() node>%lu increased to new grtt>%lf sec\n",
                 (unsigned long)LocalNodeId(), grtt_advertised);
        }
    }
    else if (receiverRtt > grtt_current_peak)
    {
        grtt_current_peak = receiverRtt;
    }
} // end NormSession::SenderUpdateGrttEstimate()

double NormSession::CalculateRate(double size, double rtt, double loss)
{
    //                                  size
    // rate = -------------------------------------------------------------
    //      rtt * (sqrt(2*loss/3) + 12*loss*(1 + 32*loss*loss)*sqrt(3*loss/8))
    //
    // notes: "b" = 1 and "t_RTO" = 4*rtt where "b" is number of TCP pkts/ACK

    double denom = rtt * (sqrt((2.0 / 3.0) * loss) +
                          (12.0 * sqrt((3.0 / 8.0) * loss) * loss *
                           (1.0 + 32.0 * loss * loss)));
    return (size / denom);
} // end NormSession::CalculateRate()

void NormSession::SenderHandleCCFeedback(struct timeval currentTime,
                                         NormNodeId nodeId,
                                         UINT8 ccFlags,
                                         double ccRtt,
                                         double ccLoss,
                                         double ccRate,
                                         UINT16 ccSequence)
{

    PLOG(PL_DEBUG, "NormSession::SenderHandleCCFeedback() cc feedback recvd at time %lu.%lf  ccRate:%9.3lf ccRtt:%lf ccLoss:%lf ccFlags:%02x\n",
         (unsigned long)currentTime.tv_sec, ((double)currentTime.tv_usec) * 1.0e-06,
         ccRate * 8.0 / 1000.0, ccRtt, ccLoss, ccFlags);
    // Keep track of current suppressing feedback
    // (non-CLR, lowest rate, unconfirmed RTT)
    if (0 == (ccFlags & NormCC::CLR))
    {
        if (suppress_rate < 0.0)
        {
            suppress_rate = ccRate;
            suppress_rtt = ccRtt;
            suppress_nonconfirmed = (0 == (ccFlags & NormCC::RTT));
        }
        else
        {
            if (ccRate < suppress_rate)
                suppress_rate = ccRate;
            if (ccRtt > suppress_rtt)
                suppress_rtt = ccRtt;
            if (0 == (ccFlags & NormCC::RTT))
                suppress_nonconfirmed = true;
        }
    }
    if (!cc_enable)
        return;

    // Adjust ccRtt if we already have state on this nodeId
    NormCCNode *node = (NormCCNode *)cc_node_list.FindNodeById(nodeId);
    if (node)
        ccRtt = node->UpdateRtt(ccRtt);

    bool ccSlowStart = (0 != (ccFlags & NormCC::START));

    if (!ccSlowStart)
    {
        double calcRate = CalculateRate(nominal_packet_size, ccRtt, ccLoss);
#ifdef LIMIT_CC_RATE
        // Experimental modification to NORM-CC where congestion control rate is limited
        // to MIN(2.0*measured recv rate, calculated rate).  This might prevent large rate
        // overshoot in conditions where the loss measurement (perhaps initial loss) is
        // very low due to big network packet buffers, etc
        // Note that when the NORM_CC_FLAG_LIMIT is set, this indicates the receiver
        // has set the rate field to 2.0 * measured recv rate instead of calculated rate.
        if (0 != (ccFlags & NormCC::LIMIT))
        {
            // receiver set a limited rate instead of calculated rate
            // so let's confirm which is the lower rate
            if (calcRate < ccRate)
                ccRate = calcRate;
        }
        else
#endif // LIMIT_CC_RATE
        {
            ccRate = calcRate;
        }
    }

    PLOG(PL_DEBUG, "NormSession::SenderHandleCCFeedback() node>%lu rate>%lf (rtt>%lf loss>%lf slow_start>%d limit>%d)\n",
         (unsigned long)nodeId, ccRate * 8.0 / 1000.0, ccRtt, ccLoss, (0 != (ccFlags & NormCC::START)),
         (0 != (ccFlags & NormCC::LIMIT)));

    // Keep the active CLR (if there is one) at the head of the list
    NormNodeListIterator iterator(cc_node_list);
    NormCCNode *next = (NormCCNode *)iterator.GetNextNode();
    // 1) Does this response replace the active CLR?
    if (next && next->IsActive())
    {
        // First, make sure this is _new_ non-duplicative
        // feedback  for the given "nodeId"
        if (next->GetId() == nodeId)
        {
            INT16 ccDelta = ccSequence - next->GetCCSequence();
            if (ccDelta <= 0)
                return;
        }
        if ((nodeId == next->GetId()) ||
            (ccRate < next->GetRate()) ||
            ((ccRate < (next->GetRate() * 1.1)) &&
             (ccRtt > next->GetRtt()))) // use Rtt as tie-breaker if close
        {
            NormNodeId savedId = next->GetId();
            bool savedRttStatus = next->HasRtt();
            double savedRtt = next->GetRtt();
            double savedLoss = next->GetLoss();
            double savedRate = next->GetRate();
            UINT16 savedSequence = next->GetCCSequence();
            struct timeval savedTime = next->GetFeedbackTime();

            next->SetId(nodeId);
            next->SetClrStatus(true);
            next->SetRttStatus(0 != (ccFlags & NormCC::RTT));

            next->SetLoss(ccLoss);
            next->SetRate(ccRate);
            next->SetCCSequence(ccSequence);
            next->SetActive(true);
            next->SetFeedbackTime(currentTime);
            cc_slow_start = ccSlowStart; // use CLR status for our slow_start state
            if (savedId == nodeId)
            {
                // This was feedback from the current CLR
                AdjustRate(true);
                return;
            }
            else
            {
                next->SetRtt(ccRtt);
                AdjustRate(true);
            }
            ccFlags = 0;
            nodeId = savedId;
            if (savedRttStatus)
                ccFlags = NormCC::RTT;
            ccRtt = savedRtt;
            ccLoss = savedLoss;
            ccRate = savedRate;
            ccSequence = savedSequence;
            currentTime = savedTime;
        }
    }
    else
    {
        // There was no active CLR
        if (!next)
        {
            if ((next = new NormCCNode(*this, nodeId)))
            {
                cc_node_list.Append(next);
            }
            else
            {
                PLOG(PL_FATAL, "NormSession::SenderHandleCCFeedback() memory allocation error: %s\n",
                     GetErrorString());
                return;
            }
        }
        next->SetId(nodeId);
        next->SetClrStatus(true);
        //next->SetPlrStatus(false);
        next->SetRttStatus(0 != (ccFlags & NormCC::RTT));
        next->SetRtt(ccRtt);
        next->SetLoss(ccLoss);
        next->SetRate(ccRate);
        next->SetCCSequence(ccSequence);
        next->SetActive(true);
        next->SetFeedbackTime(currentTime);
        AdjustRate(true);
        return;
    }

    // 2) Go through cc_node_list and find lowest priority candidate
    NormCCNode *candidate = NULL;
    if (cc_node_list.GetCount() < 5)
    {
        if ((candidate = new NormCCNode(*this, nodeId)))
        {
            cc_node_list.Append(candidate);
        }
        else
        {
            PLOG(PL_FATAL, "NormSession::SenderHandleCCFeedback() memory allocation error: %s\n",
                 GetErrorString());
        }
    }
    else
    {
        while ((next = (NormCCNode *)iterator.GetNextNode()))
        {
            if (next->GetId() == nodeId)
            {
                candidate = next;
                break;
            }
            else if (candidate)
            {
                if (candidate->IsActive() && !next->IsActive())
                {
                    candidate = next;
                    continue;
                }
                if (!next->HasRtt() && candidate->HasRtt())
                    continue;
                else if (!candidate->HasRtt() && next->HasRtt())
                    candidate = next;
                else if (candidate->GetRate() < next->GetRate())
                    candidate = next;
            }
            else
            {
                candidate = next;
                continue;
            }
        }
    }

    // 3) Replace candidate if this response is higher precedence
    if (candidate)
    {
        bool haveRtt = (0 != (ccFlags & NormCC::RTT));
        bool replace;
        if (candidate->GetId() == nodeId)
            replace = true;
        else if (!candidate->IsActive())
            replace = true;
        else if (!haveRtt && candidate->HasRtt())
            replace = true;
        else if (haveRtt && !candidate->HasRtt())
            replace = false;
        else if (ccRate < candidate->GetRate())
            replace = true;
        else
            replace = false;
        if (replace)
        {
            candidate->SetId(nodeId);
            candidate->SetClrStatus(false);
            //candidate->SetPlrStatus(true);  // do this only
            candidate->SetRttStatus(0 != (ccFlags & NormCC::RTT));
            candidate->SetRtt(ccRtt);
            candidate->SetLoss(ccLoss);
            candidate->SetRate(ccRate);
            candidate->SetCCSequence(ccSequence);
            candidate->SetActive(true);
        }
    }
} // end NormSession::SenderHandleCCFeedback()

void NormSession::SenderHandleAckMessage(const struct timeval &currentTime, const NormAckMsg &ack, bool wasUnicast)
{
    // Update GRTT estimate
    struct timeval grttResponse;
    ack.GetGrttResponse(grttResponse);
    double receiverRtt = CalculateRtt(currentTime, grttResponse);
    PLOG(PL_DEBUG, "NormSession::SenderHandleAckMessage() node>%lu sender received ACK from node>%lu rtt>%lf\n",
         (unsigned long)LocalNodeId(), (unsigned long)ack.GetSourceId(), receiverRtt);

    if (receiverRtt >= 0.0)
        SenderUpdateGrttEstimate(receiverRtt);

    // Look for NORM-CC Feedback header extension
    NormCCFeedbackExtension ext;
    while (ack.GetNextExtension(ext))
    {
        if (NormHeaderExtension::CC_FEEDBACK == ext.GetType())
        {
            SenderHandleCCFeedback(currentTime,
                                   ack.GetSourceId(),
                                   ext.GetCCFlags(),
                                   receiverRtt >= 0.0 ? receiverRtt : NormUnquantizeRtt(ext.GetCCRtt()),
                                   NormUnquantizeLoss32(ext.GetCCLoss32()),
                                   NormUnquantizeRate(ext.GetCCRate()),
                                   ext.GetCCSequence());
            if (wasUnicast && probe_proactive && Address().IsMulticast())
            {
                // if it's the CLR, it doesn't suppress anyone, don't advertise
                if (!ext.CCFlagIsSet(NormCC::CLR))
                {
                    // for suppression of unicast cc feedback
                    advertise_repairs = true;
                    QueueMessage(NULL);
                }
            }
            break;
        }
    }

    switch (ack.GetAckType())
    {
    case NormAck::CC:
        // Everything is in the ACK header or extension for this one
        break;

    case NormAck::FLUSH:
        if (watermark_pending)
        {
            NormAckingNode *acker =
                static_cast<NormAckingNode *>(acking_node_tree.FindNodeById(ack.GetSourceId()));
            if (NULL != acker)
            {
                if (!acker->AckReceived())
                {
                    const NormAckFlushMsg &flushAck = static_cast<const NormAckFlushMsg &>(ack);
                    if (flushAck.GetFecId() != fec_id)
                    {
                        PLOG(PL_ERROR, "NormSession::SenderHandleAckMessage() received watermark ACK with wrong fec_id?!\n");
                    }
                    else if ((watermark_object_id == flushAck.GetObjectId()) &&
                             (watermark_block_id == flushAck.GetFecBlockId(fec_m)) &&
                             (watermark_segment_id == flushAck.GetFecSymbolId(fec_m)))
                    {
                        // Cache any application-defined extended ACK content for this acker
                        NormAppAckExtension ext;
                        while (ack.GetNextExtension(ext))
                        {
                            if (NormHeaderExtension::APP_ACK == ext.GetType())
                            {
                                if (!acker->SetAckEx(ext.GetContent(), ext.GetContentLength()))
                                {
                                    // TBD - notify app of error
                                    PLOG(PL_ERROR, "NormSession::SenderHandleAckMessage() error: unable to cache application-defined ACK content!\n");
                                }
                            }
                        }
                        acker->MarkAckReceived();
                        /*  This code was an attempt to expedite delivery of the TX_WATERMARK_COMPLETED
                                notification to the application, but breaks some other desired behavior.
                            watermark_pending = false;
                            acking_success_count = 0;
                            NormNodeTreeIterator iterator(acking_node_tree);
                            NormAckingNode* next;
                            while (NULL != (next = static_cast<NormAckingNode*>(iterator.GetNextNode())))
                            {
                                if (next->IsPending())
                                    watermark_pending = true;
                                else if (next->AckReceived() || (NORM_NODE_NONE == next->GetId()))
                                    acking_success_count++;
                            }
                            if (!watermark_pending)
                            {
                                PLOG(PL_DEBUG, "NormSession::SenderHandleAckMessage() node>%lu watermark ack finished.\n",
                                                (unsigned long)LocalNodeId());
                                Notify(NormController::TX_WATERMARK_COMPLETED, (NormSenderNode*)NULL, (NormObject*)NULL);
                            }
                            */
                    }
                    else
                    {
                        // This can happen when new watermarks are set when an old watermark is still
                        // pending (i.e. receivers may still be in the process of replying)
                        PLOG(PL_DEBUG, "NormSession::SenderHandleAckMessage() received old/wrong watermark ACK?!\n");
                    }
                }
                else
                {
                    PLOG(PL_DEBUG, "NormSession::SenderHandleAckMessage() received redundant watermark ACK?!\n");
                }
            }
            else
            {
                PLOG(PL_WARN, "NormSession::SenderHandleAckMessage() received watermark ACK from unknown acker?!\n");
            }
        }
        else
        {
            PLOG(PL_DEBUG, "NormSession::SenderHandleAckMessage() received unsolicited watermark ACK?!\n");
        }
        break;

    // (TBD) Handle other acknowledgement types
    default:
        PLOG(PL_ERROR, "NormSession::SenderHandleAckMessage() node>%lu received unsupported ack type:%d\n",
             (unsigned long)LocalNodeId(), ack.GetAckType());
        break;
    }
} // end SenderHandleAckMessage()

void NormSession::SenderHandleNackMessage(const struct timeval &currentTime, NormNackMsg &nack)
{
    struct timeval grttResponse;
    nack.GetGrttResponse(grttResponse);
    double receiverRtt = CalculateRtt(currentTime, grttResponse);
    if (GetDebugLevel() >= PL_DEBUG)
    {
        PLOG(PL_DEBUG, "NormSession::SenderHandleNackMessage() node>%lu sender received NACK message from node>%lu rtt>%lf (tactive>%d) with content:\n",
             (unsigned long)LocalNodeId(), (unsigned long)nack.GetSourceId(), receiverRtt, repair_timer.IsActive());
        LogRepairContent(nack.GetRepairContent(), nack.GetRepairContentLength(), fec_id, fec_m);
        PLOG(PL_ALWAYS, "\n");
    }
    // (TBD) maintain average of "numErasures" for SEGMENT repair requests
    //       to use as input to a future automatic "auto parity" adjustor???
    // Update GRTT estimate
    if (receiverRtt >= 0.0)
        SenderUpdateGrttEstimate(receiverRtt);

    // Look for NORM-CC Feedback header extension
    NormCCFeedbackExtension ext;
    while (nack.GetNextExtension(ext))
    {
        if (NormHeaderExtension::CC_FEEDBACK == ext.GetType())
        {
            SenderHandleCCFeedback(currentTime,
                                   nack.GetSourceId(),
                                   ext.GetCCFlags(),
                                   receiverRtt >= 0.0 ? receiverRtt : NormUnquantizeRtt(ext.GetCCRtt()),
                                   NormUnquantizeLoss32(ext.GetCCLoss32()), // note using extended precision loss value here
                                   NormUnquantizeRate(ext.GetCCRate()),
                                   ext.GetCCSequence());
        }
        break;
    }

    // Parse and process NACK
    UINT16 requestOffset = 0;
    UINT16 requestLength = 0;
    NormRepairRequest req;
    NormObject *object = NULL;
    bool freshObject = true;
    NormObjectId prevObjectId = 0;
    NormBlock *block = NULL;
    bool freshBlock = true;
    NormBlockId prevBlockId = 0;

    bool startTimer = false;
    UINT16 numErasures = extra_parity;

    bool squelchQueued = false;

    // Get the index of our next pending NORM_DATA transmission
    NormObjectId txObjectIndex;
    NormBlockId txBlockIndex;
    if (SenderGetFirstPending(txObjectIndex))
    {
        NormObject *obj = tx_table.Find(txObjectIndex);
        ASSERT(NULL != obj);
        if (obj->IsPendingInfo())
        {
            txBlockIndex = 0;
        }
        else if (obj->GetFirstPending(txBlockIndex))
        {
            Increment(txBlockIndex);
        }
        else
        {
            txObjectIndex = next_tx_object_id;
            txBlockIndex = 0;
        }
    }
    else
    {
        txObjectIndex = next_tx_object_id;
        txBlockIndex = 0;
    }

    bool holdoff = (repair_timer.IsActive() && !repair_timer.GetRepeatCount());
    enum NormRequestLevel
    {
        SEGMENT,
        BLOCK,
        INFO,
        OBJECT
    };
    while (0 != (requestLength = nack.UnpackRepairRequest(req, requestOffset)))
    {
        NormRepairRequest::Form requestForm = req.GetForm();
        requestOffset += requestLength;
        NormRequestLevel requestLevel;
        if (req.FlagIsSet(NormRepairRequest::SEGMENT))
        {
            requestLevel = SEGMENT;
        }
        else if (req.FlagIsSet(NormRepairRequest::BLOCK))
        {
            requestLevel = BLOCK;
        }
        else if (req.FlagIsSet(NormRepairRequest::OBJECT))
        {
            requestLevel = OBJECT;
        }
        else if (req.FlagIsSet(NormRepairRequest::INFO))
        {
            requestLevel = INFO;
        }
        else
        {
            PLOG(PL_ERROR, "NormSession::SenderHandleNackMessage() node>%lu recvd repair request w/ invalid repair level\n",
                 (unsigned long)LocalNodeId());
            continue;
        }

        NormRepairRequest::Iterator iterator(req, fec_id, fec_m);
        NormObjectId nextObjectId, lastObjectId;
        NormBlockId nextBlockId, lastBlockId;
        UINT16 nextBlockLen, lastBlockLen;
        NormSegmentId nextSegmentId, lastSegmentId;
        while (iterator.NextRepairItem(&nextObjectId, &nextBlockId,
                                       &nextBlockLen, &nextSegmentId))
        {
            if (NormRepairRequest::RANGES == requestForm)
            {
                if (!iterator.NextRepairItem(&lastObjectId, &lastBlockId,
                                             &lastBlockLen, &lastSegmentId))
                {
                    PLOG(PL_ERROR, "NormSession::SenderHandleNackMessage() node>%lu recvd incomplete RANGE request!\n",
                         (unsigned long)LocalNodeId());
                    continue; // (TBD) break/return instead???
                }
                // (TBD) test for valid range form/level
            }
            else
            {
                lastObjectId = nextObjectId;
                lastBlockId = nextBlockId;
                lastBlockLen = nextBlockLen;
                lastSegmentId = nextSegmentId;
            }

            bool inRange = true;
            while (inRange)
            {
                if (nextObjectId != prevObjectId)
                    freshObject = true;
                if (freshObject)
                {
                    freshBlock = true;
                    if (!(object = tx_table.Find(nextObjectId)))
                    {
                        PLOG(PL_DEBUG, "NormSession::SenderHandleNackMessage() node>%lu recvd repair request "
                                       "for unknown object ...\n",
                             (unsigned long)LocalNodeId());
                        if (!squelchQueued)
                        {
                            SenderQueueSquelch(nextObjectId);
                            squelchQueued = true;
                        }
                        if ((OBJECT == requestLevel) || (INFO == requestLevel))
                        {
                            nextObjectId++;
                            if (nextObjectId > lastObjectId)
                                inRange = false;
                        }
                        else
                        {
                            inRange = false;
                        }
                        continue;
                    }
                    prevObjectId = nextObjectId;
                    freshObject = false;
                    // Deal with INFO request if applicable
                    if (req.FlagIsSet(NormRepairRequest::INFO))
                    {
                        if (holdoff)
                        {
                            if (nextObjectId > txObjectIndex)
                                object->HandleInfoRequest(true);
                        }
                        else
                        {
                            // Update our minimum tx repair index as needed
                            if (tx_repair_pending)
                            {
                                if (nextObjectId <= tx_repair_object_min)
                                {
                                    tx_repair_object_min = nextObjectId;
                                    tx_repair_block_min = 0;
                                    tx_repair_segment_min = 0;
                                }
                            }
                            else
                            {
                                tx_repair_pending = true;
                                tx_repair_object_min = nextObjectId;
                                tx_repair_block_min = 0;
                                tx_repair_segment_min = 0;
                            }
                            object->HandleInfoRequest(false);
                            startTimer = true;
                        }
                    }
                } // end if (freshObject)
                ASSERT(NULL != object);
                object->SetLastNackTime(ProtoTime(currentTime));

                switch (requestLevel)
                {
                case OBJECT:
                    PLOG(PL_DETAIL, "NormSession::SenderHandleNackMessage(OBJECT) objs>%hu:%hu\n",
                         (UINT16)nextObjectId, (UINT16)lastObjectId);
                    if (holdoff)
                    {
                        if (nextObjectId > txObjectIndex)
                        {
                            if (object->IsStream())
                                object->TxReset(((NormStreamObject *)object)->StreamBufferLo());
                            else
                                object->TxReset();
                            if (!tx_pending_mask.Set(nextObjectId))
                                PLOG(PL_ERROR, "NormSession::SenderHandleNackMessage() tx_pending_mask.Set(%hu) error (1)\n",
                                     (UINT16)nextObjectId);
                        }
                    }
                    else
                    {
                        // Update our minimum tx repair index as needed
                        if (tx_repair_pending)
                        {
                            if (nextObjectId <= tx_repair_object_min)
                            {
                                tx_repair_object_min = nextObjectId;
                                tx_repair_block_min = 0;
                                tx_repair_segment_min = 0;
                            }
                        }
                        else
                        {
                            tx_repair_pending = true;
                            tx_repair_object_min = nextObjectId;
                            tx_repair_block_min = 0;
                            tx_repair_segment_min = 0;
                        }
                        tx_repair_mask.Set(nextObjectId);
                        startTimer = true;
                    }
                    nextObjectId++;
                    if (nextObjectId > lastObjectId)
                        inRange = false;
                    break;
                case BLOCK:
                    PLOG(PL_DETAIL, "NormSession::SenderHandleNackMessage(BLOCK) obj>%hu blks>%lu:%lu\n",
                         (UINT16)nextObjectId,
                         (unsigned long)nextBlockId.GetValue(),
                         (unsigned long)lastBlockId.GetValue());
                    inRange = false; // BLOCK requests are processed in one pass
                    // (TBD) if entire object is TxReset(), continue
                    if (object->IsStream())
                    {
                        // mark nack time for potential flow control
                        static_cast<NormStreamObject *>(object)->SetLastNackTime(nextBlockId, ProtoTime(currentTime));
                        bool attemptLock = true;
                        NormBlockId firstLockId = nextBlockId;
                        if (holdoff)
                        {
                            // Only lock blocks for which we're going to accept the repair request
                            if (nextObjectId == txObjectIndex)
                            {
                                //if (lastBlockId < txBlockIndex)
                                if (Compare(lastBlockId, txBlockIndex) < 0)
                                    attemptLock = false;
                                //else if (nextBlockId < txBlockIndex)
                                else if (Compare(nextBlockId, txBlockIndex) < 0)
                                    firstLockId = txBlockIndex;
                            }
                            else if (nextObjectId < txObjectIndex)
                            {
                                attemptLock = false; // NACK arrived too late to be useful
                            }
                        }

                        // Make sure the stream' pending_mask can be set as needed
                        // (TBD)

                        // Lock stream_buffer pending for block data retransmissions
                        if (attemptLock)
                        {
                            if (!((NormStreamObject *)object)->LockBlocks(firstLockId, lastBlockId, currentTime))
                            {
                                PLOG(PL_DEBUG, "NormSession::SenderHandleNackMessage() node>%lu LockBlocks() failure\n",
                                     (unsigned long)LocalNodeId());
                                if (!squelchQueued)
                                {
                                    SenderQueueSquelch(nextObjectId);
                                    squelchQueued = true;
                                }
                                break;
                            }
                        }
                        else
                        {
                            break; // ignore late arriving NACK
                        }
                    } // end if (object->IsStream()
                    if (holdoff)
                    {
                        if (nextObjectId == txObjectIndex)
                        {
                            //if (nextBlockId >= txBlockIndex)
                            if (Compare(nextBlockId, txBlockIndex) >= 0)
                                object->TxResetBlocks(nextBlockId, lastBlockId);
                            //else if (lastBlockId >= txBlockIndex)
                            else if (Compare(lastBlockId, txBlockIndex) >= 0)
                                object->TxResetBlocks(txBlockIndex, lastBlockId);
                        }
                        else if (nextObjectId > txObjectIndex)
                        {
                            if (object->TxResetBlocks(nextBlockId, lastBlockId))
                            {
                                if (!tx_pending_mask.Set(nextObjectId))
                                    PLOG(PL_ERROR, "NormSession::SenderHandleNackMessage() tx_pending_mask.Set(%hu) error (2)\n",
                                         (UINT16)nextObjectId);
                            }
                        }
                    }
                    else
                    {
                        // Update our minimum tx repair index as needed
                        if (tx_repair_pending)
                        {
                            if (nextObjectId < tx_repair_object_min)
                            {
                                tx_repair_object_min = nextObjectId;
                                tx_repair_block_min = nextBlockId;
                                tx_repair_segment_min = 0;
                            }
                            else if (nextObjectId == tx_repair_object_min)
                            {
                                //if (nextBlockId <= tx_repair_block_min)
                                if (Compare(nextBlockId, tx_repair_block_min) <= 0)
                                {
                                    tx_repair_block_min = nextBlockId;
                                    tx_repair_segment_min = 0;
                                }
                            }
                        }
                        else
                        {
                            tx_repair_pending = true;
                            tx_repair_object_min = nextObjectId;
                            tx_repair_block_min = nextBlockId;
                            tx_repair_segment_min = 0;
                        }
                        if (!object->HandleBlockRequest(nextBlockId, lastBlockId))
                        {
                            if (!squelchQueued)
                            {
                                SenderQueueSquelch(nextObjectId);
                                squelchQueued = true;
                            }
                        }
                        startTimer = true;
                    }
                    break;
                case SEGMENT:
                    PLOG(PL_DETAIL, "NormSession::SenderHandleNackMessage(SEGMENT) obj>%hu blk>%lu segs>%hu:%hu\n",
                         (UINT16)nextObjectId, (unsigned long)nextBlockId.GetValue(),
                         (UINT16)nextSegmentId, (UINT16)lastSegmentId);
                    inRange = false; // SEGMENT repairs are also handled in one pass
                    if (nextBlockId != prevBlockId)
                        freshBlock = true;
                    if (freshBlock)
                    {
                        // Is this entire block already repair pending?
                        if (object->IsRepairSet(nextBlockId))
                            continue;
                        if (NULL == (block = object->FindBlock(nextBlockId)))
                        {
                            // Is this entire block already tx pending?
                            if (object->IsPendingSet(nextBlockId))
                            {
                                // Entire block already tx pending, don't worry about individual segments
                                PLOG(PL_DEBUG, "NormSession::SenderHandleNackMessage() node>%lu "
                                               "recvd SEGMENT repair request for pending block.\n",
                                     (unsigned long)LocalNodeId());
                                continue;
                            }
                            else
                            {
                                // Try to recover block including parity calculation
                                if (NULL == (block = object->SenderRecoverBlock(nextBlockId)))
                                {
                                    if (NormObject::STREAM == object->GetType())
                                    {
                                        PLOG(PL_DEBUG, "NormSession::SenderHandleNackMessage() node>%lu "
                                                       "recvd repair request for old stream block(%lu) ...\n",
                                             (unsigned long)LocalNodeId(),
                                             (unsigned long)nextBlockId.GetValue());
                                        if (!squelchQueued)
                                        {
                                            SenderQueueSquelch(nextObjectId);
                                            squelchQueued = true;
                                        }
                                    }
                                    else
                                    {
                                        // Resource constrained, move on to next repair request
                                        PLOG(PL_INFO, "NormSession::SenderHandleNackMessage() node>%lu "
                                                      "Warning - sender is resource constrained ...\n",
                                             (unsigned long)LocalNodeId());
                                    }
                                    continue;
                                }
                            }
                        }
                        freshBlock = false;
                        numErasures = extra_parity;
                        prevBlockId = nextBlockId;
                    } // end if (freshBlock)
                    ASSERT(NULL != block);

                    // If stream && explicit data repair, lock the data for retransmission
                    // (TBD) this use of "ndata" needs to be replaced for dynamically shortened blocks
                    if (object->IsStream())
                    {
                        // mark nack time for potential flow control
                        static_cast<NormStreamObject *>(object)->SetLastNackTime(nextBlockId, ProtoTime(currentTime));
                        if (nextSegmentId < ndata)
                        {
                            bool attemptLock = true;
                            NormSegmentId firstLockId = nextSegmentId;
                            NormSegmentId lastLockId = ndata - 1;
                            lastLockId = MIN(lastLockId, lastSegmentId);
                            if (holdoff)
                            {
                                if (nextObjectId == txObjectIndex)
                                {
                                    //if (nextBlockId < txBlockIndex)
                                    if (Compare(nextBlockId, txBlockIndex) < 0)
                                    {
                                        //if (1 == (txBlockIndex - nextBlockId))
                                        if (1 == (UINT32)Difference(txBlockIndex, nextBlockId))
                                        {
                                            // We're currently sending this block
                                            if (block->IsPending())
                                            {
                                                NormSegmentId firstPending = 0;
                                                block->GetFirstPending(firstPending);
                                                if (lastLockId <= firstPending)
                                                    attemptLock = false;
                                                else if (nextSegmentId < firstPending)
                                                    firstLockId = firstPending;
                                            }
                                            else
                                            {
                                                // block was just recovered
                                            }
                                        }
                                        else
                                        {
                                            attemptLock = false; // NACK arrived way too late
                                        }
                                    }
                                }
                                else if (nextObjectId < txObjectIndex)
                                {
                                    attemptLock = false; // NACK arrived too late
                                }
                            } // end if (holdoff)
                            if (attemptLock)
                            {
                                if (!((NormStreamObject *)object)->LockSegments(nextBlockId, firstLockId, lastLockId))
                                {
                                    PLOG(PL_ERROR, "NormSession::SenderHandleNackMessage() node>%lu "
                                                   "LockSegments() failure\n",
                                         (unsigned long)LocalNodeId());
                                    if (!squelchQueued)
                                    {
                                        SenderQueueSquelch(nextObjectId);
                                        squelchQueued = true;
                                    }
                                    break;
                                }
                            }
                            else
                            {
                                break; // ignore late arriving NACK
                            }
                        } // end if (nextSegmentId < ndata)
                    }     // end if (object->IsStream())

                    // With a series of SEGMENT repair requests for a block, "numErasures" will
                    // eventually total the number of missing segments in the block.
                    numErasures += (lastSegmentId - nextSegmentId + 1);
                    if (holdoff)
                    {
                        if (nextObjectId > txObjectIndex)
                        {
                            if (object->TxUpdateBlock(block, nextSegmentId, lastSegmentId, numErasures))
                            {
                                if (!tx_pending_mask.Set(nextObjectId))
                                    PLOG(PL_ERROR, "NormSession::SenderHandleNackMessage() tx_pending_mask.Set(%hu) error (3)\n",
                                         (UINT16)nextObjectId);
                            }
                        }
                        else if (nextObjectId == txObjectIndex)
                        {
                            //if (nextBlockId >= txBlockIndex)
                            if (Compare(nextBlockId, txBlockIndex) >= 0)
                            {
                                object->TxUpdateBlock(block, nextSegmentId, lastSegmentId, numErasures);
                            }
                            //else if (1 == (txBlockIndex - nextBlockId))
                            else if (1 == (UINT32)Difference(txBlockIndex, nextBlockId))
                            {
                                NormSegmentId firstPending = 0;
                                if (block->GetFirstPending(firstPending))
                                {
                                    if (nextSegmentId > firstPending)
                                        object->TxUpdateBlock(block, nextSegmentId, lastSegmentId, numErasures);
                                    else if (lastSegmentId > firstPending)
                                        object->TxUpdateBlock(block, firstPending, lastSegmentId, numErasures);
                                    else if (numErasures > block->ParityCount())
                                        object->TxUpdateBlock(block, firstPending, firstPending, numErasures);
                                }
                                else
                                {
                                    // This block was just recovered, so do full update
                                    object->TxUpdateBlock(block, nextSegmentId, lastSegmentId, numErasures);
                                }
                            }
                        }
                    }
                    else // !holdoff
                    {
                        // Update our minimum tx repair index as needed
                        ASSERT(nextBlockId == block->GetId());
                        UINT16 nextBlockSize = object->GetBlockSize(nextBlockId);
                        if (tx_repair_pending)
                        {
                            if (nextObjectId < tx_repair_object_min)
                            {
                                tx_repair_block_min = nextBlockId;
                                tx_repair_segment_min = (nextSegmentId < nextBlockSize) ? nextSegmentId : (nextBlockSize - 1);
                            }
                            else if (nextObjectId == tx_repair_object_min)
                            {
                                //if (nextBlockId < tx_repair_block_min)
                                if (Compare(nextBlockId, tx_repair_block_min) < 0)
                                {
                                    tx_repair_block_min = nextBlockId;
                                    tx_repair_segment_min = (nextSegmentId < nextBlockSize) ? nextSegmentId : (nextBlockSize - 1);
                                }
                                else if (nextBlockId == tx_repair_block_min)
                                {
                                    if (nextSegmentId < tx_repair_segment_min)
                                        tx_repair_segment_min = nextSegmentId;
                                }
                            }
                        }
                        else
                        {
                            tx_repair_pending = true;
                            tx_repair_object_min = nextObjectId;
                            tx_repair_block_min = nextBlockId;
                            tx_repair_segment_min = (nextSegmentId < nextBlockSize) ? nextSegmentId : (nextBlockSize - 1);
                        }
                        block->HandleSegmentRequest(nextSegmentId, lastSegmentId,
                                                    nextBlockSize, nparity,
                                                    numErasures);
                        startTimer = true;
                    } // end if/else (holdoff)
                    break;
                case INFO:
                    // We already dealt with INFO request above with respect to initiating repair
                    nextObjectId++;
                    if (nextObjectId > lastObjectId)
                        inRange = false;
                    break;
                } // end switch(requestLevel)
            }     // end while(inRange)
        }         // end while(NextRepairItem())
    }             // end while(UnpackRepairRequest())
    if (startTimer && !repair_timer.IsActive())
    {
        // BACKOFF related code
        double aggregateInterval = address.IsMulticast() ? grtt_advertised * (backoff_factor + 1.0) : 0.0;
        // Uncommenting the line below treats ((0 == ndata) && 0.0 == backoff_factor)
        // as a special case (sets zero sender aggregateInterval)
        aggregateInterval = ((0 != nparity) || (backoff_factor > 0.0)) ? aggregateInterval : 0.0;

        // TBD - why did we do this thing here to limit the min aggregateInterval???
        // (I think to allow "11th hour NACKs to be incorporated .. so this should be
        //  for mcast only)
        if (tx_timer.IsActive() && address.IsMulticast())
        {
            double txTimeout = tx_timer.GetTimeRemaining() - 1.0e-06;
            aggregateInterval = MAX(txTimeout, aggregateInterval);
        }
        repair_timer.SetInterval(aggregateInterval);
        PLOG(PL_DEBUG, "NormSession::SenderHandleNackMessage() node>%lu starting sender "
                       "NACK aggregation timer (%lf sec)...\n",
             (unsigned long)LocalNodeId(), aggregateInterval);
        ActivateTimer(repair_timer);
    }
} // end NormSession::SenderHandleNackMessage()

void NormSession::ReceiverHandleAckMessage(const NormAckMsg &ack)
{
    NormSenderNode *theSender = (NormSenderNode *)sender_tree.FindNodeById(ack.GetSenderId());
    if (theSender)
    {
        theSender->HandleAckMessage(ack);
    }
    else if (ack.GetSenderId() != LocalNodeId())
    {
        PLOG(PL_DEBUG, "NormSession::ReceiverHandleAckMessage() node>%lu heard ACK for unknown sender>%lu\n",
             (unsigned long)LocalNodeId(), (unsigned long)ack.GetSenderId(), IsServerListener());
    }
} // end NormSession::ReceiverHandleAckMessage()

void NormSession::ReceiverHandleNackMessage(const NormNackMsg &nack)
{
    NormSenderNode *theSender = (NormSenderNode *)sender_tree.FindNodeById(nack.GetSenderId());
    if (theSender)
    {
        theSender->HandleNackMessage(nack);
    }
    else if (nack.GetSenderId() != LocalNodeId())
    {
        PLOG(PL_DEBUG, "NormSession::ReceiverHandleNackMessage() node>%lu heard NACK for unknown sender\n",
             (unsigned long)LocalNodeId());
    }
} // end NormSession::ReceiverHandleNackMessage()

bool NormSession::SenderQueueSquelch(NormObjectId objectId)
{
    // If a squelch is already queued, update it if (objectId < squelch->objectId)
    bool doEnqueue = true;
    NormCmdSquelchMsg *squelch = NULL;
    NormMsg *msg = message_queue.GetHead();
    while (NULL != msg)
    {
        // (TBD) we need to depreceate the whole "message_pool" idea and
        // have messages be built on demand in NormSession::Serve() according
        // to some state variables (i.e. that dictate when to send a command
        // instead of data, etc).  This will simplify alot of stuff and
        // probably improve performance some too.
        if (NormMsg::CMD == msg->GetType())
        {
            if (NormCmdMsg::SQUELCH == static_cast<NormCmdMsg *>(msg)->GetFlavor())
            {
                squelch = static_cast<NormCmdSquelchMsg *>(msg);
                break;
            }
        }
        msg = msg->GetNext();
    }
    if (NULL != squelch)
    {
        if (objectId >= squelch->GetObjectId())
            return false; // no need to update squelch
        doEnqueue = false;
    }
    else
    {
        squelch = (NormCmdSquelchMsg *)GetMessageFromPool();
    }
    if (squelch)
    {
        squelch->Init(fec_id);
        squelch->SetDestination(address);
        squelch->SetGrtt(grtt_quantized);
        squelch->SetBackoffFactor((unsigned char)backoff_factor);
        squelch->SetGroupSize(gsize_quantized);
        NormObject *obj = tx_table.Find(objectId);
        NormObjectTable::Iterator iterator(tx_table);
        NormObjectId nextId;
        if (NULL != obj)
        {
            ASSERT(NormObject::STREAM == obj->GetType());
            squelch->SetObjectId(objectId);
            //NormBlockId blockId = static_cast<NormStreamObject*>(obj)->StreamBufferLo();
            NormBlockId blockId = static_cast<NormStreamObject *>(obj)->RepairWindowLo();
            squelch->SetFecPayloadId(fec_id, blockId.GetValue(), 0, obj->GetBlockSize(blockId), fec_m);
            while ((obj = iterator.GetNextObject()))
                if (objectId == obj->GetId())
                    break;
            nextId = objectId + 1;
        }
        else
        {
            obj = iterator.GetNextObject();
            if (NULL != obj)
            {
                squelch->SetObjectId(obj->GetId());
                NormBlockId blockId;
                if (obj->IsStream())
                    //blockId =static_cast<NormStreamObject*>(obj)->StreamBufferLo();
                    blockId = static_cast<NormStreamObject *>(obj)->RepairWindowLo();
                else
                    blockId = NormBlockId(0);
                squelch->SetFecPayloadId(fec_id, blockId.GetValue(), 0, obj->GetBlockSize(blockId), fec_m);
                nextId = obj->GetId() + 1;
            }
            else
            {
                // Squelch to point to future object
                squelch->SetObjectId(next_tx_object_id);
                // (TBD) should the "blockLen" here be "ndata" instead? but we can't be sure
                squelch->SetFecPayloadId(fec_id, 0, 0, 0, fec_m);
                nextId = next_tx_object_id;
            }
        }
        bool buildingList = true;
        while (buildingList && (obj = iterator.GetNextObject()))
        {
            while (nextId != obj->GetId())
            {
                if (!squelch->AppendInvalidObject(nextId, segment_size))
                {
                    buildingList = false;
                    break;
                }
                nextId++;
            }
            nextId++;
        }
        if (doEnqueue)
        {
            QueueMessage(squelch);
            PLOG(PL_DEBUG, "NormSession::SenderQueueSquelch() node>%lu sender queued squelch ...\n",
                 (unsigned long)LocalNodeId());
        }
        else
        {
            PLOG(PL_DEBUG, "NormSession::SenderQueueSquelch() node>%lu sender updated squelch ...\n",
                 (unsigned long)LocalNodeId());
        }
        return true;
    }
    else
    {
        PLOG(PL_FATAL, "NormSession::SenderQueueSquelch() node>%lu message_pool exhausted! (couldn't squelch)\n",
             (unsigned long)LocalNodeId());
        return false;
    }
} // end NormSession::SenderQueueSquelch()

bool NormSession::SenderSendCmd(const char *cmdBuffer, unsigned int cmdLength, bool robust)
{
    if (!is_sender)
    {
        PLOG(PL_ERROR, "NormSession::SenderSendCmd() error: non-sender session!\n");
        return false;
    }
    if (0 != cmd_count)
    {
        PLOG(PL_INFO, "NormSession::SenderSendCmd() error: command already pending!\n");
        return false;
    }
    else if (cmdLength > segment_size)
    {
        PLOG(PL_INFO, "NormSession::SenderSendCmd() error: command length greater than segment_size!\n");
        return false;
    }
    memcpy(cmd_buffer, cmdBuffer, cmdLength);
    cmd_length = cmdLength;
    cmd_count = robust ? tx_robust_factor : 1;
    if (!tx_timer.IsActive())
        PromptSender();
    return true;
} // end NormSession::SenderSendCmd()

void NormSession::SenderCancelCmd()
{
    if (0 != cmd_count)
    {
        if (cmd_timer.IsActive())
            cmd_timer.Deactivate();
        cmd_count = 0;
        cmd_length = 0;
    }
} // end NormSession::SenderCancelCmd()

bool NormSession::SenderSendAppCmd(const char *buffer, unsigned int length, const ProtoAddress &dst)
{
    // Build/immediately send a NORM_CMD(APPLICATION) message
    NormCmdAppMsg appMsg;
    appMsg.Init();
    appMsg.SetDestination(address);
    appMsg.SetGrtt(grtt_quantized);
    appMsg.SetBackoffFactor((unsigned char)backoff_factor);
    appMsg.SetGroupSize(gsize_quantized);
    // We use a surrogate segment_size in case sender not configured (e.g. server-listener)
    appMsg.SetContent(buffer, length, segment_size ? segment_size : 64);
    appMsg.SetDestination(dst);
    if (MSG_SEND_OK != SendMessage(appMsg))
        PLOG(PL_ERROR, "NormSession::SenderSendAppCmd() node>%lu sender unable to send app-defined cmd ...\n",
             (unsigned long)LocalNodeId());
    else
        PLOG(PL_DEBUG, "NormSession::SenderSendAppCmd() node>%lu sender sending app-defined cmd len:%u...\n",
             (unsigned long)LocalNodeId(), appMsg.GetLength());
    return true;
} // end NormSession::SenderSendAppCmd()

bool NormSession::SenderQueueAppCmd()
{
    if (0 == cmd_count)
        return false;
    ASSERT(!cmd_timer.IsActive());
    // 1) Build a NORM_CMD(APPLICATION) message
    NormCmdAppMsg *appMsg = static_cast<NormCmdAppMsg *>(GetMessageFromPool());
    if (NULL != appMsg)
    {
        appMsg->Init();
        appMsg->SetDestination(address);
        appMsg->SetGrtt(grtt_quantized);
        appMsg->SetBackoffFactor((unsigned char)backoff_factor);
        appMsg->SetGroupSize(gsize_quantized);
        appMsg->SetContent(cmd_buffer, cmd_length, segment_size);
        QueueMessage(appMsg);
        PLOG(PL_DEBUG, "NormSession::SenderQueueAppCmd() node>%lu sender queued app-defined cmd ...\n",
             (unsigned long)LocalNodeId());
        cmd_count--;
        if (0 != cmd_count)
        {
            cmd_timer.SetInterval(grtt_advertised * 2);
            ActivateTimer(cmd_timer);
        }
        else
        {
            PLOG(PL_DEBUG, "NormSession::SenderQueueAppCmd() node>%lu cmd transmission completed ...\n",
                 (unsigned long)LocalNodeId());
            Notify(NormController::TX_CMD_SENT, NULL, NULL);
        }
        return true;
    }
    else
    {
        PLOG(PL_FATAL, "NormSession::SenderQueueAppCmd() node>%lu message_pool exhausted!\n",
             (unsigned long)LocalNodeId());
        return false;
    }
} // end NormSession::SenderQueueAppCmd()

bool NormSession::OnCmdTimeout(ProtoTimer &theTimer)
{
    if (!tx_timer.IsActive())
        PromptSender();
    return true;
} // end NormSession::OnCmdTimeout()

void NormSession::ActivateFlowControl(double delay, NormObjectId objectId, NormController::Event event)
{
    flow_control_object = objectId;
    flow_control_event = event;
    flow_control_timer.SetInterval(delay);
    if (flow_control_timer.IsActive())
        flow_control_timer.Reschedule();
    else
        ActivateTimer(flow_control_timer);
} // end NormSession::ActivateFlowControl()

bool NormSession::OnFlowControlTimeout(ProtoTimer &theTimer)
{
    NormObject *object = tx_table.Find(flow_control_object);
    if (NULL == object)
    {
        PLOG(PL_WARN, "NormSession::OnFlowControlTimeout() flow_control_object removed?!\n");
        // Throw a TX_QUEUE_EMPTY just in case ???
        //Notify(NormController::TX_QUEUE_EMPTY, (NormSenderNode*)NULL, (NormObject*)NULL);
        return true;
    }
    double deltaTime;
    if (object->IsStream())
    {
        // A stream was flow-controlled, so check its stream nack age, etc
        NormBlock *block = static_cast<NormStreamObject *>(object)->StreamBlockLo();
        if (NULL == block)
        {
            // No blocks in stream buffer, thus it is actually empty
            // Notify(NormController::TX_QUEUE_VACANCY, (NormSenderNode*)NULL, object);
            posted_tx_queue_empty = true;
            Notify(NormController::TX_QUEUE_EMPTY, (NormSenderNode *)NULL, object); //(NormObject*)NULL);
            return true;
        }
        deltaTime = GetFlowControlDelay() - block->GetNackAge();
        if (deltaTime < 1.0e-06)
        {
            // no recent NACKing for "oldest block", so post EMPTY/VACANCY if non-pending
            // TBD - retest this with mixed stream/object sender sessions
            if (!block->IsPending())
            {
                posted_tx_queue_empty = (NormController::TX_QUEUE_EMPTY == flow_control_event);
                Notify(flow_control_event, (NormSenderNode *)NULL, object);
            }
            return true;
        }
    }
    else
    {
        // The tx cache (queue) is being flow controlled ...
        deltaTime = GetFlowControlDelay() - object->GetNackAge();
        if (deltaTime < 1.0e-06)
        {
            // no recent NACKing, so if non-pending, dispatch queue EMPTY or VACANCY
            if (!object->IsRepairPending() && !object->IsPending())
            {
                posted_tx_queue_empty = (NormController::TX_QUEUE_EMPTY == flow_control_event);
                Notify(flow_control_event, (NormSenderNode *)NULL, (NormObject *)NULL);
            }
            return true;
        }
    }
    // Extend flow control timeout due to recent activity
    // NOTE that above we limited the minimum "deltaTime" to 1.0-06 ... otherwise
    // ProtoTime 1usec precision limitation put us into an infinite loop in
    // _simulation_ environments where perfect scheduling occurs w/ zero processing time
    theTimer.SetInterval(deltaTime);
    theTimer.Reschedule();
    return false;
} // end NormSession::OnFlowControlTimeout()

bool NormSession::SenderBuildRepairAdv(NormCmdRepairAdvMsg &cmd)
{
    // Build a NORM_CMD(REPAIR_ADV) message with current pending repair state.
    NormRepairRequest req;
    req.SetFlag(NormRepairRequest::OBJECT);
    NormRepairRequest::Form prevForm = NormRepairRequest::INVALID;
    NormObjectId firstId;
    UINT16 objectCount = 0;
    NormObjectTable::Iterator iterator(tx_table);
    NormObject *nextObject = iterator.GetNextObject();
    while (NULL != nextObject)
    {
        NormObject *currentObject = nextObject;
        nextObject = iterator.GetNextObject();
        NormObjectId currentId = currentObject->GetId();
        bool repairEntireObject = tx_repair_mask.Test(currentId);
        if (repairEntireObject)
        {
            if (!objectCount)
                firstId = currentId; // set first OBJECT level repair id
            objectCount++;           // increment consecutive OBJECT level repair count.
        }

        // Check for non-OBJECT level request or end
        if (objectCount && (!repairEntireObject || (NULL != nextObject)))
        {
            NormRepairRequest::Form form;
            switch (objectCount)
            {
            case 0:
                form = NormRepairRequest::INVALID;
                break;
            case 1:
            case 2:
                form = NormRepairRequest::ITEMS;
                break;
            default:
                form = NormRepairRequest::RANGES;
                break;
            }
            if (form != prevForm)
            {
                if (NormRepairRequest::INVALID != prevForm)
                {
                    if (0 == cmd.PackRepairRequest(req))
                    {
                        prevForm = NormRepairRequest::INVALID;
                        PLOG(PL_WARN, "NormSession::SenderBuildRepairAdv() warning: full msg\n");
                        // (TBD) set NORM_REPAIR_ADV_LIMIT flag in this case
                        break;
                    }
                }
                req.SetForm(form);
                cmd.AttachRepairRequest(req, segment_size);
                prevForm = form;
            }
            switch (form)
            {
            case 0:
                ASSERT(0); // can't happen
                break;
            case 1:
            case 2:
                req.SetForm(NormRepairRequest::ITEMS);
                req.AppendRepairItem(fec_id, fec_m, firstId, 0, ndata, 0); // (TBD) error check
                if (2 == objectCount)
                    req.AppendRepairItem(fec_id, fec_m, currentId, 0, ndata, 0); // (TBD) error check
                break;
            default:
                req.SetForm(NormRepairRequest::RANGES);
                req.AppendRepairRange(fec_id, fec_m, firstId, 0, ndata, 0, // (TBD) error check
                                      currentId, 0, ndata, 0);
                break;
            }
            prevForm = NormRepairRequest::INVALID;
            if (0 == cmd.PackRepairRequest(req))
            {
                PLOG(PL_WARN, "NormSession::SenderBuildRepairAdv() warning: full msg\n");
                // (TBD) set NORM_REPAIR_ADV_LIMIT flag in this case
                break;
            }
            objectCount = 0;
        }
        if (!repairEntireObject)
        {
            if (currentObject->IsRepairPending())
            {
                if (NormRepairRequest::INVALID != prevForm) // && currentObject->IsRepairPending())
                {
                    prevForm = NormRepairRequest::INVALID;
                    if (0 == cmd.PackRepairRequest(req))
                    {
                        PLOG(PL_WARN, "NormSession::SenderBuildRepairAdv() warning: full msg\n");
                        // (TBD) set NORM_REPAIR_ADV_LIMIT flag in this case
                        break;
                    }
                }
                if (!currentObject->AppendRepairAdv(cmd))
                    break;
            }
            objectCount = 0; // this is probably redundant
        }
    } // end while (nextObject)
    if (NormRepairRequest::INVALID != prevForm)
    {
        if (0 == cmd.PackRepairRequest(req))
            PLOG(PL_ERROR, "NormSession::SenderBuildRepairAdv() warning: full msg\n");
        // (TBD) set NORM_REPAIR_ADV_LIMIT flag in this case
    }
    return true;
} // end NormSession::SenderBuildRepairAdv()

bool NormSession::OnRepairTimeout(ProtoTimer & /*theTimer*/)
{
    tx_repair_pending = false;
    if (0 != repair_timer.GetRepeatCount())
    {
        // NACK aggregation period has ended. (incorporate accumulated repair requests)
        PLOG(PL_DEBUG, "NormSession::OnRepairTimeout() node>%lu sender NACK aggregation time ended.\n",
             (unsigned long)LocalNodeId());
        NormObjectTable::Iterator iterator(tx_table);
        NormObject *obj;
        while ((obj = iterator.GetNextObject()))
        {
            NormObjectId objectId = obj->GetId();
            if (tx_repair_mask.Test(objectId))
            {
                PLOG(PL_TRACE, "NormSession::OnRepairTimeout() node>%lu tx reset obj>%hu ...\n",
                     (unsigned long)LocalNodeId(), (UINT16)objectId);
                if (obj->IsStream())
                    obj->TxReset(((NormStreamObject *)obj)->RepairWindowLo());
                else
                    obj->TxReset();
                tx_repair_mask.Unset(objectId);
                if (!tx_pending_mask.Set(objectId))
                {
                    PLOG(PL_ERROR, "NormSession::OnRepairTimeout() tx_pending_mask.Set(%hu) error (1)\n",
                         (UINT16)objectId);
                }
            }
            else
            {
                PLOG(PL_DEBUG, "NormSession::OnRepairTimeout() node>%lu activating obj>%hu repairs ...\n",
                     (unsigned long)LocalNodeId(), (UINT16)objectId);
                if (obj->ActivateRepairs())
                {
                    PLOG(PL_TRACE, "NormSession::OnRepairTimeout() node>%lu activated obj>%hu repairs ...\n",
                         (unsigned long)LocalNodeId(), (UINT16)objectId);
                    if (!tx_pending_mask.Set(objectId))
                        PLOG(PL_ERROR, "NormSession::OnRepairTimeout() node>%lu tx_pending_mask.Set(%hu) error (2)\n",
                             (unsigned long)LocalNodeId(), (UINT16)objectId);
                }
            }
        } // end while (iterator.GetNextObject())
        PromptSender();
        // BACKOFF related code
        // Holdoff initiation of new repair cycle for one GRTT
        // (TBD) for unicast sessions, use CLR RTT ???
        //double holdoffInterval = backoff_factor > 0.0 ? grtt_advertised : 0.0;
        double holdoffInterval = grtt_advertised;
        repair_timer.SetInterval(holdoffInterval); // repair holdoff interval = 1*GRTT
        PLOG(PL_DEBUG, "NormSession::OnRepairTimeout() node>%lu starting sender "
                       "NACK holdoff timer (%lf sec)...\n",
             (unsigned long)LocalNodeId(), holdoffInterval);
    }
    else
    {
        // REPAIR holdoff interval has now ended.
        PLOG(PL_DEBUG, "NormSession::OnRepairTimeout() node>%lu sender holdoff time ended.\n",
             (unsigned long)LocalNodeId());
    }
    return true;
} // end NormSession::OnRepairTimeout()

// (TBD) Should pass current system time to ProtoTimer timeout handlers
//       for more efficiency ...
bool NormSession::OnTxTimeout(ProtoTimer & /*theTimer*/)
{
    NormMsg *msg;

    // Note: sometimes need RepairAdv even when cc_enable is false ...
    NormCmdRepairAdvMsg adv;
    if (advertise_repairs && (probe_proactive || (repair_timer.IsActive() &&
                                                  repair_timer.GetRepeatCount())))
    {
        // Build a NORM_CMD(NACK_ADV) in response to
        // receipt of unicast NACK or CC update
        adv.Init();
        adv.SetGrtt(grtt_quantized);
        adv.SetBackoffFactor((unsigned char)backoff_factor);
        adv.SetGroupSize(gsize_quantized);
        adv.SetDestination(address);

        // Fill in congestion control header extension
        NormCCFeedbackExtension ext;
        adv.AttachExtension(ext);

        if (suppress_rate < 0.0)
        {
            ext.SetCCFlag(NormCC::RTT);
            ext.SetCCRtt(grtt_quantized);
            ext.SetCCRate(NormQuantizeRate(tx_rate));
        }
        else
        {
            if (!suppress_nonconfirmed)
                ext.SetCCFlag(NormCC::RTT);
            ext.SetCCRtt(NormQuantizeRtt(suppress_rtt));
            ext.SetCCRate(NormQuantizeRate(suppress_rate));
        }

        SenderBuildRepairAdv(adv);

        msg = (NormMsg *)&adv;
    }
    else
    {
        msg = message_queue.RemoveHead();
        advertise_repairs = false;
    }

    if (NULL != msg)
    {
        // Do "packet pairing of NORM_CMD(CC) and subsequent message (usually NORM_DATA), if any
        //unsigned int msgLength = msg->GetLength();

        unsigned int msgLength = tx_residual;
        /* Uncomment this section of code to instate CMD(CC) / NORM_DATA "packet pairing"
        tx_residual = 0;
        if ((NormMsg::CMD == msg->GetType()) && 
            (NormCmdMsg::CC == static_cast<NormCmdMsg*>(msg)->GetFlavor()))
        {
            tx_residual = msg->GetLength();
        }
        else
            */
        {
            msgLength += msg->GetLength();
        }

        switch (SendMessage(*msg))
        {
        case MSG_SEND_OK:
            if (tx_rate > 0.0)
                tx_timer.SetInterval(GetTxInterval(msgLength, tx_rate));
            if (advertise_repairs)
            {
                advertise_repairs = false;
                suppress_rate = -1.0; // reset cc feedback suppression rate
            }
            else
            {
                ReturnMessageToPool(msg);
            }
            // Pre-serve to allow pre-prompt for empty tx queue
            // (TBD) do this in a better way ???  There is a slight chance
            // that with this approach some new data may get pre-queued
            // when an interim repair request should be serviced first
            // instead ???
            //if (message_queue.IsEmpty() && IsSender()) Serve();
            return true; // reinstall tx_timer

        case MSG_SEND_BLOCKED:
            // Message was not sent due to to EWOULDBLOCK, so we invoke async i/o output notification
            if (!advertise_repairs)
                message_queue.Prepend(msg);
            if (tx_timer.IsActive())
                tx_timer.Deactivate();
            tx_socket->StartOutputNotification();
            return false; // since timer was deactivated

        case MSG_SEND_FAILED:
            // Message was not sent due to socket error (no route, etc), so so just timeout and try again
            // (TBD - is there something smarter we should do)
            if (!advertise_repairs)
                message_queue.Prepend(msg);
            if (tx_rate > 0.0)
                tx_timer.SetInterval(GetTxInterval(msgLength, tx_rate));
            else if (0.0 == tx_timer.GetInterval())
                tx_timer.SetInterval(0.001);
            return true; // timer will be reactivated
        }
    }
    else
    {
        // 1) Prompt for next sender message
        if (IsSender())
            Serve();

        if (message_queue.IsEmpty())
        {
            if (tx_timer.IsActive())
                tx_timer.Deactivate();
            // Check that any possible notifications posted in
            // the previous call to Serve() may have caused a
            // change in sender state making it ready to send
            //if (IsSender()) Serve();
            return false;
        }
        else
        {
            // We have a new message as a result of serving, so send it immediately
            return OnTxTimeout(tx_timer);
        }
    }
    return true; // actually will never get here but compiler thinks it's needed
} // end NormSession::OnTxTimeout()

NormSession::MessageStatus NormSession::SendMessage(NormMsg &msg)
{
    bool isReceiverMsg = false;
    bool isProbe = false;
    bool sendRaw = false;

    // Fill in any last minute timestamps
    // (TBD) fill in InstanceId fields on all messages as needed
    // We need "fec_m" for the message for NormTrace() purposes
    UINT8 fecM = fec_m;          // assume it's a sender message (will be overridden otherwise)
    UINT16 instId = instance_id; // assume it's a sender message (will be overridden otherwise)
    switch (msg.GetType())
    {
        case NormMsg::INFO:
        case NormMsg::DATA:
        {
            NormObjectMsg &objMsg = static_cast<NormObjectMsg &>(msg);
            objMsg.SetInstanceId(instId);
            msg.SetSequence(tx_sequence++); // (TBD) set for session dst msgs
            if (syn_status)
                objMsg.SetFlag(NormObjectMsg::FLAG_SYN);
            break;
        }
        case NormMsg::CMD:
        {
            NormCmdMsg &cmd = static_cast<NormCmdMsg &>(msg);
            ((NormCmdMsg &)msg).SetInstanceId(instId);
            switch (cmd.GetFlavor())
            {
                case NormCmdMsg::CC:
                {
                    NormCmdCCMsg &ccMsg = static_cast<NormCmdCCMsg &>(cmd);
                    struct timeval currentTime;
                    ProtoSystemTime(currentTime);
                    ccMsg.SetSendTime(currentTime);
                    isProbe = true;
                    if (0 != probe_tos)
                        sendRaw = true;  // so probe will be marked accordingly
                    if (syn_status)
                        ccMsg.SetSyn();
                    break;
                }
                case NormCmdMsg::SQUELCH:
                    break;
                default:
                    break;
            }
            msg.SetSequence(tx_sequence++); // (TBD) set for session dst msgs
            break;
        }
        case NormMsg::NACK:
        {
            msg.SetSequence(0); // TBD - set per destination
            isReceiverMsg = true;
            NormNackMsg &nack = (NormNackMsg &)msg;
            NormSenderNode *theSender =
                (NormSenderNode *)sender_tree.FindNodeById(nack.GetSenderId());
            ASSERT(NULL != theSender);
            fecM = theSender->GetFecFieldSize();
            instId = theSender->GetInstanceId();
            struct timeval grttResponse;
            // When probe_tos is non-zero, GRTT feedback is in ACKs only
            if (0 == probe_tos)
            {
                struct timeval currentTime;
                ProtoSystemTime(currentTime);
                theSender->CalculateGrttResponse(currentTime, grttResponse);
            }
            else
            {
                grttResponse.tv_sec = grttResponse.tv_usec = 0;
            }
            nack.SetGrttResponse(grttResponse);
            break;
        }
        case NormMsg::ACK:
        {
            msg.SetSequence(0); // TBD - set per destination
            isReceiverMsg = true;
            NormAckMsg &ack = (NormAckMsg &)msg;
            NormSenderNode *theSender;
            if (IsServerListener())
                theSender = client_tree.FindNodeByAddress(ack.GetDestination());
            else
                theSender = (NormSenderNode *)sender_tree.FindNodeById(ack.GetSenderId());
            ASSERT(NULL != theSender);
            fecM = theSender->GetFecFieldSize();
            instId = theSender->GetInstanceId();
            struct timeval grttResponse;
            if ((0 == probe_tos) || (NormAck::CC == ack.GetAckType()))
            {
                struct timeval currentTime;
                ProtoSystemTime(currentTime);
                theSender->CalculateGrttResponse(currentTime, grttResponse);
                if (0 != probe_tos) sendRaw = true;
            }
            else
            {
                grttResponse.tv_sec = grttResponse.tv_usec = 0;
            }
            ack.SetGrttResponse(grttResponse);
            break;
        }
        default:
            break;
    }
    // Fill in common message fields
    msg.SetSourceId(local_node_id);
    UINT16 msgSize = msg.GetLength();
    // Possibly drop some tx messages for testing purposes

    bool drop = (tx_loss_rate > 0.0) ? (UniformRand(100.0) < tx_loss_rate) : false;

    if (isReceiverMsg && receiver_silent)
    {
        // don't send receiver messages if "silent receiver"
        // TBD - perhaps we should make sure silent receivers
        //       never enqueue any receiver messages.  But we
        //       did this to make sure all integrity of timer
        //       state interdependencies wasn't messed up
        return MSG_SEND_OK; // we lie as it wasn't sent but it wasn't supposed to
    }
    else if (drop)
    {
        //DMSG(0, "TX MESSAGE DROPPED! (tx_loss_rate:%lf\n", tx_loss_rate);
        // "Pretend" like dropped message was sent for trace and timing purposes
        if (trace)
        {
            struct timeval currentTime;
            ProtoSystemTime(currentTime);
            NormTrace(currentTime, LocalNodeId(), msg, true, fecM, instId);
        }
        // Update sent rate tracker even if dropped (for testing/debugging)
        sent_accumulator.Increment(msgSize);
        nominal_packet_size += 0.01 * (((double)msgSize) - nominal_packet_size);
    }
    else
    {
        unsigned int numBytes = msgSize;
        bool result;
#ifdef ECN_SUPPORT
        if (sendRaw)
            result = RawSendTo(msg.GetBuffer(), numBytes, msg.GetDestination(), probe_tos);
        else
#endif // ECN_SUPPORT
            result = tx_socket->SendTo(msg.GetBuffer(), numBytes, msg.GetDestination());
        if (result)
        {
            if (numBytes == msgSize)
            {
                if (posted_send_error)
                {
                    // Clear SEND_ERROR indication
                    posted_send_error = false;
                    Notify(NormController::SEND_OK, NULL, NULL);
                }
                // Separate send/recv tracing
                if (trace)
                {
                    struct timeval currentTime;
                    ProtoSystemTime(currentTime);
                    NormTrace(currentTime, LocalNodeId(), msg, true, fecM, instId);
                }
                // To keep track of _actual_ sent rate
                sent_accumulator.Increment(msgSize);
                // Update nominal packet size
                nominal_packet_size += 0.01 * (((double)msgSize) - nominal_packet_size);
            }
            else
            {
                // packet not sent
                tx_sequence--;
                // TBD - is PL_WARN too verbose here
                PLOG(PL_WARN, "NormSession::SendMessage() sendto(%s/%hu) 'blocked' warning: %s\n",
                     msg.GetDestination().GetHostString(), msg.GetDestination().GetPort(), GetErrorString());
                return MSG_SEND_BLOCKED;
            }
        }
        else
        {
            // packet not sent
            tx_sequence--;
            PLOG(PL_WARN, "NormSession::SendMessage() sendto(%s/%hu) 'failed' warning: %s\n",
                 msg.GetDestination().GetHostString(), msg.GetDestination().GetPort(), GetErrorString());
            if (!posted_send_error)
            {
                // Post a Notify(NormController::SEND_ERROR, NULL, NULL);
                posted_send_error = true;
                Notify(NormController::SEND_ERROR, NULL, NULL);
            }
            return MSG_SEND_FAILED;
        }
    }
    if (isProbe)
    {
        probe_pending = false;
        probe_data_check = true;
        if (probe_reset)
        {
            probe_reset = false;
            if (!probe_timer.IsActive())
                ActivateTimer(probe_timer);
        }
    }
    else if (!isReceiverMsg && IsSender())
    {
        probe_data_check = false;
        if (!probe_pending && probe_reset)
        {
            probe_reset = false;
            OnProbeTimeout(probe_timer);
            if (!probe_timer.IsActive())
                ActivateTimer(probe_timer);
        }
    }
    return MSG_SEND_OK;
} // end NormSession::SendMessage()

#ifdef ECN_SUPPORT
bool NormSession::RawSendTo(const char* buffer, unsigned int& numBytes, const ProtoAddress& dstAddr, UINT8 trafficClass)
{
    // Send the message via proto_cap instead of UDP socket
    // (Used for marking GRTT probing with different traffic class
    //  to inform lower layer protocols to not delay the messages
    //  via retransmission or other means).
    UINT32 pcapBuffer[8192/4];  // TBD - Is this big enough???
    UINT16* ethBuffer = (UINT16*)pcapBuffer + 2;  // offset for IP packet alignment
    ProtoPktETH ethPkt(ethBuffer, 8192 - 2);
    
    // Ethernet source address will be set by ProtoCap::Forward() method
    ProtoAddress etherDst;
    if (dstAddr.IsMulticast())
        etherDst.GetEthernetMulticastAddress(dstAddr);
    else
        etherDst = PROTO_ADDR_BROADCAST;
    ethPkt.SetDstAddr(etherDst);
    
    
    
    if (ecn_enabled)
    {
        trafficClass |= ((UINT8)ProtoSocket::ECN_ECT0);  // set ECT0 bit
        trafficClass &= ~((UINT8)ProtoSocket::ECN_ECT1); // clear ECT1 bit
    }
    
    switch (dstAddr.GetType())
    {
        case ProtoAddress::IPv4:
        {
            ethPkt.SetType(ProtoPktETH::IP);
            ProtoPktIPv4 ip4Pkt;
            ip4Pkt.InitIntoBuffer(ethPkt.AccessPayload(), ethPkt.GetBufferLength() - ethPkt.GetHeaderLength());
            ip4Pkt.SetTOS(trafficClass);
            ip4Pkt.SetID((UINT16)rand());
            ip4Pkt.SetTTL(ttl);
            ip4Pkt.SetProtocol(ProtoPktIP::UDP);
            ip4Pkt.SetSrcAddr(src_addr);
            ip4Pkt.SetDstAddr(dstAddr);
            ProtoPktUDP udpPkt(ip4Pkt.AccessPayload(), ip4Pkt.GetBufferLength() - ip4Pkt.GetHeaderLength());
            udpPkt.SetSrcPort(GetTxPort());
            udpPkt.SetDstPort(dstAddr.GetPort());
            udpPkt.SetPayload(buffer, numBytes);
            ip4Pkt.SetPayloadLength(udpPkt.GetLength());
            udpPkt.FinalizeChecksum(ip4Pkt);
            ethPkt.SetPayloadLength(ip4Pkt.GetLength());
            break;
        }
        case ProtoAddress::IPv6:
            ethPkt.SetType(ProtoPktETH::IPv6);
            // IPv6 support TBD
            return false;
            break;
        default:
             PLOG(PL_ERROR, "NormSession::RawSendTo() error: invalid address type!\n");
             return false;
    } 
    unsigned int ethBytes = ethPkt.GetLength();
    bool result =  proto_cap->Forward((char*)ethPkt.AccessBuffer(), ethBytes);
    if (!result)
    {
        PLOG(PL_WARN, "NormSession::RawSendTo() warning: proto_cap send failure!\n");
        if (0 == ethBytes) numBytes = 0;
    }
    return result;
}
#endif  // NormSession::RawSendTo()

void NormSession::SetGrttProbingInterval(double intervalMin, double intervalMax)
{
    if ((intervalMin < 0.0) || (intervalMax < 0.0))
        return;
    double temp = intervalMin;
    if (temp > intervalMax)
    {
        intervalMin = intervalMax;
        intervalMax = temp;
    }
    if (intervalMin < NORM_TICK_MIN)
        intervalMin = NORM_TICK_MIN;
    if (intervalMax < NORM_TICK_MIN)
        intervalMax = NORM_TICK_MIN;
    grtt_interval_min = intervalMin;
    grtt_interval_max = intervalMax;
    if (grtt_interval < grtt_interval_min)
        grtt_interval = grtt_interval_min;
    if (grtt_interval > grtt_interval_max)
    {
        grtt_interval = grtt_interval_max;
        if (probe_timer.IsActive() && !cc_enable)
        {
            double elapsed = probe_timer.GetInterval() - probe_timer.GetTimeRemaining();
            if (elapsed < 0.0)
                elapsed = 0.0;
            if (elapsed > grtt_interval)
                probe_timer.SetInterval(0.0);
            else
                probe_timer.SetInterval(grtt_interval - elapsed);
            probe_timer.Reschedule();
        }
    }
} // end NormSession::SetGrttProbingInterval()

void NormSession::SetGrttProbingMode(ProbingMode probingMode)
{
    if (cc_enable)
        return; // can't change probing mode when cc is enabled!
                // (cc _requires_ probing mode == PROBE_ACTIVE)
    switch (probingMode)
    {
    case PROBE_NONE:
        probe_reset = false;
        if (probe_timer.IsActive())
            probe_timer.Deactivate();
        break;
    case PROBE_PASSIVE:
        probe_proactive = false;
        if (IsSender())
        {
            if (!probe_timer.IsActive())
            {
                probe_timer.SetInterval(0.0);
                ActivateTimer(probe_timer);
            }
        }
        else
        {
            probe_reset = true;
        }
        break;
    case PROBE_ACTIVE:
        probe_proactive = true;
        if (IsSender())
        {
            if (!probe_timer.IsActive())
            {
                probe_timer.SetInterval(0.0);
                ActivateTimer(probe_timer);
            }
        }
        else
        {
            probe_reset = true;
        }
        break;
    }
} // end NormSession::SetGrttProbingMode()

bool NormSession::OnProbeTimeout(ProtoTimer & /*theTimer*/)
{
    // 1) Temporarily kill probe_timer if CMD(CC) not yet tx'd
    //    (or if data has not been sent since last probe)
    if (probe_pending || (data_active && probe_data_check) || (0.0 == tx_rate))
    {
        probe_reset = true;
        if (probe_timer.IsActive())
            probe_timer.Deactivate();
        return false;
    }

    // 2) Update grtt_estimate _if_ sufficient time elapsed.
    // This new code allows more liberal downward adjustment of
    // of grtt when congestion control is enabled.

    // We have to keep track of the _actual_ deltaTime instead
    // of relying on the probe_timer interval because in real-
    // world operating systems, they're aren't the same and
    // sometimes not even close.
    struct timeval currentTime;
    ProtoSystemTime(currentTime);
    if ((0 == probe_time_last.tv_sec) && (0 == probe_time_last.tv_usec))
    {
        grtt_age += probe_timer.GetInterval();
    }
    else
    {
        double deltaTime = currentTime.tv_sec - probe_time_last.tv_sec;
        if (currentTime.tv_usec > probe_time_last.tv_usec)
            deltaTime += 1.0e-06 * ((double)(currentTime.tv_usec - probe_time_last.tv_usec));
        else
            deltaTime -= 1.0e-06 * ((double)(probe_time_last.tv_usec - currentTime.tv_usec));
        grtt_age += deltaTime;
    }
    probe_time_last = currentTime;

    // (TBD) We need to revisit the whole set of issues surrounding dynamic
    // estimation of grtt, particularly when congestion control is involved.
    // The main issue is when the rate increases rapidly with respect to
    // how the grtt estimate is descreasing ... this is most notable at
    // startup and thus the hack here to allow the grtt estimate to more
    // rapidly decrease during "slow start"
    double ageMax = grtt_advertised;
    if (!cc_enable && !cc_slow_start)
        ageMax = ageMax > grtt_interval_min ? ageMax : grtt_interval_min;
    if (grtt_age >= ageMax)
    {
        if (grtt_response)
        {
            // Update grtt estimate
            if (grtt_current_peak < grtt_measured)
            {
                grtt_measured *= 0.9;
                if (grtt_current_peak > grtt_measured)
                    grtt_measured = grtt_current_peak;
                // (TBD) "grtt_decrease_delay_count" isn't needed any more ...
                /*if (grtt_decrease_delay_count-- == 0)
                {
                    grtt_measured = 0.5 * grtt_measured + 
                                    0.5 * grtt_current_peak;
                    grtt_current_peak = 0.0;
                    grtt_decrease_delay_count = DEFAULT_GRTT_DECREASE_DELAY;
                }*/
            }
            else
            {
                // Increase already incorporated
                grtt_current_peak = 0.0;
                grtt_decrease_delay_count = DEFAULT_GRTT_DECREASE_DELAY;
            }
            if (grtt_measured < NORM_GRTT_MIN)
                grtt_measured = NORM_GRTT_MIN;
            else if (grtt_measured > grtt_max)
                grtt_measured = grtt_max;
            UINT8 grttQuantizedOld = grtt_quantized;
            double pktInterval = (double)(44 + segment_size) / tx_rate;
            if (grtt_measured < pktInterval)
                grtt_quantized = NormQuantizeRtt(pktInterval);
            else
                grtt_quantized = NormQuantizeRtt(grtt_measured);
            // Recalculate grtt_advertise since quantization rounds upward
            grtt_advertised = NormUnquantizeRtt(grtt_quantized);
            if (grtt_advertised > grtt_max)
            {
                grtt_quantized = NormQuantizeRtt(grtt_max);
                grtt_advertised = NormUnquantizeRtt(grtt_quantized);
            }
            if (grttQuantizedOld != grtt_quantized)
            {
                Notify(NormController::GRTT_UPDATED, (NormSenderNode *)NULL, (NormObject *)NULL);
                PLOG(PL_DEBUG, "NormSession::OnProbeTimeout() node>%lu decreased to new grtt to: %lf sec\n",
                     (unsigned long)LocalNodeId(), grtt_advertised);
            }
            grtt_response = false; // reset
        }
        grtt_age = 0.0;
    }

    if (grtt_interval < grtt_interval_min)
        grtt_interval = grtt_interval_min;
    else
        grtt_interval *= 1.5;
    if (grtt_interval > grtt_interval_max)
        grtt_interval = grtt_interval_max;

    // 3) Build a NORM_CMD(CC) message
    NormCmdCCMsg *cmd = (NormCmdCCMsg *)GetMessageFromPool();
    if (!cmd)
    {
        PLOG(PL_FATAL, "NormSession::OnProbeTimeout() node>%lu message_pool empty! can't probe\n",
             (unsigned long)LocalNodeId());
        ASSERT(0);
        return true;
    }
    cmd->Init();
    cmd->SetDestination(address);
    cmd->SetGrtt(grtt_quantized);
    cmd->SetBackoffFactor((unsigned char)backoff_factor);
    cmd->SetGroupSize(gsize_quantized);
    // defer SetSendTime() to when message is being sent (in OnTxTimeout())
    cmd->SetCCSequence(cc_sequence++);

    // Insert NORM-CC header extension, if applicable
    // (Note we set the extension "rate" _after_ AdjustRate() done below)
    NormCCRateExtension ext;
    if (probe_proactive)
        cmd->AttachExtension(ext);

    if (cc_enable)
    {
        // Iterate over cc_node_list and append cc_nodes ...
        // (we also check cc_node "activity status here)
        NormNodeListIterator iterator(cc_node_list);
        NormCCNode *next;
        while ((next = (NormCCNode *)iterator.GetNextNode()))
        {
            if (next->IsActive())
            {
                UINT8 ccFlags = 0;
                if (next->IsClr())
                {
                    ccFlags |= (UINT8)NormCC::CLR;
                }
                else if (next->IsPlr())
                {
                    ccFlags |= (UINT8)NormCC::PLR;
                }
                ccFlags |= (UINT8)NormCC::RTT;
                UINT8 rttQuantized = NormQuantizeRtt(next->GetRtt());
                if (cc_slow_start)
                    ccFlags |= (UINT8)NormCC::START;
                UINT16 rateQuantized = NormQuantizeRate(next->GetRate());
                // (TBD) check result
                cmd->AppendCCNode(segment_size,
                                  next->GetId(),
                                  ccFlags,
                                  rttQuantized,
                                  rateQuantized);
                //if (!next->IsClr()) next->SetActive(false);
                // "Deactivate" any nodes who have stopped providing feedback
                struct timeval feedbackTime = next->GetFeedbackTime();
                double feedbackAge = currentTime.tv_sec - feedbackTime.tv_sec;
                feedbackAge += 1.0e-06 * ((double)((currentTime.tv_usec - feedbackTime.tv_usec)));

                /*if (currentTime.tv_usec > feedbackTime.tv_usec)
                    feedbackAge += 1.0e-06*((double)(currentTime.tv_usec - feedbackTime.tv_usec));
                else
                    feedbackAge -= 1.0e-06*((double)(feedbackTime.tv_usec - currentTime.tv_usec));*/
                double maxFeedbackAge = 20 * MAX(grtt_advertised, next->GetRtt());
                // Safety bound to compensate for computer clock coarseness
                // and possible sluggish feedback from slower machines
                // at higher norm data rates (keeps rate from being
                // prematurely reduced)
                if (maxFeedbackAge < (10 * NORM_TICK_MIN))
                    maxFeedbackAge = (10 * NORM_TICK_MIN);
                INT16 ccSeqDelta = cc_sequence - next->GetCCSequence();
                if ((feedbackAge > maxFeedbackAge) && (ccSeqDelta > (INT16)(20 * probe_count)))
                {
                    PLOG(PL_DEBUG, "Deactivating cc node feedbackAge:%lf sec maxAge:%lf sec ccSeqDelta:%u\n",
                         feedbackAge, maxFeedbackAge, ccSeqDelta);
                    next->SetActive(false);
                }
            }
        }
        AdjustRate(false);
    } // end if (cc_enable)

    if (probe_proactive)
        ext.SetSendRate(NormQuantizeRate(tx_rate));

    double probeInterval = GetProbeInterval();
    /*// perhaps this instead of the commented out probe_reset case???
    double nominalInterval = ((double)segment_size)/((double)tx_rate);
    if (nominalInterval > grtt_max) nominalInterval = grtt_max;
    if (nominalInterval > probeInterval) probeInterval = nominalInterval; */

    // Set probe_timer interval for next probe
    probe_timer.SetInterval(probeInterval);

    QueueMessage(cmd);
    probe_pending = true;

    return true;
} // end NormSession::OnProbeTimeout()

double NormSession::GetProbeInterval()
{
    if (cc_enable && data_active)
    {
        const NormCCNode *clr = static_cast<const NormCCNode *>(cc_node_list.Head());
        if (NULL != clr)
        {
            double probeInterval = (clr->IsActive() ? MIN(grtt_advertised, clr->GetRtt()) : grtt_advertised);
            // For "large" RTT (100 msec or bigger), we need to possibly probe more
            // often depending on transmit rate, to make NORM-CC perform a little better
            // The "probeCount" calculated here is based on sending no more probes per RTT
            // than 0.25 * the number of data packets that would be sent per RTT.
            // (although we floor the probeCount at 1 (i.e., the usual 1 probe per RTT)
            // Note that no more than a few (e.g. 3) probes per RTT provides performance benefit
            unsigned int probeCount = (unsigned int)(0.25 * tx_rate * probeInterval / (double)segment_size);
            if (probeCount < 1)
                probeCount = 1;
            if (clr->GetRtt() > 0.200)
            {
                if (probeCount > 3)
                    probeCount = 3;
            }
            else if (clr->GetRtt() > 0.100)
            {
                if (probeCount > 2)
                    probeCount = 2;
            }
            else
            {
                probeCount = 1;
            }
            if (1 != probe_count)
                probeCount = probe_count;

            // Don't send more than one CLR probe per RTT during slow_start
            return (cc_slow_start ? probeInterval : (probeInterval / (double)probeCount));
        }
        else
        {
            return grtt_advertised;
        }
    }
    else
    {
        return grtt_interval;
    }
} // end NormSession::GetProbeInterval()

void NormSession::AdjustRate(bool onResponse)
{
    const NormCCNode *clr = (const NormCCNode *)cc_node_list.Head();
    double ccRtt = clr ? clr->GetRtt() : grtt_measured;
    double ccLoss = clr ? clr->GetLoss() : 0.0;
    double txRate = tx_rate;
    if (onResponse)
    {
        if (!cc_active)
        {
            cc_active = true;
            Notify(NormController::CC_ACTIVE, NULL, NULL);
        }
        if (data_active) // adjust only if actively transmitting
        {
            // Adjust rate based on CLR feedback and
            // adjust probe schedule
            ASSERT(NULL != clr);
            // (TBD) check feedback age
            if (cc_slow_start)
            {
                txRate = clr->GetRate();
                if (GetDebugLevel() >= 6)
                {
                    double sentRate = 8.0e-03 * sent_accumulator.GetScaledValue(1.0 / (report_timer.GetInterval() - report_timer.GetTimeRemaining()));
                    PLOG(PL_DETAIL, "NormSession::AdjustRate(slow start) clr>%lu newRate>%lf (oldRate>%lf sentRate>%lf clrRate>%lf\n",
                         (unsigned long)clr->GetId(), 8.0e-03 * txRate, 8.0e-03 * tx_rate, sentRate, 8.0e-03 * clr->GetRate());
                }
            }
            else
            {
                double clrRate = clr->GetRate();
                if (clrRate > txRate)
                {
                    double maxRate = txRate * 2;
                    txRate = MIN(clrRate, maxRate);
                }
                else
                {
                    txRate = clrRate;
                }

                // Here, we use the most recent CLR rtt sample to "damp" oscillation
                double damper = clr->GetRttSqMean() / sqrt(clr->GetRttSample());
                if (damper < 0.5)
                    damper = 0.5;
                else if (damper > 2.0)
                    damper = 2.0;
                txRate *= damper;
                PLOG(PL_DETAIL, "NormSession::AdjustRate(stdy state) clr>%lu newRate>%lf (rtt>%lf loss>%lf)\n",
                     (unsigned long)clr->GetId(), 8.0e-03 * txRate, clr->GetRtt(), clr->GetLoss());
            }
        }
        if (!address.IsMulticast())
        {
            // For unicast, adjust the probe timeout right away
            double probeInterval = GetProbeInterval(); // based on CLR RTT, etc
            if (probe_timer.GetInterval() > probeInterval)
            {
                // reduce to speed up rate increase
                double elapsed = probe_timer.GetInterval() - probe_timer.GetTimeRemaining();
                if (probeInterval > elapsed)
                    probeInterval -= elapsed;
                else
                    probeInterval = 0.0;
                probe_timer.SetInterval(probeInterval);
                if (probe_timer.IsActive())
                    probe_timer.Reschedule();
            }
        }
    }
    else if (!data_active)
    {
        // reduce rate if no active data transmission
        // (TBD) Perhaps we want to be less aggressive here someday
        txRate *= 0.5;
    }
    else if (clr && clr->IsActive())
    {
        // (TBD) fix CC feedback aging ...
        /*int feedbackAge  = abs((int)cc_sequence - (int)clr->GetCCSequence());
        DMSG(0, "NormSession::AdjustRate() feedback age>%d (%d - %d\n", 
                feedbackAge, cc_sequence, clr->GetCCSequence());
        
        if (feedbackAge > 50)
        {
            double linRate = txRate - segment_size;
            linRate = MAX(linRate, 0.0);
            double expRate = txRate * 0.5;
            if (feedbackAge > 4)
                txRate = MIN(linRate, expRate);
            else
                txRate = MAX(linRate, expRate);
            
        }*/
    }
    else
    {
        // reduce rate by half if no active clr
        txRate *= 0.5;
    }

    // Keep "tx_rate" within default or user set rate bounds (if any)
    double minRate;
    if (tx_rate_min > 0.0)
    {
        minRate = tx_rate_min;
    }
    else
    {
        // Don't let txRate below MIN(one segment per grtt, one segment per second)
        if (grtt_measured > 1.0)
            minRate = ((double)segment_size) / grtt_measured;
        else
            minRate = (double)(segment_size);
    }
    if (txRate <= minRate)
    {
        txRate = minRate;
        if ((NULL == clr) || (!clr->IsActive()))
        {
            // Post notification that no cc feedback is being received
            if (cc_active)
            {
                cc_active = false;
                Notify(NormController::CC_INACTIVE, NULL, NULL);
            }
        }
    }
    if ((tx_rate_max >= 0.0) && (txRate > tx_rate_max))
        txRate = tx_rate_max;
    if (txRate != tx_rate)
    {
        if (cc_adjust)
            SetTxRateInternal(txRate);
        if (!posted_tx_rate_changed)
        {
            // TBD - make API notification filtering more consistent
            // (e.g., "notify_on_rate_update" like for grtt, etc
            //  putting API code in charge of resetting these API
            //  state variables).
            posted_tx_rate_changed = true;
            Notify(NormController::TX_RATE_CHANGED, (NormSenderNode *)NULL, (NormObject *)NULL);
        }
    }

    struct timeval currentTime;
    ::ProtoSystemTime(currentTime);
    double theTime = (double)currentTime.tv_sec + 1.0e-06 * ((double)currentTime.tv_usec);
    PLOG(PL_DEBUG, "SenderRateTracking time>%lf rate>%lf rtt>%lf loss>%lf\n", theTime, 8.0e-03 * txRate, ccRtt, ccLoss);
    //TRACE("SenderRateTracking time>%lf rate>%lf rtt>%lf loss>%lf\n", theTime, 8.0e-03*txRate, ccRtt, ccLoss);
    //double calcRate = NormSession::CalculateRate(nominal_packet_size, ccRtt, ccLoss);
    //TRACE("SenderRateTracking time>%lf rate>%lf clrRate>%lf rtt>%lf loss>%lf calcRate>%lf size>%lf%s\n\n",
    //      theTime, 8.0e-03*txRate, clr ? 8.0e-03*clr->GetRate() : 0.0, ccRtt, ccLoss, calcRate*8.0e-03,
    //      nominal_packet_size, cc_slow_start ? " (slow start)" : "");
    //ASSERT((NULL == clr) || (txRate <= clr->GetRate()));
} // end NormSession::AdjustRate()

bool NormSession::OnReportTimeout(ProtoTimer & /*theTimer*/)
{
    // Receiver reporting (just print out for now)
    struct timeval currentTime;
    ProtoSystemTime(currentTime);
#ifdef _WIN32_WCE
    struct tm timeStruct;
    timeStruct.tm_hour = currentTime.tv_sec / 3600;
    unsigned long hourSecs = 3600 * timeStruct.tm_hour;
    timeStruct.tm_min = (currentTime.tv_sec - (hourSecs)) / 60;
    timeStruct.tm_sec = currentTime.tv_sec - (hourSecs) - (60 * timeStruct.tm_min);
    timeStruct.tm_hour = timeStruct.tm_hour % 24;
    struct tm *ct = &timeStruct;
#else
    time_t secs = (time_t)currentTime.tv_sec;
    struct tm timeStruct;
#ifdef WIN32
    gmtime_s(&timeStruct, &secs);
    struct tm *ct = &timeStruct;
#else
    struct tm *ct = gmtime_r(&secs, &timeStruct);
#endif

#endif // if/else _WIN32_WCE
    ASSERT(NULL != ct);
    ProtoDebugLevel reportDebugLevel = PL_INFO;
    PLOG(reportDebugLevel, "REPORT time>%02d:%02d:%02d.%06lu node>%lu ***************************************\n",
         ct->tm_hour, ct->tm_min, ct->tm_sec, currentTime.tv_usec, (unsigned long)LocalNodeId());
    if (IsSender())
    {
        PLOG(reportDebugLevel, "Local status:\n");
        double sentRate = 8.0e-03 * sent_accumulator.GetScaledValue(1.0 / report_timer.GetInterval()); // kbps
        sent_accumulator.Reset();
        PLOG(reportDebugLevel, "   txRate>%9.3lf kbps sentRate>%9.3lf grtt>%lf\n",
             8.0e-03 * tx_rate, sentRate, grtt_advertised);
        if (cc_enable)
        {
            const NormCCNode *clr = (const NormCCNode *)cc_node_list.Head();
            if (clr)
            {
                PLOG(reportDebugLevel, "   clr>%lu rate>%9.3lf rtt>%lf loss>%lf %s\n",
                     (unsigned long)clr->GetId(), 8.0e-03 * clr->GetRate(),
                     clr->GetRtt(), clr->GetLoss(), cc_slow_start ? "(slow_start)" : "");
            }
        }
    }
    if (IsReceiver())
    {
        NormNodeTreeIterator iterator(sender_tree);
        NormSenderNode *next;
        while ((next = (NormSenderNode *)iterator.GetNextNode()))
        {
            PLOG(reportDebugLevel, "Remote sender>%lu grtt>%lf sec loss>%lf\n", (unsigned long)next->GetId(),
                 next->GetGrttEstimate(), next->LossEstimate());
            // TBD - Output sender congestion control status if cc is enabled
            double rxRate = 8.0e-03 * next->GetRecvRate(report_timer.GetInterval());       // kbps
            double rxGoodput = 8.0e-03 * next->GetRecvGoodput(report_timer.GetInterval()); // kbps
            next->ResetRecvStats();
            PLOG(reportDebugLevel, "   rxRate>%9.3lf kbps rx_goodput>%9.3lf kbps\n", rxRate, rxGoodput);
            PLOG(reportDebugLevel, "   rxObjects> completed>%lu pending>%lu failed>%lu\n",
                 next->CompletionCount(), next->PendingCount(), next->FailureCount());
            PLOG(reportDebugLevel, "   fecBufferUsage> current>%lu peak>%lu overuns>%lu\n",
                 next->CurrentBufferUsage(), next->PeakBufferUsage(),
                 next->BufferOverunCount());
            PLOG(reportDebugLevel, "   strBufferUsage> current>%lu peak>%lu overuns>%lu\n",
                 next->CurrentStreamBufferUsage(), next->PeakStreamBufferUsage(),
                 next->StreamBufferOverunCount());
            PLOG(reportDebugLevel, "   resyncs>%lu nacks>%lu suppressed>%lu\n",
                 next->ResyncCount() ? next->ResyncCount() - 1 : 0, // "ResyncCount()" is really "SyncCount()"
                 next->NackCount(), next->SuppressCount());
            // Some stream status for current receive stream (if applicable)
            NormObject *obj = next->GetNextPendingObject();
            if ((NULL != obj) && obj->IsStream())
            {
                NormStreamObject *stream = (NormStreamObject *)obj;
                PLOG(reportDebugLevel, "   stream_sync_id>%lu stream_next_id>%lu read_index:%lu.%hu\n",
                     (unsigned long)stream->GetSyncId().GetValue(),
                     (unsigned long)stream->GetNextId().GetValue(),
                     (unsigned long)stream->GetNextBlockId().GetValue(),
                     (UINT16)stream->GetNextSegmentId());
            }
        }
    } // end if (IsReceiver())
    PLOG(reportDebugLevel, "***************************************************************************\n");
    return true;
} // end NormSession::OnReportTimeout()

bool NormSession::OnUserTimeout(ProtoTimer & /*theTimer*/)
{
    Notify(NormController::USER_TIMEOUT, (NormSenderNode *)NULL, (NormObject *)NULL);
    return true;
} // end NormSession::OnUserTimeout(

NormSessionMgr::NormSessionMgr(ProtoTimerMgr &timerMgr,
                               ProtoSocket::Notifier &socketNotifier,
                               ProtoChannel::Notifier *channelNotifier)
    : timer_mgr(timerMgr), socket_notifier(socketNotifier), channel_notifier(channelNotifier),
      controller(NULL), data_free_func(NULL), top_session(NULL)
{
}

NormSessionMgr::~NormSessionMgr()
{
    Destroy();
}

void NormSessionMgr::Destroy()
{
    NormSession *next;
    while ((next = top_session))
    {
        top_session = next->next;
        delete next;
    }
} // end NormSessionMgr::Destroy()

NormSession *NormSessionMgr::NewSession(const char *sessionAddress,
                                        UINT16 sessionPort,
                                        NormNodeId localNodeId)
{
    if ((NORM_NODE_ANY == localNodeId) || (NORM_NODE_NONE == localNodeId))
    {
#ifndef SIMULATE
        // Use local ip address to assign default localNodeId
        ProtoAddress localAddr;
        if (!localAddr.ResolveLocalAddress())
        {
            PLOG(PL_ERROR, "NormSessionMgr::NewSession() local address lookup error\n");
            return ((NormSession *)NULL);
        }
        // (TBD) test IPv6 "EndIdentifier" ???
        localNodeId = localAddr.EndIdentifier();
#else
        localNodeId = NORM_NODE_ANY - 1;
#endif
    }
    ProtoAddress theAddress;
    if (!theAddress.ResolveFromString(sessionAddress))
    {
        PLOG(PL_ERROR, "NormSessionMgr::NewSession() session address \"%s\" lookup error!\n", sessionAddress);
        return ((NormSession *)NULL);
    }
    theAddress.SetPort(sessionPort);
    NormSession *theSession = new NormSession(*this, localNodeId);
    if (!theSession)
    {
        PLOG(PL_ERROR, "NormSessionMgr::NewSession() new session error: %s\n", GetErrorString());
        return ((NormSession *)NULL);
    }
    theSession->SetAddress(theAddress);
    // Add new session to our session list
    theSession->next = top_session;
    top_session = theSession;
    return theSession;
} // end NormSessionMgr::NewSession()

void NormSessionMgr::DeleteSession(class NormSession *theSession)
{
    NormSession *prev = NULL;
    NormSession *next = top_session;
    while (next && (next != theSession))
    {
        prev = next;
        next = next->next;
    }
    if (next)
    {
        if (prev)
            prev->next = theSession->next;
        else
            top_session = theSession->next;
        delete theSession;
    }
} // end NormSessionMgr::DeleteSession()
