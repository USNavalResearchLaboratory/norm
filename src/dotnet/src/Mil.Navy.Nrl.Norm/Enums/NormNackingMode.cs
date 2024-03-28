namespace Mil.Navy.Nrl.Norm.Enums
{
    /// <summary>
    /// The available nacking modes.
    /// </summary>
    public enum NormNackingMode
    {
        /// <summary>
        /// Do not transmit any repair requests for the newly received object.
        /// </summary>
        NORM_NACK_NONE,
        /// <summary>
        /// Transmit repair requests for NORM_INFO content only as needed.
        /// </summary>
        NORM_NACK_INFO_ONLY,
        /// <summary>
        /// Transmit repair requests for entire object as needed.
        /// </summary>
        NORM_NACK_NORMAL
    }
}
