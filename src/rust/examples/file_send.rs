use norm::{Instance, Session, multicast, MulticastExt, EventType, Result};
use std::time::Duration;
use std::{thread, env, path::Path};

fn main() -> Result<()> {
    // Parse command line arguments
    let args: Vec<String> = env::args().collect();
    if args.len() < 2 {
        println!("Usage: {} <file_path> [address] [port]", args[0]);
        return Err(norm::Error::InvalidParameter);
    }

    let file_path = &args[1];
    let address = if args.len() > 2 { &args[2] } else { "224.1.2.3" };
    let port = if args.len() > 3 { args[3].parse::<u16>().unwrap_or(6003) } else { 6003 };

    // Verify the file exists
    if !Path::new(file_path).exists() {
        println!("File not found: {}", file_path);
        return Err(norm::Error::FileError("File not found".to_string()));
    }

    // Create a new NORM instance
    let instance = Instance::new(false)?;

    // Create a session with the specified address and port
    let session = instance.create_session(address, port, 1)?;

    // Configure multicast settings using our ergonomic API
    let mc_config = multicast!(address, port, {
        ttl: 64,
        loopback: true,
    });
    session.with_multicast(&mc_config)?;

    // Set the transmission rate (in bits per second)
    session.set_tx_rate(25_000_000.0); // 25 Mbps

    // Start the sender
    let session_id = rand::random::<u16>();
    session.start_sender(session_id, 4 * 1024 * 1024, 1400, 64, 16, None)?;

    // Get just the file name to use as info data
    let file_name = Path::new(file_path)
        .file_name()
        .map(|name| name.to_string_lossy().to_string())
        .unwrap_or_else(|| "unknown".to_string());

    println!("Sending file: {} ({})", file_path, file_name);

    // Enqueue the file for transmission with the file name as info
    let file_obj = session.file_enqueue(file_path, Some(file_name.as_bytes()))?;

    println!("File enqueued, waiting for transmission to complete...");

    // Event loop - wait for transmission to complete
    for event in instance.events() {
        match event.event_type {
            EventType::TxObjectSent => {
                println!("File sent successfully");
            }
            EventType::TxFlushCompleted => {
                println!("Transmission flush completed");
                break;
            }
            EventType::RemoteSenderNew => {
                println!("Receiver joined");
            }
            _ => {
                // Ignore other events
            }
        }
    }

    // Clean up
    session.stop_sender();

    println!("File send example completed");

    Ok(())
}