# Microsoft Developer Studio Project File - Name="NormLib" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Static Library" 0x0104

CFG=NormLib - Win32 Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "NormLib.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "NormLib.mak" CFG="NormLib - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "NormLib - Win32 Release" (based on "Win32 (x86) Static Library")
!MESSAGE "NormLib - Win32 Debug" (based on "Win32 (x86) Static Library")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
CPP=cl.exe
RSC=rc.exe

!IF  "$(CFG)" == "NormLib - Win32 Release"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "Release"
# PROP BASE Intermediate_Dir "Release"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "Release"
# PROP Intermediate_Dir "Release"
# PROP Target_Dir ""
# ADD BASE CPP /nologo /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_MBCS" /D "_LIB" /YX /FD /c
# ADD CPP /nologo /MD /W3 /GX /O2 /I "..\common" /I "..\protolib\common" /I "..\protolib\win32" /D "NDEBUG" /D "PROTO_DEBUG" /D "HAVE_IPV6" /D "HAVE_ASSERT" /D "WIN32" /D "_MBCS" /D "_LIB" /YX /FD /c
# ADD BASE RSC /l 0x409 /d "NDEBUG"
# ADD RSC /l 0x409 /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LIB32=link.exe -lib
# ADD BASE LIB32 /nologo
# ADD LIB32 /nologo

!ELSEIF  "$(CFG)" == "NormLib - Win32 Debug"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "Debug"
# PROP BASE Intermediate_Dir "Debug"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "Debug"
# PROP Intermediate_Dir "Debug"
# PROP Target_Dir ""
# ADD BASE CPP /nologo /W3 /Gm /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_MBCS" /D "_LIB" /YX /FD /GZ /c
# ADD CPP /nologo /MDd /W3 /GX /Od /I "..\common" /I "..\protolib\common" /I "..\protolib\win32" /D "_DEBUG" /D "PROTO_DEBUG" /D "HAVE_IPV6" /D "HAVE_ASSERT" /D "WIN32" /D "_MBCS" /D "_LIB" /YX /FD /GZ /c
# ADD BASE RSC /l 0x409 /d "_DEBUG"
# ADD RSC /l 0x409 /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LIB32=link.exe -lib
# ADD BASE LIB32 /nologo
# ADD LIB32 /nologo

!ENDIF 

# Begin Target

# Name "NormLib - Win32 Release"
# Name "NormLib - Win32 Debug"
# Begin Group "Source Files"

# PROP Default_Filter "cpp;c;cxx;rc;def;r;odl;idl;hpj;bat"
# Begin Source File

SOURCE=..\common\galois.cpp

!IF  "$(CFG)" == "NormLib - Win32 Release"

# ADD CPP /MD /vmg /I "..\win32"

!ELSEIF  "$(CFG)" == "NormLib - Win32 Debug"

# ADD CPP /MDd /vmg /I "..\win32"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\common\normApi.cpp
# End Source File
# Begin Source File

SOURCE=..\common\normBitmask.cpp

!IF  "$(CFG)" == "NormLib - Win32 Release"

# ADD CPP /MD /vmg /I "..\win32"

!ELSEIF  "$(CFG)" == "NormLib - Win32 Debug"

# ADD CPP /MDd /vmg /I "..\win32"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\common\normEncoder.cpp

!IF  "$(CFG)" == "NormLib - Win32 Release"

# ADD CPP /MD /vmg /I "..\win32"

!ELSEIF  "$(CFG)" == "NormLib - Win32 Debug"

# ADD CPP /MDd /vmg /I "..\win32"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\common\normFile.cpp

!IF  "$(CFG)" == "NormLib - Win32 Release"

# ADD CPP /MD /vmg /I "..\win32"

!ELSEIF  "$(CFG)" == "NormLib - Win32 Debug"

# ADD CPP /MDd /vmg /I "..\win32"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\common\normMessage.cpp

!IF  "$(CFG)" == "NormLib - Win32 Release"

# ADD CPP /MD /vmg /I "..\win32"

!ELSEIF  "$(CFG)" == "NormLib - Win32 Debug"

# ADD CPP /MDd /vmg /I "..\win32"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\common\normNode.cpp

!IF  "$(CFG)" == "NormLib - Win32 Release"

# ADD CPP /MD /vmg /I "..\win32"

!ELSEIF  "$(CFG)" == "NormLib - Win32 Debug"

# ADD CPP /MDd /vmg /I "..\win32"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\common\normObject.cpp

!IF  "$(CFG)" == "NormLib - Win32 Release"

# ADD CPP /MD /vmg /I "..\win32"

!ELSEIF  "$(CFG)" == "NormLib - Win32 Debug"

# ADD CPP /MDd /vmg /I "..\win32"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\common\normSegment.cpp

!IF  "$(CFG)" == "NormLib - Win32 Release"

# ADD CPP /MD /vmg /I "..\win32"

!ELSEIF  "$(CFG)" == "NormLib - Win32 Debug"

# ADD CPP /MDd /vmg /I "..\win32"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\common\normSession.cpp

!IF  "$(CFG)" == "NormLib - Win32 Release"

# ADD CPP /MD /vmg /I "..\win32"

!ELSEIF  "$(CFG)" == "NormLib - Win32 Debug"

# ADD CPP /MDd /vmg /I "..\win32"

!ENDIF 

# End Source File
# End Group
# Begin Group "Header Files"

# PROP Default_Filter "h;hpp;hxx;hm;inl"
# End Group
# End Target
# End Project
