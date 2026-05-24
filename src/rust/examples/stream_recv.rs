use norm::{Instance, multicast, MulticastExt, EventType, ObjectType, Result};
use std::env;

fn main() -> Result<()> {
    // Parse command line arguments
    let args: Vec<String> = env::args().collect();
    let (address, port) = if args.len() > 2 {
        (args[1].as_str(), args[2].parse::<u16>().unwrap_or(6003))
    } else {
        println!("Using default multicast address 224.1.2.3:6003");
        println!("Usage: {} <address> <port>", args[0]);
        ("224.1.2.3", 6003)
    };

    // Create a new NORM instance
    let instance = Instance::new(false)?;

    // Set the cache directory for file reception (required for file objects)
    instance.set_cache_directory("/tmp/norm")?;

    // Create a session with the specified address and port
    let session = instance.create_session(address, port, 2)?;

    // Configure multicast settings using our ergonomic API
    let mc_config = multicast!(address, port, {
        ttl: 64,
        loopback: true,
    });
    session.with_multicast(&mc_config)?;

    // Start the receiver with 1MB buffer
    session.start_receiver(1024 * 1024)?;

    println!("NORM stream receiver started on {}:{}", address, port);
    println!("Waiting for stream data...");

    let mut message_count = 0;
    let mut buffer = vec![0u8; 4096];

    // Event loop - wait for data to be received
    for event in instance.events() {
        match event.event_type {
            EventType::RemoteSenderNew => {
                println!("New sender connected");
            }
            EventType::RxObjectNew => {
                let obj_handle = event.object;
                let object = norm::Object::from_handle_unowned(obj_handle);

                if object.get_type() == ObjectType::Stream {
                    println!("New stream object received");

                    // Get info data if available
                    if object.has_info() {
                        if let Ok(info) = object.get_info() {
                            println!("Stream info: {:?}", String::from_utf8_lossy(&info));
                        }
                    }
                }
            }
            EventType::RxObjectUpdated => {
                let obj_handle = event.object;
                let object = norm::Object::from_handle_unowned(obj_handle);

                if object.get_type() == ObjectType::Stream {
                    // Try to read data from the stream
                    match object.stream_read(&mut buffer) {
                        Ok(bytes_read) if bytes_read > 0 => {
                            message_count += 1;
                            let data = &buffer[..bytes_read];
                            println!("Message #{}: {:?}", message_count, String::from_utf8_lossy(data));

                            // Check if we've received all expected messages
                            if message_count >= 5 {
                                println!("Received all expected messages");
                                break;
                            }
                        }
                        Ok(_) => {
                            // No data available yet
                        }
                        Err(e) => {
                            println!("Error reading from stream: {:?}", e);
                        }
                    }
                }
            }
            EventType::RxObjectCompleted => {
                println!("Stream object completed");
                break;
            }
            _ => {}
        }
    }

    // Clean up
    session.stop_receiver();

    println!("Stream receive example completed");

    Ok(())
}
