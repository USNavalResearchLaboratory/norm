use norm::{Instance, Session, multicast, MulticastExt, EventType, ObjectType, Result};
use std::time::Duration;
use std::{thread, env, path::Path};

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

    // Set the cache directory for file reception
    // IMPORTANT: This is required for receiving file objects
    let cache_dir = "/tmp/norm";
    println!("Setting cache directory to: {}", cache_dir);
    instance.set_cache_directory(cache_dir)?;

    // Create a session with the specified address and port
    let session = instance.create_session(address, port, 2)?;

    // Configure multicast settings using our ergonomic API
    let mc_config = multicast!(address, port, {
        ttl: 64,
        loopback: true,
    });
    session.with_multicast(&mc_config)?;

    // Start the receiver with 4MB buffer
    session.start_receiver(4 * 1024 * 1024)?;

    println!("NORM receiver started on {}:{}", address, port);
    println!("Waiting for file...");

    // Event loop - wait for file to be received
    for event in instance.events() {
        match event.event_type {
            EventType::RemoteSenderNew => {
                println!("New sender connected");
            }
            EventType::RxObjectNew => {
                let object = norm::Object::from_handle_unowned(event.object);
                if object.get_type() == ObjectType::File {
                    println!("New file being received...");

                    // Try to get info data which should contain the file name
                    if let Ok(info) = object.get_info() {
                        if !info.is_empty() {
                            println!("File name: {}", String::from_utf8_lossy(&info));
                        }
                    }
                }
            }
            EventType::RxObjectCompleted => {
                let object = norm::Object::from_handle_unowned(event.object);
                if object.get_type() == ObjectType::File {
                    println!("File received successfully!");

                    // The file will be in the cache directory
                    // You can find out its name from the info data
                    if let Ok(info) = object.get_info() {
                        if !info.is_empty() {
                            let filename = String::from_utf8_lossy(&info);
                            let filepath = format!("{}/{}", cache_dir, filename);
                            println!("File saved to: {}", filepath);

                            // Report file size
                            println!("File size: {} bytes", object.size());

                            // Exit after receiving one file
                            break;
                        }
                    }
                }
            }
            _ => {
                // Ignore other events
            }
        }
    }

    // Clean up
    session.stop_receiver();

    println!("File receive example completed");

    Ok(())
}