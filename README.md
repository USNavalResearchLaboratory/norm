
                    NORM SOURCE CODE RELEASE

AUTHORIZATION TO USE AND DISTRIBUTE

By receiving this distribution, you have agreed to the following 
terms governing the use and redistribution of the prototype NRL
NORM software release written and developed by Brian Adamson and
Joe Macker:

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that: 

(1) source code distributions retain this paragraph in its entirety, 

(2) all advertising materials mentioning features or use of this 
  software display the following acknowledgment:

   "This product includes software written and developed 
    by the Naval Research Laboratory (NRL)." 

The name of NRL, the name(s) of NRL  employee(s), or any entity
of the United States Government may not be used to endorse or
promote  products derived from this software, nor does the 
inclusion of the NRL written and developed software  directly or
indirectly suggest NRL or United States  Government endorsement
of this product.

THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.

---------------------------------------------------------------------


This is a release of the NRL MDP source code.  For most
purposes, the authors would prefer that the code not be
re-distributed so that users will obtain the latest (and most
debugged/improved) release from the official NORM web site:

<http://cs.itd.nrl.navy.mil/work/norm>


SOURCE CODE
===========

The following items can be build from this source code release:

1) libnorm.a - static NORM library for Unix platforms, or

   Norm.lib  - static NORM protocol library for Win32 platforms

 (These libraries can be used by applications using the NORM API.
  The file "normApi.h" provides function prototypes for NORM API
  calls and the included "Norm Developer's Guide" provides a
  reference manual for NORM application development using this
  API.  Additional tutorial material and API usage examples will
  be provided as additional documentation in the future)
 
 
2) norm (or norm.exe (WIN32) - command-line "demo" application
                               built from "common/normApp.cpp"
 
 (The included "Norm User's Guide" provides a rough overview
  of how to use this demo app.  This demo application
  is useful for file transfers (and streaming on Unix))
  
3) normTest (or normTest.exe (WIN32)) - very simple test application

 (The "normTest" application (see "common/normTest.cpp") is really
  just a simple playground for preliminary testing of the NORM
  API and does not do anything useful.  But it does provide
  a very simple example of NORM API usage.  More sophisticated
  (and better-documented) examples of NORM API usage will be 
  provided in the future.
  
4) There is also an "examples" directory in the source code tree
   that contains some simplified examples of NORM API usage.  The
   example set will be expanded as time progresses.
   
NRL also has started some apps built around NORM including:

A) A reliable multicast "chat" application (users can share files
   and images, too) with a GUI.  Like IRC with no server needed.
   
B) An application for reliably "tunneling" real-time UDP packet
   streams (like RTP video or audio) using NORM's streaming
   capability.
  
  
OTHER FILES:
============

NormDeveloperGuide.pdf  - PDF version of NORM Developer's Guide
                          (nice for printing)

NormDeveloperGuide.html - HTML version of NORM Developer's Guide
                          with Hyperlinked content 
                          (nice for browsing)

NormUserGuide.pdf       - Guide to "norm" demo app usage

VERSION.TXT             - NORM version history notes

README.TXT              - this file


NOTES:
======

The NORM code depends upon the current "Protolib" release.  The NORM
source code tarballs contain an appropriate release of "Protolib" as
part of the NORM source tree.  If the NORM code is checked out from
our CVS server, it is necessary to also check out "protolib" separately
and provide paths to it for NORM to build.
