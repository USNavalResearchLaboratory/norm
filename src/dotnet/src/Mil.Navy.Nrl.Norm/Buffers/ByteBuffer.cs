using System.Runtime.InteropServices;

namespace Mil.Navy.Nrl.Norm.Buffers 
{
    /// <summary>
    /// A byte buffer
    /// </summary>
    public abstract class ByteBuffer : SafeBuffer
    {
        /// <summary>
        /// Creates a new buffer
        /// </summary>
        protected ByteBuffer() : base(true)
        {       
        }

        /// <summary>
        /// Allocates a new direct byte buffer
        /// </summary>
        /// <param name="capacity">The new buffer's capacity, in bytes</param>
        /// <returns>The new byte buffer</returns>
        public static ByteBuffer AllocateDirect(int capacity) {
            return new DirectByteBuffer(capacity);
        }
    }
}