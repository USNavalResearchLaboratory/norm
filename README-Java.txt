Java JNI bindings for NORM
==========================

By:
  Jason Rush <jason.rush@gd-ais.com>
  Peter Griffin <peter.griffin@gd-ais.com>
  updates covering waf build system by Duc Nguyen <dnguyen@aypeks.com>

Updated: 2018-03-14

The Java JNI bindings for NORM provide Java bindings for the NORM C API.

For documentation about the main NORM API calls, refer to the NORM Developers
guide in the regular NORM distribution.

The JNI bindings for NORM can be built using two methods. The original
method uses Make and an Ant build.xml script in makefiles/java. The
newer method uses the waf build system and builds for all platforms with
various configurations via configuration switches.

------------
Requirements
------------

Java JNI bindings for NORM requires at least Java 1.5; however, it has also
been tested with Java 1.6 and Java 1.8

The NORM library should be built prior to building the Java JNI bindings since
they link against it.

### Using Ant and Make ###
Apache Ant is required for building the class files and jar file.

### Python waf ###
Tested with Python 2 but Python 3 should be ok.

------------
Building
------------

### Using Ant and Make ###

The build files for Java JNI bindings for NORM are located in the
makefiles/java directory.

To build the NORM jar file, execute Apache Ant in the makefiles/java directory:

  ant jar

This will produce a norm-<version>.jar file in the lib/ directory.

To build the accompanying NORM native bindings library, execute make in the
makefiles/java directory with the correct make file for your system:

  make -f Makefile.linux

This will produce a libmil_navy_nrl_norm.so file in the lib/ directory.

### Python waf ###

Reconfigure waf to use java and choose the target on a windows system:

  > cd <norm root dir>
  > waf configure --build-java --msvc_target=x64

The msvc_target flag allows you to choose the target Windows
architecture. The default is "x86" (i.e. 32bit arch)

Build the NORM libraries:

  > waf build

The output will be in the build/ directory. Copy both
mil_navy_nrl_norm.* and norm-1.* to the location the JRE libraries.

------------
Installation
------------

The norm-<version>.jar file must be on the classpath.

The libmil_navy_nrl_norm.so file must be installed in a location that Java
will search at runtime for native libraries.  The search path is dependent
upon the operating system:

  Windows:
    Windows system directories
    Current working directory
    PATH environment variable

  Unix:
    Current working directory
    LD_LIBRARY_PATH environment variable

You can also specify the directory with the java.library.path Java system
property on startup:

  java -Djava.library.path=<dir> ...

Finally the native library can also be installed in the JRE's library
directory.  This allows all programs that use the JRE to access the library:

  <JRE_HOME>/lib/i386/

If you observe an "java.lang.UnsatisfiedLinkError" exception while running
your application, you do not have the libmil_navy_nrl_norm.so installed in
the correct location.

If you're running on Windows, you also need to have the PATH environment
variable set with along with the -Djava.library.path=<dir>

------------
Examples
------------

Examples using the Java JNI bindings for NORM are provided in the examples/java
directory.

Running on Windows (could be put in a .bat script):

  set PATH=%PATH%;<path to mil_navy_nrl_norm.dll>
  java -Djava.library.path=<path to mil_navy_nrl_norm.dll> ...

Running on Linux or MacOS:

  java -Djava.library.path=<path to mil_navy_nrl_norm.dll> ...


