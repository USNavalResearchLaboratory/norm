cmake_minimum_required(VERSION 3.1)

# set the project name
project(norm)

set(COMMON src/common)

option(BUILD_EXAMPLES "Enables building of the examples in /examples." OFF)

# Check for libraries
find_package(protolib REQUIRED)

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
if(MSVC)
	list(APPEND PLATFORM_LIBS Shell32)
	list(APPEND PLATFORM_DEFINITIONS _CONSOLE)
	list(APPEND PLATFORM_SOURCE_FILES src/win32/win32PostProcess.cpp)
elseif(UNIX)
	list(APPEND PLATFORM_SOURCE_FILES 	src/unix/unixPostProcess.cpp)
endif()

include(GNUInstallDirs)

# Setup target
add_library(norm ${PLATFORM_SOURCE_FILES} ${COMMON_SOURCE_FILES} ${PUBLIC_HEADER_FILES})
target_link_libraries(norm PRIVATE ${PLATFORM_LIBS} protolib::protolib)
target_compile_definitions(norm PUBLIC ${PLATFORM_DEFINITIONS})
target_compile_options(norm PUBLIC ${PLATFORM_FLAGS})
target_include_directories(norm PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_include_directories(norm PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)

# Install target
install( TARGETS norm EXPORT normTargets
		RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} )

install( EXPORT normTargets
	FILE normTargets.cmake
	NAMESPACE norm::
	DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/norm
)

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/norm)

if(BUILD_EXAMPLES)
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
		target_link_libraries(${example} PRIVATE norm protolib::protolib)
	endforeach()
endif()


