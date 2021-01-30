cmake_minimum_required(VERSION 3.11)
cmake_policy(SET CMP0077 NEW)
# set the project name
project(norm VERSION 1.5.8)

set(COMMON src/common)

option(NORM_BUILD_EXAMPLES "Enables building of the examples in /examples." OFF)
set(NORM_CUSTOM_PROTOLIB_VERSION OFF CACHE STRING "Set a custom protolib version to use, ./protolib to use the local version")

include(CheckCXXSymbolExists)
check_cxx_symbol_exists(dirfd "dirent.h" HAVE_DIRFD)
if(HAVE_DIRFD)
	list(APPEND PLATFORM_DEFINITIONS HAVE_DIRFD)
endif()

check_cxx_symbol_exists(lockf "unistd.h" HAVE_LOCKF)
if(HAVE_LOCKF)
	list(APPEND PLATFORM_DEFINITIONS HAVE_LOCKF)
endif()

check_cxx_symbol_exists(flock "sys/file.h" HAVE_FLOCK)
if(HAVE_FLOCK)
	list(APPEND PLATFORM_DEFINITIONS HAVE_FLOCK)
endif()

if(NOT NORM_CUSTOM_PROTOLIB_VERSION)
	find_package(Git)
	
	if(Git_FOUND)
		message("Git found: ${GIT_EXECUTABLE}")
		execute_process(
			COMMAND
				${GIT_EXECUTABLE} ls-tree HEAD
			RESULT_VARIABLE
				GIT_SUBMODULE_STATUS
			OUTPUT_VARIABLE
	            GIT_SUBMODULE
			WORKING_DIRECTORY 
				${CMAKE_CURRENT_LIST_DIR}
		)
		string(REGEX MATCH "commit[ \t\r\n]*([a-z0-9]+)[ \t\r\n]*protolib" GIT_SUBMODULE_HASH_MATCH ${GIT_SUBMODULE})
		if(GIT_SUBMODULE_HASH_MATCH)
			set(NORM_PROTOKIT_GIT_TAG ${CMAKE_MATCH_1})
		endif()
	endif()
elseif(NOT NORM_CUSTOM_PROTOLIB_VERSION STREQUAL "./protolib")
	set(NORM_PROTOKIT_GIT_TAG ${NORM_CUSTOM_PROTOLIB_VERSION})
endif()

if(NORM_PROTOKIT_GIT_TAG)
	message(STATUS "Building protokit from ${NORM_PROTOKIT_GIT_TAG}")
	include(FetchContent)
	FetchContent_Declare(
	  protokit
	  GIT_REPOSITORY https://github.com/USNavalResearchLaboratory/protolib.git
	  GIT_TAG        ${NORM_PROTOKIT_GIT_TAG}
	)
	FetchContent_MakeAvailable(protokit)
else()
	message(STATUS "Building protokit from ${NORM_CUSTOM_PROTOLIB_VERSION}")
	add_subdirectory(protolib)
endif()


# List header files
list(APPEND PUBLIC_HEADER_FILES
            include/galois.h
            include/normApi.h
            include/normEncoder.h
            include/normEncoderMDP.h
            include/normEncoderRS16.h
            include/normEncoderRS8.h
            include/normFile.h
            include/normMessage.h
            include/normNode.h
            include/normObject.h
            include/normPostProcess.h
            include/normSegment.h
            include/normSession.h
            include/normSimAgent.h
            include/normVersion.h
)

# List platform-independent source files
list(APPEND COMMON_SOURCE_FILES 
            ${COMMON}/galois.cpp
            ${COMMON}/normApi.cpp
            ${COMMON}/normEncoder.cpp
            ${COMMON}/normEncoderMDP.cpp
            ${COMMON}/normEncoderRS16.cpp
            ${COMMON}/normEncoderRS8.cpp
            ${COMMON}/normFile.cpp
            ${COMMON}/normMessage.cpp
            ${COMMON}/normNode.cpp
            ${COMMON}/normObject.cpp
            ${COMMON}/normSegment.cpp
            ${COMMON}/normSession.cpp )

# Setup platform independent include directory
list(APPEND INCLUDE_DIRS ${CMAKE_CURRENT_LIST_DIR}/include )

# Setup platform dependent libraries, defines, source file and compiler flags
# (The "post processing" helper source is only needed for normApp build)
#if(MSVC)
#    list(APPEND PLATFORM_LIBS Shell32)
#    list(APPEND PLATFORM_DEFINITIONS _CONSOLE)
#    list(APPEND PLATFORM_SOURCE_FILES src/win32/win32PostProcess.cpp)
#elseif(UNIX)
#    list(APPEND PLATFORM_SOURCE_FILES   src/unix/unixPostProcess.cpp)
#endif()

include(GNUInstallDirs)

# Setup target
add_library(norm ${PLATFORM_SOURCE_FILES} ${COMMON_SOURCE_FILES} ${PUBLIC_HEADER_FILES})
target_link_libraries(norm PRIVATE protokit::protokit)
target_link_libraries(norm PUBLIC ${PLATFORM_LIBS})
target_compile_definitions(norm PUBLIC ${PLATFORM_DEFINITIONS})
target_compile_options(norm PUBLIC ${PLATFORM_FLAGS})
target_include_directories(norm PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_include_directories(norm PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)

if(BUILD_SHARED_LIBS AND WIN32)
	target_compile_definitions(norm PUBLIC NORM_USE_DLL)
endif()

# Install target
install( TARGETS norm protokit EXPORT normTargets
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} )

set(INSTALL_CONFIGDIR ${CMAKE_INSTALL_LIBDIR}/cmake/norm)

install( EXPORT normTargets
    FILE normTargets.cmake
    NAMESPACE norm::
    DESTINATION ${INSTALL_CONFIGDIR}
)

install(FILES include/normApi.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# Create a ConfigVersion.cmake file
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/normConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
)

configure_package_config_file(${CMAKE_CURRENT_LIST_DIR}/cmake/normConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/normConfig.cmake
    INSTALL_DESTINATION ${INSTALL_CONFIGDIR}
)

# Install the config, configversion and custom find modules
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/normConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/normConfigVersion.cmake
    DESTINATION ${INSTALL_CONFIGDIR}
)

##############################################
# Exporting from the build tree
export(EXPORT normTargets
    FILE ${CMAKE_CURRENT_BINARY_DIR}/normTargets.cmake
    NAMESPACE norm::)

# Register package in user's package registry
export(PACKAGE norm)

if(NORM_BUILD_EXAMPLES)
    # Setup examples
    list(APPEND examples 
        #normDataExample
        normDataRecv
        normDataSend
        normFileRecv
        normFileSend
        normStreamRecv
        normStreamSend
        normMsgr
        normStreamer
        normCast
        normClient
        normServer
        #wintest
        )

    foreach(example ${examples})
        add_executable(${example} examples/${example}.cpp examples/normSocket.cpp)
        target_link_libraries(${example} PRIVATE norm protokit::protokit)
    endforeach()
endif()

