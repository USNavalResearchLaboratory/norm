// data_recv receives a single data object over NORM and prints it.
//
// Usage: data_recv [address] [port]
package main

import (
	"fmt"
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
	} else {
		fmt.Printf("Usage: %s <address> <port> (defaulting to %s:%d)\n", os.Args[0], address, port)
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
	sess.SetRxPortReuse(true) // permit a co-located sender to bind the same port
	if err := sess.StartReceiver(1 << 20); err != nil {
		log.Fatalf("StartReceiver: %v", err)
	}
	defer sess.StopReceiver()

	fmt.Printf("Receiving on %s:%d ...\n", address, port)

	for ev := range inst.Events() {
		switch ev.Type {
		case norm.RemoteSenderNew:
			fmt.Println("New sender")
		case norm.RxObjectCompleted:
			obj := ev.Object()
			if obj == nil || obj.Type() != norm.ObjectData {
				continue
			}
			data, err := obj.Data()
			if err != nil {
				log.Printf("Data: %v", err)
				continue
			}
			fmt.Printf("Received %d bytes: %s\n", len(data), string(data))
			if obj.HasInfo() {
				fmt.Printf("Info: %s\n", string(obj.Info()))
			}
			return
		}
	}
}
