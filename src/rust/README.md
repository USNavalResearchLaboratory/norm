# NORM Rust Bindings

Safe, idiomatic Rust bindings for the [NORM](https://github.com/USNavalResearchLaboratory/norm) (NACK-Oriented Reliable Multicast) protocol library.

## Overview

These Rust bindings provide a safe and ergonomic interface to the NORM C library. NORM is a reliable multicast protocol developed by the US Naval Research Laboratory that provides end-to-end reliable transport of data over IP multicast or unicast.

## Features

- **RAII Resource Management**: Safe wrappers ensure proper cleanup of NORM resources
- **Ergonomic API**: Builder patterns for multicast configuration
- **Iterator-based Event Handling**: Simple event loops using Rust iterators
- **Type Safety**: Rust enums for NORM constants and options
- **Error Handling**: Rich error types with descriptive messages

## Installation

### Prerequisites

- Rust 1.70 or later
- NORM library (installed or available for linking)

### Building from Source

```bash
# Clone the NORM repository
git clone https://github.com/USNavalResearchLaboratory/norm.git
cd norm

# Build NORM with Rust bindings
./waf configure --build-rust
./waf build

# Or for release mode
./waf configure --build-rust --rust-release
./waf build
```

### Using as a Dependency

```toml
# Cargo.toml
[dependencies]
norm = "1.5.10"  # Replace with actual version
```

## Usage Examples

### Data Transfer

```rust
use norm::{Instance, multicast, MulticastExt, EventType, Result};

fn main() -> Result<()> {
    // Create a new NORM instance
    let instance = Instance::new(false)?;

    // Create a session
    let session = instance.create_session("224.1.2.3", 6003, 1)?;

    // Configure multicast with the ergonomic API
    let config = multicast!("224.1.2.3", 6003, {
        ttl: 64,
        loopback: true,
    });
    session.with_multicast(&config)?;

    // Start sender
    session
        .set_tx_rate(1_000_000.0)
        .start_sender(rand::random(), 1024*1024, 1400, 64, 16, None)?;

    // Send data
    let data = b"Hello, NORM!";
    session.data_enqueue(data, None)?;

    // Process events
    for event in instance.events() {
        if event.event_type == EventType::TxQueueEmpty {
            break;
        }
    }

    Ok(())
}
```

### Stream Transfer

```rust
use norm::{Instance, multicast, MulticastExt, FlushMode, Result};

fn main() -> Result<()> {
    let instance = Instance::new(false)?;
    let session = instance.create_session("224.1.2.3", 6003, 1)?;

    let config = multicast!("224.1.2.3", 6003, { ttl: 64, loopback: true });
    session.with_multicast(&config)?;

    session.set_tx_rate(1_000_000.0)
           .start_sender(rand::random(), 1024*1024, 1400, 64, 16, None)?;

    // Open a stream
    let stream = session.stream_open(64 * 1024, Some(b"My stream"))?;

    // Write multiple messages
    stream.stream_write(b"Message 1")?;
    stream.stream_mark_eom()?;

    stream.stream_write(b"Message 2")?;
    stream.stream_mark_eom()?;

    // Flush and close
    stream.stream_flush(true, FlushMode::Active)?;
    stream.stream_close(true)?;

    Ok(())
}
```

### Multicast Configuration

```rust
// Method 1: Builder pattern
let config = MulticastConfig::new("224.1.2.3", 6003)
    .ttl(64)
    .interface("eth0")
    .loopback(true);

// Method 2: Convenient macro
let config = multicast!("224.1.2.3", 6003, {
    ttl: 64,
    interface: "eth0",
    loopback: true,
});

// Apply to session
session.with_multicast(&config)?;
```

### Event Handling

```rust
// Iterator-based event handling
for event in instance.events() {
    match event.event_type {
        EventType::RxObjectCompleted => {
            let object = Object::from_handle_unowned(event.object);
            if let Ok(data) = object.access_data() {
                println!("Received: {:?}", data);
            }
        },
        EventType::TxObjectSent => println!("Object sent"),
        _ => {} // Ignore other events
    }
}
```

## Architecture

The bindings are split into two crates:

- **norm-sys**: Low-level FFI bindings generated with bindgen
- **norm**: Safe, idiomatic Rust wrappers with RAII and error handling

## API Documentation

For full API documentation, build the docs with:

```bash
cd src/rust
cargo doc --open
```

Or when using waf:

```bash
./waf configure --build-rust --rust-docs
./waf build
```

## Running Tests

Due to the way the NORM library is built, you may need to set the library path when running tests:

```bash
# On macOS
export DYLD_LIBRARY_PATH=/path/to/norm/build:/path/to/norm/build/protolib:$DYLD_LIBRARY_PATH
cargo test

# On Linux
export LD_LIBRARY_PATH=/path/to/norm/build:/path/to/norm/build/protolib:$LD_LIBRARY_PATH
cargo test

# Or run directly
DYLD_LIBRARY_PATH=/path/to/norm/build:/path/to/norm/build/protolib cargo test
```

Alternatively, use the waf build system which handles this automatically:

```bash
./waf configure --build-rust
./waf build
./waf test  # If test target is configured
```

## License

These bindings are distributed under the same BSD-3-Clause license as the NORM library itself.