use crate::types::*;
use norm_sys::*;

/// NORM node handle with RAII semantics.
///
/// A Node represents a participant in a NORM session, either as a sender or receiver.
///
/// When dropped, the node will be released by calling `NormNodeRelease`.
#[derive(Debug)]
pub struct Node {
    /// The raw NORM node handle
    handle: NormNodeHandle,
    /// Whether this node is owned by us
    owned: bool,
}

impl Node {
    /// Create a new Node from a raw NORM node handle
    ///
    /// # Arguments
    /// * `handle` - The raw NORM node handle
    ///
    /// # Returns
    /// A new Node
    pub(crate) fn from_handle(handle: NormNodeHandle) -> Self {
        Node { handle, owned: true }
    }

    /// Create a new Node from a raw NORM node handle, without taking ownership
    ///
    /// # Arguments
    /// * `handle` - The raw NORM node handle
    ///
    /// # Returns
    /// A new Node that will not release the handle when dropped
    pub fn from_handle_unowned(handle: NormNodeHandle) -> Self {
        Node { handle, owned: false }
    }

    /// Get the ID of the node
    ///
    /// # Returns
    /// The node ID
    pub fn id(&self) -> NodeId {
        unsafe { NormNodeGetId(self.handle) }
    }

    /// Get the raw NORM node handle
    ///
    /// # Returns
    /// The raw NORM node handle
    pub(crate) fn handle(&self) -> NormNodeHandle {
        self.handle
    }

    /// Retain a node handle, incrementing its reference count
    pub fn retain(&self) {
        unsafe { NormNodeRetain(self.handle) };
    }

    /// Explicitly release a node handle, decrementing its reference count
    ///
    /// This is called automatically when the node is dropped, if it is owned.
    pub fn release(&self) {
        unsafe { NormNodeRelease(self.handle) };
    }
}

impl Drop for Node {
    fn drop(&mut self) {
        if self.owned {
            unsafe { NormNodeRelease(self.handle) };
        }
    }
}

// More methods will be added later