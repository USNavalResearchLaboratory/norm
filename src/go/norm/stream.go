package norm

import "io"

// StreamWriter adapts a NORM send stream to io.Writer (and io.Closer). Writes
// respect NORM flow control: a single Write may loop internally until all bytes
// are accepted or the stream is closed.
type StreamWriter struct {
	obj      *Object
	autoFlsh FlushMode
}

// NewStreamWriter wraps a stream Object (from Session.StreamOpen) as an io.Writer.
// If flushOnWrite is a non-none mode, the stream is flushed with that mode after
// each Write.
func NewStreamWriter(streamObj *Object, flushOnWrite FlushMode) *StreamWriter {
	return &StreamWriter{obj: streamObj, autoFlsh: flushOnWrite}
}

// Write writes p to the stream. It loops until all bytes are accepted, since
// NormStreamWrite may accept fewer than requested under flow control. When the
// buffer is full it yields via a NORM flush and retries.
func (w *StreamWriter) Write(p []byte) (int, error) {
	total := 0
	for total < len(p) {
		n, err := w.obj.StreamWrite(p[total:])
		if err != nil {
			return total, err
		}
		total += n
		if n == 0 {
			// Buffer full: push data out and retry. This blocks progress on the
			// NORM engine draining the buffer.
			w.obj.StreamFlush(false, FlushPassive)
		}
	}
	if w.autoFlsh != FlushNone {
		w.obj.StreamFlush(false, w.autoFlsh)
	}
	return total, nil
}

// Flush flushes buffered stream data with the given mode.
func (w *StreamWriter) Flush(mode FlushMode) error {
	return w.obj.StreamFlush(false, mode)
}

// Close gracefully closes the underlying stream, flushing and signaling
// end-of-stream to receivers.
func (w *StreamWriter) Close() error {
	w.obj.StreamClose(true)
	return nil
}

// Object returns the underlying stream Object.
func (w *StreamWriter) Object() *Object { return w.obj }

// StreamReader adapts a NORM receive stream to io.Reader. It is driven by a
// receive stream Object obtained from an RxObjectNew/RxObjectUpdated event.
type StreamReader struct {
	obj *Object
}

// NewStreamReader wraps a receive stream Object as an io.Reader.
func NewStreamReader(streamObj *Object) *StreamReader {
	return &StreamReader{obj: streamObj}
}

// Read reads available stream data into p. It returns (0, nil) when no data is
// currently available (the caller should wait for the next RxObjectUpdated
// event and read again). On a detected stream break it resynchronizes to the
// next message start and returns io.ErrUnexpectedEOF so the caller can react.
func (r *StreamReader) Read(p []byte) (int, error) {
	n, inSync, err := r.obj.StreamRead(p)
	if err != nil {
		return 0, err
	}
	if !inSync {
		// Break detected: realign to the next message boundary and surface the
		// break to the caller.
		r.obj.StreamSeekMsgStart()
		return n, io.ErrUnexpectedEOF
	}
	return n, nil
}

// Object returns the underlying stream Object.
func (r *StreamReader) Object() *Object { return r.obj }

// Ensure the adapters satisfy the standard interfaces.
var (
	_ io.Writer = (*StreamWriter)(nil)
	_ io.Closer = (*StreamWriter)(nil)
	_ io.Reader = (*StreamReader)(nil)
)
