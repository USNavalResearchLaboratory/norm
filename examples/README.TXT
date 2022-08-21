================= NORM API Examples =================

This directory contains some purposely simplified examples of NORM
API usage. See comments in the source code files on how to build
these on Unix platforms.  Most of these can be built with either the
Makefiles or Visual Studio projects, but the 'waf' build option is more
straightforward to use, particularly for Java and Python builds.  

The most complete examples are the following:

normMsgr.cpp    - Also java/NormMsgr.java and python/normMsgr.py
                  implementations are available with same functionality.
                  This sends messages piped to STDIN and receiver
                  instances can pipe received messages to STDOUT.
                  "Messages" are binary with a two-byte (Big Endian)
                  message length header.  A normMsgr instnace can act
                  as a sender and/or receiver.  This program uses
                  NORM_OBJECT_DATA as its transport mode.  It has options
                  to illustrate ACK-based flow control, and passive/active
                  flushing of NORM transmission stream.
                  
normStreamer.cpp - Similar to normMsgr, but uses NORM_OBJECT_STREAM.
                   It can support byte- and message-stream transport
                   with the same 2-byte message length header.
                   (TBD - implement Java and Python versions of this)
                   
normCast.cpp    - Similar to above two, but uses NORM_OBEJCT_FILE 
                  to transmit files and/or or directories of files.
                  This will eventually have the same file casting
                  options as the "norm" demo app has such as
                  repeat iterations through the file/directory list
                  and ability to monitor one or more directories
                  as live "outboxes" where files can be deposited
                  for transmission.                  
  
normClient/normServer - illustrate use of the NormSocket API extension
                        that is a work in progress, but fully functional
                        for TCP-like byte-streaming applications.


The following example programs are mainly "sketches" that illustrate
the NORM API at a high level.  These programs primarily use a hard-coded 
multicast address and port, but some have simple options:

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
				   them, and how to run them.  Other than 
                   'NormMsgr.java', the examples there
                   haven't been tested thoroughly/recently.
                   
python -           Other than 'normMsgr.py', the examples there
                   haven't been tested thoroughly/recently. 

Brian Adamson  
<mailto: badamson@gmail.com>
27 December 2019


