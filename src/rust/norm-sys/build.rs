use std::env;
use std::path::PathBuf;

fn main() {
    // Tell cargo to invalidate the built crate whenever the wrapper.h changes
    println!("cargo:rerun-if-changed=wrapper.h");

    // First, try to find the NORM header directory from an environment variable
    let manifest_dir = env::var("CARGO_MANIFEST_DIR").unwrap();
    let norm_include_dir = env::var("NORM_INCLUDE_DIR").unwrap_or_else(|_| {
        // Default to looking for include files in the main repository directory
        format!("{}/../../../include", manifest_dir)
    });

    // Tell cargo to look for libnorm in the build directory
    let lib_path = format!("{}/../../../build", manifest_dir);

    println!("cargo:rustc-link-search={}", lib_path);
    println!("cargo:rustc-link-lib=norm");

    // Add rpath so the dynamic library can be found at runtime
    // This is especially important for development and testing
    #[cfg(target_os = "macos")]
    println!("cargo:rustc-link-arg=-Wl,-rpath,{}", lib_path);
    #[cfg(target_os = "linux")]
    println!("cargo:rustc-link-arg=-Wl,-rpath,{}", lib_path);

    // Add linkage to ProtoKit if needed
    let protolib_path = format!("{}/protolib", lib_path);
    println!("cargo:rustc-link-search={}", protolib_path);
    println!("cargo:rustc-link-lib=protokit");

    // Add rpath for protolib as well
    #[cfg(target_os = "macos")]
    println!("cargo:rustc-link-arg=-Wl,-rpath,{}", protolib_path);
    #[cfg(target_os = "linux")]
    println!("cargo:rustc-link-arg=-Wl,-rpath,{}", protolib_path);

    // Link pthread on Unix
    if cfg!(unix) {
        println!("cargo:rustc-link-lib=pthread");
    }

    // On macOS, we might need additional system libraries
    if cfg!(target_os = "macos") {
        println!("cargo:rustc-link-lib=resolv");
    }

    // On Solaris, add these libraries
    if cfg!(target_os = "solaris") {
        println!("cargo:rustc-link-lib=nsl");
        println!("cargo:rustc-link-lib=socket");
        println!("cargo:rustc-link-lib=resolv");
    }

    // Generate bindings for the NORM API
    let bindings = bindgen::Builder::default()
        .header("wrapper.h")
        // IMPORTANT: Tell clang to treat this as C++ code
        .clang_arg("-x")
        .clang_arg("c++")
        .clang_arg(format!("-I{}", norm_include_dir))
        // Whitelist NORM functions, types, and constants
        .allowlist_function("Norm.*")
        .allowlist_type("Norm.*")
        .allowlist_var("NORM_.*")
        // Make the generated bindings derive common traits
        .derive_default(true)
        .derive_debug(true)
        .derive_copy(true)
        .derive_eq(true)
        // Generate documentation from C comments
        .generate_comments(true)
        // Parse callbacks for cargo build info (updated to new API)
        .parse_callbacks(Box::new(bindgen::CargoCallbacks::new()))
        .generate()
        .expect("Unable to generate NORM bindings");

    // Write the bindings to the $OUT_DIR/bindings.rs file
    let out_path = PathBuf::from(env::var("OUT_DIR").unwrap());
    bindings
        .write_to_file(out_path.join("bindings.rs"))
        .expect("Couldn't write NORM bindings!");
}