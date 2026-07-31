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

## Prerequisites

- Go 1.21+ and a C/C++ toolchain (cgo is required; NORM is a C++ library).
- `pkg-config`.
- The native NORM library (`libnorm`) available to `pkg-config` and the linker.

## Building NORM and the bindings together (waf)

From the repository root:

```sh
./waf configure --build-go        # add --go-test to also run the Go tests
./waf build
```

This builds `libnorm`, writes a build-tree `norm.pc` into `build/pkgconfig/`, and
runs `go build ./...` with `PKG_CONFIG_PATH` and the loader path pointed at the
in-tree `build/` output. No manual environment setup is needed for this path.

## Using the bindings from another Go module (`go get`)

Because NORM is a C library, consumers need `libnorm` present at build time. The
binding finds it via pkg-config (`#cgo pkg-config: norm`), so the low-lift recipe
for a downstream service is:

1. Install NORM so `norm.pc` lands on the system `PKG_CONFIG_PATH`:
   ```sh
   ./waf configure && ./waf build && ./waf install   # installs libnorm + norm.pc
   ```
   (or install NORM from your OS/package manager if available).
2. In the consuming module:
   ```sh
   go get github.com/USNavalResearchLaboratory/norm/src/go/norm
   CGO_ENABLED=1 go build ./...
   ```

If NORM is **not** installed (e.g. you are building against a checked-out repo),
point pkg-config at the build tree instead of installing:

```sh
export PKG_CONFIG_PATH=/path/to/norm/build/pkgconfig
CGO_ENABLED=1 go build ./...
```

### CI note

This works cleanly in a CI job that builds NORM (or installs a NORM package) in
the same image before `go build`. Set `PKG_CONFIG_PATH` to the build tree, or run
`./waf install` so the default pkg-config search path finds `norm.pc`.

## Runtime library path

By default the binding links `libnorm` dynamically, so the shared library must be
locatable by the dynamic loader at runtime:

- Install NORM to a system library directory (`./waf install`), **or**
- set `LD_LIBRARY_PATH` (Linux) / `DYLD_LIBRARY_PATH` (macOS) to the directory
  containing `libnorm` and `libprotokit`, e.g. `build:build/protolib`.

To avoid a runtime library-path requirement entirely, link statically with
`-tags` / `CGO_LDFLAGS` using `pkg-config --static norm` (which now emits the full
`-lprotokit -lstdc++ -lpthread` dependency list).

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

    obj, err := sess.DataEnqueue([]byte("hello NORM"), nil)
    if err != nil {
        log.Fatal(err)
    }
    _ = obj

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

See `examples/` for runnable data, stream, and file send/receive programs, and
`API_GUIDE.md` for a deeper walkthrough of ownership and the event model.

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
portable (it runs as part of `./waf build --go-test`):

```sh
CGO_ENABLED=1 go test ./...
```

Reliable-transfer round-trip tests (data and stream) that exercise real
multicast loopback are gated behind the `norm_integration` build tag, since
loopback delivery is not reliably available in constrained CI containers:

```sh
CGO_ENABLED=1 go test -tags norm_integration ./norm/
```

Both require the loader path to point at the built libraries (see *Runtime
library path* above).

