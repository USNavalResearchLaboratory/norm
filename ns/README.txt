                NORM ns-2 Support

This directory contains files for incorporating the NORM protocol
into the ns-2 network simulation tool.

FILES:

nsNormAgent.h 
nsNormAgent.cpp - C++ files which implement an ns-2 derivative of
                  the NormSimAgent class defined in the
                  ../common/normSimAgent.h files.
                  
nackCount.cpp - Source code to count sent & suppressed NACKs
                from the reports in NORM debug log files.
                (build with "gcc -o nc nackCount.cpp")
                
example.tcl - Very simple ns-2 TCL script illustrating the 
              use of a NORM agent.
              
simplenorm.tcl - Parameterized ns-2 TCL script which can be
                 used to evaluate NORM in a hub & spoke
                 topology.
                 
suppress.tcl   - Executable TCL script which iteratively
                 invokes "ns" with the "simplenorm.tcl"
                 script and "nc" (nackCount) to evaluate
                 NORM NACK suppression performance.

                 
ns-2.1b9-Makefile.in  - Patched version of ns-2.1b9 Makefile.in
                        with NORM, MDP & PROTOLIB stuff included.
                        (MDP stuff can be removed if desired)
                 
ns-2.1b7a-Makefile.in - Patched version of ns-2.1b7a Makefile.in
                        with NORM & PROTOLIB stuff included.
                        
                        
TO BUILD NS-2 with NORM Agent capabilities:

1) Put "protolib" and "norm" source tree directories into the ns-2
   source code directory.
   
2) Add PROTOLIB and NORM paths, macros, and, source objects to
   the ns-2 "Makefile.in"  (See the included patched "
   ns-2.1b7-Makefile.in" as an example)
   
   (A future version of this README will include detailed instructions
    to patch ns-2 for PROTOLIB and NORM support)
    
3) Run "./configure" in the ns-2 source directory.

4) Use "make ns" to rebuild ns-2 with the NORM/PROTOLIB support.



Brian Adamson
<adamson@itd.nrl.navy.mil>
11 July 2002
