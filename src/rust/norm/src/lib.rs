//! Safe, idiomatic Rust bindings for the NORM (NACK-Oriented Reliable Multicast) protocol.
//!
//! NORM is a protocol for reliable multicast transmission developed by the US Naval Research
//! Laboratory. This crate provides safe Rust wrappers around the C API, handling resource
//! management and providing an idiomatic interface.
//!
//! # Features
//!
//! - RAII-based resource management for NORM handles
//! - Idiomatic error handling
//! - Iterator-based event handling
//! - Ergonomic multicast configuration
//! - Optional async support with tokio (feature = "tokio")

// Re-export norm-sys for advanced users
pub use norm_sys;

// Module declarations
mod error;
mod types;
mod instance;
mod session;
mod object;
mod node;
mod event;
mod multicast;

// Optional modules
// Note: Async support with tokio is planned for future implementation
// #[cfg(feature = "tokio")]
// pub mod tokio;

// Public re-exports
pub use error::{Error, Result};
pub use types::*;
pub use instance::Instance;
pub use session::Session;
pub use object::Object;
pub use node::Node;
pub use event::Event;
pub use multicast::{MulticastConfig, MulticastExt, is_multicast_address};

// Version information
pub const VERSION_MAJOR: u32 = norm_sys::NORM_VERSION_MAJOR as u32;
pub const VERSION_MINOR: u32 = norm_sys::NORM_VERSION_MINOR as u32;
pub const VERSION_PATCH: u32 = norm_sys::NORM_VERSION_PATCH as u32;

/// Get the version of the NORM library as a tuple (major, minor, patch)
pub fn version() -> (u32, u32, u32) {
    (VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH)
}