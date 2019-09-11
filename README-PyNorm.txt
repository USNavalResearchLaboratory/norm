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

PyNORM has been tested on Python versions 2.5 and 2.6.  The code may work on
earlier versions, but was not tested.

PyNORM requires NORM to be built as a shared library (.so, .dylib, .dll), so
that Python's "ctypes" module can load it.  Ctypes will look for the library
in similar places as your system's compiler.  On UNIX systems, this is usually
/usr/lib and /usr/local/lib, etc.  On Windows, this searches folders in the
PATH environment variable.

On Windows, PyNORM requires the "PyWin32" module, available at:
    http://sf.net/projects/pywin32

-----------------------
"Extra" Package Modules
-----------------------

manager.py:
    This provides a callback-driven event loop for NORM.  It operates on a
    separate thread, calling registered functions in response to NORM events.

logparser.py:
    This provides a function to reads NORM debug "reports" (printed at debug
    level 3 or higher) from a log file, and parses them into Python objects.
