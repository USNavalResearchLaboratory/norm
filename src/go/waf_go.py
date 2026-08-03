# Helper module for integrating the Go bindings with NORM's waf build system.
#
# Unlike the Rust binding (which hardcodes the build/ path in build.rs), the Go
# binding locates libnorm via pkg-config (#cgo pkg-config: norm). Before libnorm
# is installed there is no norm.pc on the system PKG_CONFIG_PATH, so this module
# writes a build-tree norm.pc pointing at the in-tree build/ output and exports
# PKG_CONFIG_PATH so `go build`/`go test` can find it. It also sets the loader
# path so cgo test binaries can dlopen the shared libraries at runtime.

import os
import subprocess
from waflib import Logs


def options(ctx):
    """Add options for Go integration to waf."""
    ctx.add_option('--build-go', action='store_true', default=False,
                   help='Build Go bindings for NORM')
    ctx.add_option('--go-test', action='store_true', default=False,
                   help='Run Go binding tests after building')


def configure(ctx):
    """Configure the Go toolchain if requested."""
    if ctx.options.build_go:
        try:
            ctx.find_program('go', var='GO', mandatory=True)
            go_ver = subprocess.check_output([ctx.env.GO[0], 'version']).decode('utf-8')
            ctx.msg('Checking for go version', go_ver.strip())
            ctx.env.BUILD_GO = True
            ctx.env.GO_TEST = ctx.options.go_test
        except ctx.errors.ConfigurationError:
            ctx.fatal('Go toolchain not found. Install Go from https://go.dev/dl/')


def _write_build_pc(ctx, build_dir):
    """Write a build-tree norm.pc so cgo's pkg-config can find the in-tree libs.

    Returns the directory containing the generated norm.pc.
    """
    repo = ctx.path.abspath()
    include_dir = os.path.join(repo, 'include')
    lib_dir = os.path.join(repo, build_dir)
    protolib_dir = os.path.join(lib_dir, 'protolib')

    system = os.uname().sysname.lower() if hasattr(os, 'uname') else ''
    private = '-lprotokit -lstdc++ -lpthread'
    if system == 'darwin':
        private += ' -lresolv'

    pc_dir = os.path.join(lib_dir, 'pkgconfig')
    if not os.path.isdir(pc_dir):
        os.makedirs(pc_dir)
    pc_path = os.path.join(pc_dir, 'norm.pc')
    with open(pc_path, 'w') as f:
        f.write(
            'libdir=%s\n'
            'protolibdir=%s\n'
            'includedir=%s\n'
            '\n'
            'Name: norm\n'
            'Version: %s\n'
            'Description: NACK-Oriented Reliable Multicast (NORM) library (build tree)\n'
            'Libs: -L${libdir} -L${protolibdir} -lnorm\n'
            'Libs.private: %s\n'
            'Cflags: -I${includedir}\n'
            % (lib_dir, protolib_dir, include_dir, ctx.env.VERSION or '0', private)
        )
    return pc_dir, lib_dir, protolib_dir


def build_go_bindings(ctx):
    """Build (and optionally test) the Go bindings using the go toolchain."""
    if not ctx.env.BUILD_GO:
        return

    go_dir = ctx.path.find_dir('src/go')
    if not go_dir:
        ctx.fatal('Go directory not found. Expected at: ' + ctx.path.abspath() + '/src/go')

    build_dir = ctx.bldnode.name  # e.g. "build"
    pc_dir, lib_dir, protolib_dir = _write_build_pc(ctx, build_dir)

    env = os.environ.copy()
    env['CGO_ENABLED'] = '1'
    # Prepend our build-tree norm.pc so it wins over any installed copy.
    env['PKG_CONFIG_PATH'] = pc_dir + os.pathsep + env.get('PKG_CONFIG_PATH', '')
    # Loader path so cgo test binaries can find the shared libs at runtime.
    loader = lib_dir + os.pathsep + protolib_dir
    for var in ('LD_LIBRARY_PATH', 'DYLD_LIBRARY_PATH'):
        env[var] = loader + os.pathsep + env.get(var, '')

    cwd = go_dir.abspath()
    Logs.info('Building Go bindings in: ' + cwd)
    if subprocess.call([ctx.env.GO[0], 'build', './...'], env=env, cwd=cwd) != 0:
        ctx.fatal('Failed to build Go bindings')

    if ctx.env.GO_TEST:
        Logs.info('Running Go binding tests')
        if subprocess.call([ctx.env.GO[0], 'test', './...'], env=env, cwd=cwd) != 0:
            ctx.fatal('Go binding tests failed')
