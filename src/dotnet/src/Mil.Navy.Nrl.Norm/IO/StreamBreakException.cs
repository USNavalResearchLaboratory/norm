namespace Mil.Navy.Nrl.Norm.IO
{
    public class StreamBreakException : IOException
    {
        public StreamBreakException(string? message) : base(message) 
        {
        }
    }
}
