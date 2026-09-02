// file_recv receives a file over NORM into a cache directory.
//
// Usage: file_recv <cacheDir> [address] [port]
package main

import (
	"fmt"
	"log"
	"os"
	"strconv"

	"github.com/USNavalResearchLaboratory/norm/src/go/norm"
)

func main() {
	if len(os.Args) < 2 {
		log.Fatalf("Usage: %s <cacheDir> [address] [port]", os.Args[0])
	}
	cacheDir := os.Args[1]
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

	// Required for file reception: NORM writes received files here.
	if err := inst.SetCacheDirectory(cacheDir); err != nil {
		log.Fatalf("SetCacheDirectory: %v", err)
	}

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

	fmt.Printf("Receiving files on %s:%d into %s ...\n", address, port, cacheDir)

	for ev := range inst.Events() {
		if ev.Type == norm.RxObjectCompleted {
			obj := ev.Object()
			if obj != nil && obj.Type() == norm.ObjectFile {
				fmt.Printf("Received file: %s\n", obj.FileName())
				return
			}
		}
	}
}
