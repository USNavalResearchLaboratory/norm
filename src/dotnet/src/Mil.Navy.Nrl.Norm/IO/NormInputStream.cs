using System.Runtime.CompilerServices;

namespace Mil.Navy.Nrl.Norm.IO
{
    public class NormInputStream : Stream
    {
        private NormInstance _normInstance;
        private NormSession _normSession;
        private NormStream? _normStream;

        private List<INormEventListener> _normEventListeners;

        private bool _closed;
        private object _closeLock;

        private bool _bufferIsEmpty;
        private bool _receivedEof;

        /// <exception cref="IOException"></exception>
        public NormInputStream(string address, int port)
        {
            // Create the NORM instance
            _normInstance = new NormInstance();

            // Create the NORM session
            _normSession = _normInstance.CreateSession(address, port, NormNode.NORM_NODE_ANY);
            _normStream = null;

            _normEventListeners = new List<INormEventListener>();

            _closed = true;
            _closeLock = new object();

            _bufferIsEmpty = true;
            _receivedEof = false;
        }

        /// <exception cref="IOException"></exception>
        [MethodImpl(MethodImplOptions.Synchronized)]
        public void OpenDebugLog(string fileName)
        {
            if (fileName == null)
            {
                throw new IOException("File was name was not found.");
            }
            _normInstance.OpenDebugLog(fileName);
        }

        [MethodImpl(MethodImplOptions.Synchronized)]
        public void CloseDebugLog() => _normInstance.CloseDebugLog();

        [MethodImpl(MethodImplOptions.Synchronized)]
        public void NormSetDebugLevel(int level) { _normInstance.DebugLevel = level; }

        [MethodImpl(MethodImplOptions.Synchronized)]
        public void SetMessageTrace(bool messageTrace) => _normSession.SetMessageTrace(messageTrace);

        /// <exception cref="IOException"></exception>
        [MethodImpl(MethodImplOptions.Synchronized)]
        public void SetMulticastInterface(string multicastInterface)
        {
            _normSession.SetMulticastInterface(multicastInterface);
        }

        /// <exception cref="IOException"></exception>
        [MethodImpl(MethodImplOptions.Synchronized)]
        public void SetEcnSupport(bool ecnEnable, bool ignoreLoss)
        {
            _normSession.SetEcnSupport(ecnEnable, ignoreLoss);
        }

        /// <exception cref="IOException"></exception>
        [MethodImpl(MethodImplOptions.Synchronized)]
        public void SetTtl(byte ttl)
        {
            _normSession.SetTTL(ttl);
        }

        /// <exception cref="IOException"></exception>
        [MethodImpl(MethodImplOptions.Synchronized)]
        public void SetTos(byte tos)
        {
            _normSession.SetTOS(tos);
        }

        [MethodImpl(MethodImplOptions.Synchronized)]
        public void setSilentReceiver(bool silent, int maxDelay)
        {
            _normSession.SetSilentReceiver(silent, maxDelay);
        }

        [MethodImpl(MethodImplOptions.Synchronized)]
        public void SetDefaultUnicastNack(bool defaultUnicastNack)
        {
            _normSession.SetDefaultUnicastNack(defaultUnicastNack);
        }

        /// <exception cref="InvalidOperationException"></exception>
        [MethodImpl(MethodImplOptions.Synchronized)]
        public void SeekMsgStart()
        {
            if (_normStream == null)
            {
                throw new InvalidOperationException("Can only seek msg start after the stream is connected");
            }

            _normStream.SeekMsgStart();
        }

        /// <param name="normEventListener">The INormEventListener to add.</param>
        [MethodImpl(MethodImplOptions.Synchronized)]
        public void AddNormEventListener(INormEventListener normEventListener)
        {
            lock (_normEventListeners)
            {
                _normEventListeners.Add(normEventListener);
            }
        }

        /// <param name="normEventListener">The INormEventListener to remove.</param>
        [MethodImpl(MethodImplOptions.Synchronized)]
        public void RemoveNormEventListener(INormEventListener normEventListener)
        {
            lock (_normEventListeners)
            {
                _normEventListeners.Remove(normEventListener);
            }
        }

        [MethodImpl(MethodImplOptions.Synchronized)]
        private void FireNormEventOccured(NormEvent normEvent)
        {
            lock (_normEventListeners)
            {
                foreach (var normEventListener in _normEventListeners)
                {
                    normEventListener.NormEventOccurred(normEvent);
                }

            }
        }

        /// <exception cref="IOException"></exception>
        [MethodImpl(MethodImplOptions.Synchronized)]
        public void Open(long bufferSpace)
        {
            lock (_closeLock)
            {
                if (!IsClosed)
                {
                    throw new IOException("Stream is already open");
                }

                _normSession.StartReceiver(bufferSpace);

                _closed = false;
            }
        }

        /// <exception cref="IOException"></exception>
        public override void Close()
        {
            lock (_closeLock)
            {
                if (IsClosed)
                {
                    return;
                }

                _normStream?.Close(false);
                _normSession.StopSender();
                _normInstance.StopInstance();

                _closed = true;
            }
        }

        public bool IsClosed
        {
            get
            {
                lock (_closeLock)
                {
                    return _closed;
                }
            }
        }

        /// <exception cref="IOException"></exception>
        [MethodImpl(MethodImplOptions.Synchronized)]
        public int Read()
        {
            var buffer = new byte[1];

            if (IsClosed)
            {
                throw new IOException("Stream is closed");
            }

            if (Read(buffer) < 0)
            {
                return -1;
            }

            return buffer[0];
        }

        /// <exception cref="IOException"></exception>
        [MethodImpl(MethodImplOptions.Synchronized)]
        public override int Read(byte[] buffer, int offset, int count)
        {
            int n;

            if (IsClosed)
            {
                throw new IOException("Stream is closed");
            }

            do
            {
                while (_bufferIsEmpty || _normInstance.HasNextEvent(0, 0))
                {
                    ProcessEvent();

                    if (_receivedEof) 
                    { 
                        return -1; 
                    }
                }

                if (_normStream == null)
                {
                    return -1;
                }

                // Read from the stream
                if ((n = _normStream.Read(buffer, offset, count)) < 0)
                {
                    throw new IOException("Break in stream integrity");
                }

                _bufferIsEmpty = n == 0;
            } 
            while (_bufferIsEmpty);

            return n;
        }

        private void ProcessEvent()
        {
            // Retrieve the next event
            var normEvent = _normInstance.GetNextEvent();

            // Check if the stream was closed
            if (IsClosed)
            {
                throw new IOException("Stream closed");
            }

            if (normEvent != null)
            {
                // Process the event
                var eventType = normEvent.Type;
                switch (eventType)
                {
                    case NormEventType.NORM_RX_OBJECT_NEW:
                        var normObject = normEvent.Object;
                        if (normObject != null && normObject.Type == NormObjectType.NORM_OBJECT_STREAM)
                        {
                            _normStream = (NormStream)normObject;
                        }
                        break;

                    case NormEventType.NORM_RX_OBJECT_UPDATED:
                        var theNormObject = normEvent.Object;
                        if (theNormObject == null || !theNormObject.Equals(_normStream))
                        {
                            break;
                        }

                        // Signal that the buffer is not empty
                        _bufferIsEmpty = false;
                        break;

                    case NormEventType.NORM_RX_OBJECT_ABORTED:
                    case NormEventType.NORM_RX_OBJECT_COMPLETED:
                        _normStream = null;

                        // Signal that the stream has ended
                        _receivedEof = true;
                        break;

                    default:
                        break;
                }

                // Notify listeners of the norm event
                FireNormEventOccured(normEvent);
            }
        }

        public override void Flush()
        {
            throw new NotSupportedException();
        }

        public override void Write(byte[] buffer, int offset, int count)
        {
            throw new NotSupportedException();
        }

        public override long Seek(long offset, SeekOrigin origin)
        {
            throw new NotSupportedException();
        }

        public override void SetLength(long value)
        {
            throw new NotSupportedException();
        }

        public override bool CanRead => true;

        public override bool CanSeek => false;

        public override bool CanWrite => false;

        public override long Length => throw new NotSupportedException();

        public override long Position { get => throw new NotSupportedException(); set => throw new NotSupportedException(); }
    }
}