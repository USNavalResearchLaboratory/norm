Java JNI bindings for NORM
==========================

By:
  Jason Rush <jason.rush@gd-ais.com>
  Peter Griffin <peter.griffin@gd-ais.com>

The Java JNI bindings for NORM provide Java bindings for the NORM C API.

For documentation about the main NORM API calls, refer to the NORM Developers
guide in the regular NORM distribution.

------------
Requirements
------------

Java JNI bindings for NORM requires at least Java 1.5; however, it has also
been tested with Java 1.6.

The NORM library should be built prior to building the Java JNI bindings since
they link against it.

Apache Ant is required for building the class files and jar file.

------------
Building
------------

The build files for Java JNI bindings for NORM are located in the
makefiles/java directory.

To build the NORM jar file, execute Apache Ant in the makefiles/java directory:

  ant jar

This will produce a norm-<version>.jar file in the lib/ directory.

To build the accompanying NORM native bindings library, execute make in the
makefiles/java directory with the correct make file for your system:

  make -f Makefile.linux

This will produce a libmil_navy_nrl_norm.so file in the lib/ directory.

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

------------
Examples
------------

Examples using the Java JNI bindings for NORM are provided in the examples/java
directory.
