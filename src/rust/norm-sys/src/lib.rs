//! Raw FFI bindings to the NORM (NACK-Oriented Reliable Multicast) library.
//!
//! This crate provides raw FFI bindings to the NORM C API, generated using bindgen.
//! It is not intended to be used directly, but rather through the `norm` crate which
//! provides safe, idiomatic Rust wrappers.

#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]
#![allow(clippy::all)]

// The bindings will be included here by the build script
include!(concat!(env!("OUT_DIR"), "/bindings.rs"));

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_version_constants() {
        // Verify that we can access the version constants
        assert!(NORM_VERSION_MAJOR > 0);
    }
}