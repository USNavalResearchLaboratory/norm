namespace Mil.Navy.Nrl.Norm.Enums
{
    /// <summary>
    /// The possible NORM data transport object types.
    /// </summary>
    public enum NormObjectType
    {
        /// <summary>
        /// A special NormObjectType value, NORM_OBJECT_NONE, indicates an invalid object type.
        /// </summary>
        NORM_OBJECT_NONE,
        /// <summary>
        /// A transport object of type NORM_OBJECT_DATA.
        /// </summary>
        NORM_OBJECT_DATA,
        /// <summary>
        /// A transport object of type NORM_OBJECT_FILE.
        /// </summary>
        NORM_OBJECT_FILE,
        /// <summary>
        /// A transport object of type NORM_OBJECT_STREAM.
        /// </summary>
        NORM_OBJECT_STREAM
    }
}
