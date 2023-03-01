namespace Mil.Navy.Nrl.Norm.Enums
{
    /// <summary>
    /// The possible synchronization policies.
    /// </summary>
    public enum NormSyncPolicy
    {
        /// <summary>
        /// Attempt reception of "current" and new objects only (default).
        /// </summary>
        NORM_SYNC_CURRENT,
        /// <summary>
        /// Sync to current stream, but to beginning of stream.
        /// </summary>
        NORM_SYNC_STREAM,
        /// <summary>
        /// Attempt recovery and reliable reception of all objects
        /// held in sender transmit object cache and newer objects.
        /// </summary>
        NORM_SYNC_ALL
    }
}
