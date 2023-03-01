namespace Mil.Navy.Nrl.Norm.Enums
{
    /// <summary>
    /// The possible flush modes.
    /// </summary>
    public enum NormFlushMode
    {
        /// <summary>
        /// No flushing occurs unless explicitly requested via NormStreamFlush().
        /// </summary>
        NORM_FLUSH_NONE,
        /// <summary>
        /// Causes NORM to immediately transmit all enqueued data for the stream (subject to session transmit rate limits), 
        /// even if this results in NORM_DATA messages with "small" payloads. 
        /// </summary>
        NORM_FLUSH_PASSIVE,
        /// <summary>
        /// The sender actively transmits NORM_CMD(FLUSH) messages after any enqueued stream content has been sent.
        /// </summary>
        NORM_FLUSH_ACTIVE
    }
}
