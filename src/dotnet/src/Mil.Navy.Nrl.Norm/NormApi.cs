using System.Runtime.InteropServices;

namespace Mil.Navy.Nrl.Norm
{
    /// <summary>
    /// The native NORM API functions 
    /// </summary>
    public static class NormApi
    {
        /// <summary>
        /// The name of the NORM library used when calling native NORM API functions
        /// </summary>
        public const string NORM_LIBRARY = "norm";
        
        /// <summary>
        /// The NormEvent type is a structure used to describe significant NORM protocol events.
        /// </summary>
        [StructLayout(LayoutKind.Sequential)]
        public struct NormEvent
        {
            /// <summary>
            /// The Type field indicates the NormEventType and determines how the other fields should be interpreted.
            /// </summary>
            public NormEventType Type;
            /// <summary>
            /// The Session field indicates the applicable NormSessionHandle to which the event applies.
            /// </summary>
            public long Session;
            /// <summary>
            /// The Sender field indicates the applicable NormNodeHandle to which the event applies.
            /// </summary>
            public long Sender;
            /// <summary>
            /// The Object field indicates the applicable NormObjectHandle to which the event applies.
            /// </summary>
            public long Object;
        }

        /// <summary>
        /// This function creates an instance of a NORM protocol engine and is the necessary first step before any other API functions may be used.
        /// </summary>
        /// <param name="priorityBoost">The priorityBoost parameter, when set to a value of true, specifies that the NORM protocol engine thread be run with higher priority scheduling.</param>
        /// <returns>A value of NORM_INSTANCE_INVALID is returned upon failure. </returns>
        [DllImport(NORM_LIBRARY)]
        public static extern long NormCreateInstance(bool priorityBoost);

        /// <summary>
        /// The NormDestroyInstance() function immediately shuts down and destroys the NORM protocol engine instance referred to by the instanceHandle parameter.
        /// </summary>
        /// <param name="instanceHandle">The NORM protocol engine instance referred to by the instanceHandle parameter.</param>
        [DllImport(NORM_LIBRARY)]
        public static extern void NormDestroyInstance(long instanceHandle);

        /// <summary>
        /// The NormStopInstance() this function immediately stops the NORM protocol engine thread corresponding to the given instanceHandle parameter.
        /// </summary>
        /// <param name="instanceHandle">The NORM protocol engine instance referred to by the instanceHandle parameter.</param>
        [DllImport (NORM_LIBRARY)]
        public static extern void NormStopInstance(long instanceHandle);
      
        /// <summary>
        /// The NormRestartInstance() this function creates and starts an operating system thread to resume NORM protocol engine operation for the given
        /// instanceHandle that was previously stopped by a call to NormStopInstance().
        /// </summary>
        /// <param name="instanceHandle">The NORM protocol engine instance referred to by the instanceHandle parameter.</param>
        /// <returns>Boolean as to the success of the instance restart. </returns>
        [DllImport (NORM_LIBRARY)]
        public static extern bool NormRestartInstance(long instanceHandle);

        /// <summary>
        /// The NormSuspendInstance() immediately suspends the NORM protocol engine thread corresponding to the given instanceHandle parameter
        /// </summary>
        /// <param name="instanceHandle">The NORM protocol engine instance referred to by the instanceHandle parameter. </param>
        /// <returns>Boolean as to the success of the instance suspension. </returns>
        [DllImport (NORM_LIBRARY)]
        public static extern bool NormSuspendInstance(long instanceHandle);

        /// <summary>
        /// Resumes NORM protocol engine thread corresponding to the given instanceHandler parameter.
        /// </summary>
        /// <param name="instanceHandle">The NORM protocol engine instance referred to by the instanceHandle parameter.</param>
        /// <returns>Boolean as to the success of the instance resumption.</returns>
        [DllImport (NORM_LIBRARY)]
        public static extern bool NormResumeInstance(long instanceHandle);

        /// <summary>
        /// This function sets the directory path used by receivers to cache newly-received NORM_OBJECT_FILE content.
        /// </summary>
        /// <param name="instanceHandle">The instanceHandle parameter specifies the NORM protocol engine instance 
        /// (all NormSessions associated with that instanceHandle share the same cache path).</param>
        /// <param name="cachePath">the cachePath is a string specifying a valid (and writable) directory path.</param>
        /// <returns>The function returns true on success and false on failure. The failure conditions are 
        /// that the indicated directory does not exist or the process does not have permissions to write.</returns>
        [DllImport(NORM_LIBRARY)]
        public static extern bool NormSetCacheDirectory(long instanceHandle, string cachePath);

        /// <summary>
        /// This function retrieves the next available NORM protocol event from the protocol engine.
        /// </summary>
        /// <param name="instanceHandle">The instanceHandle parameter specifies the applicable NORM protocol engine.</param>
        /// <param name="theEvent">The parameter must be a valid pointer to a NormEvent structure capable of receiving the NORM event information.</param>
        /// <param name="waitForEvent">waitForEvent specifies whether the call to this function is blocking or not,
        /// if "waitForEvent" is false, this is a non-blocking call.</param>
        /// <returns>The function returns true when a NormEvent is successfully retrieved, and false otherwise.
        /// Note that a return value of false does not indicate an error or signify end of NORM operation.</returns>
        [DllImport(NORM_LIBRARY)]
        public static extern bool NormGetNextEvent(long instanceHandle, out NormEvent theEvent, bool waitForEvent);

        /// <summary>
        /// This function is used to retrieve a NormDescriptor (Unix int file descriptor or Win32 HANDLE) suitable for
        /// asynchronous I/O notification to avoid blocking calls to NormGetNextEvent().
        /// </summary>
        /// <param name="instanceHandle">The NORM protocol engine instance referred to by the instanceHandle parameter.</param>
        /// <returns>A NormDescriptor value is returned which is valid until a call to NormDestroyInstance() is made.
        /// Upon error, a value of NORM_DESCRIPTOR_INVALID is returned.</returns>
        [DllImport(NORM_LIBRARY)]
        public static extern int NormGetDescriptor(long instanceHandle);

        /// <summary>
        /// This function creates a NORM protocol session (NormSession) using the address (multicast or unicast) and port
        /// parameters provided.While session state is allocated and initialized, active session participation does not begin
        /// until a call is made to NormStartSender() and/or NormStartReceiver() to join the specified multicast group
        /// (if applicable) and start protocol operation.
        /// </summary>
        /// <param name="instanceHandle">Valid NormInstanceHandle previously obtained with a call to NormCreateInstance().</param>
        /// <param name="sessionAddress">Specified address determines the destination of NORM messages sent.</param>
        /// <param name="sessionPort">Valid, unused port number corresponding to the desired NORM session address.</param>
        /// <param name="localNodeId">Identifies the application's presence in the NormSession.</param>
        /// <returns>Returns a session handle.</returns>
        [DllImport(NORM_LIBRARY)]
        public static extern long NormCreateSession(long instanceHandle, string sessionAddress, int sessionPort, long localNodeId);

        /// <summary>
        /// This function immediately terminates the application's participation in the NormSession and frees any resources used by that session.
        /// </summary>
        /// <param name="sessionHandle">Used to identify application in the NormSession.</param>
        [DllImport(NORM_LIBRARY)]
        public static extern void NormDestroySession(long sessionHandle);

        /// <summary>
        /// This function retrieves the NormNodeId value used for the application's participation in the NormSession.
        /// </summary>
        /// <param name="sessionHandle">Used to identify application in the NormSession.</param>
        /// <returns>The returned value indicates the NormNode identifier used by the NORM protocol engine for the local application's participation in the specified NormSession.</returns>
        [DllImport(NORM_LIBRARY)]
        public static extern long NormGetLocalNodeId(long sessionHandle);

        /// <summary>
        /// This function is used to force NORM to use a specific port number for UDP packets sent for the specified sessionHandle.
        /// </summary>
        /// <param name="sessionHandle">Used to identify application in the NormSession.</param>
        /// <param name="txPortNumber">The txPortNumber parameter, specifies which port number to use.</param>
        /// <param name="enableReuse">The enableReuse parameter, when set to true, allows that the specified port may be reused for multiple sessions.</param>
        /// <param name="txBindAddress">The txBindAddress parameter allows specification of a specific source address binding for packet transmission.</param>
        /// <returns>This function returns true upon success and false upon failure. Failure will occur if a txBindAddress is providedthat does not 
        /// correspond to a valid, configured IP address for the local host system.</returns>
        [DllImport(NORM_LIBRARY)]
        public static extern bool NormSetTxPort(long sessionHandle, int txPortNumber, bool enableReuse, string? txBindAddress);

        /// <summary>
        /// This function limits the NormSession to perform NORM sender functions only.
        /// </summary>
        /// <param name="sessionHandle">Used to identify application in the NormSession.</param>
        /// <param name="txOnly">Boolean specifing whether to turn on or off the txOnly operation.</param>
        /// <param name="connectToSessionAddress">The optional connectToSessionAddress parameter, when set to true, 
        /// causes the underlying NORM code to "connect()" the UDP socket to the session (remote receiver) address and port number.</param>
        [DllImport(NORM_LIBRARY)]
        public static extern void NormSetTxOnly(long sessionHandle, bool txOnly, bool connectToSessionAddress);

        /// <summary>
        /// This function allows the user to control the port reuse and binding behavior for the receive socket used for the given NORM sessionHandle.
        /// </summary>
        /// <param name="sessionHandle">Used to identify application in the NormSession.</param>
        /// <param name="enableReuse">When the enablReuse parameter is set to true, reuse of the NormSession port number by multiple NORM instances or sessions is enabled.</param>
        /// <param name="rxBindAddress">If the optional rxBindAddress is supplied (an IP address or host name in string form), the socket will bind() to the given address 
        /// when it is opened in a call to NormStartReceiver() or NormStartSender().</param>
        /// <param name="senderAddress">The optional senderAddress parameter can be used to connect() the underlying NORM receive socket to specific address.</param>
        /// <param name="senderPort">The optional senderPort parameter can be used to connect() the underlying NORM receive socket to specific port.</param>
        [DllImport(NORM_LIBRARY)]
        public static extern void NormSetRxPortReuse(long sessionHandle, bool enableReuse, string? rxBindAddress, string? senderAddress, int senderPort);

        /// <param name="sessionHandle">Used to identify application in the NormSession.</param>
        /// <param name="ecnEnable">Enables NORM ECN (congestion control) support.</param>
        /// <param name="ignoreLoss">With "ecnEnable", use ECN-only, ignoring packet loss.</param>
        /// <param name="tolerateLoss">Loss-tolerant congestion control, ecnEnable or not.</param>
        [DllImport(NORM_LIBRARY)]
        public static extern void NormSetEcnSupport(long sessionHandle, bool ecnEnable, bool ignoreLoss, bool tolerateLoss);

        /// <summary>
        /// This function specifies which host network interface is used for IP Multicast transmissions and group membership.
        /// This should be called before any call to NormStartSender() or NormStartReceiver() is made so that the IP multicast group is joined on the proper host interface.
        /// </summary>
        /// <param name="sessionHandle">Used to identify application in the NormSession.</param>
        /// <param name="interfaceName">Name of the interface</param>
        /// <returns>A return value of true indicates success while a return value of false indicates that the specified interface was
        /// invalid. This function will always return true if made before calls to NormStartSender() or NormStartReceiver().
        /// However, those calls may fail if an invalid interface was specified with the call described here.</returns>
        [DllImport(NORM_LIBRARY)]
        public static extern bool NormSetMulticastInterface(long sessionHandle, string interfaceName);

        /// <summary>
        /// This function sets the source address for Source-Specific Multicast (SSM) operation.
        /// </summary>
        /// <param name="sessionHandle">Used to identify application in the NormSession.</param>
        /// <param name="sourceAddress">Address to be set as source for source-specific multicast operation.</param>
        /// <returns>A return value of true indicates success while a return value of false indicates that the specified source address
        /// was invalid. Note that if a valid IP address is specified but is improper for SSM (e.g., an IP multicast address) the
        /// later calls to NormStartSender() or NormStartReceiver() may fail. </returns>
        [DllImport(NORM_LIBRARY)]
        public static extern bool NormSetSSM(long sessionHandle, string sourceAddress);

        /// <summary>
        /// This function specifies the time-to-live (ttl) for IP Multicast datagrams generated by NORM for the specified
        /// sessionHandle. The IP TTL field limits the number of router "hops" that a generated multicast packet may traverse
        /// before being dropped.
        /// </summary>
        /// <param name="sessionHandle">Used to identify application in the NormSession.</param>
        /// <param name="ttl">If TTL is equal to one, the transmissions will be limited to the local area network
        /// (LAN) of the host computers network interface. Larger TTL values should be specified to span large networks.</param>
        /// <returns>A return value of true indicates success while a return value of false indicates that the specified ttl could not
        /// be set. This function will always return true if made before calls to NormStartSender() or NormStartReceiver().
        /// However, those calls may fail if the desired ttl value cannot be set.</returns>
        [DllImport(NORM_LIBRARY)]
        public static extern bool NormSetTTL(long sessionHandle, byte ttl);

        /// <summary>
        /// This function specifies the type-of-service (tos) field value used in IP Multicast datagrams generated by NORM for the specified sessionHandle.
        /// </summary>
        /// <param name="sessionHandle">Used to identify application in the NormSession.</param>
        /// <param name="tos">The IP TOS field value can be used as an indicator that a "flow" of packets may merit special 
        /// Quality-of-Service (QoS) treatment by network devices.
        /// Users should refer to applicable QoS information for their network to determine the expected interpretation and 
        /// treatment (if any) of packets with explicit TOS marking.</param>
        /// <returns>A return value of true indicates success while a return value of false indicates that the specified tos could not
        /// be set. This function will always return true if made before calls to NormStartSender() or NormStartReceiver().
        /// However, those calls may fail if the desired tos value cannot be set.</returns>
        [DllImport(NORM_LIBRARY)]
        public static extern bool NormSetTOS(long sessionHandle, byte tos);

        /// <summary>
        /// This function enables or disables loopback operation for the indicated NORM sessionHandle.
        /// </summary>
        /// <param name="sessionHandle">Used to identify application in the NormSession. </param>
        /// <param name="loopback">If loopback is set to true, loopback operation is enabled which allows the application to receive its own message traffic.
        /// Thus, an application which is both actively receiving and sending may receive its own transmissions.</param>
        /// <returns>A return value of true indicates success while a return value of false indicates that the loopback operation could not be set.</returns>
        [DllImport(NORM_LIBRARY)]
        public static extern bool NormSetLoopback(long sessionHandle, bool loopback);

        [DllImport(NORM_LIBRARY)]
        public static extern void NormSetMessageTrace(long sessionHandle, bool flag);

        [DllImport(NORM_LIBRARY)]
        public static extern void NormSetTxLoss(long sessionHandle, double precent);

        [DllImport(NORM_LIBRARY)]
        public static extern void NormSetRxLoss(long sessionHandle, double precent);

        /// <summary>
        /// This function allows NORM debug output to be directed to a file instead of the default STDERR.
        /// </summary>
        /// <param name="instanceHandle">Used to identify application in the NormSession.</param>
        /// <param name="path">Full path and name of the debug log.</param>
        /// <returns>The function returns true on success. If the specified file cannot be opened a value of false is returned.</returns>
        [DllImport(NORM_LIBRARY)]
        public static extern bool NormOpenDebugLog(long instanceHandle, string path);

        /// <summary>
        /// This function disables NORM debug output to be directed to a file instead of the default STDERR.
        /// </summary>
        /// <param name="instanceHandle">Used to identify application in the NormSession.</param>
        /// <returns>The function returns true on success.</returns>
        [DllImport(NORM_LIBRARY)]
        public static extern bool NormCloseDebugLog(long instanceHandle);

        /// <summary>
        /// This function allows NORM debug output to be directed to a named pipe.
        /// </summary>
        /// <param name="instanceHandle">Used to identify application in the NormSession.</param>
        /// <param name="pipeName">The debug pipe name.</param>
        /// <returns>The function returns true on success.</returns>
        [DllImport(NORM_LIBRARY)]
        public static extern bool NormOpenDebugPipe(long instanceHandle, string pipeName);

        /// <summary>
        /// This function controls the verbosity of NORM debugging output. Higher values of level result in more detailed
        /// output. The highest level of debugging is 12. The debug output consists of text written to STDOUT by default but
        /// may be directed to a log file using the NormOpenDebugLog() function.
        /// </summary>
        /// <param name="level">
        /// PROTOLIB DEBUG LEVELS:
        /// PL_FATAL=0 - The FATAL level designates very severe error events that will presumably lead the application to abort.
        /// PL_ERROR=1 - The ERROR level designates error events that might still allow the application to continue running.
        /// PL_WARN=2 - The WARN level designates potentially harmful situations.
        /// PL_INFO=3 - The INFO level designates informational messages that highlight the progress of the application at coarse-grained level.
        /// PL_DEBUG=4 - The DEBUG level designates fine-grained informational events that are most useful to debug an application.
        /// PL_TRACE=5 - The TRACE level designates finer-grained informational events than the DEBUG.
        /// PL_DETAIL=6 - The TRACE level designates even finer-grained informational events than the DEBUG.
        /// PL_MAX=7 - Turn all comments on.
        /// PL_ALWAYS - Messages at this level are always printed regardless of debug level.
        /// </param>
        [DllImport(NORM_LIBRARY)]
        public static extern void NormSetDebugLevel(int level);

        /// <summary>
        /// Returns the currently set debug level.
        /// </summary>
        /// <returns>Returns the currently set debug level.</returns>
        [DllImport(NORM_LIBRARY)]
        public static extern int NormGetDebugLevel();

        [DllImport(NORM_LIBRARY)]
        public static extern void NormSetReportInterval(long sessionHandle, double interval);

        [DllImport(NORM_LIBRARY)]
        public static extern double NormGetReportInterval(long sessionHandle);

        [DllImport(NORM_LIBRARY)]
        public static extern int NormGetRandomSessionId();

        /// <summary>
        /// The application's participation as a sender within a specified NormSession begins when this function is called.
        /// </summary>
        /// <param name="instanceHandle">Valid NormSessionHandle previously obtained with a call to NormCreateSession()</param>
        /// <param name="instanceId"> Application-defined value used as the instance_id field of NORM sender messages for the application's participation within a session.</param>
        /// <param name="bufferSpace">This specifies the maximum memory space (in bytes) the NORM protocol engine is allowed to use to buffer any sender calculated FEC segments and repair state for the session.</param>
        /// <param name="segmentSize">This parameter sets the maximum payload size (in bytes) of NORM sender messages (not including any NORM message header fields).</param>
        /// <param name="numData">This parameter sets the number of source symbol segments (packets) per coding block, for the
        /// systematic Reed-Solomon FEC code used in the current NORM implementation.</param>
        /// <param name="numParity">This parameter sets the maximum number of parity symbol segments (packets) the sender is willing to calculate per FEC coding block.</param>
        /// <param name="fecId">Sets the NormFecType.</param>
        /// <returns>A value of true is returned upon success and false upon failure.</returns>
        [DllImport(NORM_LIBRARY)]
        public static extern bool NormStartSender(long instanceHandle, int instanceId, long bufferSpace, int segmentSize, short numData, short numParity, NormFecType fecId);

        /// <summary>
        /// This function terminates the application's participation in a NormSession as a sender. By default, the sender will
        /// immediately exit the session identified by the sessionHandle parameter without notifying the receiver set of its intention.
        /// </summary>
        /// <param name="sessionHandle">Used to identify application in the NormSession.</param>
        [DllImport(NORM_LIBRARY)]
        public static extern void NormStopSender(long sessionHandle);

        /// <summary>
        /// This function sets the transmission rate (in bits per second (bps)) limit used for NormSender transmissions for the given sessionHandle.
        /// </summary>
        /// <param name="sessionHandle">Used to identify application in the NormSession.</param>
        /// <param name="rate">Transmission rate.</param>
        [DllImport(NORM_LIBRARY)]
        public static extern void NormSetTxRate(long sessionHandle, double rate);

        /// <summary>
        /// This function retrieves the current sender transmission rate in units of bits per second (bps) for the given sessionHandle.
        /// </summary>
        /// <param name="sessionHandle">Used to identify application in the NormSession.</param>
        /// <returns>This function returns the sender transmission rate in units of bits per second (bps).</returns>
        [DllImport(NORM_LIBRARY)]
        public static extern double NormGetTxRate(long sessionHandle);

        /// <summary>
        /// This function can be used to set a non-default socket buffer size for the UDP socket used by the specified NORM sessionHandle for data transmission.
        /// </summary>
        /// <param name="sessionHandle">Used to identify application in the NormSession.</param>
        /// <param name="bufferSize">The bufferSize parameter specifies the desired socket buffer size in bytes.</param>
        /// <returns>This function returns true upon success and false upon failure. Possible failure modes include an invalid sessionHandle parameter, 
        /// a call to NormStartReceiver() or NormStartSender() has not yet been made for the session, or an invalid bufferSize was given.
        /// Note some operating systems may require additional system configuration to use non-standard socket buffer sizes.</returns>
        [DllImport(NORM_LIBRARY)]
        public static extern bool NormSetTxSocketBuffer(long sessionHandle, long bufferSize);

        /// <summary>
        /// This function controls a scaling factor that is used for sender timer-based flow control for the the specified NORM
        /// sessionHandle. Timer-based flow control works by preventing the NORM sender application from enqueueing
        /// new transmit objects or stream data that would purge "old" objects or stream data when there has been recent
        /// NACK activity for those old objects or data.
        /// </summary>
        /// <param name="sessionHandle">Used to identify application in the NormSession.</param>
        /// <param name="flowControlFactor">The flowControlFactor is used to compute a delay time for when a sender buffered object (or block of stream
        /// data) may be released (i.e. purged) after transmission or applicable NACKs reception.</param>
        [DllImport(NORM_LIBRARY)]
        public static extern void NormSetFlowControl(long sessionHandle, double flowControlFactor);

        /// <summary>
        /// This function enables (or disables) the NORM sender congestion control operation for the session designated by
        /// the sessionHandle parameter. For best operation, this function should be called before the call to NormStartSender() is made, but congestion control operation can be dynamically enabled/disabled during the course
        /// of sender operation.
        /// </summary>
        /// <param name="sessionHandle">Used to identify application in the NormSession.</param>
        /// <param name="enable">Specifies whether to enable or disable the NORM sender congestion control operation.</param>
        /// <param name="adjustRate">The rate set by NormSetTxRate() has no effect when congestion control operation is enabled, unless the adjustRate
        /// parameter here is set to false. When the adjustRate parameter is set to false, the NORM Congestion Control
        /// operates as usual, with feedback collected from the receiver set and the "current limiting receiver" identified, except
        /// that no actual adjustment is made to the sender's transmission rate.</param>
        [DllImport(NORM_LIBRARY)]
        public static extern void NormSetCongestionControl(long sessionHandle, bool enable, bool adjustRate);

        /// <summary>
        /// This function sets the range of sender transmission rates within which the NORM congestion control algorithm is
        /// allowed to operate for the given sessionHandle.
        /// </summary>
        /// <param name="sessionHandle">Used to identify application in the NormSession.</param>
        /// <param name="rateMin">rateMin corresponds to the minimum transmission rate (bps).</param>
        /// <param name="rateMax">rateMax corresponds to the maximum transmission rate (bps).</param>
        [DllImport(NORM_LIBRARY)]
        public static extern void NormSetTxRateBounds(long sessionHandle, double rateMin, double rateMax);

        /// <summary>
        /// This function sets limits that define the number and total size of pending transmit objects a NORM sender will allow to be enqueued by the application.
        /// </summary>
        /// <param name="sessionHandle">Used to identify application in the NormSession.</param>
        /// <param name="sizeMax">The sizeMax parameter sets the maximum total size, in bytes, of enqueued objects allowed.</param>
        /// <param name="countMin">The countMin parameter sets the minimum number of objects the application may enqueue,
        /// regardless of the objects' sizes and the sizeMax value.</param>
        /// <param name="countMax">The countMax parameter sets a ceiling on how many objects may be enqueued,
        /// regardless of their total sizes with respect to the sizeMax setting. </param>
        [DllImport(NORM_LIBRARY)]
        public static extern void NormSetTxCacheBounds(long sessionHandle, long sizeMax, long countMin, long countMax);

        /// <summary>
        /// This function sets the quantity of proactive "auto parity" NORM_DATA messages sent at the end of each FEC coding
        /// block. By default (i.e., autoParity = 0), FEC content is sent only in response to repair requests (NACKs) from receivers.
        /// </summary>
        /// <param name="sesssionHandle">Used to identify application in the NormSession.</param>
        /// <param name="autoParity">Setting a non-zero value for autoParity, the sender can automatically accompany each coding
        /// block of transport object source data segments ((NORM_DATA messages) with the set number of FEC segments.</param>
        [DllImport(NORM_LIBRARY)]
        public static extern void NormSetAutoParity(long sesssionHandle, short autoParity);

        /// <summary>
        /// This function sets the sender's estimate of group round-trip time (GRTT) (in units of seconds) for the given NORM sessionHandle.
        /// </summary>
        /// <param name="sessionHandle">Used to identify application in the NormSession.</param>
        /// <param name="grtt">group round-trip time</param>
        [DllImport(NORM_LIBRARY)]
        public static extern void NormSetGrttEstimate(long sessionHandle, double grtt);

        /// <summary>
        /// This function returns the sender's current estimate(in seconds) of group round-trip timing (GRTT) for the given NORM session.
        /// </summary>
        /// <param name="sessionHandle">Used to identify application in the NormSession.</param>
        /// <returns>This function returns the current sender group round-trip timing (GRTT) estimate (in units of seconds).
        /// A value of -1.0 is returned if an invalid session value is provided.</returns>
        [DllImport(NORM_LIBRARY)]
        public static extern double NormGetGrttEstimate(long sessionHandle);

        /// <summary>
        /// This function sets the sender's maximum advertised GRTT value for the given NORM sessionHandle.
        /// </summary>
        /// <param name="sessionHandle">Used to identify application in the NormSession.</param>
        /// <param name="grttMax">The grttMax parameter, in units of seconds, limits the GRTT used by the group for scaling protocol timers, regardless
        /// of larger measured round trip times. The default maximum for the NRL NORM library is 10 seconds.</param>
        [DllImport(NORM_LIBRARY)]
        public static extern void NormSetGrttMax(long sessionHandle, double grttMax);

        /// <summary>
        /// This function sets the sender's mode of probing for round trip timing measurement responses from the receiver set for the given NORM sessionHandle.
        /// </summary>
        /// <param name="sesssionHandle">Used to identify application in the NormSession.</param>
        /// <param name="probingMode">Possible values for the probingMode parameter include NORM_PROBE_NONE, NORM_PROBE_PASSIVE, and NORM_PROBE_ACTIVE.</param>
        [DllImport(NORM_LIBRARY)]
        public static extern void NormSetGrttProbingMode(long sesssionHandle, NormProbingMode probingMode);

        /// <summary>
        /// This function controls the sender GRTT measurement and estimation process for the given NORM sessionHandle.
        /// The NORM sender multiplexes periodic transmission of NORM_CMD(CC) messages with its ongoing data transmission
        /// or when data transmission is idle.When NORM congestion control operation is enabled, these probes are sent
        /// once per RTT of the current limiting receiver(with respect to congestion control rate). In this case the intervalMin
        /// and intervalMax parameters (in units of seconds) control the rate at which the sender's estimate of GRTT is updated.
        /// </summary>
        /// <param name="sessionHandle">Used to identify application in the NormSession.</param>
        /// <param name="intervalMin">At session start, the estimate is updated at intervalMin and the update interval time is doubled until intervalMax is reached.</param>
        /// <param name="intervalMax">At session start, the estimate is updated at intervalMin and the update interval time is doubled until intervalMax is reached.</param>
        [DllImport(NORM_LIBRARY)]
        public static extern void NormSetGrttProbingInterval(long sessionHandle, double intervalMin, double intervalMax);

        /// <summary>
        /// This function sets the sender's "backoff factor" for the given sessionHandle.
        /// </summary>
        /// <param name="sessionHandle">Used to identify application in the NormSession.</param>
        /// <param name="backoffFactor">The backoffFactor (in units of seconds) is used to scale various timeouts related to the NACK repair process.</param>
        [DllImport(NORM_LIBRARY)]
        public static extern void NormSetBackoffFactor(long sessionHandle, double backoffFactor);

        /// <summary>
        /// This function sets the sender's estimate of receiver group size for the given sessionHandle.
        /// </summary>
        /// <param name="sessionHandle">Used to identify application in the NormSession.</param>
        /// <param name="groupSize">The sender advertises its groupSize setting to the receiver group in NORM protocol message 
        /// headers that, in turn, use this information to shape the distribution curve of their random timeouts for the timer-based, 
        /// probabilistic feedback suppression technique used in the NORM protocol.</param>
        [DllImport(NORM_LIBRARY)]
        public static extern void NormSetGroupSize(long sessionHandle, long groupSize);

        /// <summary>
        /// This routine sets the "robustness factor" used for various NORM sender functions. These functions include the
        /// number of repetitions of "robustly-transmitted" NORM sender commands such as NORM_CMD(FLUSH) or similar
        /// application-defined commands, and the number of attempts that are made to collect positive acknowledgement
        /// from receivers.These commands are distinct from the NORM reliable data transmission process, but play a role
        /// in overall NORM protocol operation.
        /// </summary>
        /// <param name="sessionHandle">Used to identify application in the NormSession.</param>
        /// <param name="txRobustFactor">The default txRobustFactor value is 20. This relatively large value makes
        /// the NORM  sender end-of-transmission flushing  and positive  acknowledgement collection  functions somewhat immune from packet loss.
        /// Setting txRobustFactor to a value of -1 makes the redundant transmission of these commands continue indefinitely until completion.
        /// </param>
        [DllImport(NORM_LIBRARY)]
        public static extern void NormSetTxRobustFactor(long sessionHandle, int txRobustFactor);

        /// <summary>
        /// This function enqueues a file for transmission within the specified NORM sessionHandle.
        /// </summary>
        /// <param name="sessionHandle">Used to identify application in the NormSession.</param>
        /// <param name="fileName">The fileName parameter specifies the path to the file to be transmitted. The NORM protocol engine
        /// read and writes directly from/to file system storage for file transport, potentially providing for a very large virtual
        /// "repair window" as needed for some applications. While relative paths with respect to the "current working directory"
        /// may be used, it is recommended that full paths be used when possible.</param>
        /// <param name="infoPtr">The optional infoPtr and infoLen parameters 
        /// are used to associate NORM_INFO content with the sent transport object. The maximum allowed infoLen
        /// corresponds to the segmentSize used in the prior call to NormStartSender(). The use and interpretation of the
        /// NORM_INFO content is left to the application's discretion.</param>
        /// <param name="infoLen">The optional infoPtr and infoLen parameters 
        /// are used to associate NORM_INFO content with the sent transport object. The maximum allowed infoLen
        /// corresponds to the segmentSize used in the prior call to NormStartSender(). The use and interpretation of the
        /// NORM_INFO content is left to the application's discretion</param>
        /// <returns>A NormObjectHandle is returned which the application may use in other NORM API calls as needed.</returns>
        [DllImport(NORM_LIBRARY)]
        public static extern long NormFileEnqueue(long sessionHandle, string fileName, nint infoPtr, int infoLen);

        /// <summary>
        /// This function enqueues a segment of application memory space for transmission within the specified NORM sessionHandle.
        /// </summary>
        /// <param name="sessionHandle">Used to identify application in the NormSession.</param>
        /// <param name="dataPtr">The dataPtr parameter must be a valid pointer to the area of application memory to be transmitted.</param>
        /// <param name="dataLen">The dataLen parameter indicates the quantity of data to transmit.</param>
        /// <param name="infoPtr">The optional infoPtr and infoLen parameters 
        /// are used to associate NORM_INFO content with the sent transport object. The maximum allowed infoLen
        /// corresponds to the segmentSize used in the prior call to NormStartSender(). The use and interpretation of the
        /// NORM_INFO content is left to the application's discretion.</param>
        /// <param name="infoLen">The optional infoPtr and infoLen parameters 
        /// are used to associate NORM_INFO content with the sent transport object. The maximum allowed infoLen
        /// corresponds to the segmentSize used in the prior call to NormStartSender(). The use and interpretation of the
        /// NORM_INFO content is left to the application's discretion</param>
        /// <returns>A NormObjectHandle is returned which the application may use in other NORM API calls as needed.</returns>
        [DllImport(NORM_LIBRARY)]
        public static extern long NormDataEnqueue(long sessionHandle, nint dataPtr, int dataLen, nint infoPtr, int infoLen);

        /// <summary>
        /// This function allows the application to resend (or reset transmission of) a NORM_OBJECT_FILE or NORM_OBJECT_DATA
        /// transmit object that was previously enqueued for the indicated sessionHandle.
        /// </summary>
        /// <param name="sessionHandle">Used to identify application in the NormSession.</param>
        /// <param name="objectHandle">The objectHandle parameter must be a valid transmit NormObjectHandle that has not yet been "purged" 
        /// from the sender's transmit queue.</param>
        /// <returns>A value of true is returned upon success and a value of false is returned upon failure.</returns>
        [DllImport(NORM_LIBRARY)]
        public static extern bool NormRequeueObject(long sessionHandle, long objectHandle);

        /// <summary>
        /// This function opens a NORM_OBJECT_STREAM sender object and enqueues it for transmission within the indicated sessionHandle.
        /// </summary>
        /// <param name="sessionHandle">Used to identify application in the NormSession.</param>
        /// <param name="bufferSize">The bufferSize parameter controls the size of the stream's "repair window"
        /// which limits how far back the sender will "rewind" to satisfy receiver repair requests.</param>
        /// <param name="infoPtr">Note  that no data is sent until subsequent calls to NormStreamWrite() are made unless
        /// NORM_INFO content is specified for the stream with the infoPtr and infoLen parameters. Example usage of
        /// NORM_INFO content for NORM_OBJECT_STREAM might include application-defined data typing or other information
        /// which will enable NORM receiver applications to properly interpret the received stream as it is being received.</param>
        /// <param name="infoLen">Note  that no data is sent until subsequent calls to NormStreamWrite() are made unless
        /// NORM_INFO content is specified for the stream with the infoPtr and infoLen parameters. Example usage of
        /// NORM_INFO content for NORM_OBJECT_STREAM might include application-defined data typing or other information
        /// which will enable NORM receiver applications to properly interpret the received stream as it is being received.</param>
        /// <returns>A NormObjectHandle is returned which the application may use in other NORM API calls as needed.</returns>
        [DllImport(NORM_LIBRARY)]
        public static extern long NormStreamOpen(long sessionHandle, long bufferSize, nint infoPtr, int infoLen);

        /// <summary>
        /// This function halts transfer of the stream specified by the streamHandle parameter and releases any resources
        /// used unless the associated object has been explicitly retained by a call to NormObjectRetain().
        /// </summary>
        /// <param name="streamHandle">The streamHandle parameter must be a valid transmit NormObjectHandle.</param>
        /// <param name="graceful">The optional graceful parameter, when
        /// set to a value of true, may be used by NORM senders to initiate "graceful" shutdown of a transmit stream.</param>
        [DllImport(NORM_LIBRARY)]
        public static extern void NormStreamClose(long streamHandle, bool graceful);

        /// <summary>
        /// This function enqueues data for transmission within the NORM stream specified by the streamHandle parameter.
        /// </summary>
        /// <param name="streamHandle">The streamHandle parameter must be a valid transmit NormObjectHandle.</param>
        /// <param name="buffer">The buffer parameter must be a pointer to the data to be enqueued.</param>
        /// <param name="numBytes">The numBytes parameter indicates the length of the data content.</param>
        /// <returns>This function returns the number of bytes of data successfully enqueued for NORM stream transmission.</returns>
        [DllImport(NORM_LIBRARY)]
        internal static extern int NormStreamWrite(long streamHandle, nint buffer, int numBytes);

        /// <summary>
        /// This function causes an immediate "flush" of the transmit stream specified by the streamHandle parameter.
        /// </summary>
        /// <param name="streamHandle">The streamHandle parameter must be a valid transmit NormObjectHandle.</param>
        /// <param name="eom">The optional eom parameter, when set to true, allows the sender application to mark an end-of-message indication
        /// (see NormStreamMarkEom()) for the stream and initiate flushing in a single function call.</param>
        /// <param name="flushMode">The default stream "flush" operation invoked via
        /// NormStreamFlush() for flushMode equal to NORM_FLUSH_PASSIVE causes NORM to immediately transmit all
        /// enqueued data for the stream(subject to session transmit rate limits), even if this results in NORM_DATA messages
        /// with  "small"  payloads.If the  optional flushMode  parameter  is  set to  NORM_FLUSH_ACTIVE,  the application  can
        /// achieve reliable delivery of stream content up to the current write position in an even more proactive fashion.In
        /// this  case, the sender  additionally, actively transmits  NORM_CMD(FLUSH) messages  after any  enqueued stream
        /// content has  been sent.  This immediately  prompt receivers  for  repair requests  which reduces  latency of  reliable
        /// delivery, but at a cost of some additional messaging. Note any such "active" flush activity will be terminated upon
        /// the next subsequent write to the stream.If flushMode is set to NORM_FLUSH_NONE, this call has no effect other than
        /// the optional end-of-message marking described here</param>
        [DllImport(NORM_LIBRARY)]
        public static extern void NormStreamFlush(long streamHandle, bool eom, NormFlushMode flushMode);

        /// <summary>
        /// This function sets "automated flushing" for the NORM transmit stream indicated by the streamHandle parameter.
        /// </summary>
        /// <param name="streamHandle">The streamHandle parameter must be a valid transmit NormObjectHandle.</param>
        /// <param name="flushMode">Possible values for the flushMode parameter include NORM_FLUSH_NONE, NORM_FLUSH_PASSIVE, and NORM_FLUSH_ACTIVE.</param>
        [DllImport(NORM_LIBRARY)]
        public static extern void NormStreamSetAutoFlush(long streamHandle, NormFlushMode flushMode);

        /// <summary>
        /// This function controls how the NORM API behaves when the application attempts to enqueue new stream data
        /// for transmission when the associated stream's transmit buffer is fully occupied with data pending original or repair
        /// transmission.
        /// </summary>
        /// <param name="streamHandle">The streamHandle parameter must be a valid transmit NormObjectHandle.</param>
        /// <param name="pushEnable"> By default (pushEnable = false), a call to NormStreamWrite() will return a zero value under this
        /// condition, indicating it was unable to enqueue the new data. However, if pushEnable is set to true for a given
        /// streamHandle, the NORM protocol engine will discard the oldest buffered stream data(even if it is pending repair
        /// transmission or has never been transmitted) as needed to enqueue the new data.</param>
        [DllImport(NORM_LIBRARY)]
        public static extern void NormStreamSetPushEnable(long streamHandle, bool pushEnable);

        /// <summary>
        /// This function can be used to query whether the transmit stream, specified by the streamHandle parameter, has
        /// buffer space available so that the application may successfully make a call to NormStreamWrite().
        /// </summary>
        /// <param name="streamHandle">The streamHandle parameter must be a valid transmit NormObjectHandle.</param>
        /// <returns>This function returns a value of true when there is transmit buffer space to which the application may write and false otherwise.</returns>
        [DllImport(NORM_LIBRARY)]
        public static extern bool NormStreamHasVacancy(long streamHandle);

        /// <summary>
        /// This function allows the application to indicate to the NORM protocol engine that the last data successfully written
        /// to the stream indicated by streamHandle corresponded to the end of an application-defined message boundary.
        /// </summary>
        /// <param name="streamHandle">The streamHandle parameter must be a valid transmit NormObjectHandle.</param>
        [DllImport(NORM_LIBRARY)]
        public static extern void NormStreamMarkEom(long streamHandle);

        /// <summary>
        /// This function specifies a "watermark" transmission point at which NORM sender protocol operation should perform
        /// a flushing process and/or positive acknowledgment collection for a given sessionHandle.
        /// </summary>
        /// <param name="sessionHandle">Used to identify application in the NormSession.</param>
        /// <param name="objectHandle">The objectHandle parameter must be a valid transmit NormObjectHandle that has not yet been "purged" from the sender's transmit queue.</param>
        /// <param name="overrideFlush">The optional overrideFlush parameter, when set to true, causes the watermark acknowledgment process that is
        /// established with this function call to potentially fully supersede the usual NORM end-of-transmission flushing
        /// process that  occurs.If overrideFlush  is  set and  the  "watermark"  transmission point  corresponds to  the last
        /// transmission that will result from data enqueued by the sending application, then the watermark flush completion
        /// will terminate the usual flushing process</param>
        /// <returns>The function returns true upon successful establishment of the watermark point. The function may return false upon failure.</returns>
        [DllImport(NORM_LIBRARY)]
        public static extern bool NormSetWatermark(long sessionHandle, long objectHandle, bool overrideFlush);
        
        [DllImport(NORM_LIBRARY)]
        public static extern bool NormResetWatermark(long sessionHandle);

        /// <summary>
        /// This function cancels any "watermark" acknowledgement request that was previously set via the NormSetWatermark() function for the given sessionHandle.
        /// </summary>
        /// <param name="sessionHandle">Used to identify application in the NormSession.</param>
        [DllImport(NORM_LIBRARY)]
        public static extern void NormCancelWatermark(long sessionHandle);

        /// <summary>
        /// When this function is called, the specified nodeId is added to the list of NormNodeId values (i.e., the "acking node"
        /// list) used when NORM sender operation performs positive acknowledgement (ACK) collection for the specified sessionHandle. 
        /// </summary>
        /// <param name="sessionHandle">Used to identify application in the NormSession.</param>
        /// <param name="nodeId">Identifies the application's presence in the NormSession.</param>
        /// <returns>The function returns true upon success and false upon failure.</returns>
        [DllImport(NORM_LIBRARY)]
        public static extern bool NormAddAckingNode(long sessionHandle, long nodeId);

        /// <summary>
        /// This function deletes the specified nodeId from the list of NormNodeId values used when NORM sender operation
        /// performs positive acknowledgement (ACK) collection for the specified sessionHandle.
        /// </summary>
        /// <param name="sessionHandle">Used to identify application in the NormSession.</param>
        /// <param name="nodeId">Identifies the application's presence in the NormSession.</param>
        [DllImport(NORM_LIBRARY)]
        public static extern void NormRemoveAckingNode(long sessionHandle, long nodeId);

        /// <summary>
        /// This function queries the status of the watermark flushing process and/or positive acknowledgment collection
        /// initiated by a prior call to NormSetWatermark() for the given sessionHandle.
        /// </summary>
        /// <param name="sessionHandle">Used to identify application in the NormSession.</param>
        /// <param name="nodeId">Identifies the application's presence in the NormSession.</param>
        /// <returns>
        /// Possible return values include:
        /// NORM_ACK_INVALID - The given sessionHandle is invalid or the given nodeId is not in the sender's acking list.
        /// NORM_ACK_FAILURE - The positive acknowledgement collection process did not receive acknowledgment from every listed receiver (nodeId = NORM_NODE_ANY) or the identified nodeId did not respond.
        /// NORM_ACK_PENDING - The flushing process at large has not yet completed (nodeId = NORM_NODE_ANY) or the given individual nodeId is still being queried for response.
        /// NORM_ACK_SUCCESS - All receivers (nodeId = NORM_NODE_ANY) responded with positive acknowledgement or the given specific nodeId did acknowledge.
        /// </returns>
        [DllImport(NORM_LIBRARY)]
        public static extern NormAckingStatus NormGetAckingStatus(long sessionHandle, long nodeId);

        /// <summary>
        /// This function enqueues a NORM application-defined command for transmission.
        /// </summary>
        /// <param name="sessionHandle">Used to identify application in the NormSession.</param>
        /// <param name="cmdBuffer">The cmdBuffer parameter points to a buffer containing the application-defined command content 
        /// that will be contained in the NORM_CMD(APPLICATION) message payload.</param>
        /// <param name="cmdLength">The cmdLength indicates the length of this content (in bytes) and MUST be less than or equal 
        /// to the segmentLength value for the given session (see NormStartSender()).</param>
        /// <param name="robust">The command is NOT delivered reliably, but can be optionally transmitted with repetition 
        /// (once per GRTT) according to the NORM transmit robust factor value (see NormSetTxRobustFactor()) for the given 
        /// session if the robust parameter is set to true.</param>
        /// <returns>The function returns true upon success. The function may fail, returning false, if the session is not set for sender
        /// operation (see NormStartSender()), the cmdLength exceeds the configured session segmentLength, or a previously-
        /// enqueued command has not yet been sent.</returns>
        [DllImport(NORM_LIBRARY)]
        public static extern bool NormSendCommand(long sessionHandle, nint cmdBuffer, int cmdLength, bool robust);

        /// <summary>
        /// This function terminates any pending NORM_CMD(APPLICATION) transmission that was previously initiated with the NormSendCommand() call. 
        /// </summary>
        /// <param name="sessionHandle">Used to identify application in the NormSession.</param>
        [DllImport(NORM_LIBRARY)]
        public static extern void NormCancelCommand(long sessionHandle);

        /// <summary>
        /// This function initiates the application's participation as a receiver within the NormSession identified by the sessionHandle parameter.
        /// </summary>
        /// <param name="sessionHandle">Used to identify application in the NormSession.</param>
        /// <param name="bufferSpace">The bufferSpace parameter is used to set a limit on the amount of bufferSpace allocated
        /// by the receiver per active NormSender within the session.</param>
        /// <returns>A value of true is returned upon success and false upon failure.</returns>
        [DllImport(NORM_LIBRARY)]
        public static extern bool NormStartReceiver(long sessionHandle, long bufferSpace);

        /// <summary>
        /// This function ends the application's participation as a receiver in the NormSession specified by the session parameter.
        /// </summary>
        /// <param name="sessionHandle">Used to identify application in the NormSession.</param>
        [DllImport(NORM_LIBRARY)]
        public static extern void NormStopReceiver(long sessionHandle);

        /// <summary>
        /// This function sets a limit on the number of outstanding (pending) NormObjects for which a receiver will keep state on a per-sender basis.
        /// </summary>
        /// <param name="sessionHandle">Used to identify application in the NormSession.</param>
        /// <param name="countMax"> Note that the value countMax sets a limit on the maximum consecutive range of objects that can be pending</param>
        [DllImport(NORM_LIBRARY)]
        public static extern void NormSetRxCacheLimit(long sessionHandle, int countMax);

        /// <summary>
        /// This function allows the application to set an alternative, non-default buffer size for the UDP socket used by the specified NORM sessionHandle for packet reception. 
        /// </summary>
        /// <param name="sessionHandle">Used to identify application in the NormSession.</param>
        /// <param name="bufferSize">The bufferSize parameter specifies the socket buffer size in bytes.</param>
        /// <returns>This function returns true upon success and false upon failure.</returns>
        [DllImport(NORM_LIBRARY)]
        public static extern bool NormSetRxSocketBuffer(long sessionHandle, long bufferSize);
        
        /// <summary>
        /// This function provides the option to configure a NORM receiver application as a "silent receiver". This mode of
        /// receiver operation dictates that the host does not generate any protocol messages while operating as a receiver
        /// within the specified sessionHandle.
        /// </summary>
        /// <param name="sessionHandle">Used to identify application in the NormSession.</param>
        /// <param name="silent">SSetting the silent parameter to true enables silent receiver operation while
        /// setting it to false results in normal protocol operation where feedback is provided as needed for reliability and
        /// protocol operation.</param>
        /// <param name="maxDelay">When the maxDelay parameter is set to a non-negative value, the value determines the maximum number
        /// of FEC coding blocks (according to a NORM sender's current transmit position) the receiver will cache an incompletely-received 
        /// FEC block before giving the application the (incomplete) set of received source segments.</param>
        [DllImport(NORM_LIBRARY)]
        public static extern void NormSetSilentReceiver(long sessionHandle, bool silent, int maxDelay);

        /// <summary>
        /// This function controls the default behavior determining the destination of receiver feedback messages generated
        /// while participating in the session.
        /// </summary>
        /// <param name="sessionHandle">Used to identify application in the NormSession.</param>
        /// <param name="unicastNacks">If the unicastNacks parameter is true, "unicast NACKing" is enabled for new remote
        /// senders while it is disabled for state equal to false.</param>
        [DllImport(NORM_LIBRARY)]
        public static extern void NormSetDefaultUnicastNack(long sessionHandle, bool unicastNacks);

        /// <summary>
        /// This function controls the destination address of receiver feedback messages generated in response to a specific
        /// remote NORM sender corresponding to the remoteSender parameter.
        /// </summary>
        /// <param name="remoteSender">Used to specify the remote NORM sender.</param>
        /// <param name="unicastNacks">If unicastNacks is true, "unicast NACKing" is enabled
        /// while it is disabled for enable equal to false.</param>
        [DllImport(NORM_LIBRARY)]
        public static extern void NormNodeSetUnicastNack(long remoteSender, bool unicastNacks);

        /// <summary>
        /// This function sets the default "synchronization policy" used when beginning (or restarting) reception of objects
        /// from a remote sender (i.e., "syncing" to the sender) for the given sessionHandle
        /// </summary>
        /// <param name="sessionHandle">Used to identify application in the NormSession.</param>
        /// <param name="syncPolicy">The "synchronization policy"
        /// is the behavior observed by the receiver with regards to what objects it attempts to reliably receive (via transmissions
        /// of Negative Acknowledgements to the sender(s) or group as needed). There are currently two synchronization policy types defined.</param>
        [DllImport(NORM_LIBRARY)]
        public static extern void NormSetDefaultSyncPolicy(long sessionHandle, NormSyncPolicy syncPolicy);

        /// <summary>
        /// This function sets the default "nacking mode" used when receiving objects for the given sessionHandle.
        /// This allows the receiver application some control of its degree of participation in the repair process. By limiting receivers
        /// to only request repair of objects in which they are really interested in receiving, some overall savings in unnecessary
        /// network loading might be realized for some applications and users.
        /// </summary>
        /// <param name="sessionHandle">Used to identify application in the NormSession.</param>
        /// <param name="nackingMode">Specifies the nacking mode. </param>
        [DllImport(NORM_LIBRARY)]
        public static extern void NormSetDefaultNackingMode(long sessionHandle, NormNackingMode nackingMode);

        /// <summary>
        /// This function sets the default "nacking mode" used for receiving new objects from a specific sender as identified
        /// by the remoteSender parameter.
        /// </summary>
        /// <param name="remoteSender">Used to specify the remote NORM sender.</param>
        /// <param name="nackingMode">Specifies the nacking mode. </param>
        [DllImport(NORM_LIBRARY)]
        public static extern void NormNodeSetNackingMode(long remoteSender, NormNackingMode nackingMode);

        /// <summary>
        /// This function sets the "nacking mode" used for receiving a specific transport object as identified by the objectHandle parameter.
        /// </summary>
        /// <param name="objectHandle">Specifies the transport object.</param>
        /// <param name="nackingMode">Specifies the nacking mode. </param>
        [DllImport(NORM_LIBRARY)]
        public static extern void NormObjectSetNackingMode(long objectHandle, NormNackingMode nackingMode);

        /// <summary>
        /// This function allows the receiver application to customize, for a given sessionHandle, at what points the receiver
        /// initiates the NORM NACK repair process during protocol operation.
        /// </summary>
        /// <param name="sessionHandle">Used to identify application in the NormSession.</param>
        /// <param name="repairBoundary">Specifies the repair boundary. </param>
        [DllImport(NORM_LIBRARY)]
        public static extern void NormSetDefaultRepairBoundary(long sessionHandle, NormRepairBoundary repairBoundary);

        /// <summary>
        /// This function allows the receiver application to customize, for the specific remote sender referenced by the remoteSender 
        /// parameter, at what points the receiver initiates the NORM NACK repair process during protocol operation.
        /// </summary>
        /// <param name="remoteSender">Used to specify the remote NORM sender. </param>
        /// <param name="repairBoundary">Specifies the repair boundary. </param>
        [DllImport(NORM_LIBRARY)]
        public static extern void NormNodeSetRepairBoundary(long remoteSender, NormRepairBoundary repairBoundary);

        /// <summary>
        /// This routine controls how persistently NORM receivers will maintain state for sender(s) and continue to request
        /// repairs from the sender(s) even when packet reception has ceased.
        /// </summary>
        /// <param name="sessionHandle">Used to identify application in the NormSession.</param>
        /// <param name="robustFactor">The robustFactor value determines how
        /// many times a NORM receiver will self-initiate NACKing (repair requests) upon cessation of packet reception from
        /// a sender. The default value is 20. Setting rxRobustFactor to -1 will make the NORM receiver infinitely persistent
        /// (i.e., it will continue to NACK indefinitely as long as it is missing data content).</param>
        [DllImport(NORM_LIBRARY)]
        public static extern void NormSetDefaultRxRobustFactor(long sessionHandle, int robustFactor);

        /// <summary>
        /// This routine sets the robustFactor as described in NormSetDefaultRxRobustFactor() for an individual remote
        /// sender identified by the remoteSender parameter.
        /// </summary>
        /// <param name="remoteSender">Used to specify the remote NORM sender.</param>
        /// <param name="robustFactor">The robustFactor value determines how
        /// many times a NORM receiver will self-initiate NACKing (repair requests) upon cessation of packet reception from
        /// a sender. The default value is 20. Setting rxRobustFactor to -1 will make the NORM receiver infinitely persistent
        /// (i.e., it will continue to NACK indefinitely as long as it is missing data content).</param>
        [DllImport(NORM_LIBRARY)]
        public static extern void NormNodeSetRxRobustFactor(long remoteSender, int robustFactor);

        /// <summary>
        /// This function can be used by the receiver application to read any available data from an incoming NORM stream.
        /// </summary>
        /// <param name="streamHandle">The streamHandle parameter here must correspond to a valid NormObjectHandle value provided during such a
        /// prior NORM_RX_OBJECT_NEW notification.</param>
        /// <param name="buffer">The buffer parameter must be a pointer to an array where the received
        /// data can be stored of a length as referenced by the numBytes pointer</param>
        /// <param name="numBytes">Specifies the length of data.</param>
        /// <returns>This function normally returns a value of true. However, if a break in the integrity of the reliable received stream 
        /// occurs(or the stream has been ended by the sender), a value of false is returned to indicate the break. </returns>
        [DllImport(NORM_LIBRARY)]
        public static extern bool NormStreamRead(long streamHandle, nint buffer, ref int numBytes);

        /// <summary>
        /// This function advances the read offset of the receive stream referenced by the streamHandle parameter to align
        /// with the next available message boundary
        /// </summary>
        /// <param name="streamHandle">The streamHandle parameter here must correspond to a valid NormObjectHandle value provided during such a
        /// prior NORM_RX_OBJECT_NEW notification.</param>
        /// <returns>This function returns a value of true when start-of-message is found. The next call to NormStreamRead() will
        /// retrieve data aligned with the message start. If no new message boundary is found in the buffered receive data for
        /// the stream, the function returns a value of false. In this case, the application should defer repeating a call to this
        /// function until a subsequent NORM_RX_OBJECT_UPDATE notification is posted.</returns>
        [DllImport(NORM_LIBRARY)]
        public static extern bool NormStreamSeekMsgStart(long streamHandle);

        /// <summary>
        /// This function retrieves the current read offset value for the receive stream indicated by the streamHandle parameter.
        /// </summary>
        /// <param name="streamHandle">The streamHandle parameter here must correspond to a valid NormObjectHandle value provided during such a
        /// prior NORM_RX_OBJECT_NEW notification. </param>
        /// <returns>This function returns the current read offset in bytes. The return value is undefined for sender streams. There is
        /// no error result.</returns>
        [DllImport(NORM_LIBRARY)]
        public static extern long NormStreamGetReadOffset(long streamHandle);
        
        /// <summary>
        /// This function can be used to determine the object type (NORM_OBJECT_DATA, NORM_OBJECT_FILE, or NORM_OBJECT_STREAM) for the 
        /// NORM transport object identified by the objectHandle parameter.
        /// </summary>
        /// <param name="objectHandle">The objectHandle must refer to a current, valid transport object.</param>
        /// <returns>This function returns the NORM object type. Valid NORM object types include NORM_OBJECT_DATA, NORM_OBJECT_FILE, 
        /// or NORM_OBJECT_STREAM. A type value of NORM_OBJECT_NONE will be returned for an objectHandle value of NORM_OBJECT_INVALID.</returns>
        [DllImport(NORM_LIBRARY)]
        public static extern NormObjectType NormObjectGetType(long objectHandle);

        /// <summary>
        /// This function can be used to determine if the sender has associated any NORM_INFO content with the transport object
        /// specified by the objectHandle parameter.
        /// </summary>
        /// <param name="objectHandle">The objectHandle must refer to a current, valid transport object.</param>
        /// <returns>A value of true is returned if NORM_INFO is (or will be) available for the specified transport object. A value of
        /// false is returned otherwise.</returns>
        [DllImport(NORM_LIBRARY)]
        public static extern bool NormObjectHasInfo(long objectHandle);

        /// <summary>
        /// This function can be used to determine the length of currently available NORM_INFO content (if any) associated
        /// with the transport object referenced by the objectHandle parameter.
        /// </summary>
        /// <param name="objectHandle">The objectHandle must refer to a current, valid transport object.</param>
        /// <returns>The length of the NORM_INFO content, in bytes, of currently available for the specified transport object is returned.
        /// A value of 0 is returned if no NORM_INFO content is currently available or associated with the object.</returns>
        [DllImport(NORM_LIBRARY)]
        public static extern int NormObjectGetInfoLength(long objectHandle);

        /// <summary>
        /// This function copies any NORM_INFO content associated (by the sender application) with the transport object specified
        /// by objectHandle into the provided memory space referenced by the buffer parameter.
        /// </summary>
        /// <param name="objectHandle">The objectHandle must refer to a current, valid transport object.</param>
        /// <param name="buffer">Provided memory space for the object info. </param>
        /// <param name="bufferLen">The bufferLen parameter indicates the length of the buffer space in bytes.</param>
        /// <returns>The actual length of currently available NORM_INFO content for the specified transport object is returned. This
        /// function can be used to determine the length of NORM_INFO content for the object even if a NULL buffer value and
        /// zero bufferLen is provided. A zero value is returned if NORM_INFO content has not yet been received (or is nonexistent) for the specified object.</returns>
        [DllImport(NORM_LIBRARY)]
        public static extern int NormObjectGetInfo(long objectHandle, nint buffer, int bufferLen);

        /// <summary>
        /// This function can be used to determine the size (in bytes) of the transport object specified by the objectHandle parameter.
        /// </summary>
        /// <param name="objectHandle">The objectHandle must refer to a current, valid transport object.</param>
        /// <returns>A size of the data content of the specified object, in bytes, is returned. Note that it may be possible that some objects
        /// have zero data content, but do have NORM_INFO content available.</returns>
        [DllImport(NORM_LIBRARY)]
        public static extern int NormObjectGetSize(long objectHandle);

        /// <summary>
        /// This function can be used to determine the progress of reception of the NORM transport object identified by the objectHandle parameter
        /// </summary>
        /// <param name="objectHandle">The objectHandle must refer to a current, valid transport object.</param>
        /// <returns>A number of object source data bytes pending reception (or transmission) is returned.</returns>
        [DllImport(NORM_LIBRARY)]
        public static extern long NormObjectGetBytesPending(long objectHandle);

        /// <summary>
        /// This function immediately cancels the transmission of a local sender transport object or the reception of a specified
        /// object from a remote sender as specified by the objectHandle parameter
        /// </summary>
        /// <param name="objectHandle">The objectHandle must refer to a current, valid transport object.</param>
        [DllImport(NORM_LIBRARY)]
        public static extern void NormObjectCancel(long objectHandle);

        /// <summary>
        /// This function "retains" the objectHandle and any state associated with it for further use by the application even
        /// when the NORM protocol engine may no longer require access to the associated transport object. 
        /// </summary>
        /// <param name="objectHandle">The objectHandle must refer to a current, valid transport object.</param>
        [DllImport(NORM_LIBRARY)]
        public static extern void NormObjectRetain(long objectHandle);

        /// <summary>
        /// This function complements the NormObjectRetain() call by immediately freeing any resources associated with
        /// the given objectHandle, assuming the underlying NORM protocol engine no longer requires access to the corresponding
        /// transport object. Note the NORM protocol engine retains/releases state for associated objects for its own
        /// needs and thus it is very unsafe for an application to call NormObjectRelease() for an objectHandle for which
        /// it has not previously explicitly retained via NormObjectRetain().
        /// </summary>
        /// <param name="objectHandle">The objectHandle must refer to a current, valid transport object.</param>
        [DllImport(NORM_LIBRARY)]
        public static extern void NormObjectRelease(long objectHandle);

        /// <summary>
        /// This function copies the name, as a NULL-terminated string, of the file object specified by the objectHandle
        /// parameter into the nameBuffer of length bufferLen bytes provided by the application.
        /// </summary>
        /// <param name="fileHandle">This type is used to reference state kept for data transport objects being actively transmitted or received. The objectHandle parameter 
        /// must refer to a valid NormObjectHandle for an object of type NORM_OBJECT_FILE.</param>
        /// <param name="nameBuffer">provided memory space for the name of the file</param>
        /// <param name="bufferLen">idicates the length of the nameBuffer</param>
        /// <returns>
        /// This function returns true upon success and false upon failure. Possible failure conditions include the objectHandle
        /// does not refer to an object of type NORM_OBJECT_FILE.
        /// </returns>
        [DllImport(NORM_LIBRARY)]
        public static extern bool NormFileGetName(long fileHandle, nint nameBuffer, int bufferLen);

        /// <summary>
        /// This function renames the file used to store content for the NORM_OBJECT_FILE transport object specified by 
        /// the objectHandle parameter. This allows receiver applications to rename (or move) received files as needed.NORM
        /// uses temporary file names for received files until the application explicitly renames the file.For example, sender
        /// applications may choose to use the NORM_INFO content associated with a file object to provide name and/or typing
        /// information to receivers.
        /// </summary>
        /// <param name="fileHandle">This type is used to reference state kept for data transport objects being actively transmitted or received.</param>
        /// <param name="fileName">parameter must be a NULL-terminated string which should specify the full desired path name to be used</param>
        /// <returns>
        /// This function returns true upon success and false upon failure. Possible failure conditions include the case where
        /// the objectHandle does not refer to an object of type NORM_OBJECT_FILE and where NORM was unable to successfully
        /// create any needed directories and/or the file itself.
        /// </returns>
        [DllImport(NORM_LIBRARY)]
        public static extern bool NormFileRename(long fileHandle, string fileName);

        /// <summary>
        /// This function allows the application to access the data storage area associated with a transport object of type
        /// NORM_OBJECT_DATA.For example, the application may use this function to copy the received data content for its 
        /// own use.Alternatively, the application may establish "ownership" for the allocated memory space using the
        /// NormDataDetachData() function if it is desired to avoid the copy.
        /// </summary>
        /// <param name="objectHandle">This type is used to reference state kept for data transport objects being actively transmitted or received.</param>
        /// <returns>
        /// This function returns a pointer to the data storage area for the specified transport object. A NULL value may be
        /// returned if the object has no associated data content or is not of type NORM_OBJECT_DATA.
        /// </returns>
        [DllImport(NORM_LIBRARY)]
        public static extern nint NormDataAccessData(long objectHandle);

        /// <summary>
        /// This function retrieves the NormNodeHandle corresponding to the remote sender of the transport object associated with the given objectHandle parameter.
        /// </summary>
        /// <param name="objectHandle">This type is used to reference state kept for data transport objects being actively transmitted or received.</param>
        /// <returns>This function returns the NormNodeHandle corresponding to the remote sender of the transport object associated with the given objectHandle parameter.
        /// A value of NORM_NODE_INVALID is returned if the specified objectHandle 
        /// references a locally originated, sender object.</returns>
        [DllImport(NORM_LIBRARY)]
        public static extern long NormObjectGetSender(long objectHandle);

        /// <summary>
        /// This function retrieves the NormNodeId identifier for the remote participant referenced by the given nodeHandle  value.
        /// </summary>
        /// <param name="nodeHandle">This type is used to reference state kept by the NORM implementation with respect to other participants within a NormSession.</param>
        /// <returns>This function returns the NormNodeId value associated with the specified nodeHandle.
        /// In the case nodeHandle is equal to NORM_NODE_INVALID, the return value will be NORM_NODE_NONE.</returns>
        [DllImport(NORM_LIBRARY)]
        public static extern long NormNodeGetId(long nodeHandle);

        /// <summary>
        /// This function retrieves the current network source address detected for packets received from remote NORM sender referenced by the nodeHandle parameter.
        /// </summary>
        /// <param name="nodeHandle"> This type is used to reference state kept by the NORM implementation with respect to other participants within a NormSession.</param>
        /// <param name="addrBuffer">The addrBuffer must be a pointer to storage of bufferLen bytes in length in which the referenced sender node's address will be returned</param>
        /// <param name="bufferLen">A return value of false indicates that either no command was available or the provided buffer size</param>
        /// <param name="port">port number and/or specify a specific source address binding that is used for packet transmission.</param>
        /// <returns>A value of true is returned upon success and false upon failure. An invalid nodeHandle parameter value would lead to such failure.</returns>
        [DllImport(NORM_LIBRARY)]
        public static extern bool NormNodeGetAddress(long nodeHandle, nint addrBuffer, ref int bufferLen, out int port);

        /// <summary>
        /// This function retrieves the advertised estimate of group round-trip timing (GRTT) for the remote sender referenced by the given nodeHandle value.
        /// Newly-starting senders that have been participating as a receiver within a group 
        /// may wish to use this function to provide a more accurate startup estimate of GRTT prior to a call to NormStartSender()
        /// </summary>
        /// <param name="nodeHandle"> This type is used to reference state kept by the NORM implementation with respect to other participants within a NormSession.</param>
        /// <returns>This function returns the remote sender's advertised GRTT estimate in units of seconds.
        /// A value of -1.0 is returned upon failure.An invalid nodeHandle parameter value will lead to such failure.</returns>
        [DllImport(NORM_LIBRARY)]
        public static extern double NormNodeGetGrtt(long nodeHandle);

        /// <summary>
        /// This function retrieves the content of an application-defined command that was received from a remote sender associated with the given nodeHandle.
        /// </summary>
        /// <param name="remoteSender"> notification for a given remote sender when multiple senders may be providing content</param>
        /// <param name="cmdBuffer">Allocated system resources for each active sender</param>
        /// <param name="buflen">A return value of false indicates that either no command was available or the provided buffer size</param>
        /// <returns>This function returns true upon successful retrieval of command content. A return value of false indicates that
        /// either no command was available or the provided buffer size (buflen parameter) was inadequate.
        /// The value referenced by the buflen parameter is adjusted to indicate the actual command length (in bytes) upon return.</returns>
        [DllImport(NORM_LIBRARY)]
        public static extern bool NormNodeGetCommand(long remoteSender, nint cmdBuffer, ref int buflen);

        /// <summary>
        /// This function releases memory resources that were allocated for a remote sender. 
        /// </summary>
        /// <param name="remoteSender">notification for a given remote sender when multiple senders may be providing content</param>
        [DllImport(NORM_LIBRARY)]
        public static extern void NormNodeFreeBuffers(long remoteSender);

        /// <summary>
        /// this function allows the application to retain state associated with a given nodeHandle 
        /// value even when the underlying NORM protocol engine might normally 
        /// free the associated state and thus invalidate the NormNodeHandle.
        /// </summary>
        /// <param name="nodeHandle">This type is used to reference state kept by the NORM implementation with respect to other participants within a NormSession.</param>
        [DllImport(NORM_LIBRARY)]
        public static extern void NormNodeRetain(long nodeHandle);
        
        /// <summary>
        /// In complement to the NormNodeRetain() function, this API call releases the specified nodeHandle so that the
        /// NORM protocol engine may free associated resources as needed.Once this call is made, the application should
        /// no longer reference the specified NormNodeHandle, unless it is still valid.
        /// </summary>
        /// <param name="nodeHandle">This type is used to reference state kept by the NORM implementation with respect to other participants within a NormSession.</param>
        [DllImport(NORM_LIBRARY)]
        public static extern void NormNodeRelease(long nodeHandle);
    }
}
