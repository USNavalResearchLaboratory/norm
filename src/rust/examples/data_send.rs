use norm::{Instance, multicast, MulticastExt, EventType, Result};
use std::time::Duration;
use std::{thread, env};

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

    // Create a session with the specified address and port
    let session = instance.create_session(address, port, 1)?;

    // Configure multicast settings using our ergonomic API
    let mc_config = multicast!(address, port, {
        ttl: 64,
        loopback: true,
    });
    session.with_multicast(&mc_config)?;

    // Set the transmission rate (in bits per second)
    session.set_tx_rate(1_000_000.0);

    // Start the sender with 1MB buffer, 1400 byte segments, 64 data segments per block, 16 parity segments
    let session_id = rand::random::<u16>();
    session.start_sender(session_id, 1024 * 1024, 1400, 64, 16, None)?;

    // Prepare some data to send
    let data = b"Hello, NORM! This is a test message sent using the Rust bindings.";
    let info = b"Example data message";

    println!("Sending data: {:?}", String::from_utf8_lossy(data));

    // Enqueue the data for transmission
    let _data_obj = session.data_enqueue(data, Some(info))?;

    println!("Data enqueued, waiting for transmission to complete...");

    // Event loop - wait for transmission to complete
    for event in instance.events() {
        match event.event_type {
            EventType::TxObjectSent => {
                println!("Object sent successfully");
            }
            EventType::TxQueueEmpty => {
                println!("Transmission queue empty");
                // Short delay before exiting to allow any final processing
                thread::sleep(Duration::from_millis(500));
                break;
            }
            _ => {
                // Ignore other events
            }
        }
    }

    // Clean up (this happens automatically via Drop, but being explicit)
    session.stop_sender();

    println!("Data send example completed");

    Ok(())
}