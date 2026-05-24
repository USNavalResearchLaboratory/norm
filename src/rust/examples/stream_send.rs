use norm::{Instance, multicast, MulticastExt, EventType, Result, FlushMode};
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

    println!("Opening NORM stream...");

    // Open a stream with 64KB buffer
    let stream = session.stream_open(64 * 1024, Some(b"Example stream"))?;

    // Send multiple messages through the stream
    for i in 1..=5 {
        let message = format!("Stream message #{}", i);
        println!("Sending: {}", message);

        // Write to stream
        let bytes_written = stream.stream_write(message.as_bytes())?;
        println!("Wrote {} bytes", bytes_written);

        // Mark end of message
        stream.stream_mark_eom()?;

        // Small delay between messages
        thread::sleep(Duration::from_millis(100));
    }

    println!("Flushing stream...");

    // Flush the stream with end-of-message marker
    stream.stream_flush(true, FlushMode::Active)?;

    // Wait for transmission to complete
    let mut tx_complete = false;
    for event in instance.events() {
        match event.event_type {
            EventType::TxObjectSent => {
                println!("Stream object sent");
            }
            EventType::TxFlushCompleted => {
                println!("Stream flush completed");
                tx_complete = true;
            }
            EventType::TxQueueEmpty => {
                if tx_complete {
                    println!("Transmission queue empty");
                    break;
                }
            }
            _ => {}
        }
    }

    println!("Closing stream gracefully...");
    stream.stream_close(true)?;

    // Clean up
    session.stop_sender();

    println!("Stream send example completed");

    Ok(())
}
