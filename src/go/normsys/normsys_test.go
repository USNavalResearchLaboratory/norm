package normsys

import "testing"

// TestGetVersion is a smoke test that the binding links against libnorm and can
// call into it. A zero major version would indicate a broken link or stale lib.
func TestGetVersion(t *testing.T) {
	major, minor, patch := GetVersion()
	if major <= 0 {
		t.Fatalf("unexpected NORM version %d.%d.%d (major must be > 0)", major, minor, patch)
	}
	t.Logf("linked NORM version %d.%d.%d", major, minor, patch)
}

// TestCreateDestroyInstance exercises the most basic instance lifecycle through
// the raw binding.
func TestCreateDestroyInstance(t *testing.T) {
	h := CreateInstance(false)
	if h == InstanceInvalid {
		t.Fatal("CreateInstance returned invalid handle")
	}
	DestroyInstance(h)
}
