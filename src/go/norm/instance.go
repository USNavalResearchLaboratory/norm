package norm

import (
	"fmt"

	"github.com/USNavalResearchLaboratory/norm/src/go/normsys"
)

// Instance is a NORM protocol engine. It owns the background thread and event
// queue and is the factory for Sessions. Create one with NewInstance and release
// it with Close.
type Instance struct {
	handle normsys.InstanceHandle
}

// NewInstance creates a NORM protocol instance. If priorityBoost is true, the
// protocol engine thread runs at elevated priority.
func NewInstance(priorityBoost bool) (*Instance, error) {
	h := normsys.CreateInstance(priorityBoost)
	if h == normsys.InstanceInvalid {
		return nil, fmt.Errorf("%w: NormCreateInstance", ErrInvalidHandle)
	}
	return &Instance{handle: h}, nil
}

// Close destroys the instance and releases all associated resources. After Close
// the Instance and any Sessions created from it must not be used.
func (i *Instance) Close() {
	if i.handle != normsys.InstanceInvalid {
		normsys.DestroyInstance(i.handle)
		i.handle = normsys.InstanceInvalid
	}
}

// Stop halts the protocol engine thread without destroying the instance. Use
// Restart to resume.
func (i *Instance) Stop() { normsys.StopInstance(i.handle) }

// Restart resumes a stopped instance.
func (i *Instance) Restart() error {
	return boolErr(normsys.RestartInstance(i.handle), "NormRestartInstance")
}

// SetCacheDirectory sets the directory used to store received file objects. This
// MUST be set before receiving NORM_OBJECT_FILE objects.
func (i *Instance) SetCacheDirectory(path string) error {
	return boolErr(normsys.SetCacheDirectory(i.handle, path), "NormSetCacheDirectory")
}

// Descriptor returns the underlying file descriptor (Unix) that becomes readable
// when a NORM event is pending. Use it with a poller (select/epoll, or Go's
// runtime via os.NewFile) to integrate NORM's event loop with other I/O without
// a blocking NextEvent call.
func (i *Instance) Descriptor() int {
	return normsys.GetDescriptor(i.handle)
}

// NextEvent returns the next protocol event. If wait is true it blocks until an
// event is available; if false it returns ok=false when no event is pending.
func (i *Instance) NextEvent(wait bool) (Event, bool) {
	raw, ok := normsys.GetNextEvent(i.handle, wait)
	if !ok {
		return Event{}, false
	}
	return eventFromRaw(raw), true
}

// Events returns a channel that delivers protocol events until the instance is
// closed or stopped. It launches a goroutine that blocks on NextEvent; the
// channel is closed when NextEvent reports no more events (e.g. after Close from
// another goroutine). For fine-grained control, use NextEvent or Descriptor
// directly instead.
func (i *Instance) Events() <-chan Event {
	ch := make(chan Event)
	go func() {
		defer close(ch)
		for {
			ev, ok := i.NextEvent(true)
			if !ok {
				return
			}
			ch <- ev
		}
	}()
	return ch
}

// CreateSession creates a session bound to the given multicast (or unicast)
// address and port, using localNodeId as this participant's node id.
func (i *Instance) CreateSession(address string, port uint16, localNodeId NodeId) (*Session, error) {
	h := normsys.CreateSession(i.handle, address, port, localNodeId)
	if h == normsys.SessionInvalid {
		return nil, fmt.Errorf("%w: NormCreateSession(%s:%d)", ErrInvalidHandle, address, port)
	}
	return &Session{handle: h, instance: i}, nil
}

// SetDebugLevel sets the NORM library debug verbosity (0 = none).
func SetDebugLevel(level uint) { normsys.SetDebugLevel(level) }

// boolErr converts a NORM boolean result into an error, wrapping ErrOperationFailed.
func boolErr(ok bool, op string) error {
	if ok {
		return nil
	}
	return fmt.Errorf("%w: %s", ErrOperationFailed, op)
}
