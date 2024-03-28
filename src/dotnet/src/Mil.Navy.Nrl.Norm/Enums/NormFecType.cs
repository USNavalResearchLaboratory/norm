namespace Mil.Navy.Nrl.Norm.Enums
{
    /// <summary>
    /// The valid FEC Types.
    /// </summary>
    public enum NormFecType
    {
        /// <summary>
        /// Fully-specified, general purpose Reed-Solomon.
        /// </summary>
        RS,
        /// <summary>
        /// Fully-specified 8-bit Reed-Solmon per RFC 5510.
        /// </summary>
        RS8,
        /// <summary>
        /// Partially-specified "small block" codes.
        /// </summary>
        SB
    }
}