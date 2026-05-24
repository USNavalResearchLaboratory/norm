# Helper module for integrating Rust bindings with NORM's waf build system

import os
import subprocess
from waflib import TaskGen, Task, Logs, Errors, Context

def options(ctx):
    """Add options for Rust integration to waf"""
    ctx.add_option('--build-rust', action='store_true', default=False,
                    help='Build Rust bindings for NORM')
    ctx.add_option('--rust-release', action='store_true', default=False,
                    help='Build Rust bindings in release mode')
    ctx.add_option('--rust-docs', action='store_true', default=False,
                    help='Generate documentation for Rust bindings')

def configure(ctx):
    """Configure Rust toolchain if requested"""
    if ctx.options.build_rust:
        # Check for Rust toolchain
        try:
            ctx.find_program('cargo', var='CARGO', mandatory=True)
            rustc_cmd = ctx.find_program('rustc', var='RUSTC', mandatory=True)

            # Get Rust version
            rustc_ver = subprocess.check_output([rustc_cmd[0], '--version']).decode('utf-8')
            ctx.msg('Checking for rustc version', rustc_ver.strip())

            # Set rust environment variables
            ctx.env.BUILD_RUST = True
            ctx.env.RUST_RELEASE = ctx.options.rust_release
            ctx.env.RUST_DOCS = ctx.options.rust_docs

        except Errors.WafError:
            ctx.fatal('Rust toolchain not found. Install Rust from https://rustup.rs')

def build_rust_bindings(ctx):
    """Build the Rust bindings using cargo"""
    if not ctx.env.BUILD_RUST:
        return

    rust_dir = ctx.path.find_dir('src/rust')
    if not rust_dir:
        ctx.fatal('Rust directory not found. Expected at: ' + ctx.path.abspath() + '/src/rust')

    # Set environment variables for cargo
    cargo_env = os.environ.copy()
    cargo_env['NORM_INCLUDE_DIR'] = os.path.abspath('include')
    cargo_env['NORM_LIB_DIR'] = os.path.abspath('lib')

    # Prepare cargo command
    cargo_cmd = [ctx.env.CARGO[0], 'build']
    if ctx.env.RUST_RELEASE:
        cargo_cmd.append('--release')

    # Build Rust bindings
    cwd = rust_dir.abspath()
    try:
        Logs.info("Building Rust bindings in: " + cwd)
        ret = subprocess.call(cargo_cmd, env=cargo_env, cwd=cwd)
        if ret != 0:
            ctx.fatal('Failed to build Rust bindings')

        # Build documentation if requested
        if ctx.env.RUST_DOCS:
            Logs.info("Building Rust documentation")
            doc_cmd = [ctx.env.CARGO[0], 'doc', '--no-deps']
            if ctx.env.RUST_RELEASE:
                doc_cmd.append('--release')

            ret = subprocess.call(doc_cmd, env=cargo_env, cwd=cwd)
            if ret != 0:
                Logs.warn('Failed to build Rust documentation')

    except Exception as e:
        ctx.fatal('Error building Rust bindings: ' + str(e))

def install_rust_bindings(ctx):
    """Install the Rust bindings if requested"""
    if not ctx.env.BUILD_RUST:
        return

    # Define installation directories
    rust_dir = ctx.path.find_dir('src/rust')
    build_type = 'release' if ctx.env.RUST_RELEASE else 'debug'
    target_dir = rust_dir.find_dir('target')

    # Install libraries
    if target_dir:
        lib_dir = target_dir.find_dir(build_type)
        if lib_dir:
            # Install .rlib files
            ctx.install_files(
                '${LIBDIR}/rust',
                lib_dir.ant_glob('*.rlib'),
                postpone=False
            )

            # Install dynamic libraries
            if ctx.env.DEST_OS == 'win32':
                ctx.install_files(
                    '${BINDIR}',
                    lib_dir.ant_glob('*.dll'),
                    postpone=False
                )
            else:
                ctx.install_files(
                    '${LIBDIR}',
                    lib_dir.ant_glob('*.so') + lib_dir.ant_glob('*.dylib'),
                    postpone=False
                )

    # Install documentation
    if ctx.env.RUST_DOCS and target_dir:
        doc_dir = target_dir.find_dir('doc')
        if doc_dir:
            ctx.install_files(
                '${PREFIX}/share/doc/norm-rust',
                doc_dir.ant_glob('**/*'),
                cwd=doc_dir,
                relative_trick=True,
                postpone=False
            )

    # Install Cargo.toml and source files for developers
    ctx.install_files(
        '${PREFIX}/share/norm/rust',
        rust_dir.ant_glob('**/*.rs') + rust_dir.ant_glob('**/*.toml'),
        cwd=rust_dir,
        relative_trick=True,
        postpone=False
    )