
                    NORM SOURCE CODE RELEASE

/*********************************************************************
 *
 * AUTHORIZATION TO USE AND DISTRIBUTE
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that: 
 *
 * (1) source code distributions retain this paragraph in its entirety, 
 *  
 * (2) distributions including binary code include this paragraph in
 *     its entirety in the documentation or other materials provided 
 *     with the distribution.
 * 
 *      "This product includes software written and developed 
 *       by Code 5520 of the Naval Research Laboratory (NRL)." 
 *         
 *  The name of NRL, the name(s) of NRL  employee(s), or any entity
 *  of the United States Government may not be used to endorse or
 *  promote  products derived from this software, nor does the 
 *  inclusion of the NRL written and developed software  directly or
 *  indirectly suggest NRL or United States  Government endorsement
 *  of this product.
 * 
 * THIS SOFTWARE IS PROVIDED "AS IS" AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 ********************************************************************/

This is the NRL NORM source code repository. 

The Github "Discussions" feature is available here for comment and question in addition 
to the regular code issue reporting:

https://github.com/USNavalResearchLaboratory/norm/discussions


SOURCE CODE
===========

The "norm" source Git repository includes "protolib" as a git submodule. You can
add the "--recurse-submodules" to your git clone command to automate inclusion: 

git clone --recurse-submodules https://github.com/USNavalResearchLaboratory/norm.git

The following items can be built from this source code release:

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

NACK-Oriented Proxy (NORP)
==========================
NORM Proxy code is also included in the 'norp' directory.
More info is available in the norp/doc directory, as well as the
NORP website:  https://www.nrl.navy.mil/itd/ncs/products/norp


OTHER FILES:
============

NormDeveloperGuide.pdf  - PDF version of NORM Developer's Guide
                          (nice for printing)

NormDeveloperGuide.html - HTML version of NORM Developer's Guide
                          with Hyperlinked content 
                          (nice for browsing)

NormUserGuide.pdf       - Guide to "norm" demo app usage

VERSION.TXT             - NORM version history notes

README.md               - this file


NOTES:
======

The NORM code depends upon the current "Protolib" release:

  https://github.com/USNavalResearchLaboratory/protolib 
  
It has been addded as a git submodule to the NORM git repository.  So, to 
to build you will need to do the following steps to download the protolib code:

git clone --recurse-submodules https://github.com/USNavalResearchLaboratory/norm.git

Alternatively after a basic "git clone" you can do the folowing to pull in the protolib source:

cd norm
git submodule update --init

To keep the 'protolib' submodule up to date, you will need to do the 
following:

cd norm/protolib
git checkout master

This will enable you to issue 'git pull', etc to treat the 'protolib' 
sub-directory as its own (sub-) repository.



