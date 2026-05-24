use crate::error::{Result, bool_result, check_handle, string_to_c_string};
use crate::types::*;
use crate::event::Event;
use crate::session::Session;
use norm_sys::*;
use std::mem;

#[cfg(unix)]
use std::os::unix::io::{AsRawFd, RawFd};

/// NORM instance handle with RAII semantics.
///
/// This is the top-level object for interacting with the NORM library. It provides
/// functionality for creating sessions, handling events, and configuring global options.
///
/// When dropped, it will automatically clean up resources by calling `NormDestroyInstance`.
#[derive(Debug)]
pub struct Instance {
    /// The raw NORM instance handle
    handle: NormInstanceHandle,
}

impl Instance {
    /// Create a new NORM instance
    ///
    /// # Arguments
    /// * `priority_boost` - Whether to boost the priority of the NORM thread
    ///
    /// # Returns
    /// A new NORM instance or an error if the instance could not be created
    pub fn new(priority_boost: bool) -> Result<Self> {
        let handle = unsafe { NormCreateInstance(priority_boost) };
        unsafe { check_handle(handle, NORM_INSTANCE_INVALID)? };
        Ok(Instance { handle })
    }

    /// Stop the NORM instance
    ///
    /// This stops any running NORM threads but doesn't destroy the instance.
    pub fn stop(&self) {
        unsafe { NormStopInstance(self.handle) };
    }

    /// Restart the NORM instance
    ///
    /// # Returns
    /// `Ok(())` if the instance was successfully restarted, `Err` otherwise
    pub fn restart(&self) -> Result<()> {
        let success = unsafe { NormRestartInstance(self.handle) };
        bool_result(success, "Failed to restart NORM instance")
    }

    /// Suspend the NORM instance
    ///
    /// # Returns
    /// `Ok(())` if the instance was successfully suspended, `Err` otherwise
    pub fn suspend(&self) -> Result<()> {
        let success = unsafe { NormSuspendInstance(self.handle) };
        bool_result(success, "Failed to suspend NORM instance")
    }

    /// Resume a suspended NORM instance
    pub fn resume(&self) {
        unsafe { NormResumeInstance(self.handle) };
    }

    /// Set the cache directory for file reception
    ///
    /// This MUST be set to enable NORM_OBJECT_FILE reception!
    ///
    /// # Arguments
    /// * `cache_path` - The path to the cache directory
    ///
    /// # Returns
    /// `Ok(())` if the cache directory was successfully set, `Err` otherwise
    pub fn set_cache_directory<P: AsRef<str>>(&self, cache_path: P) -> Result<()> {
        let c_path = string_to_c_string(cache_path.as_ref())?;
        let success = unsafe { NormSetCacheDirectory(self.handle, c_path.as_ptr()) };
        bool_result(success, "Failed to set cache directory")
    }

    /// Get the next NORM event
    ///
    /// # Arguments
    /// * `wait` - Whether to wait for an event or return immediately if none is available
    ///
    /// # Returns
    /// `Ok(Some(Event))` if an event was available, `Ok(None)` if no event was available
    /// and `wait` was `false`, or `Err` if an error occurred
    pub fn next_event(&self, wait: bool) -> Result<Option<Event>> {
        let mut raw_event = unsafe { mem::zeroed::<NormEvent>() };
        let success = unsafe { NormGetNextEvent(self.handle, &mut raw_event, wait) };

        if !success {
            return Ok(None);
        }

        Ok(Some(Event::from_raw(raw_event)))
    }

    /// Create an iterator over NORM events
    ///
    /// This is a convenience wrapper around `next_event` that returns an iterator
    /// that will continuously yield events. The iterator will block until an event
    /// is available.
    ///
    /// # Returns
    /// An iterator over NORM events
    pub fn events(&self) -> EventIterator<'_> {
        EventIterator { instance: self }
    }

    /// Create a new NORM session
    ///
    /// # Arguments
    /// * `session_address` - The multicast address for the session
    /// * `session_port` - The port number for the session
    /// * `local_node_id` - The local node ID (use `NORM_NODE_ANY` for automatic)
    ///
    /// # Returns
    /// A new NORM session or an error if the session could not be created
    pub fn create_session<A: AsRef<str>>(
        &self,
        session_address: A,
        session_port: u16,
        local_node_id: NodeId,
    ) -> Result<Session> {
        Session::new(self.handle, session_address, session_port, local_node_id)
    }

    /// Get the file descriptor for the NORM instance
    ///
    /// This descriptor can be used with `select()` or similar APIs to wait for
    /// NORM events.
    ///
    /// # Returns
    /// The file descriptor for the NORM instance
    #[cfg(unix)]
    pub fn descriptor(&self) -> RawFd {
        unsafe { NormGetDescriptor(self.handle) }
    }

    /// Open a debug log file
    ///
    /// # Arguments
    /// * `path` - The path to the log file
    ///
    /// # Returns
    /// `Ok(())` if the log file was successfully opened, `Err` otherwise
    pub fn open_debug_log<P: AsRef<str>>(&self, path: P) -> Result<()> {
        let c_path = string_to_c_string(path.as_ref())?;
        let success = unsafe { NormOpenDebugLog(self.handle, c_path.as_ptr()) };
        bool_result(success, "Failed to open debug log file")
    }

    /// Close the debug log file
    pub fn close_debug_log(&self) {
        unsafe { NormCloseDebugLog(self.handle) };
    }

    /// Set custom allocation functions for NORM_OBJECT_DATA
    ///
    /// # Arguments
    /// * `alloc_func` - Function to allocate memory for NORM_OBJECT_DATA
    /// * `free_func` - Function to free memory allocated by `alloc_func`
    ///
    /// # Safety
    /// This function is unsafe because it takes raw function pointers that
    /// must adhere to the expected memory allocation/deallocation semantics.
    pub unsafe fn set_allocation_functions(
        &self,
        alloc_func: NormAllocFunctionHandle,
        free_func: NormFreeFunctionHandle,
    ) {
        NormSetAllocationFunctions(self.handle, alloc_func, free_func);
    }

    /// Get the raw NORM instance handle
    ///
    /// # Returns
    /// The raw NORM instance handle
    pub(crate) fn handle(&self) -> NormInstanceHandle {
        self.handle
    }
}

impl Drop for Instance {
    fn drop(&mut self) {
        unsafe { NormDestroyInstance(self.handle) };
    }
}

#[cfg(unix)]
impl AsRawFd for Instance {
    fn as_raw_fd(&self) -> RawFd {
        self.descriptor()
    }
}

/// Iterator over NORM events
///
/// This iterator will continuously yield events from a NORM instance.
/// It will block until an event is available.
pub struct EventIterator<'a> {
    instance: &'a Instance,
}

impl<'a> Iterator for EventIterator<'a> {
    type Item = Event;

    fn next(&mut self) -> Option<Self::Item> {
        match self.instance.next_event(true) {
            Ok(Some(event)) => Some(event),
            _ => None,
        }
    }
}