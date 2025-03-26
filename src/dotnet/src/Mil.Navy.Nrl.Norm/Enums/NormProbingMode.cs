namespace Mil.Navy.Nrl.Norm.Enums
{
    /// <summary>
    /// The possible probing modes.
    /// </summary>
    public enum NormProbingMode
    {
        /// <summary>
        /// The sender application must explicitly set its estimate of GRTT using the NormSetGrttEstimate() function.
        /// </summary>
        NORM_PROBE_NONE,
        /// <summary>
        /// The NORM sender still transmits NORM_CMD(CC) probe messages multiplexed with its data transmission, 
        /// but the receiver set does not explicitly acknowledge these probes. Instead the receiver set is limited 
        /// to opportunistically piggy-backing responses when NORM_NACK messages are generated.
        /// </summary>
        NORM_PROBE_PASSIVE,
        /// <summary>
        /// In this mode, the receiver set explicitly acknowledges NORM sender GRTT probes ((NORM_CMD(CC) messages) 
        /// with NORM_ACK responses that are group-wise suppressed.
        /// </summary>
        NORM_PROBE_ACTIVE
    }
}
