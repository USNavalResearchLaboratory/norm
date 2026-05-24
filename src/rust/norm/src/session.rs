use crate::error::{Error, Result, bool_result, check_handle, string_to_c_string};
use crate::types::*;
use crate::object::Object;
use norm_sys::*;
use std::os::raw::c_char;
use std::ptr;

/// NORM session handle with RAII semantics.
///
/// A Session represents a NORM protocol session, which can be used to send and receive data.
/// Sessions can operate in multicast or unicast mode, and can act as a sender, receiver, or both.
///
/// When dropped, the session will automatically clean up resources by calling `NormDestroySession`.
#[derive(Debug)]
pub struct Session {
    /// The raw NORM session handle
    handle: NormSessionHandle,
}

impl Session {
    /// Create a new NORM session.
    ///
    /// This is typically called through `Instance::create_session` rather than directly.
    ///
    /// # Arguments
    /// * `instance` - The NORM instance to create the session on
    /// * `session_address` - The multicast or unicast address for the session
    /// * `session_port` - The port number for the session
    /// * `local_node_id` - The local node ID (use `NORM_NODE_ANY` for automatic)
    ///
    /// # Returns
    /// A new NORM session or an error if the session could not be created
    pub(crate) fn new<A: AsRef<str>>(
        instance: NormInstanceHandle,
        session_address: A,
        session_port: u16,
        local_node_id: NodeId,
    ) -> Result<Self> {
        let c_addr = string_to_c_string(session_address.as_ref())?;
        let handle = unsafe {
            NormCreateSession(instance, c_addr.as_ptr(), session_port, local_node_id)
        };

        unsafe { check_handle(handle, NORM_SESSION_INVALID)? };
        Ok(Session { handle })
    }

    /// Start the session as a NORM sender
    ///
    /// # Arguments
    /// * `instance_id` - The instance ID (session identifier)
    /// * `buffer_space` - Buffer size in bytes for the sender's transmission queue
    /// * `segment_size` - Size in bytes of the FEC payload segments
    /// * `num_data` - Number of data segments per FEC block
    /// * `num_parity` - Number of parity segments per FEC block
    /// * `fec_id` - FEC encoding identifier (usually 0)
    ///
    /// # Returns
    /// `Ok(())` on success or an `Err` if the sender could not be started
    pub fn start_sender(
        &self,
        instance_id: SessionId,
        buffer_space: u32,
        segment_size: u16,
        num_data: u16,
        num_parity: u16,
        fec_id: Option<u8>,
    ) -> Result<&Self> {
        let fec_id = fec_id.unwrap_or(0);
        let success = unsafe {
            NormStartSender(
                self.handle,
                instance_id,
                buffer_space,
                segment_size,
                num_data,
                num_parity,
                fec_id,
            )
        };
        bool_result(success, "Failed to start NORM sender")?;
        Ok(self)
    }

    /// Stop the session as a sender
    pub fn stop_sender(&self) -> &Self {
        unsafe { NormStopSender(self.handle) };
        self
    }

    /// Start the session as a NORM receiver
    ///
    /// # Arguments
    /// * `buffer_space` - Buffer size in bytes for the receiver
    ///
    /// # Returns
    /// `Ok(())` on success or an `Err` if the receiver could not be started
    pub fn start_receiver(&self, buffer_space: u32) -> Result<&Self> {
        let success = unsafe { NormStartReceiver(self.handle, buffer_space) };
        bool_result(success, "Failed to start NORM receiver")?;
        Ok(self)
    }

    /// Stop the session as a receiver
    pub fn stop_receiver(&self) -> &Self {
        unsafe { NormStopReceiver(self.handle) };
        self
    }

    /// Set the transmission rate in bits per second
    ///
    /// # Arguments
    /// * `bits_per_second` - The transmission rate in bits per second
    ///
    /// # Returns
    /// A reference to self for method chaining
    pub fn set_tx_rate(&self, bits_per_second: f64) -> &Self {
        unsafe { NormSetTxRate(self.handle, bits_per_second) };
        self
    }

    /// Get the current transmission rate in bits per second
    ///
    /// # Returns
    /// The current transmission rate in bits per second
    pub fn tx_rate(&self) -> f64 {
        unsafe { NormGetTxRate(self.handle) }
    }

    /// Set the socket buffer size for transmission
    ///
    /// # Arguments
    /// * `buffer_size` - The socket buffer size in bytes
    ///
    /// # Returns
    /// `Ok(())` on success or an `Err` if the buffer size could not be set
    pub fn set_tx_socket_buffer(&self, buffer_size: u32) -> Result<&Self> {
        let success = unsafe { NormSetTxSocketBuffer(self.handle, buffer_size) };
        bool_result(success, "Failed to set TX socket buffer size")?;
        Ok(self)
    }

    /// Set the socket buffer size for reception
    ///
    /// # Arguments
    /// * `buffer_size` - The socket buffer size in bytes
    ///
    /// # Returns
    /// `Ok(())` on success or an `Err` if the buffer size could not be set
    pub fn set_rx_socket_buffer(&self, buffer_size: u32) -> Result<&Self> {
        let success = unsafe { NormSetRxSocketBuffer(self.handle, buffer_size) };
        bool_result(success, "Failed to set RX socket buffer size")?;
        Ok(self)
    }

    /// Set the flow control factor
    ///
    /// # Arguments
    /// * `flow_control_factor` - The flow control factor
    ///
    /// # Returns
    /// A reference to self for method chaining
    pub fn set_flow_control(&self, flow_control_factor: f64) -> &Self {
        unsafe { NormSetFlowControl(self.handle, flow_control_factor) };
        self
    }

    /// Enable or disable congestion control
    ///
    /// # Arguments
    /// * `enable` - Whether to enable congestion control
    /// * `adjust_rate` - Whether to adjust the rate based on congestion control
    ///
    /// # Returns
    /// A reference to self for method chaining
    pub fn set_congestion_control(&self, enable: bool, adjust_rate: bool) -> &Self {
        unsafe { NormSetCongestionControl(self.handle, enable, adjust_rate) };
        self
    }

    /// Set the transmission rate bounds
    ///
    /// # Arguments
    /// * `rate_min` - The minimum transmission rate in bits per second
    /// * `rate_max` - The maximum transmission rate in bits per second
    ///
    /// # Returns
    /// A reference to self for method chaining
    pub fn set_tx_rate_bounds(&self, rate_min: f64, rate_max: f64) -> &Self {
        unsafe { NormSetTxRateBounds(self.handle, rate_min, rate_max) };
        self
    }

    /// Set the multicast interface
    ///
    /// # Arguments
    /// * `interface_name` - The name of the interface to use for multicast
    ///
    /// # Returns
    /// `Ok(())` on success or an `Err` if the interface could not be set
    pub fn set_multicast_interface<I: AsRef<str>>(&self, interface_name: I) -> Result<&Self> {
        let c_iface = string_to_c_string(interface_name.as_ref())?;
        let success = unsafe { NormSetMulticastInterface(self.handle, c_iface.as_ptr()) };
        bool_result(success, "Failed to set multicast interface")?;
        Ok(self)
    }

    /// Set source-specific multicast (SSM) source address
    ///
    /// # Arguments
    /// * `source_address` - The source address for SSM
    ///
    /// # Returns
    /// `Ok(())` on success or an `Err` if the SSM source could not be set
    pub fn set_ssm<A: AsRef<str>>(&self, source_address: A) -> Result<&Self> {
        let c_addr = string_to_c_string(source_address.as_ref())?;
        let success = unsafe { NormSetSSM(self.handle, c_addr.as_ptr()) };
        bool_result(success, "Failed to set SSM source address")?;
        Ok(self)
    }

    /// Set the time-to-live (TTL) for multicast packets
    ///
    /// # Arguments
    /// * `ttl` - The TTL value (0-255)
    ///
    /// # Returns
    /// `Ok(())` on success or an `Err` if the TTL could not be set
    pub fn set_ttl(&self, ttl: u8) -> Result<&Self> {
        let success = unsafe { NormSetTTL(self.handle, ttl) };
        bool_result(success, "Failed to set TTL")?;
        Ok(self)
    }

    /// Set the TOS (Type of Service) value for packets
    ///
    /// # Arguments
    /// * `tos` - The TOS value (0-255)
    ///
    /// # Returns
    /// `Ok(())` on success or an `Err` if the TOS could not be set
    pub fn set_tos(&self, tos: u8) -> Result<&Self> {
        let success = unsafe { NormSetTOS(self.handle, tos) };
        bool_result(success, "Failed to set TOS")?;
        Ok(self)
    }

    /// Enable or disable loopback
    ///
    /// # Arguments
    /// * `enable` - Whether to enable loopback
    ///
    /// # Returns
    /// `Ok(())` on success or an `Err` if loopback could not be set
    pub fn set_loopback(&self, enable: bool) -> Result<&Self> {
        let success = unsafe { NormSetLoopback(self.handle, enable) };
        bool_result(success, "Failed to set loopback")?;
        Ok(self)
    }

    /// Enable or disable multicast loopback
    ///
    /// # Arguments
    /// * `enable` - Whether to enable multicast loopback
    ///
    /// # Returns
    /// `Ok(())` on success or an `Err` if multicast loopback could not be set
    pub fn set_multicast_loopback(&self, enable: bool) -> Result<&Self> {
        let success = unsafe { NormSetMulticastLoopback(self.handle, enable) };
        bool_result(success, "Failed to set multicast loopback")?;
        Ok(self)
    }

    /// Check if an address is a unicast address
    ///
    /// # Arguments
    /// * `address` - The address to check
    ///
    /// # Returns
    /// `true` if the address is a unicast address, `false` otherwise
    pub fn is_unicast_address<A: AsRef<str>>(address: A) -> bool {
        if let Ok(c_addr) = string_to_c_string(address.as_ref()) {
            unsafe { NormIsUnicastAddress(c_addr.as_ptr()) }
        } else {
            false
        }
    }

    /// Set the GRTT (Group Round Trip Time) estimate
    ///
    /// # Arguments
    /// * `grtt_estimate` - The GRTT estimate in seconds
    ///
    /// # Returns
    /// A reference to self for method chaining
    pub fn set_grtt_estimate(&self, grtt_estimate: f64) -> &Self {
        unsafe { NormSetGrttEstimate(self.handle, grtt_estimate) };
        self
    }

    /// Get the current GRTT estimate
    ///
    /// # Returns
    /// The current GRTT estimate in seconds
    pub fn grtt_estimate(&self) -> f64 {
        unsafe { NormGetGrttEstimate(self.handle) }
    }

    /// Enqueue a file for transmission
    ///
    /// # Arguments
    /// * `file_path` - The path to the file to send
    /// * `info` - Optional info data to associate with the file
    ///
    /// # Returns
    /// A handle to the enqueued file object or an error if the file could not be enqueued
    pub fn file_enqueue<P: AsRef<str>>(&self, file_path: P, info: Option<&[u8]>) -> Result<Object> {
        let c_path = string_to_c_string(file_path.as_ref())?;

        let (info_ptr, info_len) = match info {
            Some(i) => (i.as_ptr() as *const c_char, i.len()),
            None => (ptr::null(), 0),
        };

        let handle = unsafe {
            NormFileEnqueue(self.handle, c_path.as_ptr(), info_ptr, info_len as u32)
        };

        unsafe {
            check_handle(handle, NORM_OBJECT_INVALID)
                .map_err(|_| Error::FileError("Failed to enqueue file".to_string()))
                .map(|h| Object::from_handle(h))
        }
    }

    /// Enqueue data for transmission
    ///
    /// # Arguments
    /// * `data` - The data to send
    /// * `info` - Optional info data to associate with the data
    ///
    /// # Returns
    /// A handle to the enqueued data object or an error if the data could not be enqueued
    pub fn data_enqueue(&self, data: &[u8], info: Option<&[u8]>) -> Result<Object> {
        let (info_ptr, info_len) = match info {
            Some(i) => (i.as_ptr() as *const c_char, i.len()),
            None => (ptr::null(), 0),
        };

        let handle = unsafe {
            NormDataEnqueue(
                self.handle,
                data.as_ptr() as *const c_char,
                data.len() as u32,
                info_ptr,
                info_len as u32,
            )
        };

        unsafe {
            check_handle(handle, NORM_OBJECT_INVALID)
                .map_err(|_| Error::OperationFailed("Failed to enqueue data".to_string()))
                .map(|h| Object::from_handle(h))
        }
    }

    /// Open a stream for transmission
    ///
    /// # Arguments
    /// * `buffer_size` - The size of the stream buffer in bytes
    /// * `info` - Optional info data to associate with the stream
    ///
    /// # Returns
    /// A handle to the stream object or an error if the stream could not be opened
    pub fn stream_open(&self, buffer_size: u32, info: Option<&[u8]>) -> Result<Object> {
        let (info_ptr, info_len) = match info {
            Some(i) => (i.as_ptr() as *const c_char, i.len()),
            None => (ptr::null(), 0),
        };

        let handle = unsafe {
            NormStreamOpen(self.handle, buffer_size, info_ptr, info_len as u32)
        };

        unsafe {
            check_handle(handle, NORM_OBJECT_INVALID)
                .map_err(|_| Error::OperationFailed("Failed to open stream".to_string()))
                .map(|h| Object::from_handle(h))
        }
    }

    /// Set a watermark for acknowledgment
    ///
    /// # Arguments
    /// * `object` - The object to set as a watermark
    /// * `override_flush` - Whether to override any active flush
    ///
    /// # Returns
    /// `Ok(())` on success or an `Err` if the watermark could not be set
    pub fn set_watermark(&self, object: &Object, override_flush: bool) -> Result<()> {
        let success = unsafe {
            NormSetWatermark(self.handle, object.handle(), override_flush)
        };
        bool_result(success, "Failed to set watermark")
    }

    /// Reset the watermark
    ///
    /// # Returns
    /// `Ok(())` on success or an `Err` if the watermark could not be reset
    pub fn reset_watermark(&self) -> Result<()> {
        let success = unsafe { NormResetWatermark(self.handle) };
        bool_result(success, "Failed to reset watermark")
    }

    /// Cancel the watermark
    pub fn cancel_watermark(&self) {
        unsafe { NormCancelWatermark(self.handle) };
    }

    /// Send a command to all receivers
    ///
    /// # Arguments
    /// * `command` - The command data to send
    /// * `robust` - Whether to send the command robustly (with more reliability)
    ///
    /// # Returns
    /// `Ok(())` on success or an `Err` if the command could not be sent
    pub fn send_command(&self, command: &[u8], robust: bool) -> Result<()> {
        let success = unsafe {
            NormSendCommand(
                self.handle,
                command.as_ptr() as *const c_char,
                command.len() as u32,
                robust,
            )
        };
        bool_result(success, "Failed to send command")
    }

    /// Cancel a command in progress
    pub fn cancel_command(&self) {
        unsafe { NormCancelCommand(self.handle) };
    }

    /// Get the raw NORM session handle
    ///
    /// # Returns
    /// The raw NORM session handle
    pub(crate) fn handle(&self) -> NormSessionHandle {
        self.handle
    }
}

impl Drop for Session {
    fn drop(&mut self) {
        unsafe { NormDestroySession(self.handle) };
    }
}