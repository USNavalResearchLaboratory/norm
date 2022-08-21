================= NORM Java API Examples =================

This directory contains some purposely simplified examples of NORM
Java API usage. See comments below on how to build and run these on
Unix platforms.  Eventually, "build.xml" will be provided.


The following example programs are included:

(These two programs use a hard-coded multicast address and port,
but take a file name and directory name, respectively, as a
command-line argument to determine the file sent and the directory
to which receive files are stored.)

NormFileSend.java - simple file transmission program.  Sends one
                    file.  The address is 224.1.2.3:6003.

NormFileRecv.java - simple file reception program.  Receives one
                    file.  The address is 224.1.2.3:6003.

NormFileSendRecv.java - One application that sends a file to itself.
					The address is 224.1.2.3:6003.

NormStreamSend.java - This is an example of writing to a NORM stream.
					It reads from STDIN and writes to the
					stream. Specify the stream address on the command
					line or use the default address, 224.1.2.3:6003.
					Enable congestion control by setting the Java
					system property Norm.CC to "on"
					(i.e. -DNorm.CC=on).

NormStreamRecv.java - This is an example of reading from a NORM
					stream.  It writes to STDOUT and writes from the
					stream. Specify the stream address on the command
					line or use the default address, 224.1.2.3:6003.


COMPILING THE Java API EXAMPLES

After you build the JNI and the Norm Java jar file, you can compile
the examples in this directory all at one go:

javac -cp ../../lib/norm-1.0.0.jar *.java



RUNNING THE Java API EXAMPLES

You can use the shell scripts supplied to run the similarly named
example programs.

filesend.sh   - Run NormFileSend
filerecv.sh	  - Run NormFileRecv
streamsend.sh - Run NormStreamSend
streamrecv.sh - Run NormStreamRecv

Alternatively, you can run any example program by invoking java. You
must define the system property java.library.path and set the
classpath so that java can find the norm jar file as well as the local
classes.  For example, to run NormStreamSend:

java -Djava.library.path=../../lib -cp .:../../lib/norm-1.0.0.jar NormStreamSend 

NOTE ABOUT NormStreamSend

NormStreamSend supports NORM's TCP-friendly congestion control.
Enable that by setting the system property Norm.CC to "on".  You can
do that in streamsend.sh or you can do that on the command line:

java -DNorm.CC=on -Djava.library.path=../../lib -cp .:../../lib/norm-1.0.0.jar NormStreamSend 


