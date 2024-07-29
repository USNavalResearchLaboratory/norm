using System.Runtime.InteropServices;
using System.Text;

namespace Mil.Navy.Nrl.Norm
{
    /// <summary>
    /// A transport object of type NORM_OBJECT_FILE.
    /// </summary>
    public class NormFile : NormObject
    {
        /// <summary>
        /// Maximum length of file names.
        /// </summary>
        public const int FILENAME_MAX = 260;

        /// <summary>
        /// Constructor of NormFile
        /// </summary>
        /// <param name="handle">This type is used to reference state kept for data transport objects being actively transmitted or received.</param>
        internal NormFile(long handle) : base(handle)
        {
        }

        /// <summary>
        /// The name of the file.
        /// </summary>
        /// <exception cref="IOException">Thrown when failed to get file name.</exception>
        public string Name
        {
            get
            {
                var buffer = new byte[FILENAME_MAX];
                var bufferHandle = GCHandle.Alloc(buffer, GCHandleType.Pinned);

                try
                {
                    var bufferPtr = bufferHandle.AddrOfPinnedObject();
                    if (!NormFileGetName(_handle, bufferPtr, FILENAME_MAX))
                    {
                        throw new IOException("Failed to get file name");
                    }
                } 
                finally
                {
                    bufferHandle.Free();
                }
                
                buffer = buffer.Where(c => c != 0).ToArray();
                return new string(buffer.Select(Convert.ToChar).ToArray());
            }
        }

        /// <summary>
        /// This function renames the file used to store content for the NORM_OBJECT_FILE transport object.
        /// This allows receiver applications to rename (or move) received files as needed.
        /// NORM uses temporary file names for received files until the application explicitly renames the file.
        /// </summary>
        /// <param name="filePath">The full path of received file.</param>
        /// <exception cref="IOException">Thrown when failed to rename file.</exception>
        public void Rename(string filePath)
        {
            if(!NormFileRename(_handle, filePath))
            {
                throw new IOException("Failed to rename file");
            }
        }
    }
}
