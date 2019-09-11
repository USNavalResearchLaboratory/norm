================= NORM API Examples =================

This directory contains some purposely simplified examples of NORM
API usage. See comments in the source code files on how to build
these on Unix platforms.  Eventually, "Makefiles" (and hopefully
"waf" configure/build scripts) will be provided as well as Visual
C++ project files for Win32 builds.  

The following example programs are included:

(These two programs use a hard-coded multicast address and port,
but take a file name and directory name, respectively, as a
command-line argument to determine the file sent and the directory
to which receive files are stored.)

normFileSend.cpp - simple file transmission program.  Sends one
                   file and exits.

normFileRecv.cpp - simple file reception program.  Receives one
                   file and exits.


normDataSend.cpp - simple NORM_OBJECT_DATA transmission program.
                   Sends a series of data objects of random size. 
                   In this example, the NORM_INFO for the objects
                   is set with some text with  some "info" about
                   the object.

normDataRecv.cpp - simple NORM_OBJECT_DATA reception program.  This
                   receives the data objects that "normDataSend"
                   transmits and validates their correctness, in
                   part based on the "info" embedded in the
                   NORM_INFO.

java -             There is a README.TXT in the java directory.
                   It explains the java examples, how to build
				   them, and how to run them.

NOTES:

Althought the normDataSend/Recv example use relatively small data
object sizes, the _intended_ use of NORM_OBJECT_DATA is for bulkier
content stored in application memory space.

For "messaging" applications that use modestly small message
sizes,  the NORM_OBJECT_STREAM transport option in NORM is likely
to provide more efficient service than NORM_OBJECT_DATA for small
objects due to the packet level Forward Error Correction (FEC)
based packet  recovery mechanism that NORM uses.

In the future, a similar "simple" example pair will be provided to
illustrate the NORM_OBJECT_STREAM form of transport.  Meanwhile,
the "normTest.cpp" file in the "norm/src/common" directory 
provides an example of the NORM API calls related to this.
  
Other examples will be added later including examples using the
"NormGetDescriptor()" function to allow NORM API events to be
multiplexed with other possible application events (e.g. other I/O
or Windows messages, etc).

Brian Adamson  
<mailto:adamson@itd.nrl.navy.mil>
20 August 2010


