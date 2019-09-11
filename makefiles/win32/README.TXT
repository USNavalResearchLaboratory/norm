Visual Studio 2012 and 2013 require different versions of the Platform Toolset:

Visual Studio 2012 v110 (or v110_xp for applications that need to run
under Windows XP)

Visual Studio 2013 v120 (or v120_xp for applications that need to run
under Windows XP)

For each project included by the solution file modify the Platform
Toolset under project Configuration Properties, General as appropriate
for your Visual Studio version.

A Visual Studio solutions file and associated .vcproj files are
included as a convenience for those using earlier versions of visual
studio (2008 and 2010): Norm-2008.sln
