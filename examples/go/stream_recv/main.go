// stream_recv receives a NORM stream and copies it to stdout via the io.Reader
// adapter.
//
// Usage: stream_recv [address] [port]
package main

import (
	"errors"
	"fmt"
	"io"
	"log"
	"os"
	"strconv"

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

	sess, err := inst.CreateSession(address, port, 2)
	if err != nil {
		log.Fatalf("CreateSession: %v", err)
	}
	defer sess.Close()

	_ = sess.SetLoopback(true)
	sess.SetRxPortReuse(true)
	if err := sess.StartReceiver(1 << 20); err != nil {
		log.Fatalf("StartReceiver: %v", err)
	}
	defer sess.StopReceiver()

	fmt.Printf("Receiving stream on %s:%d ...\n", address, port)

	var reader *norm.StreamReader
	buf := make([]byte, 4096)

	for ev := range inst.Events() {
		switch ev.Type {
		case norm.RxObjectNew:
			obj := ev.Object()
			if obj != nil && obj.Type() == norm.ObjectStream {
				// Retain the stream object so it stays valid across events.
				reader = norm.NewStreamReader(obj.Retain())
			}
		case norm.RxObjectUpdated:
			if reader == nil {
				continue
			}
			for {
				n, err := reader.Read(buf)
				if n > 0 {
					os.Stdout.Write(buf[:n])
				}
				if errors.Is(err, io.ErrUnexpectedEOF) {
					fmt.Fprintln(os.Stderr, "[stream break]")
					break
				}
				if err != nil || n == 0 {
					break
				}
			}
		case norm.RxObjectCompleted, norm.RxObjectAborted:
			fmt.Fprintln(os.Stderr, "[stream ended]")
			return
		}
	}
}
