// file_send transmits a local file over NORM.
//
// Usage: file_send <filepath> [address] [port]
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
	if len(os.Args) < 2 {
		log.Fatalf("Usage: %s <filepath> [address] [port]", os.Args[0])
	}
	filePath := os.Args[1]
	address, port := "224.1.2.3", uint16(6003)
	if len(os.Args) > 3 {
		address = os.Args[2]
		if p, err := strconv.ParseUint(os.Args[3], 10, 16); err == nil {
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

	if _, err := sess.FileEnqueue(filePath, []byte(filePath)); err != nil {
		log.Fatalf("FileEnqueue: %v", err)
	}
	fmt.Printf("Sending file %s ...\n", filePath)

	for ev := range inst.Events() {
		switch ev.Type {
		case norm.TxObjectSent:
			fmt.Println("File sent")
		case norm.TxQueueEmpty:
			time.Sleep(500 * time.Millisecond)
			return
		}
	}
}
