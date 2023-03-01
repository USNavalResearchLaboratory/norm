using Microsoft.Win32.SafeHandles;
using System.Runtime.InteropServices;
using System.Net.Sockets;

namespace Mil.Navy.Nrl.Norm
{
    /// <summary>
    /// An instance of a NORM protocol engine
    /// </summary>
    public class NormInstance
    {
        /// <summary>
        /// Returned on error when getting the file descriptor for a NormInstance.
        /// </summary>
        public const int NORM_DESCRIPTOR_INVALID = 0;
        /// <summary>
        /// The _handle refers to the NORM protocol engine instance
        /// </summary>
        private long _handle;

        /// <summary>
        /// Constructor for NormInstance with priority boost
        /// </summary>
        /// <param name="priorityBoost">The priorityBoost parameter, when set to a value of true, specifies that the NORM protocol engine thread be run with higher priority scheduling.</param>
        public NormInstance(bool priorityBoost)
        {
            CreateInstance(priorityBoost);
        }

        /// <summary>
        /// Default constructor for NormInstance
        /// </summary>
        public NormInstance() : this(false)
        {
        }

        /// <summary>
        /// This function creates an instance of a NORM protocol engine and is the necessary first step before any other API functions may be used.
        /// </summary>
        /// <param name="priorityBoost">The priorityBoost parameter, when set to a value of true, specifies that the NORM protocol engine thread be run with higher priority scheduling.</param>
        private void CreateInstance(bool priorityBoost)
        {
            var handle = NormCreateInstance(priorityBoost);
            _handle = handle;
        }

        /// <summary>
        /// The function immediately shuts down and destroys the NORM protocol engine instance referred to by the instanceHandle parameter.
        /// </summary>
        public void DestroyInstance()
        {
            NormDestroyInstance(_handle);
        }

        /// <summary>
        /// This function creates a NORM protocol session (NormSession) using the address (multicast or unicast) and port
        /// parameters provided. While session state is allocated and initialized, active session participation does not begin
        /// until the session starts the sender and/or receiver to join the specified multicast group
        /// (if applicable) and start protocol operation.
        /// </summary>
        /// <param name="address">Specified address determines the destination of NORM messages sent </param>
        /// <param name="port">Valid, unused port number corresponding to the desired NORM session address. </param>
        /// <param name="localNodeId">Identifies the application's presence in the NormSession </param>
        /// <returns>The NormSession that was created</return
        /// <exception cref="IOException">Throws when fails to create session</exception>
        public NormSession CreateSession(string address, int port, long localNodeId)
        {
            var session = NormCreateSession(_handle, address, port, localNodeId);
            if (session == NormSession.NORM_SESSION_INVALID)
            {
                throw new IOException("Failed to create session");
            }
            return new NormSession(session);
        }

        /// <summary>
        /// Determines if the NORM protocol engine instance has a next event
        /// </summary>
        /// <param name="sec">The seconds to wait</param>
        /// <param name="usec">The microseconds to wait</param>
        /// <returns>True if the NORM protocol engine instance has a next event</returns>
        public bool HasNextEvent(int sec, int usec)
        {
            var totalMilliseconds = sec * 1000 + usec / 1000;
            var waitTime = TimeSpan.FromMilliseconds(totalMilliseconds);
            return HasNextEvent(waitTime);
        }

        /// <summary>
        /// Determines if the NORM protocol engine instance has a next event
        /// </summary>
        /// <param name="waitTime">The time to wait</param>
        /// <returns>True if the NORM protocol engine instance has a next event</returns>
        public bool HasNextEvent(TimeSpan waitTime)
        {
            var normDescriptor = NormGetDescriptor(_handle);
            if (normDescriptor == NORM_DESCRIPTOR_INVALID)
            {
                return false;
            }
            if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows)) 
            {
                using var eventWaitHandle = new EventWaitHandle(false, EventResetMode.AutoReset);
                eventWaitHandle.SafeWaitHandle = new SafeWaitHandle(new IntPtr(normDescriptor), false);
                return eventWaitHandle.WaitOne(waitTime);
            }
            var hasNextEvent = false;
            var timeout = DateTime.Now.Add(waitTime);
            while (!hasNextEvent && DateTime.Now <= timeout)
            {
                using var socketHandle = new SafeSocketHandle(new IntPtr(normDescriptor), false);
                using var socket = new Socket(socketHandle);
                hasNextEvent = socket.Available > 0;
            }  
            return hasNextEvent;
        }

        /// <summary>
        /// This function retrieves the next available NORM protocol event from the protocol engine.
        /// </summary>
        /// <param name="waitForEvent">waitForEvent specifies whether the call to this function is blocking or not, if "waitForEvent" is false, this is a non-blocking call. </param>
        /// <returns>Returns an instance of NormEvent if NormGetNextEvent() returns true, returns null otherwise. </returns>
        public NormEvent? GetNextEvent(bool waitForEvent)
        {
            bool success = NormGetNextEvent(_handle, out NormApi.NormEvent normEvent, waitForEvent);
            if (!success)
            {
                return null;
            }
            return new NormEvent(normEvent.Type, normEvent.Session, normEvent.Sender, normEvent.Object);
        }

        /// <summary>
        /// This function retrieves the next available NORM protocol event from the protocol engine.
        /// </summary>
        /// <remarks>
        /// This is a default function which calls GetNextEvent(bool waitForEvent) override
        /// with waitForEvent set as true.
        /// </remarks>
        /// <returns>Returns an instance of NormEvent if NormGetNextEvent() returns true, returns null otherwise.</returns>
        public NormEvent? GetNextEvent()
        {
            return GetNextEvent(true);
        }

        /// <summary>
        /// This function sets the directory path used by receivers to cache newly-received NORM_OBJECT_FILE content.
        /// </summary>
        /// <param name="cachePath">the cachePath is a string specifying a valid (and writable) directory path.</param>
        /// <exception cref="IOException">Throws when fails to set the cache directory</exception>
        public void SetCacheDirectory(string cachePath)
        {
            if(!NormSetCacheDirectory(_handle, cachePath))
            {
                throw new IOException("Failed to set the cache directory");
            }
        }

        /// <summary>
        /// This function immediately stops the NORM protocol engine thread.
        /// </summary>
        public void StopInstance()
        {
            NormStopInstance(_handle);
        }

        /// <summary>
        /// This function creates and starts an operating system thread to resume NORM protocol engine operation that was previously stopped by a call to StopInstance().
        /// </summary>
        /// <returns>Boolean as to the success of the instance restart. </return>
        public bool RestartInstance()
        {
            return NormRestartInstance(_handle);
        }
        
        /// <summary>
        /// Immediately suspends the NORM protocol engine thread.
        /// </summary>
        /// <returns>Boolean as to the success of the instance suspension. </returns>
        public bool SuspendInstance()
        {
            return NormSuspendInstance(_handle);
        }

        /// <summary>
        /// This function allows NORM debug output to be directed to a file instead of the default STDERR.
        /// </summary>
        public void ResumeInstance()
        {
            NormResumeInstance(_handle);
        }

        /// <summary>
        /// This function allows NORM debug output to be directed to a file instead of the default STDERR.
        /// </summary>
        /// <param name="fileName">Full path and name of the debug log.</param>
        /// <exception cref="IOException">Throws when fails to open debug log"</exception>
        public void OpenDebugLog(string fileName)
        {
            if (!NormOpenDebugLog(_handle, fileName))
            {
                throw new IOException("Failed to open debug log");
            }
        }

        /// <summary>
        /// This function disables NORM debug output to be directed to a file instead of the default STDERR.
        /// </summary>
        public void CloseDebugLog()
        {
            NormCloseDebugLog(_handle);
        }

        /// <summary>
        /// This function allows NORM debug output to be directed to a named pipe.
        /// </summary>
        /// <param name="pipename">The debug pipe name.</param>
        /// <exception cref="IOException">Throws when fails to open debug pipe.</exception>
        public void OpenDebugPipe(string pipename)
        {
            if (!NormOpenDebugPipe(_handle, pipename))
            {
                throw new IOException("Failed to open debug pipe");
            }
        }

        /// <summary>
        /// The currently set debug level.
        /// </summary>
        public int DebugLevel 
        { 
            get => NormGetDebugLevel(); 
            set => NormSetDebugLevel(value); 
        }
    }
}
