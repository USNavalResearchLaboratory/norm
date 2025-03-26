namespace Mil.Navy.Nrl.Norm
{
    /// <summary>
    /// The NormEvent type is used to describe significant NORM protocol events.
    /// </summary>
    public class NormEvent
    {
        /// <summary>
        /// The type identifies the event with one of NORM protocol events.
        /// </summary>
        private NormEventType _type { get; }
        /// <summary>
        /// Used to identify application in the NormSession.
        /// </summary>
        private long _sessionHandle { get; }
        /// <summary>
        /// This type is used to reference state kept by the NORM implementation with respect to other participants within a NormSession.
        /// </summary>
        private long _nodeHandle { get; }
        /// <summary>
        /// This type is used to reference state kept for data transport objects being actively transmitted or received.
        /// </summary>
        private long _objectHandle { get; }

        /// <summary>
        /// The Parameterized constructor of NormEvent
        /// </summary>
        /// <param name="type">indicates the NormEventType and determines how the other fields should be interpreted.</param>
        /// <param name="sessionHandle">indicates the applicable NormSessionHandle to which the event applies.</param>
        /// <param name="nodeHandle">indicates the applicable NormNodeHandle to which the event applies</param>
        /// <param name="objectHandle">indicates the applicable NormObjectHandle to which the event applies.</param>
        public NormEvent(NormEventType type, long sessionHandle, long nodeHandle, long objectHandle)
        {
            _type = type;
            _sessionHandle = sessionHandle;
            _nodeHandle = nodeHandle;
            _objectHandle = objectHandle;
        }

        /// <summary>
        /// The type identifies the event with one of NORM protocol events.
        /// </summary>
        public NormEventType Type => _type;

        /// <summary>
        /// The NormSession associated with the event.
        /// </summary>
        public NormSession? Session 
        { 
            get
            {
                if (_sessionHandle == NormSession.NORM_SESSION_INVALID)
                {
                    return null;
                }
                return NormSession.GetSession(_sessionHandle);
            } 
        }

        /// <summary>
        /// A remote participant associated with the event.
        /// </summary>
        public NormNode? Node 
        { 
            get
            {
                if (_nodeHandle == 0)
                {
                    return null;
                }
                return new NormNode(_nodeHandle);
            } 
        }

        /// <summary>
        /// NORM transport object.
        /// </summary>
        public NormObject? Object 
        { 
            get
            {
                NormObject? normObject = null;
                var normObjectType = NormObjectGetType(_objectHandle);
                switch (normObjectType)
                {
                    case NormObjectType.NORM_OBJECT_DATA:
                        normObject = new NormData(_objectHandle);
                        break;
                    case NormObjectType.NORM_OBJECT_FILE:
                        normObject = new NormFile(_objectHandle);
                        break;
                    case NormObjectType.NORM_OBJECT_STREAM:
                        normObject= new NormStream(_objectHandle);
                        break;
                    case NormObjectType.NORM_OBJECT_NONE:
                    default:
                        break;
                }
                return normObject;
            } 
        }

        /// <summary>
        /// This function returns a description of the NORM protocol event
        /// </summary>
        /// <returns>A string that includes the event type.</returns>
        public override string ToString()
        {
            return $"NormEvent [type={_type}]";
        }
    }
}
