package norm

import (
	"fmt"
	"unsafe"

	"github.com/USNavalResearchLaboratory/norm/src/go/normsys"
)

// Object is a NORM transport object (data, file, or stream).
//
// Ownership: objects obtained from send operations (Session.DataEnqueue,
// FileEnqueue, StreamOpen) are "owned" — Close releases the underlying NORM
// reference. Objects obtained from events (Event.Object) are "unowned" borrows
// valid only during event handling; Close is a no-op on them. To keep an unowned
// object alive past the event, call Retain (which returns an owned wrapper).
type Object struct {
	handle normsys.ObjectHandle
	owned  bool
}

// Handle exposes the raw normsys handle for use with the low-level package.
func (o *Object) Handle() normsys.ObjectHandle { return o.handle }

// Close releases the object's NORM reference if this wrapper owns it.
func (o *Object) Close() {
	if o.owned && o.handle != normsys.ObjectInvalid {
		normsys.ObjectRelease(o.handle)
		o.handle = normsys.ObjectInvalid
	}
}

// Retain increments the object's reference count and returns an owned wrapper
// whose Close will release that reference. Use it to keep a received (unowned)
// object valid beyond the event that delivered it.
func (o *Object) Retain() *Object {
	normsys.ObjectRetain(o.handle)
	return &Object{handle: o.handle, owned: true}
}

// Type returns the object's type.
func (o *Object) Type() ObjectType { return normsys.ObjectGetType(o.handle) }

// Size returns the object's total size in bytes.
func (o *Object) Size() int64 { return normsys.ObjectGetSize(o.handle) }

// BytesPending returns the number of bytes not yet sent/received.
func (o *Object) BytesPending() int64 { return normsys.ObjectGetBytesPending(o.handle) }

// HasInfo reports whether the object carries NORM_INFO content.
func (o *Object) HasInfo() bool { return normsys.ObjectHasInfo(o.handle) }

// Info returns a copy of the object's NORM_INFO content, or nil if none.
func (o *Object) Info() []byte { return normsys.ObjectGetInfo(o.handle) }

// Sender returns an (unowned) wrapper for the remote sender of a received
// object, or nil if none.
func (o *Object) Sender() *Node {
	h := normsys.ObjectGetSender(o.handle)
	if h == normsys.NodeInvalid {
		return nil
	}
	return &Node{handle: h, owned: false}
}

// Cancel aborts transmission or reception of the object.
func (o *Object) Cancel() { normsys.ObjectCancel(o.handle) }

// Data returns a copy of a received data object's payload. It is valid to call
// only on ObjectData objects; callers should typically check Type first.
func (o *Object) Data() ([]byte, error) {
	if o.Type() != ObjectData {
		return nil, fmt.Errorf("%w: Data requires ObjectData, got %s", ErrWrongObjectType, ObjectTypeString(o.Type()))
	}
	ptr := normsys.DataAccessData(o.handle)
	size := o.Size()
	if ptr == nil || size <= 0 {
		return nil, nil
	}
	// Copy out of NORM-managed memory into a Go slice; the source is freed when
	// the object is released, so we must not alias it.
	out := make([]byte, int(size))
	copy(out, unsafe.Slice((*byte)(ptr), int(size)))
	return out, nil
}

// --- stream operations (valid only on ObjectStream objects) ---

func (o *Object) requireStream(op string) error {
	if o.Type() != ObjectStream {
		return fmt.Errorf("%w: %s requires ObjectStream, got %s", ErrWrongObjectType, op, ObjectTypeString(o.Type()))
	}
	return nil
}

// StreamWrite writes to a send stream, returning bytes accepted (may be < len(p)
// under flow control).
func (o *Object) StreamWrite(p []byte) (int, error) {
	if err := o.requireStream("StreamWrite"); err != nil {
		return 0, err
	}
	return int(normsys.StreamWrite(o.handle, p)), nil
}

// StreamRead reads from a receive stream into p. inSync is false when a stream
// break was detected (see NormStreamRead semantics); the caller should then use
// StreamSeekMsgStart to resynchronize.
func (o *Object) StreamRead(p []byte) (n int, inSync bool, err error) {
	if err := o.requireStream("StreamRead"); err != nil {
		return 0, false, err
	}
	num, sync := normsys.StreamRead(o.handle, p)
	return int(num), sync, nil
}

// StreamFlush flushes buffered stream data. If eom is true it marks end-of-message.
func (o *Object) StreamFlush(eom bool, mode FlushMode) error {
	if err := o.requireStream("StreamFlush"); err != nil {
		return err
	}
	normsys.StreamFlush(o.handle, eom, mode)
	return nil
}

// StreamSetAutoFlush configures automatic flushing behavior for a send stream.
func (o *Object) StreamSetAutoFlush(mode FlushMode) error {
	if err := o.requireStream("StreamSetAutoFlush"); err != nil {
		return err
	}
	normsys.StreamSetAutoFlush(o.handle, mode)
	return nil
}

// StreamSetPushEnable controls whether writes push through the buffer aggressively.
func (o *Object) StreamSetPushEnable(enable bool) error {
	if err := o.requireStream("StreamSetPushEnable"); err != nil {
		return err
	}
	normsys.StreamSetPushEnable(o.handle, enable)
	return nil
}

// StreamHasVacancy reports whether the send stream buffer has room for more data.
func (o *Object) StreamHasVacancy() bool { return normsys.StreamHasVacancy(o.handle) }

// StreamMarkEom marks the end of the current message in a send stream.
func (o *Object) StreamMarkEom() error {
	if err := o.requireStream("StreamMarkEom"); err != nil {
		return err
	}
	normsys.StreamMarkEom(o.handle)
	return nil
}

// StreamSeekMsgStart advances a receive stream to the start of the next message
// after a break. Returns true if a message start was found.
func (o *Object) StreamSeekMsgStart() bool { return normsys.StreamSeekMsgStart(o.handle) }

// StreamClose closes a stream. If graceful is true it flushes and signals
// end-of-stream to receivers before closing.
func (o *Object) StreamClose(graceful bool) { normsys.StreamClose(o.handle, graceful) }

// --- file operations (valid only on ObjectFile objects) ---

// FileName returns the local path of a received file object.
func (o *Object) FileName() string { return normsys.FileGetName(o.handle) }

// FileRename moves/renames a received file object.
func (o *Object) FileRename(name string) error {
	return boolErr(normsys.FileRename(o.handle, name), "NormFileRename")
}
