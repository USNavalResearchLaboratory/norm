using System.Runtime.CompilerServices;

namespace Mil.Navy.Nrl.Norm.IO
{
    public class NormOutputStream : Stream
    {
        private NormInstance _normInstance;
        private NormSession _normSession;
        private NormStream? _normStream;

        private List<INormEventListener> _normEventListeners;

        private bool _closed;
        private object _closeLock;

        private bool _bufferIsFull;

        /// <exception cref="IOException"></exception>
        public NormOutputStream(string address, int port)
        {
            // Create the NORM instance
            _normInstance = new NormInstance();

            // Create the NORM session
            _normSession = _normInstance.CreateSession(address, port, NormNode.NORM_NODE_ANY);

            _normStream = null;

            _normEventListeners = new List<INormEventListener>();

            _closed = true;
            _closeLock = new object();

            _bufferIsFull = false;
        }

        /// <exception cref="IOException"></exception>
        [MethodImpl(MethodImplOptions.Synchronized)]
        public void OpenDebugLog(string filename)
        {
            _normInstance.OpenDebugLog(filename);
        }

        [MethodImpl(MethodImplOptions.Synchronized)]
        public void CloseDebugLog()
        {
            _normInstance.CloseDebugLog();
        }

        [MethodImpl(MethodImplOptions.Synchronized)]
        public void SetDebugLevel(int level)
        {
            _normInstance.DebugLevel = level;
        }

        [MethodImpl(MethodImplOptions.Synchronized)]
        public void SetMessageTrace(bool messageTrace)
        {
            _normSession.SetMessageTrace(messageTrace);
        }

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
        public void SetCongestionControl(bool ccEnabled, bool ccAdjustRate)
        {
            _normSession.SetCongestionControl(ccEnabled, ccAdjustRate);
        }

        [MethodImpl(MethodImplOptions.Synchronized)]
        public void SetTxRateBounds(double minTxRate, double maxTxRate)
        {
            _normSession.SetTxRateBounds(maxTxRate, minTxRate);
        }

        public double TxRate
        {
            get
            {
                lock (this)
                {
                    return _normSession.TxRate;
                }
            }
            set
            {
                lock (this)
                {
                    _normSession.TxRate = value;
                }
            }
        }

        public double GrttEstimate
        {
            get
            {
                lock (this)
                {
                    return _normSession.GrttEstimate;
                }
            }
            set
            {
                lock (this)
                {
                    _normSession.GrttEstimate = value;
                }
            }
        }

        [MethodImpl(MethodImplOptions.Synchronized)]
        public void SetGroupSize(long groupSize)
        {
            _normSession.SetGroupSize(groupSize);
        }

        [MethodImpl(MethodImplOptions.Synchronized)]
        public void SetAutoParity(short autoParity)
        {
            _normSession.SetAutoParity(autoParity);
        }

        [MethodImpl(MethodImplOptions.Synchronized)]
        public void SetBackoffFactor(double backoffFactor)
        {
            _normSession.SetBackoffFactor(backoffFactor);
        }

        /// <exception cref="InvalidOperationException"></exception>
        [MethodImpl(MethodImplOptions.Synchronized)]
        public void SetAutoFlush(NormFlushMode flushMode)
        {
            if (_normStream == null)
            {
                throw new InvalidOperationException("Can only set auto flush after the stream is open");
            }
            _normStream.SetAutoFlush(flushMode);
        }

        /// <exception cref="InvalidOperationException"></exception>
        [MethodImpl(MethodImplOptions.Synchronized)]
        public void SetPushEnable(bool pushEnable)
        {
            if (_normStream == null)
            {
                throw new InvalidOperationException("Can only set push enabled after the stream is open");
            }
            _normStream.SetPushEnable(pushEnable);
        }

        /// <exception cref="InvalidOperationException"></exception>
        [MethodImpl(MethodImplOptions.Synchronized)]
        public void MarkEom()
        {
            if (_normStream == null)
            {
                throw new InvalidOperationException("Can only mark EOM after the stream is open");
            }
            _normStream.MarkEom();
        }

        /// <param name="normEventListener">The INormEventListener to add.</param>
        public void AddNormEventListener(INormEventListener normEventListener)
        {
            lock (_normEventListeners)
            {
                _normEventListeners.Add(normEventListener);
            }
        }

        /// <param name="normEventListener">The INormEventListener to remove.</param>
        public void RemoveNormEventListener(INormEventListener normEventListener)
        {
            lock (_normEventListeners)
            {
                _normEventListeners.Remove(normEventListener);
            }
        }

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
        public void Open(int sessionId, long bufferSpace, int segmentSize, short blockSize, short numParity, long repairWindow)
        {
            lock (_closeLock)
            {
                if (IsClosed)
                {
                    throw new IOException("Stream is already open");
                }

                _normSession.StartSender(sessionId, bufferSpace, segmentSize, blockSize, numParity);

                // Open the stream
                _normStream = _normSession.StreamOpen(repairWindow);

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
        public void Write(int b)
        {
            if (IsClosed)
            {
                throw new IOException("Stream is closed");
            }

            var buffer = new byte[1];
            buffer[0] = (byte)b;

            Write(buffer);
        }

        /// <exception cref="IOException"></exception>
        [MethodImpl(MethodImplOptions.Synchronized)]
        public override void Write(byte[] buffer, int offset, int count)
        {
            int n;

            if (IsClosed)
            {
                throw new IOException("Stream is closed");
            }

            while (count > 0)
            {
                while (_normInstance.HasNextEvent(0, 0))
                {
                    ProcessEvent();
                }

                // Wait while the buffer is full
                while (_bufferIsFull)
                {
                    ProcessEvent();
                }

                // Write some data
                if (_normStream == null || (n = _normStream.Write(buffer, offset, count)) < 0)
                {
                    throw new IOException("Failed to write to stream");
                }

                _bufferIsFull = n == 0;

                count -= n;
                offset += n;
            }
        }

        /// <exception cref="IOException"></exception>
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
                    case NormEventType.NORM_TX_QUEUE_VACANCY:
                    case NormEventType.NORM_TX_QUEUE_EMPTY:
                        var normObject = normEvent.Object;
                        if (normObject == null || !normObject.Equals(_normStream))
                        {
                            break;
                        }

                        // Signal that the buffer is not full
                        _bufferIsFull = false;
                        break;

                    case NormEventType.NORM_TX_OBJECT_SENT:
                    case NormEventType.NORM_TX_OBJECT_PURGED:
                        _normStream = null;
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

        public override int Read(byte[] buffer, int offset, int count)
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

        public override bool CanRead => false;

        public override bool CanSeek => false;

        public override bool CanWrite => IsClosed;

        public override long Length => throw new NotSupportedException();

        public override long Position { get => throw new NotSupportedException(); set => throw new NotSupportedException(); }
    }
}