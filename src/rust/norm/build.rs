fn main() {
    // The norm-sys crate already handles linking to libnorm
    // This build script is included for future expansion if needed

    // We might want to add conditional compilation flags, feature detection, etc.
    println!("cargo:rerun-if-changed=build.rs");
}