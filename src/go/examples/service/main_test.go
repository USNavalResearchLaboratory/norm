//go:build norm_integration

package main

import "testing"

// TestServiceRoundTrip runs the sample service's core logic end to end so the
// runbook's reference program is validated by CI (opt-in via the
// norm_integration tag, like the package's other loopback tests). If the binding
// API drifts, this fails to compile or fails at runtime.
func TestServiceRoundTrip(t *testing.T) {
	messages := []string{"registration:hello", "registration:world", "registration:done"}
	if err := run("224.1.2.3", 6045, messages); err != nil {
		t.Fatalf("sample service round-trip failed: %v", err)
	}
}
