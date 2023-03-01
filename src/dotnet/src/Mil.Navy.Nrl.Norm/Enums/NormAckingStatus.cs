namespace Mil.Navy.Nrl.Norm.Enums
{
    /// <summary>
    /// The status of the watermark flushing process and/or positive acknowledgment collection.
    /// </summary>
    public enum NormAckingStatus
    {
        /// <summary>
        /// The given sessionHandle is invalid or the given nodeId is not in the sender's acking list.
        /// </summary>
        NORM_ACK_INVALID,
        /// <summary>
        /// The positive acknowledgement collection process did not receive acknowledgment from every 
        /// listed receiver (nodeId = NORM_NODE_ANY) or the identified nodeId did not respond.
        /// </summary>
        NORM_ACK_FAILURE,
        /// <summary>
        /// The flushing process at large has not yet completed (nodeId = NORM_NODE_ANY) or the given 
        /// individual nodeId is still being queried for response.
        /// </summary>
        NORM_ACK_PENDING,
        /// <summary>
        /// All receivers (nodeId = NORM_NODE_ANY) responded with positive acknowledgement or the given 
        /// specific nodeId did acknowledge.
        /// </summary>
        NORM_ACK_SUCCESS
    }
}
