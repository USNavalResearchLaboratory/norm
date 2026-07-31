// data_send transmits a single data object over NORM and waits for it to drain.
//
// Usage: data_send [address] [port]
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
	} else {
		fmt.Printf("Usage: %s <address> <port> (defaulting to %s:%d)\n", os.Args[0], address, port)
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

	if err := sess.SetTTL(16); err != nil {
		log.Fatalf("SetTTL: %v", err)
	}
	_ = sess.SetLoopback(true) // allow a receiver on the same host to see our packets
	sess.SetRxPortReuse(true)  // permit a co-located receiver to bind the same port
	sess.SetTxRate(1_000_000)  // 1 Mbit/s

	if err := sess.StartSender(norm.SessionId(time.Now().UnixNano()&0xffff), 1<<20, 1400, 64, 16); err != nil {
		log.Fatalf("StartSender: %v", err)
	}
	defer sess.StopSender()

	data := []byte("Hello, NORM! This is a test message sent using the Go bindings.")
	info := []byte("Example data message")
	if _, err := sess.DataEnqueue(data, info); err != nil {
		log.Fatalf("DataEnqueue: %v", err)
	}
	fmt.Printf("Sending %d bytes...\n", len(data))

	for ev := range inst.Events() {
		switch ev.Type {
		case norm.TxObjectSent:
			fmt.Println("Object sent")
		case norm.TxObjectPurged:
			// Free the C buffer backing the enqueued data.
			sess.ReleasePurged(ev.Object())
		case norm.TxQueueEmpty:
			fmt.Println("Transmission queue empty; done")
			time.Sleep(500 * time.Millisecond)
			return
		}
	}
}
