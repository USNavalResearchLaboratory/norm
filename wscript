#!/usr/bin/env  
'''
wscript - Waf build script for NORM
See http://waf.googlecode.com/ for more information.

In order to use different build directories (for example, a release and a debug
build), use the -o (--out) flag when configuring.  For example:

    ./waf configure -o build-debug --debug
    ./waf

    ./waf configure -o build-release
    ./waf

'''

import platform

import waflib

# So you don't need to do ./waf configure if you are just using the defaults
waflib.Configure.autoconfig = True

# Top-level project directory
top = '.'
# Directory where build files are placed
out = 'build'

# System waf is running on: linux, darwin (Mac OSX), freebsd, windows, etc.
system = platform.system().lower()

def options(ctx):
    ctx.recurse('protolib')

def configure(ctx):
    ctx.recurse('protolib')

    # Use this USE variable to add flags to NORM's compilation
    ctx.env.USE_BUILD_NORM += ['BUILD_NORM', 'protolib']

    if system in ('linux', 'darwin', 'freebsd'):
        ctx.env.DEFINES_BUILD_NORM += ['ECN_SUPPORT']

    if system == 'windows':
        ctx.env.DEFINES_BUILD_NORM += ['NORM_USE_DLL']

def build(ctx):
    ctx.recurse('protolib')

    ctx.shlib(
        target = 'norm',
        includes = ['include'],
        export_includes = ['include'],
        vnum = '1.0.0',
        use = ctx.env.USE_BUILD_NORM,
        source = ['src/common/{0}.cpp'.format(x) for x in [
            'galois',
            'normApi',
            'normEncoder',
            'normEncoderMDP',
            'normEncoderRS16',
            'normEncoderRS8',
            'normFile',
            'normMessage',
            'normNode',
            'normObject',
            'normSegment',
            'normSession',
        ]],
    )

    if ctx.env.BUILD_PYTHON:
        ctx(
            features='py',
            source=ctx.path.ant_glob('src/pynorm/**/*.py'),
            install_from='src',
        )

    if ctx.env.BUILD_JAVA:
        ctx.shlib(
            target = 'mil_navy_nrl_norm',
            use = ['norm', 'JAVA'],
            source = ['src/java/jni/{0}.cpp'.format(x) for x in [
                'normJni',
                'normInstanceJni',
                'normSessionJni',
                'normObjectJni',
                'normDataJni',
                'normFileJni',
                'normStreamJni',
                'normEventJni',
                'normNodeJni',
            ]],
        )
        ctx(
            features = ['javac', 'jar'],
            srcdir = 'src/java/src',
            outdir = 'src/java/src',
            basedir = 'src/java/src',
            destfile = 'norm.jar',
        )

    normapp = ctx.program(
        target = 'norm',
        # Need to explicitly set a different name, because the  library is also
        # named "norm"
        name = 'normapp',
        use = ['protolib', 'norm'],
        source = ['src/common/{0}'.format(x) for x in [
            'normPostProcess.cpp',
            'normApp.cpp',
        ]],
        # Disabled by default
        posted = True,
    )

    if system in ('linux', 'darwin', 'freebsd'):
        normapp.source.append('src/unix/unixPostProcess.cpp')

    if system == 'windows':
        normapp.source.append('src/win32/win32PostProcess.cpp')

    for example in (
            'normDataExample',
            'normDataRecv',
            'normDataSend',
            'normFileRecv',
            'normFileSend',
            'normStreamRecv',
            'normStreamSend',
            ):
        _make_simple_example(ctx, example)

    for prog in (
            'fecTest',
            'normPrecode',
            'normTest',
            'normThreadTest',
            'raft',
            ):
        _make_simple_example(ctx, prog, 'src/common')

    # Enable example targets specified on the command line
    ctx._parse_targets()

def _make_simple_example(ctx, name, path='examples'):
    '''Makes a task from a single source file in the examples directory.

    These tasks are not built by default.  Use the --targets flag.
    '''
    ctx.program(
        target = name,
        use = ['protolib', 'norm'],
        source = ['{0}/{1}.cpp'.format(path, name)],
        # Don't build examples by default
        posted = True,
        # Don't install examples
        install_path = False,
    )
