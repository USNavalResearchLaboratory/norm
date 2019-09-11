 NORM API Examples 
 =================

 This directory contains some purposely simplified examples of NORM
 API usage. See comments in the source code files on how to build
 these on Unix platforms.  Eventually, "Makefiles" (and hopefully
 "configure" scripts) will be provided as well as Visual C++
 project files for Win32 builds.  

 The following example programs are included:

 
 normFileSend.cpp - simple file transmission program.  Sends one
                    file and exits.

 normFileRecv.cpp - simple file reception program.  Receives one
                    file and exits.

 (These two programs use a hard-coded multicast address and port,
  but take a file name and directory name, respectively, as a
  command-line argument to determine the file sent and the
  directory to which receive files are stored.)

  
 Other examples will be added later including examples using the
 "NormGetDescriptor()" function to allow NORM API events to be
 multiplexed with other possible application events (e.g. other I/O
 or Windows messages, etc).

 ------------- Brian Adamson <mailto:adamson@itd.nrl.navy.mil> 9
 June 2005

 
