# NORM Go API Guide

This guide covers the `norm` package's model in more depth. For build/install
instructions see `README.md`.

## Package layout

- `normsys` — raw cgo binding. Handles are pointer-backed named types
  (`InstanceHandle`, `SessionHandle`, `ObjectHandle`, `NodeHandle`); enum values
  map directly to the C constants. Signatures use Go types so the package is
  consumable without a second cgo layer.
- `norm` — idiomatic wrapper. This is the package applications should import.

## Object hierarchy

```
Instance                     // protocol engine + event queue
  └── Session                // bound to an address/port; sender and/or receiver
        └── Object           // a transported data blob, file, or stream
```

- Create an `Instance` with `NewInstance`. It owns a background protocol thread.
- Create `Session`s from the instance with `CreateSession`.
- Send objects via `Session.DataEnqueue` / `FileEnqueue` / `StreamOpen`.
- Receive objects by reading events (below) and inspecting `Event.Object()`.

Call `Close()` on each in reverse order of creation. `Session.Close` also frees
any pending data-enqueue buffers (see below).

## The event model

NORM is event driven. An `Instance` exposes three ways to consume events:

1. **`NextEvent(wait bool)`** — the primitive. With `wait=true` it blocks until an
   event is ready; with `wait=false` it returns `ok=false` when none is pending.
2. **`Events()`** — a convenience channel. It launches a goroutine that blocks on
   `NextEvent(true)` and forwards events until the instance stops. Stop consuming
   before calling `Close()`: destroying the instance while a goroutine is blocked
   in the native `select()` is racy.
3. **`Descriptor()`** — the underlying fd (Unix), which becomes readable when an
   event is pending. Integrate it with your own poller and then call
   `NextEvent(false)`; this is the way to fold NORM into an existing event loop.

Common events: `TxObjectSent`, `TxObjectPurged`, `TxQueueEmpty` (sender side);
`RemoteSenderNew`, `RxObjectNew`, `RxObjectUpdated`, `RxObjectCompleted` (receiver
side). Use `EventTypeString` for names.

## Ownership: owned vs unowned handles

NORM uses reference counting for objects and nodes. This binding mirrors that with
an `owned` flag:

- Handles returned by **send operations** (`DataEnqueue`, `FileEnqueue`,
  `StreamOpen`) are **owned**: their `Close()` releases the NORM reference.
- Handles delivered by **events** (`Event.Object()`, `Event.Sender()`) are
  **unowned borrows**, valid only while handling that event. Their `Close()` is a
  no-op. To keep one past the event, call `Retain()`, which returns an owned
  wrapper you must later `Close()`.

```go
case norm.RxObjectNew:
    obj := ev.Object()             // unowned; valid this event only
    keep := obj.Retain()           // owned; survives future events
    defer keep.Close()             // releases the retained reference
```

## Data buffer lifetime (important)

`NormDataEnqueue` does **not** copy the payload — it retains the caller's pointer
until it purges the object. cgo forbids handing Go memory to C to hold across
calls, so `Session.DataEnqueue` copies the payload into C memory and tracks it.
You must free that memory when NORM is done with it by calling
`Session.ReleasePurged(ev.Object())` on `TxObjectPurged` events:

```go
obj, _ := sess.DataEnqueue(payload, nil)
for ev := range inst.Events() {
    if ev.Type == norm.TxObjectPurged {
        sess.ReleasePurged(ev.Object())   // frees the C buffer
    }
}
```

`Session.Close` frees any still-pending buffers as a backstop, but relying on
`ReleasePurged` keeps memory bounded for long-lived senders. `FileEnqueue` and
`StreamOpen` have no such concern (NORM reads files itself; stream writes are
copied during the call).

Received data (`Object.Data()`) is copied out of NORM-managed memory into a fresh
Go slice, so it remains valid after the object is released.

## Errors

Fallible operations return a Go `error` wrapping one of the sentinels in
`error.go` (`ErrOperationFailed`, `ErrInvalidHandle`, `ErrWrongObjectType`); use
`errors.Is` to classify. Configuration setters that NORM reports as failing return
a wrapped `ErrOperationFailed` naming the underlying call.

## Streams and the io adapters

`Session.StreamOpen` returns a stream `Object`. Wrap it for idiomatic I/O:

- `NewStreamWriter(obj, flushMode)` implements `io.Writer`/`io.Closer`. `Write`
  loops until all bytes are accepted (NORM stream writes are subject to flow
  control and may accept fewer than requested). `Close` closes the stream
  gracefully.
- `NewStreamReader(obj)` implements `io.Reader`. On a detected stream break it
  resynchronizes to the next message and returns `io.ErrUnexpectedEOF` so the
  caller can react; otherwise it returns the bytes read (possibly 0 when no data
  is currently buffered — wait for the next `RxObjectUpdated` event and read
  again).

## Dropping to the raw API

Anything not covered by `norm` is reachable through `normsys` using the handle
accessors (`Session.Handle()`, `Object.Handle()`, etc.):

```go
import "github.com/USNavalResearchLaboratory/norm/src/go/normsys"

normsys.SetGrttProbingMode(sess.Handle(), normsys.ProbeActive)
```
