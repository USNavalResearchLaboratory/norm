//go:build norm_integration

// Package-level integration tests exercising real NORM transfer over multicast
// loopback. They are gated behind the "norm_integration" build tag because they
// depend on OS multicast-loopback behavior (and, on macOS, binding to lo0), which
// is not reliably available in constrained CI containers.
//
// Run with:
//
//	go test -tags norm_integration ./norm/
//
// with the loader path set to the built libnorm (see README.md).
package norm

import (
	"runtime"
	"testing"
	"time"
)

// setupLoopback configures a session so a co-located sender and receiver on the
// same host can exchange packets.
func setupLoopback(s *Session) {
	s.SetRxPortReuse(true)
	_ = s.SetLoopback(true)
	_ = s.SetMulticastLoopback(true)
	if runtime.GOOS == "darwin" {
		// macOS routes multicast loopback via the loopback interface.
		_ = s.SetMulticastInterface("lo0")
	}
}

// pollUntil drives the instance's event loop with non-blocking NextEvent until
// handle returns true or the deadline passes. Non-blocking polling avoids the
// teardown race of a goroutine blocked in the native select().
func pollUntil(t *testing.T, inst *Instance, deadline time.Duration, handle func(Event) bool) bool {
	t.Helper()
	end := time.Now().Add(deadline)
	for time.Now().Before(end) {
		ev, ok := inst.NextEvent(false)
		if !ok {
			time.Sleep(5 * time.Millisecond)
			continue
		}
		if handle(ev) {
			return true
		}
	}
	return false
}

// TestDataRoundTrip sends a data object and verifies the receiver gets the exact
// payload and info back. This is the core reliable-transfer capability and also
// exercises the DataEnqueue C-buffer lifetime (ReleasePurged) and Object.Data
// copy-out.
func TestDataRoundTrip(t *testing.T) {
	const port = 6210
	payload := []byte("reliable multicast payload \x00\x01\x02 end")
	info := []byte("info-metadata")

	rinst, err := NewInstance(false)
	if err != nil {
		t.Fatalf("recv NewInstance: %v", err)
	}
	defer rinst.Close()
	rs, err := rinst.CreateSession("224.1.2.3", port, 2)
	if err != nil {
		t.Fatalf("recv CreateSession: %v", err)
	}
	setupLoopback(rs)
	if err := rs.StartReceiver(1 << 20); err != nil {
		t.Fatalf("StartReceiver: %v", err)
	}

	sinst, err := NewInstance(false)
	if err != nil {
		t.Fatalf("send NewInstance: %v", err)
	}
	defer sinst.Close()
	ss, err := sinst.CreateSession("224.1.2.3", port, 1)
	if err != nil {
		t.Fatalf("send CreateSession: %v", err)
	}
	setupLoopback(ss)
	ss.SetTxRate(10_000_000)
	if err := ss.StartSender(4321, 1<<20, 1400, 64, 16); err != nil {
		t.Fatalf("StartSender: %v", err)
	}
	defer ss.StopSender()

	// Give the receiver a moment to join before sending.
	time.Sleep(300 * time.Millisecond)
	if _, err := ss.DataEnqueue(payload, info); err != nil {
		t.Fatalf("DataEnqueue: %v", err)
	}

	// Drive the sender so it frees the enqueue buffer on purge.
	go func() {
		end := time.Now().Add(4 * time.Second)
		for time.Now().Before(end) {
			if ev, ok := sinst.NextEvent(false); ok && ev.Type == TxObjectPurged {
				ss.ReleasePurged(ev.Object())
			}
			time.Sleep(5 * time.Millisecond)
		}
	}()

	var got, gotInfo []byte
	ok := pollUntil(t, rinst, 4*time.Second, func(ev Event) bool {
		if ev.Type != RxObjectCompleted {
			return false
		}
		obj := ev.Object()
		if obj == nil || obj.Type() != ObjectData {
			return false
		}
		d, err := obj.Data()
		if err != nil {
			t.Errorf("Data: %v", err)
			return true
		}
		got = d
		if obj.HasInfo() {
			gotInfo = obj.Info()
		}
		return true
	})
	rs.StopReceiver()

	if !ok {
		t.Fatal("timed out waiting for RxObjectCompleted")
	}
	if string(got) != string(payload) {
		t.Errorf("payload mismatch:\n got  %q\n want %q", got, payload)
	}
	if string(gotInfo) != string(info) {
		t.Errorf("info mismatch:\n got  %q\n want %q", gotInfo, info)
	}
}

// TestStreamRoundTrip writes messages through the io.Writer adapter and reads
// them back through the io.Reader adapter, verifying ordered delivery.
func TestStreamRoundTrip(t *testing.T) {
	const port = 6211
	want := "line-0\nline-1\nline-2\n"

	rinst, err := NewInstance(false)
	if err != nil {
		t.Fatalf("recv NewInstance: %v", err)
	}
	defer rinst.Close()
	rs, err := rinst.CreateSession("224.1.2.3", port, 2)
	if err != nil {
		t.Fatalf("recv CreateSession: %v", err)
	}
	setupLoopback(rs)
	if err := rs.StartReceiver(1 << 20); err != nil {
		t.Fatalf("StartReceiver: %v", err)
	}

	sinst, err := NewInstance(false)
	if err != nil {
		t.Fatalf("send NewInstance: %v", err)
	}
	defer sinst.Close()
	ss, err := sinst.CreateSession("224.1.2.3", port, 1)
	if err != nil {
		t.Fatalf("send CreateSession: %v", err)
	}
	setupLoopback(ss)
	ss.SetTxRate(10_000_000)
	if err := ss.StartSender(4321, 1<<20, 1400, 64, 16); err != nil {
		t.Fatalf("StartSender: %v", err)
	}
	defer ss.StopSender()

	stream, err := ss.StreamOpen(1<<20, nil)
	if err != nil {
		t.Fatalf("StreamOpen: %v", err)
	}
	w := NewStreamWriter(stream, FlushActive)

	go func() {
		end := time.Now().Add(6 * time.Second)
		for time.Now().Before(end) {
			sinst.NextEvent(false) // drain sender events
			time.Sleep(5 * time.Millisecond)
		}
	}()

	time.Sleep(300 * time.Millisecond)
	go func() {
		if _, err := w.Write([]byte(want)); err != nil {
			t.Errorf("Write: %v", err)
		}
	}()

	var reader *StreamReader
	buf := make([]byte, 4096)
	var got []byte
	ok := pollUntil(t, rinst, 6*time.Second, func(ev Event) bool {
		switch ev.Type {
		case RxObjectNew:
			if o := ev.Object(); o != nil && o.Type() == ObjectStream {
				reader = NewStreamReader(o.Retain())
			}
		case RxObjectUpdated:
			if reader == nil {
				return false
			}
			for {
				n, _, _ := reader.Object().StreamRead(buf)
				if n == 0 {
					break
				}
				got = append(got, buf[:n]...)
			}
			return len(got) >= len(want)
		}
		return false
	})
	rs.StopReceiver()

	if !ok {
		t.Fatalf("timed out; received %q", got)
	}
	if string(got[:len(want)]) != want {
		t.Errorf("stream mismatch:\n got  %q\n want %q", got, want)
	}
}
