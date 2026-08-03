# NORM Go Bindings

Go bindings for the NORM (NACK-Oriented Reliable Multicast) protocol library,
built with cgo. Two packages are provided:

- **`normsys`** — a low-level, near 1:1 binding of the NORM C API
  (`include/normApi.h`). Handles are pointer-backed named types; functions take
  and return Go types. Use it as an escape hatch for API calls the idiomatic
  package does not yet wrap.
- **`norm`** — an idiomatic wrapper with resource-owning types (`Instance`,
  `Session`, `Object`, `Node`), Go errors, and `io.Reader`/`io.Writer` stream
  adapters. Most applications should use this package.

## What to understand first: this is cgo, not pure Go

NORM is a **C++ library**, and these bindings use **cgo**. That makes them
fundamentally different from a pure-Go transport such as `quic-go`, and it drives
almost every setup decision below:

- You cannot build the code paths that import `norm` with `CGO_ENABLED=0`.
- The **build host** needs a C/C++ toolchain, `pkg-config`, and `libnorm`.
- The **runtime host** needs `libnorm`/`libprotokit` available to the dynamic
  loader — unless you link statically.
- A `FROM scratch` container image will **not** run a dynamically linked NORM
  binary (no loader, no libc).

None of this is hard; it just has to be handled deliberately. The rest of this
document is about doing that with the least friction.

## Prerequisites

- Go 1.21+ and a C/C++ toolchain (cgo is required).
- `pkg-config`.
- The native NORM library (`libnorm`) discoverable by `pkg-config` and the linker
  (see the next two sections).

## Building NORM and the bindings together (waf)

From the repository root:

```sh
./waf configure --build-go        # add --go-test to also run the Go tests
./waf build
```

This builds `libnorm`, writes a build-tree `norm.pc` into `build/pkgconfig/`, and
runs `go build ./...` with `PKG_CONFIG_PATH` and the loader path pointed at the
in-tree `build/` output. No manual environment setup is needed for this path.

## Making `libnorm` discoverable at build time

The binding's cgo directive is `#cgo pkg-config: norm`, so `go build` must be able
to find a `norm.pc` on `PKG_CONFIG_PATH`. There are two ways.

### Option 1 — install NORM (recommended for shared/CI images)

```sh
git clone --recurse-submodules <norm-repo>
cd norm
./waf configure
./waf build
./waf install     # installs libnorm + headers + norm.pc to the prefix (default /usr/local)
```

After install, `pkg-config --cflags --libs norm` works with no environment
variables, and consuming code builds with just `CGO_ENABLED=1 go build ./...`.

### Option 2 — point at a build tree (no install)

```sh
cd norm && ./waf configure --build-go && ./waf build
export PKG_CONFIG_PATH=/path/to/norm/build/pkgconfig
CGO_ENABLED=1 go build ./...
```

`./waf build` (after `--build-go`) writes `build/pkgconfig/norm.pc` pointing at
the in-tree libraries.

> **Note:** two `norm.pc` files exist after a build. `build/pkgconfig/norm.pc`
> (from `--build-go`) points at the build tree; `build/norm.pc` (from
> `norm.pc.in`) targets the *install* prefix and only resolves after `./waf
> install`. Point `PKG_CONFIG_PATH` at whichever matches your situation.

> **Sanity check anytime:** `pkg-config --cflags --libs norm` should print a real
> include path and `-lnorm`. `pkg-config --static --libs norm` should additionally
> list `-lprotokit -lstdc++ -lpthread` (plus `-lresolv` on macOS).

## Using the bindings from another Go module (`go get`)

Because NORM is a C library, consumers need `libnorm` present at build time. The
binding resolves it via pkg-config, so the recipe is:

1. Make `norm.pc` discoverable — either `./waf install` NORM (Option 1 above) or
   `export PKG_CONFIG_PATH=/path/to/norm/build/pkgconfig` (Option 2).
2. In the consuming module:
   ```sh
   go get github.com/USNavalResearchLaboratory/norm/src/go/norm
   CGO_ENABLED=1 go build ./...
   ```

Import paths:

- `github.com/USNavalResearchLaboratory/norm/src/go/norm` — idiomatic API (use this)
- `github.com/USNavalResearchLaboratory/norm/src/go/normsys` — raw escape hatch

If your project vendors dependencies (`go mod vendor`), the Go source vendors like
any module — but **the native `libnorm` is not vendored**; it is still resolved at
build time via pkg-config.

### In CI

Provision NORM **before** `go build`/`go test`. Sketch (adapt to your CI):

```yaml
steps:
  - run: apt-get install -y build-essential pkg-config   # C++ toolchain + pkg-config
  - run: |
      git clone --recurse-submodules <norm-repo> /tmp/norm
      cd /tmp/norm && ./waf configure && ./waf build && ./waf install
      ldconfig                                            # refresh loader cache (Linux)
  - run: CGO_ENABLED=1 go build ./...
  - run: CGO_ENABLED=1 go test ./...
```

Pin the NORM version (git tag/commit) so the linked ABI is reproducible. If your
project also has a `CGO_ENABLED=0` build or test gate, keep the NORM-importing
code behind a build tag (e.g. `//go:build norm`) so that gate still compiles
without cgo. (The repo's own Go CI lives in `.github/workflows/go.yml` and is a
concrete working example.)

## Runtime library path

By default the binding links `libnorm` dynamically, so the shared library must be
locatable by the dynamic loader at runtime:

- Install NORM to a system library directory (`./waf install`, then `ldconfig` on
  Linux), **or**
- set `LD_LIBRARY_PATH` (Linux) / `DYLD_LIBRARY_PATH` (macOS) to the directory
  containing `libnorm` and `libprotokit`, e.g. `build:build/protolib`.

To avoid a runtime library-path requirement entirely, link statically (see below).

## Deployment and packaging

Dynamic linking is the default, so a runtime image must provide the shared
libraries and a dynamic loader. Common approaches:

**Slim base image + shipped libraries** — simplest:

```dockerfile
FROM debian:stable-slim
COPY --from=build /usr/local/lib/libnorm.so*     /usr/local/lib/
COPY --from=build /usr/local/lib/libprotokit.so* /usr/local/lib/
RUN ldconfig
COPY --from=build /app/yourprogram /yourprogram
ENTRYPOINT ["/yourprogram"]
```

**Static link** — a single self-contained binary with no runtime `.so`
dependency. Link `libnorm.a` + `libprotokit.a` + the C++ runtime:

```sh
CGO_ENABLED=1 \
CGO_LDFLAGS="$(pkg-config --static --libs norm)" \
go build -ldflags '-linkmode external -extldflags "-static"' ./...
```

Fully static cgo binaries are easiest with a musl (Alpine) toolchain; glibc static
linking has known limitations (e.g. NSS). Test the resulting binary in the target
image.

**Keep cgo isolated** — if part of your system must stay pure-Go (for example a
binary shipped `FROM scratch` or built `CGO_ENABLED=0`), a common pattern is to
run NORM in a **separate small cgo binary/sidecar** that bridges to the rest of
your system over gRPC, a Unix socket, or stdin/stdout. The pure-Go component then
has no cgo dependency at all.

> If you package with Nix, declare `norm` as a build input so `norm.pc` and the
> libraries are on the build sandbox paths.

## Quick example

```go
package main

import (
    "fmt"
    "log"

    "github.com/USNavalResearchLaboratory/norm/src/go/norm"
)

func main() {
    inst, err := norm.NewInstance(false)
    if err != nil {
        log.Fatal(err)
    }
    defer inst.Close()

    sess, err := inst.CreateSession("224.1.2.3", 6003, 1)
    if err != nil {
        log.Fatal(err)
    }
    defer sess.Close()

    sess.SetTxRate(1_000_000)
    if err := sess.StartSender(1234, 1<<20, 1400, 64, 16); err != nil {
        log.Fatal(err)
    }
    defer sess.StopSender()

    if _, err := sess.DataEnqueue([]byte("hello NORM"), nil); err != nil {
        log.Fatal(err)
    }

    for ev := range inst.Events() {
        switch ev.Type {
        case norm.TxObjectPurged:
            sess.ReleasePurged(ev.Object()) // free the enqueue buffer
        case norm.TxQueueEmpty:
            fmt.Println("done")
            return
        }
    }
}
```

`API_GUIDE.md` has a deeper walkthrough of ownership and the event model.

## Integrating into a long-running program

NORM is event-driven: an `Instance` owns a background thread and an event queue,
and `Session`s created from it send/receive `Object`s. The robust way to embed
this in a long-running program is a worker that owns the `Instance` and pumps
events with a **non-blocking loop tied to a `context.Context`** — so shutdown is
prompt and `Close()` never races a goroutine blocked in the native `select()`.

The worker below is a plain type with a `Run(ctx)` method and no framework
dependency, so it drops into a bare goroutine, an `errgroup.Group`, a worker pool,
or any task interface your program already uses.

```go
package normtransport

import (
    "context"

    "github.com/USNavalResearchLaboratory/norm/src/go/norm"
)

type Receiver struct {
    Address string
    Port    uint16
    NodeID  norm.NodeId
    Handle  func([]byte)
}

// Run owns the NORM instance for its lifetime and stops cleanly on ctx
// cancellation. It drives events with non-blocking NextEvent so a canceled
// context is observed promptly and Close() never races a blocked native select().
func (r *Receiver) Run(ctx context.Context) error {
    inst, err := norm.NewInstance(false)
    if err != nil {
        return err
    }
    defer inst.Close()

    sess, err := inst.CreateSession(r.Address, r.Port, r.NodeID)
    if err != nil {
        return err
    }
    defer sess.Close()

    if err := sess.StartReceiver(1 << 20); err != nil {
        return err
    }
    defer sess.StopReceiver()

    for {
        select {
        case <-ctx.Done():
            return ctx.Err()
        default:
        }
        ev, ok := inst.NextEvent(false) // non-blocking
        if !ok {
            // No event pending. Sleep briefly, or (better) block on the
            // descriptor with a timeout so cancellation stays responsive:
            //   fd := inst.Descriptor(); poll(fd, timeout)
            continue
        }
        if ev.Type == norm.RxObjectCompleted {
            if obj := ev.Object(); obj != nil && obj.Type() == norm.ObjectData {
                data, _ := obj.Data() // copied out; safe to keep after the event
                r.Handle(data)
            }
        }
    }
}
```

Sender side, minimal:

```go
sess.SetTxRate(10_000_000)
sess.StartSender(sessionID, 1<<20, 1400, 64, 16)
sess.DataEnqueue(payload, nil)
// ... in the event loop, free the enqueue buffer when NORM purges it:
case norm.TxObjectPurged:
    sess.ReleasePurged(ev.Object())
```

`examples/service` is a complete, dependency-free program implementing exactly
this pattern (receiver worker + sender with `ReleasePurged`) that self-checks a
round-trip; run it with `go run ./examples/service`.

## Correctness notes (read before shipping)

1. **Free enqueued data buffers.** `DataEnqueue` copies your payload into C memory
   that NORM retains until it purges the object. You **must** call
   `sess.ReleasePurged(ev.Object())` on `TxObjectPurged` events, or memory grows
   unbounded for a long-lived sender. `Session.Close` frees any leftovers as a
   backstop only. (Files and streams have no such concern.)

2. **Owned vs unowned handles.** Objects/nodes returned by send calls are
   *owned* — their `Close()` releases the NORM reference. Objects/nodes delivered
   by events are *unowned borrows*, valid only during that event; to keep one,
   call `Retain()` (returns an owned wrapper you must `Close()`). Do not `Close()`
   a borrowed handle.

3. **Don't `Close()` while another goroutine blocks in `NextEvent`.** Destroying
   the instance under a blocking native `select()` is racy. Use non-blocking
   `NextEvent(false)` (as above) or the `Descriptor()` fd with your own poller, and
   stop the event loop before `Close()`. Prefer this over the `Events()`
   convenience channel when you need deterministic shutdown.

4. **Received `Data()` is safe to keep.** It is copied out of NORM-managed memory
   into a fresh Go slice, so it remains valid after the object is released.

5. **File reception needs a cache directory.** Call `inst.SetCacheDirectory(dir)`
   before receiving `ObjectFile` objects, or they are dropped.

6. **Multicast plumbing.** Set an appropriate TTL (`SetTTL`); for a same-host
   sender+receiver, use `SetRxPortReuse(true)`, and on macOS loopback pin the
   interface with `SetMulticastInterface("lo0")`.

## Examples

Each subdirectory under `examples/` is a standalone `main` package:

```sh
# terminal 1 (receiver), then terminal 2 (sender)
go run ./examples/data_recv 224.1.2.3 6003
go run ./examples/data_send 224.1.2.3 6003
```

On macOS, same-host multicast loopback may require binding the multicast
interface to `lo0` (`Session.SetMulticastInterface("lo0")`); the examples set
loopback and rx-port reuse so a sender and receiver can share a host.

## Testing

The default test suite links `libnorm` but does not use the network, so it is
portable (it also runs as part of `./waf build --go-test`):

```sh
CGO_ENABLED=1 go test ./...
```

Reliable-transfer round-trip tests (data and stream) that exercise real multicast
loopback are gated behind the `norm_integration` build tag, since loopback
delivery is not reliably available in constrained CI containers:

```sh
CGO_ENABLED=1 go test -tags norm_integration ./...
```

Both require the loader path to point at the built libraries (see *Runtime
library path* above).

Smoke test that linking works at all:

```go
maj, min, patch := norm.Version() // prints the linked libnorm version
inst, err := norm.NewInstance(false)
```

## Troubleshooting

| Symptom | Cause / fix |
|---|---|
| `Package norm was not found in the pkg-config search path` | `norm.pc` not on `PKG_CONFIG_PATH`. Install NORM or export the build-tree `pkgconfig` dir. |
| `# pkg-config --cflags -- norm ... exit status 1` during `go build` | Same as above; also confirm `pkg-config` itself is installed. |
| `undefined reference` / linker errors for `Norm*` | Wrong/old `libnorm`, or an arch mismatch. Rebuild NORM for the target arch and pin the version. |
| `ld: library not found for -lprotokit` | Static build without protolib on the search path. Use `pkg-config --static --libs norm` for `CGO_LDFLAGS`. |
| `cannot open shared object file: libnorm.so` at startup | Dynamic lib not on the loader path. `ldconfig` / `LD_LIBRARY_PATH`, or link statically. |
| Binary won't start in `FROM scratch` | scratch has no loader/libc. Use a slim base image, link statically, or isolate NORM in a sidecar. |
| `CGO_ENABLED=0` build/gate fails after adding NORM | NORM-importing code isn't behind a build tag. Gate it (`//go:build norm`) so the pure-Go build stays clean. |
| Receiver gets nothing on same host | Missing `SetRxPortReuse(true)` / loopback config; on macOS, `SetMulticastInterface("lo0")`. |
| Sender memory grows over time | Not calling `ReleasePurged` on `TxObjectPurged` (see Correctness notes #1). |

## See also

- `API_GUIDE.md` — API semantics: ownership, the event model, streams.
- `examples/` — runnable data, stream, file, and service programs.
- `include/normApi.h`, `doc/NormDeveloperGuide.pdf` — the underlying C API.
