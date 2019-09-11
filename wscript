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
    
To build examples, use the --target directive.  For example:

    ./waf build --target=normClient,normServer

'''

import platform

import waflib

# Fetch VERSION from include/normVersion.h file
VERSION = None
vfile = open('include/normVersion.h', 'r')
for line in vfile.readlines():
    line = line.split()
    if len(line) != 3:
        continue
    if "#define" == line[0] and "VERSION" == line[1]:
        VERSION = line[2].strip('"')
if VERSION is None:
    print "Warning: NORM VERSION not found!?"

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
    build_opts = ctx.parser.add_option_group('Compile/install Options', 'Use during build/install step.')
    build_opts.add_option('--enable-static-library', action='store_true',
            help='Enable building and installing static library. [default:false]')


def configure(ctx):
    ctx.recurse('protolib')

    # Use this USE variable to add flags to NORM's compilation
    ctx.env.USE_BUILD_NORM += ['BUILD_NORM', 'protolib']

    if system in ('linux', 'darwin', 'freebsd', 'gnu', 'gnu/kfreebsd'):
        ctx.env.DEFINES_BUILD_NORM += ['ECN_SUPPORT']

    if system == 'windows':
        ctx.env.DEFINES_BUILD_NORM += ['NORM_USE_DLL']

    if ctx.env.COMPILER_CXX == 'g++' or ctx.env.COMPILER_CXX == 'clang++':
        ctx.env.CFLAGS += ['-fvisibility=hidden']
        ctx.env.CXXFLAGS += ['-fvisibility=hidden']

def build(ctx):
    ctx.recurse('protolib')
    
    # Setup to install NORM header file
    ctx.install_files("${PREFIX}/include/", "include/normApi.h")
    
    ctx.objects(
        target = 'objs',
        includes = ['include'],
        export_includes = ['include'],
        use = ctx.env.USE_BUILD_NORM,
        stlib = ["protolib"],
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
    
	# Use static lib for Unix examples for convenience
	# (so we don't have to worry about LD_LIBRARY_PATH)
    ctx.shlib(
        target = 'norm',
        includes = ['include'],
        export_includes = ['include'],
        vnum = VERSION,
        stlib = ["protolib"],
        use = ['objs'] + ctx.env.USE_BUILD_NORM,
        source = [],
        features = 'cxx cxxshlib',
    )

    if ctx.options.enable_static_library:
        ctx.stlib(
            target = 'norm',
            includes = ['include'],
            export_includes = ['include'],
            vnum = VERSION,
            stlib = ["protolib"],
            use = ['objs'] + ctx.env.USE_BUILD_NORM,
            source = [],
            features = 'cxx cxxstlib',
            install_path = '${LIBDIR}',
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
            use = ['norm_shlib', 'JAVA'],
		    vnum = VERSION,
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
        # Need to explicitly set a different name, because 
        # the  library is also named "norm"
        name = 'normapp',
        target = 'normapp',
        use = ['protolib', 'norm_stlib'],
        defines = [],
        source = ['src/common/{0}'.format(x) for x in [
            'normPostProcess.cpp',
            'normApp.cpp',
        ]],
        # Disabled by default
        posted = True,
    )

    if system in ('linux', 'darwin', 'freebsd', 'gnu', 'gnu/kfreebsd'):
        normapp.source.append('src/unix/unixPostProcess.cpp')

    if system == 'windows':
        normapp.source.append('src/win32/win32PostProcess.cpp')
        normapp.defines.append('_CONSOLE')
        normapp.stlib = (["Shell32"]);

    for example in (
            'normDataExample',
            'normDataRecv',
            'normDataSend',
            'normFileRecv',
            'normFileSend',
            'normStreamRecv',
            'normStreamSend',
            'normMsgr',
            'normStreamer',
            'normClient',
            'normServer',
            'wintest'
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

    Note these tasks are not built by default.  Use the --targets flag.
    '''
    source = ['{0}/{1}.cpp'.format(path, name)]
    if 'normClient' == name or 'normServer' == name:
        source.append('%s/normSocket.cpp' % path)
    example = ctx.program(
        target = name,
        use = ['protolib'],
        defines = [],
        source = source,
        # Don't build examples by default
        posted = True,
        # Don't install examples
        install_path = False,
    )
    if 'windows' == system:
        example.use.append('norm_shlib')
        example.defines.append('_CONSOLE')
    else:
        example.use.append('norm_stlib')
    
