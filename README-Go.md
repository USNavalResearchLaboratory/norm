# NORM Go Bindings

Go language bindings for NORM are provided under `src/go/`, alongside the Java,
Python, .NET, and Rust bindings. They are built with cgo and link against the
native NORM library via `pkg-config`.

## Layout

- `src/go/normsys` — low-level, near 1:1 cgo binding of `include/normApi.h`.
- `src/go/norm` — idiomatic wrapper (`Instance`/`Session`/`Object`/`Node`, Go
  errors, `io.Reader`/`io.Writer` stream adapters).
- `src/go/examples` — runnable data/stream/file send/receive programs.

## Building

With the waf build system from the repository root:

```sh
./waf configure --build-go     # add --go-test to run the Go tests too
./waf build
```

This builds `libnorm`, generates a build-tree `norm.pc`, and runs `go build`
against the freshly built library automatically.

## Using from another Go module

The binding locates `libnorm` through pkg-config (`#cgo pkg-config: norm`). A
downstream service can therefore:

```sh
# ensure libnorm + norm.pc are discoverable (install NORM, or point at the build tree)
./waf install                                   # installs libnorm and norm.pc
# ...or:  export PKG_CONFIG_PATH=/path/to/norm/build/pkgconfig

go get github.com/USNavalResearchLaboratory/norm/src/go/norm
CGO_ENABLED=1 go build ./...
```

At runtime the dynamic loader must find `libnorm`/`libprotokit` (install to a
system lib dir, set `LD_LIBRARY_PATH`/`DYLD_LIBRARY_PATH`, or link statically via
`pkg-config --static norm`).

See `src/go/README.md` and `src/go/API_GUIDE.md` for full details.
