cmake_minimum_required(VERSION 3.14)
cmake_policy(SET CMP0077 NEW)
# set the project name
project(norm VERSION 1.5.10)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fvisibility=hidden")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=hidden")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(COMMON src/common)

option(NORM_BUILD_EXAMPLES "Enables building of the examples in /examples." OFF)
set(NORM_CUSTOM_PROTOLIB_VERSION OFF CACHE STRING "Set a custom protolib version to use, ./protolib to use the local version")

# Self-checking tests build by default only when NORM is the top-level project, so
# projects that add_subdirectory() NORM don't grow an extra binary unasked.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(NORM_BUILD_TESTING_DEFAULT ON)
else()
    set(NORM_BUILD_TESTING_DEFAULT OFF)
endif()
option(NORM_BUILD_TESTING "Enables building of the self-checking tests in /examples." ${NORM_BUILD_TESTING_DEFAULT})
if(NORM_BUILD_TESTING)
    enable_testing()
endif()

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
	FetchContent_GetProperties(protokit SOURCE_DIR PROTOKIT_SRC_DIR)
else()
	message(STATUS "Building protokit from ${NORM_CUSTOM_PROTOLIB_VERSION}")
	add_subdirectory(protolib)
	set(PROTOKIT_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/protolib)
endif()

if (MINGW AND TARGET protokit)
	target_sources(protokit PRIVATE
		${PROTOKIT_SRC_DIR}/src/win32/win32Net.cpp
		${PROTOKIT_SRC_DIR}/src/win32/win32RouteMgr.cpp
	)
	target_link_libraries(protokit PUBLIC ws2_32 iphlpapi user32 gdi32 Advapi32 ntdll)
	target_compile_definitions(protokit PUBLIC _CRT_SECURE_NO_WARNINGS)
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

if(BUILD_SHARED_LIBS)
	set_target_properties(norm PROPERTIES SOVERSION 1.5.10)
	if(WIN32)
		target_compile_definitions(norm PUBLIC NORM_USE_DLL)
		# NORM_USE_DLL alone makes NORM_API_LINKAGE "dllimport" everywhere, which is
		# what consumers want but is wrong inside the DLL itself: the classes marked
		# with it (NormEncoder/NormDecoder) define members in normEncoder.cpp, and
		# MSVC rejects defining a member of a dllimport class.  normApi.cpp defines
		# this itself for the non-CMake builds; the whole library needs it.
		target_compile_definitions(norm PRIVATE _NORM_API_BUILD)
	endif()
endif()

if(BUILD_SHARED_LIBS AND BUILD_STATIC_LIBS)
	add_library(norm-static STATIC ${PLATFORM_SOURCE_FILES} ${COMMON_SOURCE_FILES} ${PUBLIC_HEADER_FILES})
	target_link_libraries(norm-static PRIVATE protokit::protokit)
	target_compile_definitions(norm-static PUBLIC ${PLATFORM_DEFINITIONS})
	target_compile_options(norm-static PUBLIC ${PLATFORM_FLAGS})
	target_include_directories(norm-static PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
	target_include_directories(norm-static PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
	set_target_properties(norm-static PROPERTIES OUTPUT_NAME "norm" PREFIX "lib")
	set(NORM_STATIC "norm-static")
endif()

# Install target
install( TARGETS norm ${NORM_STATIC} protokit EXPORT normTargets
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

# Create pkg-config file norm.pc
# TODO: once waf is removed, norm.pc.in can be edited to use the variables CMake sets directly, and
# remove these re-definitions
set(VERSION ${PROJECT_VERSION})
set(PREFIX ${CMAKE_INSTALL_PREFIX})
set(LIBDIR ${CMAKE_INSTALL_LIBDIR})
set(STATIC_LIBS "libprotokit.a")
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/norm.pc.in ${CMAKE_CURRENT_BINARY_DIR}/norm.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/norm.pc DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

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
        chant
        normClient
        normServer
        #wintest
        )

    foreach(example ${examples})
        add_executable(${example} examples/${example}.cpp examples/normSocket.cpp)
        target_link_libraries(${example} PRIVATE norm protokit::protokit)
    endforeach()

endif()

if(NORM_BUILD_TESTING)
    add_executable(normRatelessTest examples/normRatelessTest.cpp)
    target_link_libraries(normRatelessTest PRIVATE norm protokit::protokit)
    add_test(NAME normRatelessTest COMMAND normRatelessTest)
    add_test(NAME normCustomFecTest COMMAND normRatelessTest)
    set_tests_properties(normCustomFecTest PROPERTIES ENVIRONMENT "NORM_FEC_ID=132")
    add_test(NAME normRatelessExhaustionTest COMMAND normRatelessTest)
    set_tests_properties(normRatelessExhaustionTest PROPERTIES ENVIRONMENT
        "NORM_FORCE_DECODE_FAILURE=1;NORM_NDATA=4;NORM_NPARITY=4;NORM_DATA_LEN=2000;NORM_RX_LOSS=50")
    add_test(NAME normRatelessReNackTest COMMAND normRatelessTest)
    set_tests_properties(normRatelessReNackTest PROPERTIES ENVIRONMENT
        "NORM_DECODE_SHORT_COUNT=1;NORM_NDATA=4;NORM_NPARITY=8;NORM_DATA_LEN=2000;NORM_RX_LOSS=30")
    add_test(NAME normRatelessReNackMultiTest COMMAND normRatelessTest)
    set_tests_properties(normRatelessReNackMultiTest PROPERTIES ENVIRONMENT
        "NORM_DECODE_SHORT_COUNT=8;NORM_NDATA=8;NORM_NPARITY=32;NORM_DATA_LEN=20000;NORM_RX_LOSS=40")
    set_tests_properties(normRatelessTest normCustomFecTest normRatelessExhaustionTest
        normRatelessReNackTest normRatelessReNackMultiTest
        PROPERTIES RUN_SERIAL TRUE TIMEOUT 75)
    # This test exercises internal sender repair state and message generation,
    # so compile it with the implementation sources instead of depending on
    # symbols hidden by shared-library builds.
    add_executable(normRepairFlagTest
        tests/normRepairFlagTest.cpp
        ${PLATFORM_SOURCE_FILES}
        ${COMMON_SOURCE_FILES})
    target_link_libraries(normRepairFlagTest PRIVATE protokit::protokit ${PLATFORM_LIBS})
    target_compile_definitions(normRepairFlagTest PRIVATE ${PLATFORM_DEFINITIONS})
    target_compile_options(normRepairFlagTest PRIVATE ${PLATFORM_FLAGS})
    target_include_directories(normRepairFlagTest PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    add_test(NAME normRepairFlagTest COMMAND normRepairFlagTest)
    set_tests_properties(normRepairFlagTest PROPERTIES TIMEOUT 15)
endif()
