
cmake_minimum_required(VERSION 3.4.1)

project("norm")

add_definitions("-DANDROID=1" "-DANDROID_API_VERSION=15" "-DPUSH_NOTIFICATIONS=1" "-DANDROID" "-DLINUX" "-DUNIX" "-DHAVE_IPV6" "-DHAVE_DIRFD" "-DPROTO_DEBUG" "-DHAVE_ASSERT" "-DHAVE_GETLOGIN" "-DUSE_SELECT" "-D_FILE_OFFSET_BITS=64" "-DHAVE_OLD_SIGNALHANDLER" "-DHAVE_SCHED" "-DNO_SCM_RIGHTS" "-Wno-attributes" "-DAPP_VERSION=1.0.0" "-DAPP_VERSION_HEX=0x10000")

include_directories( AFTER
    "../../OID_NDK}/sources/android/cpufeatures"
    "../../include"
    "../../protolib/include"
)

enable_language(ASM)

IF(BUILD_CONFIGURATION MATCHES "DEBUG")
    SET(BINARY_NAME "norm")
    add_definitions("-DDEBUG=1" "-D_DEBUG=1")
ELSEIF(BUILD_CONFIGURATION MATCHES "RELEASE")
    SET(BINARY_NAME "norm")
    add_definitions("-DNDEBUG=1")
ELSE(BUILD_CONFIGURATION MATCHES "DEBUG")
    MESSAGE( FATAL_ERROR "No matching build-configuration found." )
ENDIF(BUILD_CONFIGURATION MATCHES "DEBUG")

add_subdirectory("../../protolib/android/lib" ./protolib)

add_library( ${BINARY_NAME}

    STATIC
    
    "../../src/common/galois.cpp"
    "../../src/common/normApi.cpp"
    "../../src/common/normEncoder.cpp"
    "../../src/common/normEncoderMDP.cpp"
    "../../src/common/normEncoderRS16.cpp"
    "../../src/common/normEncoderRS8.cpp"
    "../../src/common/normFile.cpp"
    "../../src/common/normMessage.cpp"
    "../../src/common/normNode.cpp"
    "../../src/common/normObject.cpp"
    "../../src/common/normSegment.cpp"
    "../../src/common/normSession.cpp"
)

add_library( mil_navy_nrl_norm

    SHARED
    
    "../../src/java/jni/normDataJni.cpp"
	"../../src/java/jni/normEventJni.cpp"
	"../../src/java/jni/normFileJni.cpp"
	"../../src/java/jni/normInstanceJni.cpp"
	"../../src/java/jni/normJni.cpp"
	"../../src/java/jni/normNodeJni.cpp"
	"../../src/java/jni/normObjectJni.cpp"
	"../../src/java/jni/normSessionJni.cpp"
	"../../src/java/jni/normStreamJni.cpp"
)

find_library(log "log")
find_library(android "android")
find_library(glesv2 "GLESv2")
find_library(egl "EGL")

target_link_libraries(mil_navy_nrl_norm
    norm
    protokit
    ${log}
    ${android}
    ${glesv2}
    ${egl}
)
