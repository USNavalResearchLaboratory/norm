package norm

import (
	"errors"
	"testing"
)

// These tests require linking libnorm but do not exercise the network, so they
// are deterministic and run as part of the default suite (including waf --go-test).

// newSender creates an instance + started sender on the given port. Sender start
// binds a socket but sends nothing until data is enqueued at a tx rate, so this
// stays local and fast.
func newSender(t *testing.T, port uint16) (*Instance, *Session) {
	t.Helper()
	inst, err := NewInstance(false)
	if err != nil {
		t.Fatalf("NewInstance: %v", err)
	}
	s, err := inst.CreateSession("224.1.2.3", port, 1)
	if err != nil {
		inst.Close()
		t.Fatalf("CreateSession: %v", err)
	}
	s.SetRxPortReuse(true)
	if err := s.StartSender(1, 1<<20, 1400, 64, 16); err != nil {
		s.Close()
		inst.Close()
		t.Fatalf("StartSender: %v", err)
	}
	return inst, s
}

// TestWrongObjectType verifies type-guarded operations reject mismatched objects.
func TestWrongObjectType(t *testing.T) {
	inst, s := newSender(t, 6200)
	defer inst.Close()
	defer s.Close()
	defer s.StopSender()

	stream, err := s.StreamOpen(1<<20, nil)
	if err != nil {
		t.Fatalf("StreamOpen: %v", err)
	}
	if _, err := stream.Data(); !errors.Is(err, ErrWrongObjectType) {
		t.Errorf("stream.Data() err = %v, want ErrWrongObjectType", err)
	}

	// A data object rejects stream operations. The enqueue buffer is reclaimed by
	// Session.Close at teardown.
	data, err := s.DataEnqueue([]byte("payload"), nil)
	if err != nil {
		t.Fatalf("DataEnqueue: %v", err)
	}
	if data.Type() != ObjectData {
		t.Fatalf("enqueued object type = %s, want DATA", ObjectTypeString(data.Type()))
	}
	if _, err := data.StreamWrite([]byte("x")); !errors.Is(err, ErrWrongObjectType) {
		t.Errorf("data.StreamWrite() err = %v, want ErrWrongObjectType", err)
	}
}

// TestCloseIdempotent verifies Close can be called repeatedly without panicking
// or double-freeing native resources.
func TestCloseIdempotent(t *testing.T) {
	inst, err := NewInstance(false)
	if err != nil {
		t.Fatalf("NewInstance: %v", err)
	}
	s, err := inst.CreateSession("224.1.2.3", 6201, 1)
	if err != nil {
		inst.Close()
		t.Fatalf("CreateSession: %v", err)
	}
	s.Close()
	s.Close() // second Close must be a no-op
	inst.Close()
	inst.Close()
}

// TestReleasePurgedNoop verifies ReleasePurged tolerates nil and untracked objects.
func TestReleasePurgedNoop(t *testing.T) {
	inst, err := NewInstance(false)
	if err != nil {
		t.Fatalf("NewInstance: %v", err)
	}
	defer inst.Close()
	s, err := inst.CreateSession("224.1.2.3", 6202, 1)
	if err != nil {
		t.Fatalf("CreateSession: %v", err)
	}
	defer s.Close()

	s.ReleasePurged(nil)       // must not panic
	s.ReleasePurged(&Object{}) // untracked handle: must not panic
}

// TestEventNilAccessors verifies a zero-value Event yields nil object/sender
// wrappers rather than wrapping invalid handles.
func TestEventNilAccessors(t *testing.T) {
	var e Event
	if e.Object() != nil {
		t.Error("zero Event.Object() = non-nil, want nil")
	}
	if e.Sender() != nil {
		t.Error("zero Event.Sender() = non-nil, want nil")
	}
}
