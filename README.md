
            NORM PRELIMINARY CODE RELEASE

The NORM code here is still preliminary but a functional "norm"
command-line application can be built which can send and receive
a set of files _or_ a stream piped to/from stdin/stdout.  The command-
line options are not yet fully documented, but there is abbreviated "help"
available in the "norm" application.  And many options are similar to the 
preceding NRL MDP work (see http://mdp.pf.itd.nrl.navy.mil).

A "normTest.cpp" program is now included which is a simple demonstration 
(and active test code) of the evolving NORM API.  When the API is
further completed, I plan to provide a few different example applications
(file transfer, streaming, etc) which use the API.

The Win32 code is not yet as well-tested as the Unix version, but
might be OK at this point.  I will do more Win32 testing when I get
caught up on the API development and documentation.

The NORM code depends upon the current "Protolib" release.  The NORM
source code tarballs contain an appropriate release of "Protolib" as
part of the NORM source tree.  If the NORM code is checked out from
our CVS server, it is necessary to also check out "protolib" separately
and provide paths to it for NORM to build.
