use norm::{Instance, Session, multicast, MulticastExt, EventType, ObjectType, FlushMode, Result};
use std::time::Duration;
use std::{thread, env};
use std::str;

// This example demonstrates a simple stream sender and receiver in one program
// It creates both sender and receiver threads for demonstration purposes
fn main() -> Result<()> {
    let address = "224.1.2.3";
    let port = 6003;

    println!("NORM Stream Example");
    println!("-------------------");
    println!("Using multicast address: {}:{}", address, port);

    // Spawn receiver thread
    let receiver_thread = thread::spawn(move || {
        if let Err(e) = run_receiver(address, port) {
            eprintln!("Receiver error: {:?}", e);
        }
    });

    // Give the receiver a moment to start
    thread::sleep(Duration::from_millis(500));

    // Run sender in the main thread
    if let Err(e) = run_sender(address, port) {
        eprintln!("Sender error: {:?}", e);
    }

    // Wait for receiver to finish
    receiver_thread.join().expect("Receiver thread panicked");

    println!("Stream example completed");
    Ok(())
}

fn run_sender(address: &str, port: u16) -> Result<()> {
    // Create NORM instance and session for sender
    let instance = Instance::new(false)?;
    let session = instance.create_session(address, port, 1)?;

    // Configure multicast
    let mc_config = multicast!(address, port, {
        ttl: 64,
        loopback: true,
    });
    session.with_multicast(&mc_config)?;

    // Set transmission rate
    session.set_tx_rate(1_000_000.0);

    // Start sender
    let session_id = rand::random::<u16>();
    session.start_sender(session_id, 1024 * 1024, 1400, 64, 16, None)?;

    // Open a stream with 64KB buffer
    let stream_buffer_size = 64 * 1024;
    let info = b"Example stream";
    let stream = session.stream_open(stream_buffer_size, Some(info))?;

    println!("Stream opened, sending messages...");

    // Send 10 messages through the stream
    for i in 1..=10 {
        let message = format!("Stream message #{}", i);
        println!("Sending: {}", message);

        // Write message to stream
        let bytes_written = unsafe {
            // Using the raw API here for simplicity
            // In a real implementation, you'd want to create a safer wrapper
            norm_sys::NormStreamWrite(
                stream.handle(),
                message.as_ptr() as *const i8,
                message.len() as u32,
            )
        };

        if bytes_written < message.len() as u32 {
            println!("Warning: Only wrote {} of {} bytes", bytes_written, message.len());
        }

        // Mark end of message and flush passively
        unsafe {
            norm_sys::NormStreamMarkEom(stream.handle());
            norm_sys::NormStreamFlush(stream.handle(), false, norm_sys::NormFlushMode_NORM_FLUSH_PASSIVE);
        }

        // Small delay between messages
        thread::sleep(Duration::from_millis(500));
    }

    // Close the stream gracefully
    unsafe {
        norm_sys::NormStreamClose(stream.handle(), true);
    }

    println!("Stream closed, waiting for transmission to complete...");

    // Wait for a short time to ensure all data is flushed
    thread::sleep(Duration::from_secs(2));

    // Clean up
    session.stop_sender();

    Ok(())
}

fn run_receiver(address: &str, port: u16) -> Result<()> {
    // Create NORM instance and session for receiver
    let instance = Instance::new(false)?;
    let session = instance.create_session(address, port, 2)?;

    // Configure multicast
    let mc_config = multicast!(address, port, {
        ttl: 64,
        loopback: true,
    });
    session.with_multicast(&mc_config)?;

    // Start receiver
    session.start_receiver(1024 * 1024)?;

    println!("Receiver started, waiting for stream data...");

    // Variables to track the current stream object
    let mut current_stream = None;
    let mut message_count = 0;
    let mut timeout_count = 0;

    // Event loop
    for event in instance.events() {
        match event.event_type {
            EventType::RemoteSenderNew => {
                println!("New stream sender detected");
            }
            EventType::RxObjectNew => {
                let object = norm::Object::from_handle_unowned(event.object);
                if object.get_type() == ObjectType::Stream {
                    println!("New stream object received");

                    // Store the stream object
                    current_stream = Some(event.object);

                    // Check for info
                    if object.has_info() {
                        if let Ok(info) = object.get_info() {
                            println!("Stream info: {}", String::from_utf8_lossy(&info));
                        }
                    }
                }
            }
            EventType::RxObjectUpdated => {
                // Stream data is available to read
                if let Some(stream) = current_stream {
                    if event.object == stream {
                        // Read from the stream
                        let mut buffer = vec![0u8; 1024];
                        let mut bytes_read = 0u32;

                        let success = unsafe {
                            norm_sys::NormStreamRead(
                                stream,
                                buffer.as_mut_ptr() as *mut i8,
                                &mut bytes_read as *mut u32,
                            )
                        };

                        if success && bytes_read > 0 {
                            buffer.truncate(bytes_read as usize);
                            println!("Received: {}", String::from_utf8_lossy(&buffer));
                            message_count += 1;

                            // Reset timeout counter when we get data
                            timeout_count = 0;
                        }
                    }
                }
            }
            EventType::RxObjectCompleted => {
                if let Some(stream) = current_stream {
                    if event.object == stream {
                        println!("Stream completed, received {} messages", message_count);

                        // Exit when stream is completed
                        return Ok(());
                    }
                }
            }
            _ => {
                // Check for timeout
                timeout_count += 1;
                if timeout_count > 10000 {
                    // If we haven't seen any activity for a while, exit
                    if message_count > 0 {
                        println!("Stream timeout after receiving {} messages", message_count);
                        return Ok(());
                    }
                    timeout_count = 0;
                }
            }
        }
    }

    Ok(())
}