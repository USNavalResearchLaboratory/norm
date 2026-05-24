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

    println!("NORM receiver started on {}:{}", address, port);
    println!("Waiting for data...");

    // Event loop - wait for data to be received
    for event in instance.events() {
        match event.event_type {
            EventType::RemoteSenderNew => {
                println!("New sender connected");
            }
            EventType::RxObjectNew => {
                println!("New object received, waiting for completion...");
            }
            EventType::RxObjectCompleted => {
                // Check if it's a data object
                let obj_handle = event.object;
                let object = norm::Object::from_handle_unowned(obj_handle);

                if object.get_type() == ObjectType::Data {
                    // Access the data
                    if let Ok(data) = object.access_data() {
                        println!("Received data: {:?}", String::from_utf8_lossy(data));

                        // Check if there's info data
                        if object.has_info() {
                            if let Ok(info) = object.get_info() {
                                println!("Info data: {:?}", String::from_utf8_lossy(&info));
                            }
                        }

                        // Exit after receiving one data object
                        break;
                    } else {
                        println!("Failed to access data");
                    }
                }
            }
            _ => {
                // Ignore other events
            }
        }
    }

    // Clean up (this happens automatically via Drop, but being explicit)
    session.stop_receiver();

    println!("Data receive example completed");

    Ok(())
}