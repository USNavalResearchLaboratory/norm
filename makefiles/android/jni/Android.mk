LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := norm
LOCAL_C_INCLUDES := \
	$(LOCAL_PATH)/../../../protolib/include \
	$(LOCAL_PATH)/../../../include
LOCAL_EXPORT_C_INCLUDES := $(LOCAL_C_INCLUDES)
LOCAL_STATIC_LIBRARIES := protolib

ifeq ($(APP_OPTIM),debug)
	LOCAL_CFLAGS += -DNORM_DEBUG
endif
LOCAL_EXPORT_CFLAGS := $(LOCAL_CFLAGS)

LOCAL_SRC_FILES := \
	../../../src/common/galois.cpp \
	../../../src/common/normApi.cpp \
	../../../src/common/normEncoder.cpp \
	../../../src/common/normEncoderMDP.cpp \
	../../../src/common/normEncoderRS16.cpp \
	../../../src/common/normEncoderRS8.cpp \
	../../../src/common/normFile.cpp \
	../../../src/common/normMessage.cpp \
	../../../src/common/normNode.cpp \
	../../../src/common/normObject.cpp \
	../../../src/common/normSegment.cpp \
	../../../src/common/normSession.cpp
include $(BUILD_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := mil_navy_nrl_norm
LOCAL_STATIC_LIBRARIES := norm protolib
LOCAL_SRC_FILES := \
	../../../src/java/jni/normDataJni.cpp \
	../../../src/java/jni/normEventJni.cpp \
	../../../src/java/jni/normFileJni.cpp \
	../../../src/java/jni/normInstanceJni.cpp \
	../../../src/java/jni/normJni.cpp \
	../../../src/java/jni/normNodeJni.cpp \
	../../../src/java/jni/normObjectJni.cpp \
	../../../src/java/jni/normSessionJni.cpp \
	../../../src/java/jni/normStreamJni.cpp
include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := norm-app
LOCAL_STATIC_LIBRARIES := norm
LOCAL_SRC_FILES := \
	../../../src/common/normApp.cpp \
	../../../src/common/normPostProcess.cpp \
	../../../src/unix/unixPostProcess.cpp
include $(BUILD_EXECUTABLE)


include $(CLEAR_VARS)
LOCAL_MODULE := normMsgr
LOCAL_STATIC_LIBRARIES := norm
LOCAL_SRC_FILES := \
	../../../examples/normMsgr.cpp
include $(BUILD_EXECUTABLE)

$(call import-module,protolib/makefiles/android/jni)
