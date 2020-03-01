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
    print ("Warning: NORM VERSION not found!?")

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

def configure(ctx):
    ctx.recurse('protolib')

    # Use this USE variable to add flags to NORM's compilation
    ctx.env.USE_BUILD_NORM += ['BUILD_NORM']
    
    if system in ('linux', 'darwin', 'freebsd', 'gnu', 'gnu/kfreebsd'):
        ctx.env.DEFINES_BUILD_NORM += ['ECN_SUPPORT']

    #if system == 'windows':
    #    ctx.env.DEFINES_BUILD_NORM += ['NORM_USE_DLL']

    if ctx.env.COMPILER_CXX == 'g++' or ctx.env.COMPILER_CXX == 'clang++':
        ctx.env.CFLAGS += ['-fvisibility=hidden', '-Wno-attributes']
        ctx.env.CXXFLAGS += ['-fvisibility=hidden', '-Wno-attributes']
        #if 'darwin' == system:
        #    ctx.env.LINKFLAGS += ['-L/opt/local/lib']

    # Will be used by the pkg-config generator
    ctx.env.VERSION = VERSION

def build(ctx):
    ctx.recurse('protolib')
    
    # Setup to install NORM header file
    ctx.install_files("${PREFIX}/include/", "include/normApi.h")
    
    normSrc = ['src/common/{0}.cpp'.format(x) for x in [
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
        ]]
    
    # Use static lib for Unix examples for convenience
	# (so we don't have to worry about LD_LIBRARY_PATH)
    ctx.shlib(
        target = 'norm',
        name = 'norm_shlib',
        includes = ['include'],
        export_includes = ['include'],
        use = ctx.env.USE_BUILD_NORM + ['protolib_st'],
        #stlib = ['protokit'],
        defines = ['NORM_USE_DLL'] if 'windows' == system else [],
        # Hack so clang links to libprotokit.a static library instead of dynamic
        linkflags = ['protolib/libprotokit.a'] if ctx.env.COMPILER_CXX == 'clang++' else [],
        vnum = VERSION,
        source = normSrc,
        features = 'cxx cxxshlib',
        install_path = '${LIBDIR}',
    )
    
    ctx.stlib(
        target = 'norm' if 'windows' != system else 'norm_static',
        name = 'norm_stlib',
        includes = ['include'],
        export_includes = ['include'],
        use = ctx.env.USE_BUILD_NORM + ['protolib_st'],
        vnum = VERSION,
        source = normSrc,
        features = 'cxx cxxstlib',
        install_path = '${LIBDIR}',
    )
    
    if ctx.env.BUILD_PYTHON:
        ctx(
            use = ['norm_shlib'],
            features='py',
            source=ctx.path.ant_glob('src/pynorm/**/*.py'),
            install_from='src',
        )

    if ctx.env.BUILD_JAVA:
        ctx.shlib(
            target = 'mil_navy_nrl_norm',
            includes = ['include'],
            use = ['norm_shlib', 'protolib_st', 'JAVA'],
            stlib = ['protokit'],
            vnum = VERSION,
            defines = ['NORM_USE_DLL'] if 'windows' == system else [],
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

    # Links to static library since it uses C++ objects directly instead of API
    normapp = ctx.program(
        # Need to explicitly set a different name, because 
        # the  library is also named "norm"
        name = 'normapp',
        target = 'normapp',
        #includes = ['include'],
        use = ['protolib_st', 'norm_stlib'], 
        defines = [],
        source = ['src/common/{0}.cpp'.format(x) for x in [
            'normPostProcess',
            'normApp',
        ]],
        # Disabled by default
        posted = True,
    )
    
    # Hack to force clang to link static libnorm.a
    if ctx.env.COMPILER_CXX == 'clang++': 
        normapp.linkflags ='libnorm.a'
        
    if system in ('linux', 'darwin', 'freebsd', 'gnu', 'gnu/kfreebsd'):
        normapp.source.append('src/unix/unixPostProcess.cpp')

    if system == 'windows':
        normapp.source.append('src/win32/win32PostProcess.cpp')
        normapp.defines.append('_CONSOLE')
        normapp.stlib = (["Shell32"]);

    for example in (
            #'normDataExample',
            'normDataRecv',
            'normDataSend',
            'normFileRecv',
            'normFileSend',
            'normStreamRecv',
            'normStreamSend',
            'normMsgr',
            'normStreamer',
            'normCast',
            'normClient',
            'normServer',
            #'wintest'  # Windows only (can uncomment on Windows)
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

    # Generate pkg-config file
    # Add additional static compilation dependencies based on the system.
    # libpcap is used by protolib on GNU/Hurd based systems.
    '''
    static_libs = ''
    if ctx.options.enable_static_library:
        static_libs += ' -lstdc++ -lprotokit'
        if system == "gnu":
            static_libs += ' -lpcap'
    ctx(source='norm.pc.in', STATIC_LIBS = static_libs)
    '''
    
    
def _make_simple_example(ctx, name, path='examples'):
    '''Makes a task from a single source file in the examples directory.

    Note these tasks are not built by default.  Use the --targets flag.
    '''
    source = ['{0}/{1}.cpp'.format(path, name)]
    if 'normClient' == name or 'normServer' == name:
        source.append('%s/normSocket.cpp' % path)
    example = ctx.program(
        target = name,
        includes = ['include', 'protolib/include'],
        use = ['protolib_st'],
        defines = [],
        source = source,
        # Don't build examples by default
        posted = True,
        # Don't install examples
        install_path = False,
    )

    # TBD - figure out how build NORM DLL and stil
    # enable Windows examples to link against static lib
    if 'windows' == system:
        example.use.append('norm_stlib')
        example.defines.append('_CONSOLE')
    else:
        example.use.append('norm_stlib')
        
    # Hack to force clang to link static libnorm.a
    if ctx.env.COMPILER_CXX == 'clang++': 
        example.linkflags ='libnorm.a'
    
