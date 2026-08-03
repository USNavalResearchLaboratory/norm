// Command service is a self-contained, runnable validation of the patterns in
// .claude/norm-go-service-runbook.md. It is the executable form of the runbook's
// section 7 code: a worker that owns a NORM Instance and pumps events with a
// non-blocking loop driven by context cancellation (the graceful-shutdown shape
// a long-running service worker would use), plus a sender that observes the
// ReleasePurged buffer-lifetime discipline.
//
// The worker here is a plain type with a Run(ctx) method and no framework
// dependencies. It is written so it can drop into any service's background-task
// abstraction (a goroutine, an errgroup, a worker pool, or a framework's task
// interface) without modification.
//
// It runs a receiver and a sender in-process over multicast loopback, transfers
// a set of messages, verifies every one arrived intact, and exits 0 on success
// or non-zero on failure. That makes it usable both as a copy-paste reference
// and as a smoke test:
//
//	go run ./examples/service            # default 224.1.2.3:6040
//	go run ./examples/service 224.1.2.3 7000
//
// Requires the loader path to point at the built libnorm (see src/go/README.md).
package main

import (
	"context"
	"fmt"
	"os"
	"runtime"
	"strconv"
	"sync"
	"time"

	"github.com/USNavalResearchLaboratory/norm/src/go/norm"
)

func main() {
	address, port := "224.1.2.3", uint16(6040)
	if len(os.Args) > 2 {
		address = os.Args[1]
		if p, err := strconv.ParseUint(os.Args[2], 10, 16); err == nil {
			port = uint16(p)
		}
	}

	messages := []string{"registration:hello", "registration:world", "registration:done"}

	if err := run(address, port, messages); err != nil {
		fmt.Fprintf(os.Stderr, "FAIL: %v\n", err)
		os.Exit(1)
	}
	fmt.Printf("OK: round-tripped %d messages over %s:%d\n", len(messages), address, port)
}

func run(address string, port uint16, messages []string) error {
	// Bound the whole exchange so a lost packet fails the smoke test instead of
	// hanging. A real service would use its own lifecycle context.
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()

	received := make(chan string, len(messages))

	// Start the receiver worker (the background-worker pattern from the runbook).
	rx := &receiverTask{address: address, port: port, nodeID: 2, out: received}
	var wg sync.WaitGroup
	wg.Add(1)
	rxErr := make(chan error, 1)
	go func() {
		defer wg.Done()
		rxErr <- rx.Run(ctx)
	}()

	// Give the receiver a moment to join the group before sending.
	time.Sleep(300 * time.Millisecond)

	// Send from a separate instance in this process.
	if err := sendAll(ctx, address, port, messages); err != nil {
		cancel()
		wg.Wait()
		return fmt.Errorf("send: %w", err)
	}

	// Collect received payloads until we have them all or the context expires.
	got := make(map[string]bool)
	for len(got) < len(messages) {
		select {
		case <-ctx.Done():
			cancel()
			wg.Wait()
			return fmt.Errorf("timed out; received %d/%d: %v", len(got), len(messages), keys(got))
		case msg := <-received:
			got[msg] = true
		}
	}

	// Stop the receiver worker and surface any error it hit.
	cancel()
	wg.Wait()
	if err := <-rxErr; err != nil && err != context.Canceled {
		return fmt.Errorf("receiver: %w", err)
	}

	for _, m := range messages {
		if !got[m] {
			return fmt.Errorf("missing message %q", m)
		}
	}
	return nil
}

// receiverTask owns a NORM instance for its lifetime and stops cleanly on ctx
// cancellation. This mirrors the runbook's recommended worker: a non-blocking
// NextEvent loop so a canceled context is observed promptly and Close() never
// races a goroutine blocked in the native select(). It has no framework
// dependency — Run(ctx) can be launched from a bare goroutine, an errgroup, or
// any task interface a service already uses.
type receiverTask struct {
	address string
	port    uint16
	nodeID  norm.NodeId
	out     chan<- string
}

func (t *receiverTask) Run(ctx context.Context) error {
	inst, err := norm.NewInstance(false)
	if err != nil {
		return err
	}
	defer inst.Close()

	sess, err := inst.CreateSession(t.address, t.port, t.nodeID)
	if err != nil {
		return err
	}
	defer sess.Close()
	configureLoopback(sess)

	if err := sess.StartReceiver(1 << 20); err != nil {
		return err
	}
	defer sess.StopReceiver()

	for {
		select {
		case <-ctx.Done():
			return ctx.Err()
		default:
		}
		ev, ok := inst.NextEvent(false) // non-blocking: keeps shutdown responsive
		if !ok {
			time.Sleep(5 * time.Millisecond)
			continue
		}
		if ev.Type == norm.RxObjectCompleted {
			obj := ev.Object() // unowned borrow, valid only this event
			if obj == nil || obj.Type() != norm.ObjectData {
				continue
			}
			data, err := obj.Data() // copied out; safe to keep after the event
			if err != nil {
				return err
			}
			t.out <- string(data)
		}
	}
}

func sendAll(ctx context.Context, address string, port uint16, messages []string) error {
	inst, err := norm.NewInstance(false)
	if err != nil {
		return err
	}
	defer inst.Close()

	sess, err := inst.CreateSession(address, port, 1)
	if err != nil {
		return err
	}
	defer sess.Close()
	configureLoopback(sess)

	sess.SetTxRate(10_000_000)
	if err := sess.StartSender(4321, 1<<20, 1400, 64, 16); err != nil {
		return err
	}
	defer sess.StopSender()

	for _, m := range messages {
		if _, err := sess.DataEnqueue([]byte(m), nil); err != nil {
			return err
		}
	}

	// Drain sender events until the queue empties, freeing each enqueue buffer
	// on purge. Skipping ReleasePurged here would leak C memory in a long-lived
	// sender (runbook §8.1).
	for {
		select {
		case <-ctx.Done():
			return ctx.Err()
		default:
		}
		ev, ok := inst.NextEvent(false)
		if !ok {
			time.Sleep(5 * time.Millisecond)
			continue
		}
		switch ev.Type {
		case norm.TxObjectPurged:
			sess.ReleasePurged(ev.Object())
		case norm.TxQueueEmpty:
			// All objects have been sent and purged; give NORM a moment to
			// flush the last packets to the wire before we tear down.
			time.Sleep(300 * time.Millisecond)
			return nil
		}
	}
}

// configureLoopback lets a co-located sender and receiver exchange packets on
// one host. On macOS, multicast loopback is routed via the lo0 interface.
func configureLoopback(s *norm.Session) {
	s.SetRxPortReuse(true)
	_ = s.SetLoopback(true)
	_ = s.SetMulticastLoopback(true)
	if runtime.GOOS == "darwin" {
		_ = s.SetMulticastInterface("lo0")
	}
}

func keys(m map[string]bool) []string {
	out := make([]string, 0, len(m))
	for k := range m {
		out = append(out, k)
	}
	return out
}
