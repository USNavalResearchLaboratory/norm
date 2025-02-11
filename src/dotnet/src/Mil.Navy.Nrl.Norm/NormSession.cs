using System.Runtime.InteropServices;
using System.Text;

namespace Mil.Navy.Nrl.Norm
{
    /// <summary>
    /// NORM transport session
    /// </summary>
    public class NormSession
    {
        /// <summary>
        /// The special value NORM_SESSION_INVALID is used to refer to invalid session references.
        /// </summary>
        public const int NORM_SESSION_INVALID = 0;
        /// <summary>
        /// A dictionary of NORM sessions with their respective handles.
        /// </summary>
        private static Dictionary<long, NormSession> _normSessions = new Dictionary<long, NormSession>();
        /// <summary>
        /// The NormSessionHandle type is used to reference the NORM transport session.
        /// </summary>
        private long _handle;

        /// <summary>
        /// Used for the application's participation in the NormSession.
        /// </summary>
        public long LocalNodeId
        {
            get => NormGetLocalNodeId(_handle);
        }

        /// <summary>
        /// The report interval.
        /// </summary>
        public double ReportInterval 
        {
            get => NormGetReportInterval(_handle); 
            set => NormSetReportInterval(_handle, value); 
        }

        /// <summary>
        /// Transmission rate (in bits per second (bps)) limit used for NormSender transmissions.
        /// </summary>
        public double TxRate
        {
            get => NormGetTxRate(_handle);
            set => NormSetTxRate(_handle, value);
        }

        /// <summary>
        /// Group round-trip timing.
        /// </summary>
        public double GrttEstimate
        {
            get => NormGetGrttEstimate(_handle);
            set => NormSetGrttEstimate(_handle, value);
        }

        /// <summary>
        /// Internal constructor of NormSession.
        /// </summary>
        /// <param name="handle">Used to identify application in the NormSession.</param>
        /// <remarks>The handle and NormSession are added to the dictionary of NORM sessions.</remarks>
        internal NormSession(long handle)
        {
            _handle = handle;
            lock (_normSessions)
            {
                _normSessions.Add(handle, this);
            }     
        }

        /// <summary>
        /// Get a specified NormSession from the dictionary of NORM sessions.
        /// </summary>
        /// <param name="handle">Specifies the session to return.</param>
        /// <returns>Returns a NormSession.</returns>
        internal static NormSession? GetSession(long handle)
        {
            lock (_normSessions)
            {
                return _normSessions.TryGetValue(handle, out NormSession? session) ? session : null;
            }
        }

        /// <summary>
        /// This function immediately terminates the application's participation in the NormSession and frees any resources used by that session.
        /// </summary>
        public void DestroySession()
        {
            lock (_normSessions)
            {
                _normSessions.Remove(_handle);
            }
            DestroySessionNative();
        }

        /// <summary>
        /// This function immediately terminates the application's participation in the NormSession and frees any resources used by that session.
        /// </summary>
        private void DestroySessionNative()
        {
            NormDestroySession(_handle);
        }

        /// <summary>
        /// This function is used to force NORM to use a specific port number for UDP packets sent.
        /// </summary>
        /// <remarks>
        /// This is an overload which calls SetTxPort() with enableReuse set as false and txBindAddress set to null.
        /// </remarks>
        /// <param name="port">The port parameter, specifies which port number to use.</param>
        /// <exception cref="IOException">Thrown when NormSetTxPort() returns false, indicating the failure to set tx port.</exception>
        public void SetTxPort(int port)
        {
            SetTxPort(port, false, null);
        }

        /// <summary>
        /// This function is used to force NORM to use a specific port number for UDP packets sent.
        /// </summary>
        /// <param name="port">The port parameter, specifies which port number to use.</param>
        /// <param name="enableReuse">When set to true, allows that the specified port may be reused for multiple sessions.</param>
        /// <param name="txBindAddress">The txBindAddress parameter allows specification of a specific source address binding for packet transmission.</param>
        /// <exception cref="IOException">Thrown when NormSetTxPort() returns false, indicating the failure to set tx port.</exception>
        public void SetTxPort(int port, bool enableReuse, string? txBindAddress)
        {
            if (!NormSetTxPort(_handle, port, enableReuse, txBindAddress))
            {
                throw new IOException("Failed to set Tx Port");
            }
        }

        /// <summary>
        /// This function limits the NormSession to perform NORM sender functions only.
        /// </summary>
        /// <remarks>
        /// This is an overload which calls SetTxOnly() with connectToSessionAddress set as false.
        /// </remarks>
        /// <param name="txOnly">Boolean specifing whether to turn on or off the txOnly operation.</param>
        public void SetTxOnly(bool txOnly)
        {
            SetTxOnly(txOnly, false);
        }

        /// <summary>
        /// This function limits the NormSession to perform NORM sender functions only.
        /// </summary>
        /// <param name="txOnly">Boolean specifing whether to turn on or off the txOnly operation.</param>
        /// <param name="connectToSessionAddress">The optional connectToSessionAddress parameter, when set to true, causes the underlying NORM code to
        /// "connect()" the UDP socket to the session (remote receiver) address and port number.</param>
        public void SetTxOnly(bool txOnly, bool connectToSessionAddress)
        {
            NormSetTxOnly(_handle, txOnly, connectToSessionAddress);
        }

        /// <summary>
        /// This function allows the user to control the port reuse and binding behavior for the receive socket.
        /// </summary>
        /// <remarks>
        /// This is an overload that calls SetRxPortReuse() with rxBindAddress set to null, senderAddress set to null, and senderPort set to 0.
        /// </remarks>
        /// <param name="enable">When the enable parameter is set to true, reuse of the NormSession port number by multiple NORM instances or sessions is enabled.</param>
        public void SetRxPortReuse(bool enable)
        {
            SetRxPortReuse(enable, null, null, 0);
        }

        /// <summary>
        /// This function allows the user to control the port reuse and binding behavior for the receive socket.
        /// </summary>
        /// <param name="enable">When the enable parameter is set to true, reuse of the NormSession port number by multiple NORM instances or sessions is enabled.</param>
        /// <param name="rxBindAddress">If the optional rxBindAddress is supplied (an IP address or host name in string form),
        /// the socket will bind() to the given address when it is opened in a call to StartReceiver() or StartSender().</param>
        /// <param name="senderAddress">The optional senderAddress parameter can be used to connect() the underlying NORM receive socket to specific address.</param>
        /// <param name="senderPort">The optional senderPort parameter can be used to connect() the underlying NORM receive socket to specific port.</param>
        public void SetRxPortReuse(bool enable, string? rxBindAddress, string? senderAddress, int senderPort)
        {
            NormSetRxPortReuse(_handle, enable, rxBindAddress, senderAddress, senderPort);
        }

        /// <remarks>
        /// This is an overload which calls SetEcnSupport() with tolerateLoss set as false.
        /// </remarks>
        /// <param name="ecnEnable">Enables NORM ECN (congestion control) support.</param>
        /// <param name="ignoreLoss">With "ecnEnable", use ECN-only, ignoring packet loss.</param>
        public void SetEcnSupport(bool ecnEnable, bool ignoreLoss)
        {
            SetEcnSupport(ecnEnable, ignoreLoss, false);
        }

        /// <param name="ecnEnable">Enables NORM ECN (congestion control) support.</param>
        /// <param name="ignoreLoss">With "ecnEnable", use ECN-only, ignoring packet loss.</param>
        /// <param name="tolerateLoss">Loss-tolerant congestion control, ecnEnable or not.</param>
        public void SetEcnSupport(bool ecnEnable, bool ignoreLoss, bool tolerateLoss)
        {
            NormSetEcnSupport(_handle, ecnEnable, ignoreLoss, tolerateLoss);
        }

        /// <summary>
        /// This function specifies which host network interface is used for IP Multicast transmissions and group membership.
        /// This should be called before any call to StartSender() or StartReceiver() is made so that the IP multicast
        /// group is joined on the proper host interface.
        /// </summary>
        /// <param name="interfaceName">Name of the interface</param>
        /// <exception cref="IOException">Thrown when NormSetMulticastInterface() returns false, indicating the failure to set multicast interface.</exception>
        public void SetMulticastInterface(string interfaceName)
        {
            if(!NormSetMulticastInterface(_handle, interfaceName))
            {
                throw new IOException("Failed to set multicast interface");
            }
        }

        /// <summary>
        /// This function sets the source address for Source-Specific Multicast (SSM) operation.
        /// </summary>
        /// <param name="sourceAddress">Address to be set as source for source-specific multicast operation. </param>
        /// <exception cref="IOException">Thrown when NormSetSSM() returns false, indicating the failure to set ssm.</exception>
        public void SetSSM(string sourceAddress)
        {
            if(!NormSetSSM(_handle, sourceAddress))
            {
                throw new IOException("Failed to set SSM");
            }
        }

        /// <summary>
        /// This function specifies the time-to-live (ttl) for IP Multicast datagrams generated by NORM for the specified
        /// sessionHandle. The IP TTL field limits the number of router "hops" that a generated multicast packet may traverse
        /// before being dropped.
        /// </summary>
        /// <param name="ttl">If TTL is equal to one, the transmissions will be limited to the local area network
        /// (LAN) of the host computers network interface. Larger TTL values should be specified to span large networks.</param>
        /// <exception cref="IOException">Thrown when NormSetTTL() returns false, indicating the failure to set ttl.</exception>
        public void SetTTL(byte ttl)
        {
            if (!NormSetTTL(_handle, ttl))
            {
                throw new IOException("Failed to set TTL");
            }
        }

        /// <summary>
        /// This function specifies the type-of-service (tos) field value used in IP Multicast datagrams generated by NORM.
        /// </summary>
        /// <param name="tos">The IP TOS field value can be used as an indicator that a "flow" of packets may merit special Quality-of-Service (QoS) treatment by network devices.
        /// Users should refer to applicable QoS information for their network to determine the expected interpretation and treatment (if any) of packets with explicit TOS marking.</param>
        /// <exception cref="IOException">Thrown when NormSetTOS() returns false, indicating the failure to set TOS.</exception>
        public void SetTOS(byte tos)
        {
            if(!NormSetTOS(_handle, tos))
            {
                throw new IOException("Failed to set TOS");
            }
        }

        /// <summary>
        /// This function enables or disables loopback operation.
        /// </summary>
        /// <param name="loopbackEnable">If loopbackEnable is set to true, loopback operation is enabled which allows the application to receive its own message traffic.
        /// Thus, an application which is both actively receiving and sending may receive its own transmissions.</param>
        /// <exception cref="IOException">Thrown when NormSetLoopback() returns false, indicating the failure to set loopback.</exception>
        public void SetLoopback(bool loopbackEnable)
        {
            if (!NormSetLoopback(_handle, loopbackEnable))
            {
                throw new IOException("Failed to set loopback");
            }
        }

        public void SetMessageTrace(bool flag)
        {
            NormSetMessageTrace(_handle, flag);
        }

        public void SetTxLoss(double precent)
        {
            NormSetTxLoss(_handle, precent);
        }

        public void SetRxLoss(double precent)
        {
            NormSetRxLoss(_handle, precent);
        }

        /// <summary>
        /// This function controls a scaling factor that is used for sender timer-based flow control. 
        /// Timer-based flow control works by preventing the NORM sender application from enqueueing
        /// new transmit objects or stream data that would purge "old" objects or stream data when there has been recent
        /// NACK activity for those old objects or data.
        /// </summary>
        /// <param name="precent">The precent is used to compute a delay time for when a sender buffered object (or block of stream
        /// data) may be released (i.e. purged) after transmission or applicable NACKs reception. </param>
        public void SetFlowControl(double precent)
        {
            NormSetFlowControl(_handle, precent);
        }

        /// <summary>
        /// This function can be used to set a non-default socket buffer size for the UDP socket used for data transmission.
        /// </summary>
        /// <param name="bufferSize">The bufferSize parameter specifies the desired socket buffer size in bytes.</param>
        /// <exception cref="IOException">Thrown when NormSetTxSocketBuffer() returns false, indicating the failure to set tx socket buffer.
        /// Possible failure modes include an invalid sessionHandle parameter, a call to StartReceiver() or StartSender() has not yet been made for the
        /// session, or an invalid bufferSize was given.
        /// Note some operating systems may require additional system configuration to use non-standard socket buffer sizes.</exception>
        public void SetTxSocketBuffer(long bufferSize)
        {
            if(!NormSetTxSocketBuffer(_handle, bufferSize))
            {
                throw new IOException("Failed to set tx socket buffer");
            }
        }

        /// <summary>
        /// This function enables (or disables) the NORM sender congestion control operation. 
        /// For best operation, this function should be called before the call to StartSender() is made,
        /// but congestion control operation can be dynamically enabled/disabled during the course of sender operation.
        /// </summary>
        /// <remarks>
        /// This is an overload which calls SetCongestionControl() with adjustRate set to true.
        /// </remarks>
        /// <param name="enable">Specifies whether to enable or disable the NORM sender congestion control operation.</param>
        public void SetCongestionControl(bool enable)
        {
            SetCongestionControl(enable, true);
        }

        /// <summary>
        /// This function enables (or disables) the NORM sender congestion control operation. 
        /// For best operation, this function should be called before the call to StartSender() is made,
        /// but congestion control operation can be dynamically enabled/disabled during the course of sender operation.
        /// </summary>
        /// <param name="enable">Specifies whether to enable or disable the NORM sender congestion control operation.</param>
        /// <param name="adjustRate">The rate set by SetTxRate() has no effect when congestion control operation is enabled, unless the adjustRate
        /// parameter here is set to false. When the adjustRate parameter is set to false, the NORM Congestion Control
        /// operates as usual, with feedback collected from the receiver set and the "current limiting receiver" identified, except
        /// that no actual adjustment is made to the sender's transmission rate.</param>
        public void SetCongestionControl(bool enable, bool adjustRate)
        {
            NormSetCongestionControl(_handle, enable, adjustRate);
        }

        /// <summary>
        /// This function sets the range of sender transmission rates within which the NORM congestion control algorithm is allowed to operate.
        /// </summary>
        /// <param name="rateMin">rateMin corresponds to the minimum transmission rate (bps).</param>
        /// <param name="rateMax">rateMax corresponds to the maximum transmission rate (bps).</param>
        public void SetTxRateBounds(double rateMin, double rateMax)
        {
            NormSetTxRateBounds(_handle, rateMin, rateMax);
        }

        /// <summary>
        /// This function sets limits that define the number and total size of pending transmit objects a NORM sender will allow to be enqueued by the application.
        /// </summary>
        /// <param name="sizeMax">The sizeMax parameter sets the maximum total size, in bytes, of enqueued objects allowed.</param>
        /// <param name="countMin">The countMin parameter sets the minimum number of objects the application may enqueue, regardless of the objects' sizes and the sizeMax value.</param>
        /// <param name="countMax">The countMax parameter sets a ceiling on how many objects may be enqueued, regardless of their total sizes with respect to the sizeMax setting.</param>
        public void SetTxCacheBounds(long sizeMax, long countMin, long countMax)
        {
            NormSetTxCacheBounds(_handle,sizeMax, countMin, countMax);
        }

        /// <summary>
        /// The application's participation as a sender begins when this function is called.
        /// </summary>
        /// <param name="sessionId">Application-defined value used as the instance_id field of NORM sender messages for the application's participation within a session.</param>
        /// <param name="bufferSpace">This specifies the maximum memory space (in bytes) the NORM protocol engine is allowed to use to buffer any sender calculated FEC segments and repair state for the session.</param>
        /// <param name="segmentSize">This parameter sets the maximum payload size (in bytes) of NORM sender messages (not including any NORM message header fields).</param>
        /// <param name="blockSize">This parameter sets the number of source symbol segments (packets) per coding block, for the systematic Reed-Solomon FEC code used in the current NORM implementation.</param>
        /// <param name="numParity">This parameter sets the maximum number of parity symbol segments (packets) the sender is willing to calculate per FEC coding block.</param>
        /// <param name="fecId">Sets the NormFecType.</param>
        /// <exception cref="IOException">Thrown when NormStartSender() returns false, indicating the failure to start sender.</exception>
        public void StartSender(int sessionId, long bufferSpace, int segmentSize, short blockSize, short numParity, NormFecType fecId)
        {
            if (!NormStartSender(_handle, sessionId, bufferSpace, segmentSize, blockSize, numParity, fecId))
            {
                throw new IOException("Failed to start sender");
            }
        }

        /// <summary>
        /// The application's participation as a sender begins when this function is called.
        /// </summary>
        /// <param name="sessionId">Application-defined value used as the instance_id field of NORM sender messages for the application's participation within a session.</param>
        /// <param name="bufferSpace">This specifies the maximum memory space (in bytes) the NORM protocol engine is allowed to use to buffer any sender calculated FEC segments and repair state for the session.</param>
        /// <param name="segmentSize">This parameter sets the maximum payload size (in bytes) of NORM sender messages (not including any NORM message header fields).</param>
        /// <param name="blockSize">This parameter sets the number of source symbol segments (packets) per coding block, for the systematic Reed-Solomon FEC code used in the current NORM implementation.</param>
        /// <param name="numParity">This parameter sets the maximum number of parity symbol segments (packets) the sender is willing to calculate per FEC coding block.</param>
        /// <remarks>Uses NormFecType.RS for fecId.</remarks>
        /// <exception cref="IOException">Thrown when NormStartSender() returns false, indicating the failure to start sender.</exception>
        public void StartSender(int sessionId, long bufferSpace, int segmentSize, short blockSize, short numParity)
        {
            StartSender(sessionId, bufferSpace, segmentSize, blockSize, numParity, NormFecType.RS);
        }

        /// <summary>
        /// The application's participation as a sender begins when this function is called.
        /// </summary>
        /// <param name="bufferSpace">This specifies the maximum memory space (in bytes) the NORM protocol engine is allowed to use to buffer any sender calculated FEC segments and repair state for the session.</param>
        /// <param name="segmentSize">This parameter sets the maximum payload size (in bytes) of NORM sender messages (not including any NORM message header fields).</param>
        /// <param name="blockSize">This parameter sets the number of source symbol segments (packets) per coding block, for the systematic Reed-Solomon FEC code used in the current NORM implementation.</param>
        /// <param name="numParity">This parameter sets the maximum number of parity symbol segments (packets) the sender is willing to calculate per FEC coding block.</param>
        /// <param name="fecId">Sets the NormFecType.</param>
        /// <remarks>Generates a random sessionId.</remarks>
        /// <exception cref="IOException">Thrown when NormStartSender() returns false, indicating the failure to start sender.</exception>
        public void StartSender(long bufferSpace, int segmentSize, short blockSize, short numParity, NormFecType fecId)
        {
            var sessionId = NormGetRandomSessionId();
            StartSender(sessionId, bufferSpace, segmentSize, blockSize, numParity, fecId);
        }

        /// <summary>
        /// The application's participation as a sender begins when this function is called.
        /// </summary>
        /// <param name="bufferSpace">This specifies the maximum memory space (in bytes) the NORM protocol engine is allowed to use to buffer any sender calculated FEC segments and repair state for the session.</param>
        /// <param name="segmentSize">This parameter sets the maximum payload size (in bytes) of NORM sender messages (not including any NORM message header fields).</param>
        /// <param name="blockSize">This parameter sets the number of source symbol segments (packets) per coding block, for the systematic Reed-Solomon FEC code used in the current NORM implementation.</param>
        /// <param name="numParity">This parameter sets the maximum number of parity symbol segments (packets) the sender is willing to calculate per FEC coding block.</param>
        /// <remarks>Generates a random sessionId and uses NormFecType.RS for fecId.</remarks>
        /// <exception cref="IOException">Thrown when NormStartSender() returns false, indicating the failure to start sender.</exception>
        public void StartSender(long bufferSpace, int segmentSize, short blockSize, short numParity)
        {
            StartSender(bufferSpace, segmentSize, blockSize, numParity, NormFecType.RS);
        }

        /// <summary>
        /// This function terminates the application's participation in a NormSession as a sender. By default, the sender will
        /// immediately exit the session without notifying the receiver set of its intention.
        /// </summary>
        public void StopSender()
        {
            NormStopSender(_handle);
        }

        /// <summary>
        /// This function enqueues a file for transmission.
        /// </summary>
        /// <remarks>
        /// This is an overload which will call FileEnqueue() with info set to encoding of the filename, infoOffset set to 0, and infoLength set to info.Length.
        /// </reamarks>
        /// <param name="filename">The fileName parameter specifies the path to the file to be transmitted. The NORM protocol engine
        /// read and writes directly from/to file system storage for file transport, potentially providing for a very large virtual
        /// "repair window" as needed for some applications. While relative paths with respect to the "current working directory"
        /// may be used, it is recommended that full paths be used when possible.</param>
        /// <returns>A NormFile is returned which the application may use in other NORM API calls as needed.</returns>
        /// <exception cref="IOException">Thrown when NormFileEnqueue() returns NORM_OBJECT_INVALID, indicating the failure to enqueue file.</exception>
        public NormFile FileEnqueue(string filename)
        {
            var info = Encoding.ASCII.GetBytes(filename);
            return FileEnqueue(filename, info, 0, info.Length);
        }

        /// <summary>
        /// This function enqueues a file for transmission.
        /// </summary>
        /// <param name="filename">The fileName parameter specifies the path to the file to be transmitted. The NORM protocol engine
        /// read and writes directly from/to file system storage for file transport, potentially providing for a very large virtual
        /// "repair window" as needed for some applications. While relative paths with respect to the "current working directory"
        /// may be used, it is recommended that full paths be used when possible.</param>
        /// <param name="info">The optional info and infoLength parameters are used to associate NORM_INFO content with the sent transport object.</param>
        /// <param name="infoOffset">Indicates the start of the message. Anything before it will not be sent. 
        /// Note: to send full message infoOffset should be set to 0.</param>
        /// <param name="infoLength">The optional info and infoLength parameters are used to associate NORM_INFO content with the sent transport object.</param>
        /// <returns>A NormFile is returned which the application may use in other NORM API calls as needed.</returns>
        /// <exception cref="IOException">Thrown when NormFileEnqueue() returns NORM_OBJECT_INVALID, indicating the failure to enqueue file.</exception>
        /// <exception cref="ArgumentOutOfRangeException">Thrown when the info offset or info length are outside of the info buffer.</exception>
        public NormFile FileEnqueue(string filename, byte[]? info, int infoOffset, int infoLength)
        {
            if (infoOffset < 0 || infoOffset >= info?.Length)
            {
                throw new ArgumentOutOfRangeException(nameof(infoOffset), "The info offset is out of range");
            }
            if (info != null && infoLength < 1 || infoOffset + infoLength > info?.Length)
            {
                throw new ArgumentOutOfRangeException(nameof(infoLength), "The info length is out of range");
            }

            long objectHandle;
            var infoHandle = GCHandle.Alloc(info, GCHandleType.Pinned);

            try
            {
                var infoPtr = infoHandle.AddrOfPinnedObject() + infoOffset;
                objectHandle = NormFileEnqueue(_handle, filename, infoPtr, infoLength);
                if (objectHandle == NormObject.NORM_OBJECT_INVALID)
                {
                    throw new IOException("Failed to enqueue file");
                }
            } 
            finally
            {
                infoHandle.Free();
            }

            return new NormFile(objectHandle);
        }

        /// <summary>
        /// This function enqueues a segment of application memory space for transmission.
        /// </summary>
        /// <remarks>
        /// This is an overload which will call DataEnqueue() with info set to null, infoOffset set to 0, and infoLength set to 0.
        /// </remarks>
        /// <param name="dataBuffer">The dataBuffer is a byte array containing the message to be transmitted.</param>
        /// <param name="dataOffset">Indicates the start of the message. Anything before it will not be sent. 
        /// Note: to send full message dataOffset should be set to 0.</param>
        /// <param name="dataLength">Size of the message.</param>
        /// <returns>A NormData is returned which the application may use in other NORM API calls as needed.</returns>
        /// <exception cref="IOException">Thrown when NormDataEnqueue() returns NORM_OBJECT_INVALID, indicating the failure to enqueue data.</exception>
        /// <exception cref="ArgumentOutOfRangeException">Thrown when the data offset or data length are outside of the data buffer.</exception>
        public NormData DataEnqueue(SafeBuffer dataBuffer, int dataOffset, int dataLength)
        {
            return DataEnqueue(dataBuffer, dataOffset, dataLength, null, 0, 0);
        }

        /// <summary>
        /// This function enqueues a segment of application memory space for transmission.
        /// </summary>
        /// <param name="dataBuffer">The dataBuffer is a byte array containing the message to be transmitted.</param>
        /// <param name="dataOffset">Indicates the start of the message. Anything before it will not be sent. 
        /// Note: to send full message dataOffset should be set to 0.</param>
        /// <param name="dataLength">Size of the message.</param>
        /// <param name="info">The optional info and infoLength parameters are used to associate NORM_INFO content with the sent transport object.</param>
        /// <param name="infoOffset">Indicates the start of the message.</param>
        /// <param name="infoLength">The optional info and infoLength parameters are used to associate NORM_INFO content with the sent transport object.</param>
        /// <returns>A NormData is returned which the application may use in other NORM API calls as needed.</returns>
        /// <exception cref="IOException">Thrown when NormDataEnqueue() returns NORM_OBJECT_INVALID, indicating the failure to enqueue data.</exception>
        /// <exception cref="ArgumentOutOfRangeException">Thrown when the data offset, data length, info offset or info length are outside of the associated buffer.</exception>
        public NormData DataEnqueue(SafeBuffer dataBuffer, int dataOffset, int dataLength, byte[]? info, int infoOffset, int infoLength)
        {
            if (dataOffset < 0 || Convert.ToUInt64(dataOffset) >= dataBuffer.ByteLength)
            {
                throw new ArgumentOutOfRangeException(nameof(dataOffset), "The data offset is out of range");
            }
            if (dataLength < 1 || Convert.ToUInt64(dataOffset + dataLength) > dataBuffer.ByteLength)
            {
                throw new ArgumentOutOfRangeException(nameof(dataLength), "The data length is out of range");
            }
            
            unsafe 
            {
                byte* dataPtr = null;
                dataBuffer.AcquirePointer(ref dataPtr);
                return DataEnqueue((nint)dataPtr, dataOffset, dataLength, info, infoOffset, infoLength);
            }
        }

        /// <summary>
        /// This function enqueues a segment of application memory space for transmission.
        /// </summary>
        /// <remarks>
        /// This is an overload which will call DataEnqueue() with info set to null, infoOffset set to 0, and infoLength set to 0.
        /// </remarks>
        /// <param name="dataPtr">The dataPtr is a pointer to the message to be transmitted.</param>
        /// <param name="dataOffset">Indicates the start of the message. Anything before it will not be sent. 
        /// Note: to send full message dataOffset should be set to 0.</param>
        /// <param name="dataLength">Size of the message.</param>
        /// <returns>A NormData is returned which the application may use in other NORM API calls as needed.</returns>
        /// <exception cref="IOException">Thrown when NormDataEnqueue() returns NORM_OBJECT_INVALID, indicating the failure to enqueue data.</exception>
        /// <exception cref="ArgumentOutOfRangeException">Thrown when the data offset or data length are outside of the data buffer.</exception>
        public NormData DataEnqueue(nint dataPtr, int dataOffset, int dataLength)
        {
            return DataEnqueue(dataPtr, dataOffset, dataLength, null, 0, 0);
        }

        /// <summary>
        /// This function enqueues a segment of application memory space for transmission.
        /// </summary>
        /// <param name="dataPtr">The dataPtr is a pointer to the message to be transmitted.</param>
        /// <param name="dataOffset">Indicates the start of the message. Anything before it will not be sent. 
        /// Note: to send full message dataOffset should be set to 0.</param>
        /// <param name="dataLength">Size of the message.</param>
        /// <param name="info">The optional info and infoLength parameters are used to associate NORM_INFO content with the sent transport object.</param>
        /// <param name="infoOffset">Indicates the start of the message.</param>
        /// <param name="infoLength">The optional info and infoLength parameters are used to associate NORM_INFO content with the sent transport object.</param>
        /// <returns>A NormData is returned which the application may use in other NORM API calls as needed.</returns>
        /// <exception cref="IOException">Thrown when NormDataEnqueue() returns NORM_OBJECT_INVALID, indicating the failure to enqueue data.</exception>
        /// <exception cref="ArgumentOutOfRangeException">Thrown when the data offset, data length, info offset or info length are outside of the associated buffer.</exception>
        public NormData DataEnqueue(nint dataPtr, int dataOffset, int dataLength, byte[]? info, int infoOffset, int infoLength)
        {
            if (infoOffset < 0 || infoOffset >= info?.Length)
            {
                throw new ArgumentOutOfRangeException(nameof(infoOffset), "The info offset is out of range");
            }
            if (info != null && infoLength < 1 || infoOffset + infoLength > info?.Length)
            {
                throw new ArgumentOutOfRangeException(nameof(infoLength), "The info length is out of range");
            }

            long objectHandle;
            var infoHandle = GCHandle.Alloc(info, GCHandleType.Pinned);

            try
            {
                dataPtr += dataOffset;
                var infoPtr = infoHandle.AddrOfPinnedObject() + infoOffset;
                objectHandle = NormDataEnqueue(_handle, dataPtr, dataLength, infoPtr, infoLength);
                if (objectHandle == NormObject.NORM_OBJECT_INVALID)
                {
                    throw new IOException("Failed to enqueue data");
                }
            } 
            finally
            {
                infoHandle.Free();
            }

            return new NormData(objectHandle);
        }

        /// <summary>
        /// This function opens a NORM_OBJECT_STREAM sender object and enqueues it for transmission.
        /// </summary>
        /// <remarks>
        /// No data is sent until subsequent calls to StreamWrite() are made unless
        /// NORM_INFO content is specified for the stream with the info and infoLength parameters. Example usage of
        /// NORM_INFO content for NORM_OBJECT_STREAM might include application-defined data typing or other information
        /// which will enable NORM receiver applications to properly interpret the received stream as it is being received.
        /// This is an overload which will call StreamOpen() with info set to null, infoOffset set to 0, and infoLength set to 0.
        /// </remarks>
        /// <param name="bufferSize">
        /// The bufferSize parameter controls the size of the stream's "repair window"
        /// which limits how far back the sender will "rewind" to satisfy receiver repair requests.
        /// </param>
        /// <returns> A NormStream is returned which the application may use in other NORM API calls as needed.</returns>
        /// <exception cref="IOException">Thrown when NormStreamOpen() returns NORM_OBJECT_INVALID, indicating the failure to open stream.</exception>
        public NormStream StreamOpen(long bufferSize)
        {
            return StreamOpen(bufferSize, null, 0, 0);
        }

        /// <summary>
        /// This function opens a NORM_OBJECT_STREAM sender object and enqueues it for transmission.
        /// </summary>
        /// <remarks>
        /// No data is sent until subsequent calls to NormStreamWrite() are made unless
        /// NORM_INFO content is specified for the stream with the info and infoLength parameters. Example usage of
        /// NORM_INFO content for NORM_OBJECT_STREAM might include application-defined data typing or other information
        /// which will enable NORM receiver applications to properly interpret the received stream as it is being received.
        /// </remarks>
        /// <param name="bufferSize"> The bufferSize parameter controls the size of the stream's "repair window"
        /// which limits how far back the sender will "rewind" to satisfy receiver repair requests.</param>
        /// <param name="info">Alloted memory space for transmitted information.</param>
        /// <param name="infoOffset">Indicates the start of the message. Anything before it will not be sent. 
        /// Note: to send full message infoOffset should be set to 0.</param>
        /// <param name="infoLength">Size of the message.</param>
        /// <returns>A NormStream is returned which the application may use in other NORM API calls as needed.</returns>
        /// <exception cref="IOException">Thrown when NormStreamOpen() returns NORM_OBJECT_INVALID, indicating the failure to open stream.</exception>
        /// <exception cref="ArgumentOutOfRangeException">Thrown when the info offset or info length are outside of the info buffer.</exception>
        public NormStream StreamOpen(long bufferSize, byte[]? info, int infoOffset, int infoLength)
        {
            if (infoOffset < 0 || infoOffset >= info?.Length)
            {
                throw new ArgumentOutOfRangeException(nameof(infoOffset), "The info offset is out of range");
            }
            if (info != null && infoLength < 1 || infoOffset + infoLength > info?.Length)
            {
                throw new ArgumentOutOfRangeException(nameof(infoLength), "The info length is out of range");
            }

            long objectHandle;
            var infoHandle = GCHandle.Alloc(info, GCHandleType.Pinned);

            try
            {
                var infoPtr = infoHandle.AddrOfPinnedObject() + infoOffset;
                objectHandle = NormStreamOpen(_handle, bufferSize, infoPtr, infoLength);
                if (objectHandle == NormObject.NORM_OBJECT_INVALID)
                {
                    throw new IOException("Failed to open stream");
                }
            } 
            finally
            {
                infoHandle.Free();
            }

            return new NormStream(objectHandle);
        }

        /// <summary>
        /// This function initiates the application's participation as a receiver within the NormSession.
        /// </summary>
        /// <param name="bufferSpace">The bufferSpace parameter is used to set a limit on the amount of bufferSpace allocated
        /// by the receiver per active NormSender within the session.</param>
        /// <exception cref="IOException">Thrown when NormStartReceiver() returns false, indicating the failure to start receiver.</exception>
        public void StartReceiver(long bufferSpace)
        {
            if(!NormStartReceiver(_handle, bufferSpace))
            {
                throw new IOException("Failed to start receiver");
            }
        }

        /// <summary>
        /// This function ends the application's participation as a receiver in the NormSession.
        /// </summary>
        public void StopReceiver()
        {
            NormStopReceiver(_handle);
        }

        /// <summary>
        /// This function sets the quantity of proactive "auto parity" NORM_DATA messages sent at the end of each FEC coding
        /// block. By default (i.e., autoParity = 0), FEC content is sent only in response to repair requests (NACKs) from receivers.
        /// </summary>
        /// <param name="autoParity">Setting a non-zero value for autoParity, the sender can automatically accompany each coding
        /// block of transport object source data segments ((NORM_DATA messages) with the set number of FEC segments.</param>
        public void SetAutoParity(short autoParity)
        {
            NormSetAutoParity(_handle, autoParity);
        }

        /// <summary>
        /// This function sets the sender's maximum advertised GRTT value.
        /// </summary>
        /// <param name="grttMax">The grttMax parameter, in units of seconds, limits the GRTT used by the group for scaling protocol timers, regardless
        /// of larger measured round trip times. The default maximum for the NRL NORM library is 10 seconds.</param>
        public void SetGrttMax(double grttMax)
        {
            NormSetGrttMax(_handle, grttMax);
        }

        /// <summary>
        /// This function sets the sender's mode of probing for round trip timing measurement responses from the receiver.
        /// </summary>
        /// <param name="probingMode">Possible values for the probingMode parameter include NORM_PROBE_NONE, NORM_PROBE_PASSIVE, and NORM_PROBE_ACTIVE.</param>
        public void SetGrttProbingMode(NormProbingMode probingMode)
        {
            NormSetGrttProbingMode(_handle, probingMode);
        }

        /// <summary>
        /// This function controls the sender GRTT measurement and estimation process.
        /// The NORM sender multiplexes periodic transmission of NORM_CMD(CC) messages with its ongoing data transmission
        /// or when data transmission is idle.When NORM congestion control operation is enabled, these probes are sent
        /// once per RTT of the current limiting receiver(with respect to congestion control rate). In this case the intervalMin
        /// and intervalMax parameters (in units of seconds) control the rate at which the sender's estimate of GRTT is updated.
        /// </summary>
        /// <param name="intervalMin">At session start, the estimate is updated at intervalMin and the update interval time is doubled until intervalMax is reached.</param>
        /// <param name="intervalMax">At session start, the estimate is updated at intervalMin and the update interval time is doubled until intervalMax is reached.</param>
        public void SetGrttProbingInterval(double intervalMin, double intervalMax)
        {
            NormSetGrttProbingInterval(_handle, intervalMin, intervalMax);
        }

        /// <summary>
        /// This function sets the sender's "backoff factor".
        /// </summary>
        /// <param name="backoffFactor">The backoffFactor (in units of seconds) is used to scale various timeouts related to the NACK repair process.</param>
        public void SetBackoffFactor(double backoffFactor)
        {
            NormSetBackoffFactor(_handle, backoffFactor);
        }

        /// <summary>
        /// This function sets the sender's estimate of receiver group size.
        /// </summary>
        /// <param name="groupSize">The sender advertises its groupSize setting to the receiver group in NORM protocol message
        /// headers that, in turn, use this information to shape the distribution curve of their random timeouts for the timer-based,
        /// probabilistic feedback suppression technique used in the NORM protocol.</param>
        public void SetGroupSize(long groupSize)
        {
            NormSetGroupSize(_handle, groupSize);
        }

        /// <summary>
        /// This routine sets the "robustness factor" used for various NORM sender functions. These functions include the
        /// number of repetitions of "robustly-transmitted" NORM sender commands such as NORM_CMD(FLUSH) or similar
        /// application-defined commands, and the number of attempts that are made to collect positive acknowledgement
        /// from receivers.These commands are distinct from the NORM reliable data transmission process, but play a role
        /// in overall NORM protocol operation.
        /// </summary>
        /// <param name="txRobustFactor">The default txRobustFactor value is 20. This relatively large value makes
        /// the NORM  sender end-of-transmission flushing  and positive  acknowledgement collection  functions somewhat immune from packet loss.
        /// Setting txRobustFactor to a value of -1 makes the redundant transmission of these commands continue indefinitely until completion.</param>
        public void SetTxRobustFactor(int txRobustFactor)
        {
            NormSetTxRobustFactor(_handle, txRobustFactor);
        }

        /// <summary>
        /// This function allows the application to resend (or reset transmission of) a NORM_OBJECT_FILE or NORM_OBJECT_DATA
        /// transmit object that was previously enqueued for the session.
        /// </summary>
        /// <param name="normObject">The normObject parameter must be a valid transmit NormObject that has not yet been "purged" from the sender's transmit queue.</param>
        /// <exception cref="IOException">Thrown when NormRequeueObject() returns false, indicating the failure to requeue object.</exception>
        public void RequeueObject(NormObject normObject)
        {
            if(!NormRequeueObject(_handle, normObject.Handle))
            {
                throw new IOException("Failed to requeue object");
            }
        }

        /// <summary>
        /// This function specifies a "watermark" transmission point at which NORM sender protocol operation should perform
        /// a flushing process and/or positive acknowledgment collection for a given sessionHandle.
        /// </summary>
        /// <remarks>This is an overload which will call the SetWatermark() override with overrideFlush set as false.</remarks>
        /// <param name="normObject">The normObject parameter must be a valid transmit NormObject that has not yet been "purged" from the sender's transmit queue.</param>
        /// <exception cref="IOException">Thrown when NormSetWatermark() returns false, indicating the failure to set watermark.</exception>
        public void SetWatermark(NormObject normObject)
        {
            SetWatermark(normObject, false);
        }

        /// <summary>
        /// This function specifies a "watermark" transmission point at which NORM sender protocol operation should perform
        /// a flushing process and/or positive acknowledgment collection for a given sessionHandle.
        /// </summary>
        /// <param name="normObject">The normObject parameter must be a valid transmit NormObject that has not yet been "purged" from the sender's transmit queue.</param>
        /// <param name="overrideFlush">The optional overrideFlush parameter, when set to true, causes the watermark acknowledgment process that is
        /// established with this function call to potentially fully supersede the usual NORM end-of-transmission flushing
        /// process that  occurs. If overrideFlush  is  set and  the  "watermark"  transmission point  corresponds to  the last
        /// transmission that will result from data enqueued by the sending application, then the watermark flush completion
        /// will terminate the usual flushing process</param>
        /// <exception cref="IOException">Thrown when NormSetWatermark() returns false, indicating the failure to set watermark.</exception>
        public void SetWatermark(NormObject normObject, bool overrideFlush)
        {
            if(!NormSetWatermark(_handle, normObject.Handle, overrideFlush))
            {
                throw new IOException("Failed to set watermark");
            }
        }

        /// <summary>
        /// This function cancels any "watermark" acknowledgement request that was previously set via the SetWatermark() function.
        /// </summary>
        public void CancelWatermark()
        {
            NormCancelWatermark(_handle);
        }

        /// <exception cref="IOException">Thrown when NormResetWatermark() returns false, indicating the failure to reset watermark.</exception>
        public void ResetWatermark()
        {
            if(!NormResetWatermark(_handle))
            {
                throw new IOException("Failed to reset watermark");
            }
        }

        /// <summary>
        /// When this function is called, the specified nodeId is added to the list of NormNodeId values (i.e., the "acking node"
        /// list) used when NORM sender operation performs positive acknowledgement (ACK) collection. 
        /// </summary>
        /// <param name="nodeId">Identifies the application's presence in the NormSession.</param>
        /// <exception cref="IOException">Thrown when NormAddAckingNode() returns false, indicating the failure to add acking node.</exception>
        public void AddAckingNode(long nodeId)
        {
            if(!NormAddAckingNode(_handle, nodeId))
            {
                throw new IOException("Failed to add acking node");
            }
        }

        /// <summary>
        /// This function deletes the specified nodeId from the list of NormNodeId values used when NORM sender operation
        /// performs positive acknowledgement (ACK) collection.
        /// </summary>
        /// <param name="nodeId">Identifies the application's presence in the NormSession.</param>
        public void RemoveAckingNode(long nodeId)
        {
            NormRemoveAckingNode(_handle, nodeId);
        }

        /// <summary>
        /// This function queries the status of the watermark flushing process and/or positive acknowledgment collection
        /// initiated by a prior call to SetWatermark().
        /// </summary>
        /// <param name="nodeId">Identifies the application's presence in the NormSession.</param>
        /// <returns>
        /// Possible return values include:
        /// NORM_ACK_INVALID - The given sessionHandle is invalid or the given nodeId is not in the sender's acking list.
        /// NORM_ACK_FAILURE - The positive acknowledgement collection process did not receive acknowledgment from every listed receiver (nodeId = NORM_NODE_ANY) or the identified nodeId did not respond.
        /// NORM_ACK_PENDING - The flushing process at large has not yet completed (nodeId = NORM_NODE_ANY) or the given individual nodeId is still being queried for response.
        /// NORM_ACK_SUCCESS - All receivers (nodeId = NORM_NODE_ANY) responded with positive acknowledgement or the given specific nodeId did acknowledge.
        /// </returns>
        public NormAckingStatus GetAckingStatus(long nodeId)
        {
            return NormGetAckingStatus(_handle, nodeId);
        }

        /// <summary>
        /// This function enqueues a NORM application-defined command for transmission.
        /// </summary>
        /// <param name="cmdBuffer">The cmdBuffer parameter points to a buffer containing the application-defined command content that will be contained in the NORM_CMD(APPLICA-TION) message payload.</param>
        /// <param name="cmdOffset"></param>
        /// <param name="cmdLength">The cmdLength indicates the length of this content (in bytes) and MUST be less than or equal to the segmentLength value for the given session.</param>
        /// <param name="robust">The command is NOT delivered reliably, 
        /// but can be optionally transmitted with repetition (once per GRTT) according to the NORM transmit robust factor
        /// value for the given session if the robust parameter is set to true.</param>
        /// <exception cref="IOException">Thrown when NormSendCommand() returns false, indicating the failure to send command.</exception>
        /// <exception cref="ArgumentOutOfRangeException">Thrown when the offset or length are outside of the buffer.</exception>
        public void SendCommand(byte[] cmdBuffer, int cmdOffset, int cmdLength, bool robust)
        {
            if (cmdOffset < 0 || cmdOffset >= cmdBuffer.Length)
            {
                throw new ArgumentOutOfRangeException(nameof(cmdOffset), "The offset is out of range");
            }
            if (cmdLength < 1 || cmdOffset + cmdLength > cmdBuffer.Length)
            {
                throw new ArgumentOutOfRangeException(nameof(cmdLength), "The command length is out of range");
            }

            var commandHandle = GCHandle.Alloc(cmdBuffer, GCHandleType.Pinned);

            try
            {
                var cmdPtr = commandHandle.AddrOfPinnedObject() + cmdOffset;
                if (!NormSendCommand(_handle, cmdPtr, cmdLength, robust))
                {
                    throw new IOException("Failed to send command");
                }
            }
            finally
            {
                commandHandle.Free();
            }
        }

        /// <summary>
        /// This function terminates any pending NORM_CMD(APPLICATION) transmission that was previously initiated with the SendCommand() call. 
        /// </summary>
        public void CancelCommand()
        {
            NormCancelCommand(_handle);
        }

        /// <summary>
        /// This function sets a limit on the number of outstanding (pending) NormObjects for which a receiver will keep state on a per-sender basis.
        /// </summary>
        /// <param name="countMax">The value countMax sets a limit on the maximum consecutive range of objects that can be pending.</param>
        public void SetRxCacheLimit(int countMax)
        {
            NormSetRxCacheLimit(_handle, countMax);
        }

        /// <summary>
        /// This function allows the application to set an alternative, non-default buffer size for the UDP socket for packet reception. 
        /// </summary>
        /// <param name="bufferSize">The bufferSize parameter specifies the socket buffer size in bytes.</param>
        /// <exception cref="IOException">Thrown when NormSetRxSocketBuffer() returns false, indicating the failure to set rx socket buffer.</exception>
        public void SetRxSocketBuffer(long bufferSize)
        {
            if(!NormSetRxSocketBuffer(_handle, bufferSize)) 
            {
                throw new IOException("Failed to set rx socket buffer");
            }
        }

        /// <summary>
        /// This function provides the option to configure a NORM receiver application as a "silent receiver". This mode of
        /// receiver operation dictates that the host does not generate any protocol messages while operating as a receiver.
        /// </summary>
        /// <param name="silent">Setting the silent parameter to true enables silent receiver operation while
        /// setting it to false results in normal protocol operation where feedback is provided as needed for reliability and
        /// protocol operation.</param>
        /// <param name="maxDelay">When the maxDelay parameter is set to a non-negative value, the value determines the maximum number
        /// of FEC coding blocks (according to a NORM sender's current transmit position) the receiver will cache an incompletely-received 
        /// FEC block before giving the application the (incomplete) set of received source segments.</param>
        public void SetSilentReceiver(bool silent, int maxDelay)
        {
            NormSetSilentReceiver(_handle, silent, maxDelay);
        }

        /// <summary>
        /// This function controls the default behavior determining the destination of receiver feedback messages generated
        /// while participating in the session.
        /// </summary>
        /// <param name="enable">If the enable parameter is true, "unicast NACKing" is enabled for new remote
        /// senders while it is disabled for state equal to false.</param>
        public void SetDefaultUnicastNack(bool enable)
        {
            NormSetDefaultUnicastNack(_handle, enable);
        }

        /// <summary>
        /// This function sets the default "synchronization policy" used when beginning (or restarting) reception of objects
        /// from a remote sender (i.e., "syncing" to the sender).
        /// </summary>
        /// <param name="syncPolicy">The "synchronization policy" is the behavior observed by the receiver with regards to what objects it attempts to reliably receive
        /// (via transmissions of Negative Acknowledgements to the sender(s) or group as needed).</param>
        public void SetDefaultSyncPolicy(NormSyncPolicy syncPolicy)
        {
            NormSetDefaultSyncPolicy(_handle, syncPolicy);
        }

        /// <summary>
        /// This function sets the default "nacking mode" used when receiving objects.
        /// This allows the receiver application some control of its degree of participation in the repair process. By limiting receivers
        /// to only request repair of objects in which they are really interested in receiving, some overall savings in unnecessary
        /// network loading might be realized for some applications and users.
        /// </summary>
        /// <param name="nackingMode">Specifies the nacking mode.</param>
        public void SetDefaultNackingMode(NormNackingMode nackingMode)
        {
            NormSetDefaultNackingMode(_handle, nackingMode);
        }

        /// <summary>
        /// This function allows the receiver application to customize at what points the receiver
        /// initiates the NORM NACK repair process during protocol operation.
        /// </summary>
        /// <param name="repairBoundary">Specifies the repair boundary.</param>
        public void SetDefaultRepairBoundary(NormRepairBoundary repairBoundary)
        {
            NormSetDefaultRepairBoundary(_handle, repairBoundary);
        }

        /// <summary>
        /// This routine controls how persistently NORM receivers will maintain state for sender(s) and continue to request
        /// repairs from the sender(s) even when packet reception has ceased.
        /// </summary>
        /// <param name="rxRobustFactor">The rxRobustFactor value determines how
        /// many times a NORM receiver will self-initiate NACKing (repair requests) upon cessation of packet reception from
        /// a sender. The default value is 20. Setting rxRobustFactor to -1 will make the NORM receiver infinitely persistent
        /// (i.e., it will continue to NACK indefinitely as long as it is missing data content).</param>
        public void SetDefaultRxRobustFactor(int rxRobustFactor)
        {
            NormSetDefaultRxRobustFactor(_handle, rxRobustFactor);
        }
    }
}