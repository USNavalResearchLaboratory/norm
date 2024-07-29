using System.Runtime.InteropServices;

namespace Mil.Navy.Nrl.Norm
{
    /// <summary>
    /// The base transport object.
    /// </summary>
    public class NormObject
    {
        /// <summary>
        /// The special value NORM_OBJECT_INVALID corresponds to an invalid reference.
        /// </summary>
        public const int NORM_OBJECT_INVALID = 0;
        /// <summary>
        /// Used to reference state kept for data transport objects being actively transmitted or received.
        /// </summary>
        protected long _handle;

        /// <summary>
        /// Internal constructor for NormObject
        /// </summary>
        /// <param name="handle">Identifies an instance of NormObject</param>
        internal NormObject(long handle)
        {
            _handle = handle;
        }

        /// <summary>
        /// Used to reference state kept for data transport objects being actively transmitted or received.
        /// </summary>
        public long Handle => _handle;

        /// <summary>
        /// Data associated with NormObject.
        /// </summary>
        public byte[]? Info
        {
            get
            {
                if (!NormObjectHasInfo(_handle))
                {
                    return null;
                } 

                var length = NormObjectGetInfoLength(_handle);
                var buffer = new byte[length];
                var bufferHandle = GCHandle.Alloc(buffer, GCHandleType.Pinned);

                try
                {
                    var bufferPtr = bufferHandle.AddrOfPinnedObject();
                    NormObjectGetInfo(_handle, bufferPtr, length);
                }
                finally
                {
                    bufferHandle.Free();
                }

                return buffer;
            }
        }

        /// <summary>
        /// The type of Norm object.
        /// Valid types include:
        /// NORM_OBJECT_FILE
        /// NORM_OBJECT_DATA
        /// NORM_OBJECT_STREAM
        /// </summary>
        public NormObjectType Type 
        {
            get 
            {
                return NormObjectGetType(_handle);
            }
        }

        /// <summary>
        /// The size (in bytes) of the transport object.
        /// </summary>
        public long Size
        {
            get
            {
                return NormObjectGetSize(_handle);
            }
        }

        /// <summary>
        /// The NormNodeHandle corresponding to the remote sender of the transport object.
        /// </summary>
        /// <exception cref="IOException">Thrown when NormObjectGetSender() returns NORM_NODE_INVALID, indicating locally originated sender object.</exception>
        public long Sender
        {
            get
            {
                var sender = NormObjectGetSender(_handle);
                if(sender == NormNode.NORM_NODE_INVALID)
                {
                    throw new IOException("Locally originated sender object");
                }
                return sender;
            }
        }

        /// <summary>
        /// This function sets the "nacking mode" used for receiving a specific transport object.
        /// </summary>
        /// <param name="nackingMode">Specifies the nacking mode.</param>
        public void SetNackingMode(NormNackingMode nackingMode)
        {
            NormObjectSetNackingMode(_handle, nackingMode);
        }

        /// <summary>
        ///  This function can be used to determine the progress of reception of the NORM transport object
        /// </summary>
        /// <returns>A number of object source data bytes pending reception (or transmission) is returned.</returns>
        public long GetBytesPending()
        {
            return NormObjectGetBytesPending(_handle);
        }

        /// <summary>
        /// This function immediately cancels the transmission of a local sender transport object or the reception of a specified
        /// object from a remote sender.
        /// </summary>
        public void Cancel()
        {
            NormObjectCancel(_handle);
        }

        /// <summary>
        /// This function "retains" the objectHandle and any state associated with it for further use by the application even
        /// when the NORM protocol engine may no longer require access to the associated transport object. 
        /// </summary>
        public void Retain()
        {
            NormObjectRetain(_handle);
        }

        /// <summary>
        /// This function complements the Retain() call by immediately freeing any resources associated with
        /// the given objectHandle, assuming the underlying NORM protocol engine no longer requires access to the corresponding
        /// transport object. Note the NORM protocol engine retains/releases state for associated objects for its own
        /// needs and thus it is very unsafe for an application to call Release() for an objectHandle for which
        /// it has not previously explicitly retained via Retain().
        /// </summary>
        public void Release()
        {
            NormObjectRelease(_handle);
        }

        /// <summary>
        /// Gets the hash code of the object.
        /// </summary>
        /// <returns>Returns the handle of the object.</returns>
        public override int GetHashCode()
        {
            return (int)_handle;
        }

        /// <summary>
        /// Decides wether specified object is equal another.
        /// </summary>
        /// <param name="obj">NormObject to compare to.</param>
        /// <returns>Returns true if two objects equal, false otherwise.</returns>
        public override bool Equals(object? obj)
        {
            if(obj is NormObject)
            {
                return _handle == ((NormObject)obj).Handle;
            }
            return false;
        }
    }
}
