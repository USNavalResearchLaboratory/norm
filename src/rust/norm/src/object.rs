use crate::error::{Error, Result, check_handle};
use crate::types::*;
use crate::node::Node;
use norm_sys::*;
use std::slice;
use std::os::raw::c_char;

/// NORM object handle with RAII semantics.
///
/// A Object represents a NORM transmission object, which can be a file, data, or stream.
///
/// When dropped, the object will be released by calling `NormObjectRelease`.
#[derive(Debug)]
pub struct Object {
    /// The raw NORM object handle
    handle: NormObjectHandle,
    /// Whether this object is owned by us
    owned: bool,
}

impl Object {
    /// Create a new Object from a raw NORM object handle
    ///
    /// # Arguments
    /// * `handle` - The raw NORM object handle
    ///
    /// # Returns
    /// A new Object
    pub(crate) fn from_handle(handle: NormObjectHandle) -> Self {
        Object { handle, owned: true }
    }

    /// Create a new Object from a raw NORM object handle, without taking ownership
    ///
    /// # Arguments
    /// * `handle` - The raw NORM object handle
    ///
    /// # Returns
    /// A new Object that will not release the handle when dropped
    pub fn from_handle_unowned(handle: NormObjectHandle) -> Self {
        Object { handle, owned: false }
    }

    /// Get the type of the object
    ///
    /// # Returns
    /// The object type
    pub fn get_type(&self) -> ObjectType {
        let obj_type = unsafe { NormObjectGetType(self.handle) };
        ObjectType::from(obj_type)
    }

    /// Check if the object has info data
    ///
    /// # Returns
    /// `true` if the object has info data, `false` otherwise
    pub fn has_info(&self) -> bool {
        unsafe { NormObjectHasInfo(self.handle) }
    }

    /// Get the info data associated with the object
    ///
    /// # Returns
    /// The info data as a byte vector, or an error if the info data could not be retrieved
    pub fn get_info(&self) -> Result<Vec<u8>> {
        if !self.has_info() {
            return Ok(Vec::new());
        }

        let info_len = unsafe { NormObjectGetInfoLength(self.handle) };
        if info_len == 0 {
            return Ok(Vec::new());
        }

        let mut buffer = vec![0u8; info_len as usize];
        let bytes_read = unsafe {
            NormObjectGetInfo(
                self.handle,
                buffer.as_mut_ptr() as *mut c_char,
                info_len,
            )
        };

        if bytes_read == 0 {
            Err(Error::OperationFailed("Failed to get info data".to_string()))
        } else {
            buffer.truncate(bytes_read as usize);
            Ok(buffer)
        }
    }

    /// Get the size of the object in bytes
    ///
    /// # Returns
    /// The size of the object in bytes
    pub fn size(&self) -> Size {
        unsafe { NormObjectGetSize(self.handle) }
    }

    /// Get the number of bytes still pending transmission
    ///
    /// # Returns
    /// The number of bytes still pending transmission
    pub fn bytes_pending(&self) -> Size {
        unsafe { NormObjectGetBytesPending(self.handle) }
    }

    /// Access the data of a NORM_OBJECT_DATA object
    ///
    /// # Returns
    /// A slice containing the data, or an error if the data could not be accessed
    pub fn access_data(&self) -> Result<&[u8]> {
        if self.get_type() != ObjectType::Data {
            return Err(Error::InvalidParameter);
        }

        let data_ptr = unsafe { NormDataAccessData(self.handle) };
        if data_ptr.is_null() {
            return Err(Error::NullPointer);
        }

        let size = self.size() as usize;
        let data_slice = unsafe { slice::from_raw_parts(data_ptr as *const u8, size) };
        Ok(data_slice)
    }

    /// Cancel the object
    ///
    /// This aborts transmission or reception of the object.
    pub fn cancel(&self) {
        unsafe { NormObjectCancel(self.handle) };
    }

    /// Get the sender of the object
    ///
    /// # Returns
    /// The sender node, or an error if the sender could not be retrieved
    pub fn get_sender(&self) -> Result<Node> {
        let sender = unsafe { NormObjectGetSender(self.handle) };
        unsafe { check_handle(sender, NORM_NODE_INVALID) }
            .map(Node::from_handle_unowned)
    }

    /// Get the raw NORM object handle
    ///
    /// # Returns
    /// The raw NORM object handle
    pub(crate) fn handle(&self) -> NormObjectHandle {
        self.handle
    }

    /// Retain an object handle, incrementing its reference count
    pub fn retain(&self) {
        unsafe { NormObjectRetain(self.handle) };
    }

    /// Explicitly release an object handle, decrementing its reference count
    ///
    /// This is called automatically when the object is dropped, if it is owned.
    pub fn release(&self) {
        unsafe { NormObjectRelease(self.handle) };
    }

    // Stream-specific operations

    /// Write data to a stream object
    ///
    /// # Arguments
    /// * `data` - The data to write to the stream
    ///
    /// # Returns
    /// The number of bytes written, or an error if the operation failed
    ///
    /// # Errors
    /// Returns `Error::InvalidParameter` if this is not a stream object
    pub fn stream_write(&self, data: &[u8]) -> Result<usize> {
        if self.get_type() != ObjectType::Stream {
            return Err(Error::InvalidParameter);
        }

        let bytes_written = unsafe {
            NormStreamWrite(
                self.handle,
                data.as_ptr() as *const c_char,
                data.len() as u32,
            )
        };

        Ok(bytes_written as usize)
    }

    /// Flush a stream object
    ///
    /// # Arguments
    /// * `eom` - Whether to mark this as end-of-message
    /// * `flush_mode` - The flush mode to use
    ///
    /// # Errors
    /// Returns `Error::InvalidParameter` if this is not a stream object
    pub fn stream_flush(&self, eom: bool, flush_mode: crate::types::FlushMode) -> Result<()> {
        if self.get_type() != ObjectType::Stream {
            return Err(Error::InvalidParameter);
        }

        unsafe {
            NormStreamFlush(
                self.handle,
                eom,
                flush_mode.into(),
            );
        }

        Ok(())
    }

    /// Mark end-of-message on a stream object
    ///
    /// # Errors
    /// Returns `Error::InvalidParameter` if this is not a stream object
    pub fn stream_mark_eom(&self) -> Result<()> {
        if self.get_type() != ObjectType::Stream {
            return Err(Error::InvalidParameter);
        }

        unsafe { NormStreamMarkEom(self.handle) };
        Ok(())
    }

    /// Close a stream object
    ///
    /// # Arguments
    /// * `graceful` - Whether to close gracefully (wait for pending data) or immediately
    ///
    /// # Errors
    /// Returns `Error::InvalidParameter` if this is not a stream object
    pub fn stream_close(&self, graceful: bool) -> Result<()> {
        if self.get_type() != ObjectType::Stream {
            return Err(Error::InvalidParameter);
        }

        unsafe { NormStreamClose(self.handle, graceful) };
        Ok(())
    }

    /// Read data from a stream object
    ///
    /// # Arguments
    /// * `buffer` - The buffer to read data into
    ///
    /// # Returns
    /// A tuple of (bytes_read, data_slice) or an error if the operation failed
    ///
    /// # Errors
    /// Returns `Error::InvalidParameter` if this is not a stream object
    /// Returns `Error::OperationFailed` if the read operation failed
    pub fn stream_read(&self, buffer: &mut [u8]) -> Result<usize> {
        if self.get_type() != ObjectType::Stream {
            return Err(Error::InvalidParameter);
        }

        let mut num_bytes = buffer.len() as u32;
        let success = unsafe {
            NormStreamRead(
                self.handle,
                buffer.as_mut_ptr() as *mut c_char,
                &mut num_bytes,
            )
        };

        if success {
            Ok(num_bytes as usize)
        } else {
            Err(Error::OperationFailed("Failed to read from stream".to_string()))
        }
    }

    /// Check if a stream has available space for writing
    ///
    /// # Returns
    /// `true` if the stream has vacancy for writing, `false` otherwise
    ///
    /// # Errors
    /// Returns `Error::InvalidParameter` if this is not a stream object
    pub fn stream_has_vacancy(&self) -> Result<bool> {
        if self.get_type() != ObjectType::Stream {
            return Err(Error::InvalidParameter);
        }

        let has_vacancy = unsafe { NormStreamHasVacancy(self.handle) };
        Ok(has_vacancy)
    }

    /// Seek to the start of the next message in a stream
    ///
    /// # Returns
    /// `true` if a message start was found, `false` otherwise
    ///
    /// # Errors
    /// Returns `Error::InvalidParameter` if this is not a stream object
    pub fn stream_seek_msg_start(&self) -> Result<bool> {
        if self.get_type() != ObjectType::Stream {
            return Err(Error::InvalidParameter);
        }

        let found = unsafe { NormStreamSeekMsgStart(self.handle) };
        Ok(found)
    }
}

impl Drop for Object {
    fn drop(&mut self) {
        if self.owned {
            unsafe { NormObjectRelease(self.handle) };
        }
    }
}