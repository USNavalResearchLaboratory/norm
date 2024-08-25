using System.Net;
using System.Runtime.InteropServices;

namespace Mil.Navy.Nrl.Norm
{
    /// <summary>
    /// A participant in a NORM protocol session
    /// </summary>
    public class NormNode
    {
        /// <summary>
        /// When creating a session, allows the NORM implementation to attempt to pick an identifier based on the host computer's "default" IP address.
        /// </summary>
        public const long NORM_NODE_ANY = 0xffffffff;
        /// <summary>
        /// The special value NORM_NODE_NONE corresponds to an invalid (or null) node.
        /// </summary>
        public const int NORM_NODE_NONE = 0;
        /// <summary>
        /// The special value NORM_NODE_INVALID corresponds to an invalid reference.
        /// </summary>
        public const int NORM_NODE_INVALID = 0;
        /// <summary>
        /// The handle is associated to the NORM protocol engine instance.
        /// </summary>
        private long _handle;

        /// <summary>
        /// Parameterized contructor.
        /// </summary>
        /// <param name="handle">The handle is associated to the NORM protocol engine instance.</param>
        internal NormNode(long handle)
        {
            _handle = handle;
        }

        /// <summary>
        /// The NormNodeId identifier for the remote participant.
        /// </summary>
        public long Id => NormNodeGetId(_handle);

        /// <summary>
        /// The current network source address detected for packets received from remote NORM sender.
        /// </summary>
        public unsafe IPEndPoint Address
        {
            get
            {
                var bufferLength = 256;
                var buffer = stackalloc byte[bufferLength];
                var addrBuffer = (nint)buffer;

                if (!NormNodeGetAddress(_handle, addrBuffer, ref bufferLength, out int port))
                {
                    throw new IOException("Failed to get node address");
                }

                var addressBytes = new ReadOnlySpan<byte>(buffer, bufferLength);
                var ipAddress = new IPAddress(addressBytes);
                return new IPEndPoint(ipAddress, port);
            }
        }

        /// <summary>
        /// The advertised estimate of group round-trip timing (GRTT) for the remote sender.
        /// </summary>
        public double Grtt => NormNodeGetGrtt(_handle);

        /// <summary>
        /// This function retrieves the content of an application-defined command that was received from a remote sender.
        /// </summary>
        /// <exception cref="ArgumentOutOfRangeException">Thrown when the offset or length are outside of the buffer.</exception>
        public int GetCommand(byte[] buffer, int offset, int length)
        {
            if (offset < 0 || offset >= buffer.Length)
            {
                throw new ArgumentOutOfRangeException(nameof(offset), "The offset is out of range");
            }
            if (length < 1 || offset + length > buffer.Length)
            {
                throw new ArgumentOutOfRangeException(nameof(length), "The length is out of range");
            }

            var bufferHandle = GCHandle.Alloc(buffer, GCHandleType.Pinned);

            try
            {
                var bufferPtr = bufferHandle.AddrOfPinnedObject() + offset;
                if (!NormNodeGetCommand(_handle, bufferPtr, ref length))
                {
                    throw new IOException("Failed to get command");
                }
            } 
            finally
            {
                bufferHandle.Free();
            }
            return length;
        }

        /// <summary>
        /// This function controls the destination address of receiver feedback messages generated in response to a specific remote NORM sender.
        /// </summary>
        /// <param name="state">If state is true, "unicast NACKing" is enabled.</param>
        public void SetUnicastNack(bool state)
        {
            NormNodeSetUnicastNack(_handle, state);
        }

        /// <summary>
        /// This function sets the default "nacking mode" used for receiving new objects from a specific sender.
        /// </summary>
        /// <param name="nackingMode">Specifies the nacking mode.</param>
        public void SetNackingMode(NormNackingMode nackingMode)
        {
            NormNodeSetNackingMode(_handle, nackingMode);
        }

        /// <summary>
        /// This function allows the receiver application to customize at what points the receiver initiates the NORM NACK repair process during protocol operation.
        /// </summary>
        /// <param name="repairBoundary">Specifies the repair boundary.</param>
        public void SetRepairBoundary(NormRepairBoundary repairBoundary)
        {
            NormNodeSetRepairBoundary(_handle, repairBoundary);
        }

        /// <summary>
        /// This routine sets the robustFactor as described in NormSetDefaultRxRobustFactor() for an individual remote sender.
        /// </summary>
        /// <param name="robustFactor">The robustFactor value determines how
        /// many times a NORM receiver will self-initiate NACKing (repair requests) upon cessation of packet reception from
        /// a sender. The default value is 20. Setting rxRobustFactor to -1 will make the NORM receiver infinitely persistent
        /// (i.e., it will continue to NACK indefinitely as long as it is missing data content).</param>
        public void SetRxRobustFactor(int robustFactor)
        {
            NormNodeSetRxRobustFactor(_handle, robustFactor);
        }

        /// <summary>
        /// This function releases memory resources that were allocated for a remote sender. 
        /// </summary>
        public void FreeBuffers()
        {
            NormNodeFreeBuffers(_handle);
        }

        /// <summary>
        /// This function allows the application to retain state associated even when the underlying NORM protocol engine might normally free the associated state.
        /// </summary>
        public void Retain()
        {
            NormNodeRetain(_handle);
        }

        /// <summary>
        /// This function releases the Node so that the NORM protocol engine may free associated resources as needed.
        /// </summary>
        public void Release()
        {
            NormNodeRelease(_handle);
        }
    }
}
