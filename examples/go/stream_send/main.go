// stream_send opens a NORM stream and writes messages to it via the io.Writer
// adapter.
//
// Usage: stream_send [address] [port]
package main

import (
	"fmt"
	"log"
	"os"
	"strconv"
	"time"

	"github.com/USNavalResearchLaboratory/norm/src/go/norm"
)

func main() {
	address, port := "224.1.2.3", uint16(6003)
	if len(os.Args) > 2 {
		address = os.Args[1]
		if p, err := strconv.ParseUint(os.Args[2], 10, 16); err == nil {
			port = uint16(p)
		}
	}

	inst, err := norm.NewInstance(false)
	if err != nil {
		log.Fatalf("NewInstance: %v", err)
	}
	defer inst.Close()

	sess, err := inst.CreateSession(address, port, 1)
	if err != nil {
		log.Fatalf("CreateSession: %v", err)
	}
	defer sess.Close()

	_ = sess.SetLoopback(true)
	sess.SetRxPortReuse(true)
	sess.SetTxRate(1_000_000)
	if err := sess.StartSender(norm.SessionId(time.Now().UnixNano()&0xffff), 1<<20, 1400, 64, 16); err != nil {
		log.Fatalf("StartSender: %v", err)
	}
	defer sess.StopSender()

	streamObj, err := sess.StreamOpen(1<<20, nil)
	if err != nil {
		log.Fatalf("StreamOpen: %v", err)
	}

	// Flush actively after each Write so receivers see messages promptly.
	w := norm.NewStreamWriter(streamObj, norm.FlushActive)

	for i := 0; i < 5; i++ {
		msg := fmt.Sprintf("stream message %d\n", i)
		if _, err := w.Write([]byte(msg)); err != nil {
			log.Fatalf("Write: %v", err)
		}
		fmt.Printf("wrote: %s", msg)
		time.Sleep(200 * time.Millisecond)
	}

	if err := w.Close(); err != nil {
		log.Fatalf("Close: %v", err)
	}

	// Drain the send queue before exiting.
	for ev := range inst.Events() {
		if ev.Type == norm.TxQueueEmpty {
			time.Sleep(500 * time.Millisecond)
			return
		}
	}
}
