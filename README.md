
            NORM PRELIMINARY CODE RELEASE

These directories contain source code for a very preliminary
implementation of the Nack Oriented Reliable Multicast (NORM)
protocol.  Currently, a very dumb "norm" command-line application
(which currently takes no command-line arguments) is
created. This application creates a NormSession and acts
both as a sender and receiver to the multicast group.  It
creates a "NORM_STREAM" object and writes repeated,
continuous strings of "aaaaaaa ..." to the stream.  As the
stream is received, the client (receiver) portion of the
"norm" app writes a notices of a successful Read()
operation from the stream.

This currently uses a fixed transmission rate of 64 kbps
and  a Reed-Solomon FEC encoding block with 20 user data
segments and calculates 8 parity segments per coding
block.  Four parity segments are sent at the end of each
coding block as "auto parity".  The current NORM code is
currently automatically discarding segment 9 of the
received data stream to test the FEC encoding/decoding and
stream buffer routines.  

The code does not currently generate any NACK messages,
but the routines to perform checks for losses is in place
and the routines for building NACK messages are in place,
so the addition of  the timer installation routines to
schedule NACK back-off and subsequent transmission will be
added very soon.  Then, routines will be added for the
server (sender) side to process received NACKs and
subsequently provide repair messages.  Then, client-side
routines for NACK suppression will be added.  After that,
the final details of GRTT collection, unicast feedback
suppression will be added and finally one or more
congestion control schemes will be included.

The purpose of this release is to illustrate routines to
build and parse the messages currently defined in the NORM
Internet Draft.

The current code only has Makefiles for Unix platforms, but the
code could be assembled as Win32 project under VC++.

The NORM code depends upon the current "Protolib" release.
See <http://pf.itd.nrl.navy.mil> for that code.
