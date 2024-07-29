using System.Runtime.InteropServices;

namespace Mil.Navy.Nrl.Norm.Buffers 
{
    /// <summary>
    /// A direct byte buffer
    /// </summary>
    internal sealed class DirectByteBuffer : ByteBuffer
    {
        /// <summary>
        /// Creates a new direct byte buffer with given capacity
        /// </summary>
        /// <param name="capacity">The new buffer's capacity, in bytes</param>
        internal DirectByteBuffer(int capacity)
        {
            SetHandle(Marshal.AllocHGlobal(capacity));
            Initialize(Convert.ToUInt64(capacity));
        }

        /// <summary>
        /// Executes the code required to free the handle
        /// </summary>
        /// <returns>true if the handle is released successfully; otherwise, in the event of a catastrophic failure, false</returns>
        protected override bool ReleaseHandle()
        {
            Marshal.FreeHGlobal(handle);
            return true;
        }
    }
}