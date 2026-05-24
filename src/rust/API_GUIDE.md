# NORM Rust Bindings API Guide

This guide provides a detailed overview of the main components of the NORM Rust bindings API.

## Core Components

The NORM Rust API follows a hierarchical structure similar to the underlying C API but with
Rust idioms and safety guarantees:

```
Instance
  └── Session
       ├── Object (Data)
       ├── Object (File)
       └── Object (Stream)
```

### Instance

`Instance` is the top-level object that represents a NORM protocol instance. It's the starting point
for all NORM operations.

```rust
use norm::Instance;

// Create a new NORM instance
let instance = Instance::new(false)?; // false = no priority boost

// Set cache directory for receiving files
instance.set_cache_directory("/tmp/norm")?;

// Process events
for event in instance.events() {
    // Handle events
}
```

### Session

A `Session` represents a NORM protocol session, which can operate in sender mode, receiver mode, or both.

```rust
use norm::{Instance, Session};

let instance = Instance::new(false)?;

// Create a session with address, port, and node ID
let session = instance.create_session("224.1.2.3", 6003, 1)?;

// Configure session
session
    .set_tx_rate(1_000_000.0) // 1 Mbps
    .set_ttl(64)?
    .set_loopback(true)?;

// Start sender
session.start_sender(
    rand::random(), // Session ID
    1024 * 1024,    // Buffer space (1 MB)
    1400,           // Segment size
    64,             // Data segments per block
    16,             // Parity segments per block
    None,           // FEC ID (default = 0)
)?;

// Or start receiver
session.start_receiver(1024 * 1024)?;
```

### Objects

NORM supports three types of objects for data transfer:

1. **Data Objects**: For memory buffer transfers
2. **File Objects**: For file transfers
3. **Stream Objects**: For continuous data streaming

```rust
// Send data
let data = b"Hello, NORM!";
let data_obj = session.data_enqueue(data, Some(b"Info"))?;

// Send file
let file_obj = session.file_enqueue("/path/to/file.txt", Some(b"file.txt"))?;

// Open stream
let stream_obj = session.stream_open(64 * 1024, Some(b"Stream info"))?;
```

### Events

NORM uses an event-based model for signaling state changes. The Rust bindings provide an iterator-based approach for event handling.

```rust
// Process events using iterator
for event in instance.events() {
    match event.event_type {
        EventType::RxObjectCompleted => {
            let object = Object::from_handle_unowned(event.object);

            // Handle based on object type
            match object.get_type() {
                ObjectType::Data => {
                    if let Ok(data) = object.access_data() {
                        println!("Received data: {:?}", data);
                    }
                },
                ObjectType::File => {
                    if let Ok(info) = object.get_info() {
                        println!("Received file: {}", String::from_utf8_lossy(&info));
                    }
                },
                _ => {}
            }
        },
        EventType::TxObjectSent => println!("Object sent"),
        _ => {} // Handle other events
    }
}
```

## Multicast Configuration

The Rust bindings provide an ergonomic API for configuring multicast:

```rust
use norm::{multicast, MulticastExt};

// Configure multicast with builder pattern
let config = multicast!("224.1.2.3", 6003, {
    ttl: 64,
    interface: "eth0",
    loopback: true,
    ssm_source: "192.168.1.1",
});

// Apply configuration to session
session.with_multicast(&config)?;
```

## Error Handling

All operations that might fail return a `Result<T, Error>` type:

```rust
match instance.set_cache_directory("/nonexistent/path") {
    Ok(()) => println!("Cache directory set"),
    Err(e) => match e {
        Error::FileError(msg) => println!("File error: {}", msg),
        Error::InvalidParameter => println!("Invalid parameter"),
        _ => println!("Other error: {:?}", e),
    }
}
```

## Ownership and Lifetimes

The Rust bindings use RAII (Resource Acquisition Is Initialization) to ensure proper resource management:

- `Instance`, `Session`, and `Object` implement `Drop` to automatically clean up resources
- Objects created directly have ownership and will be freed when dropped
- Objects obtained from events are not owned and are marked as such

```rust
// Owned object from direct creation
let data_obj = session.data_enqueue(data, None)?;
// Will be released when data_obj goes out of scope

// Non-owned object from event
let object = Object::from_handle_unowned(event.object);
// Will NOT be released when object goes out of scope
```

## Utility Functions

The API provides several utility functions:

```rust
// Check if an address is a multicast address
let is_mcast = is_multicast_address("224.1.2.3");

// Get NORM version
let (major, minor, patch) = norm::version();
```

## Advanced Features

### Custom Memory Allocation

```rust
unsafe {
    instance.set_allocation_functions(
        my_alloc_function,
        my_free_function
    );
}
```

### File Transfers

When receiving files, you must set a cache directory:

```rust
instance.set_cache_directory("/tmp/norm_files")?;
```

### Stream Management

For stream objects, additional methods are available on the `Object` type:

```rust
// Open a stream
let stream = session.stream_open(64 * 1024, Some(b"Stream info"))?;

// Write to stream
let bytes_written = stream.stream_write(b"Hello, stream!")?;

// Check if stream has space for more data
if stream.stream_has_vacancy()? {
    stream.stream_write(b"More data")?;
}

// Mark end of message
stream.stream_mark_eom()?;

// Flush stream with end-of-message marker
stream.stream_flush(true, FlushMode::Passive)?;

// Read from stream (receiver side)
let mut buffer = vec![0u8; 1024];
let bytes_read = stream.stream_read(&mut buffer)?;

// Seek to next message start
if stream.stream_seek_msg_start()? {
    println!("Found next message");
}

// Close stream gracefully
stream.stream_close(true)?;
```

## Best Practices

1. **Always check return values** - Use the `?` operator or explicitly handle errors
2. **Process all events** - Use the event iterator to process all events
3. **Close resources properly** - Let RAII handle cleanup or explicitly call `stop_sender()`/`stop_receiver()`
4. **Configure multicast correctly** - Use the ergonomic multicast API
5. **Use appropriate buffer sizes** - Match buffer sizes to your application's needs