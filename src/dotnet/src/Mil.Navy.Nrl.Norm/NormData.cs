using System.Runtime.InteropServices;

namespace Mil.Navy.Nrl.Norm
{
    /// <summary>
    /// A transport object of type NORM_OBJECT_FILE.
    /// </summary>
    /// <remarks>
    /// The data storage area for the specified transport object.
    /// </remarks>
    public class NormData : NormObject
    {
        /// <summary>
        /// The data storage area associated with a transport object of type NORM_OBJECT_DATA.
        /// </summary>
        public byte[] Data
        {
            get
            {
                var dataPointer = NormDataAccessData(_handle);
                var length = NormObjectGetSize(_handle);
                var data = new byte[length];
                for (var i = 0; i < length; i++)
                {
                    data[i] = Marshal.ReadByte(dataPointer, i);
                }
                return data;
            }
        }

        /// <summary>
        /// Constructor of NormData
        /// </summary>
        /// <param name="handle">Type is used to reference state kept for data transport objects being actively transmitted or received.</param>
        internal NormData(long handle) : base(handle)
        {
        }
    }
}
