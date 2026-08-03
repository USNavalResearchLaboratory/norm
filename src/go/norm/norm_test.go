package norm

import "testing"

func TestVersion(t *testing.T) {
	major, minor, patch := Version()
	if major <= 0 {
		t.Fatalf("unexpected version %d.%d.%d", major, minor, patch)
	}
}

func TestObjectTypeString(t *testing.T) {
	cases := map[ObjectType]string{
		ObjectNone:   "NONE",
		ObjectData:   "DATA",
		ObjectFile:   "FILE",
		ObjectStream: "STREAM",
	}
	for typ, want := range cases {
		if got := ObjectTypeString(typ); got != want {
			t.Errorf("ObjectTypeString(%d) = %q, want %q", typ, got, want)
		}
	}
}

func TestEventTypeString(t *testing.T) {
	if got := EventTypeString(RxObjectCompleted); got != "RX_OBJECT_COMPLETED" {
		t.Errorf("EventTypeString(RxObjectCompleted) = %q", got)
	}
	if got := EventTypeString(EventType(9999)); got != "UNKNOWN" {
		t.Errorf("EventTypeString(bogus) = %q, want UNKNOWN", got)
	}
}

func TestIsUnicastAddress(t *testing.T) {
	if !IsUnicastAddress("192.168.1.1") {
		t.Error("192.168.1.1 should be unicast")
	}
	if IsUnicastAddress("224.1.2.3") {
		t.Error("224.1.2.3 should be multicast, not unicast")
	}
}

// TestInstanceLifecycle exercises create/session/close through the idiomatic API.
func TestInstanceLifecycle(t *testing.T) {
	inst, err := NewInstance(false)
	if err != nil {
		t.Fatalf("NewInstance: %v", err)
	}
	defer inst.Close()

	sess, err := inst.CreateSession("224.1.2.3", 6003, 1)
	if err != nil {
		t.Fatalf("CreateSession: %v", err)
	}
	defer sess.Close()

	if got := sess.LocalNodeId(); got != 1 {
		t.Errorf("LocalNodeId = %d, want 1", got)
	}
}
