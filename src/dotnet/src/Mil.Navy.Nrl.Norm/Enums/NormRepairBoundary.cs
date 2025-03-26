namespace Mil.Navy.Nrl.Norm.Enums
{
    /// <summary>
    /// The possible for values for NORM repair boundary.
    /// </summary>
    /// <remarks>
    /// Customizes at what points the receiver initiates the NORM NACK repair process during protocol operation.
    /// </remarks>
    public enum NormRepairBoundary
    {
        /// <summary>
        /// For smaller block sizes, the NACK repair process is often/quickly initiated and the 
        /// repair of an object will occur, as needed, during the transmission of the object.
        /// </summary>
        NORM_BOUNDARY_BLOCK,
        /// <summary>
        /// Causes the protocol to defer NACK process initiation until the current transport object 
        /// has been completely transmitted.
        /// </summary>
        NORM_BOUNDARY_OBJECT
    }
}
