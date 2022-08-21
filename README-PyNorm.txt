PyNORM - Python Wrapper for NORM & Extras
=========================================

By: Tom Wambold <wambold@itd.nrl.navy.mil>

PyNORM provides a thin wrapper around the NORM C API in the main package.  It
also provides several additional modules in the extra package to provide higher
level usage of NORM.

For documentation about the main NORM API calls, refer to the NORM Developers
guide in the regular NORM distribution.

Also, the files in the examples/python directory have a lot of good info on how
to use the library.  For simple apps, see the normFileSend and normFileReceive
scripts.

------------
Requirements
------------

PyNORM has been tested on Python versions 2.5, 2.6, and 2.7.  The code may work on
earlier versions, but was not tested.

PyNORM requires NORM to be built as a shared library (.so, .dylib, .dll), so
that Python's "ctypes" module can load it.  Ctypes will look for the library
in similar places as your system's compiler.  On UNIX systems, this is usually
/usr/lib and /usr/local/lib, etc.  On Windows, this searches folders in the
PATH environment variable.

On Windows, PyNORM requires the "PyWin32" module, available at:
    http://sf.net/projects/pywin32
    
-------------
Installation
-------------
A "setup.py" script is included that installs the 'pynorm' packages.  Note, as
mentioned above, the NORM shared library must be installed on your system.  The
'waf' install script will do this or it can be built with the NORM 'makefiles'
and manually emplaced.  If you use the 'setup.py' script, you do _not_ need
to use the '--build-python' option of the 'waf' configuration script even if you
use 'waf' to build the NORM shared library.  The 'waf' build tool can also be used
to install the 'pynorm' package.

For example, to build and install "libnorm.so" and "pynorm" on Linux, use the 
following steps.

cd norm/makefiles
make -f Makefile.linux libnorm.so
sudo cp libnorm.so /usr/local/lib/
cd ../
sudo python setup.py install

On MacOS, the same steps can be used, but using "libnorm.dylib" instead.

-----------------------
Examples
-----------------------
Some 'pynorm' *.py examples are in the "norm/examples" directory.

The 'normMsgr.py' example is functionally equivalent to the 'normMsgr.cpp'
example and the 'java/NormMsgr.java' example and illustrates multi-threaded
use of the 'pynorm' package for NORM-based data transmission and reception.
Note the "NORM_OBJECT_DATA" model is used and may provide out-of-order delivery
of the transmitted messages.  A similar 'normStreamer.py' example will be 
provided in the future to illustrate the NORM ordered, message stream delivery
mode of operation.

-----------------------
"Extra" Package Modules
-----------------------

manager.py:
    This provides a callback-driven event loop for NORM.  It operates on a
    separate thread, calling registered functions in response to NORM events.

logparser.py:
    This provides a function to reads NORM debug "reports" (printed at debug
    level 3 or higher) from a log file, and parses them into Python objects.
