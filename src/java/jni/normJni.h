#ifndef __NORMJNI_H__
#define __NORMJNI_H__

#include <jni.h>
#include <stdio.h>
#include <string.h>
#include <normApi.h>

/*
 * This version string is used to validate the compatibility between the Java
 * and C native libraries. Update this string along with it's counterpart in
 * the NormInstance.java file whenever the native API changes.
 */
#define VERSION "20130415-0927"

#define PKGNAME(str) Java_mil_navy_nrl_norm_##str

#ifdef __cplusplus
extern "C" {
#endif

extern jweak jw_InetAddress;
extern jmethodID mid_InetAddress_getByAddress;

extern jweak jw_InetSocketAddress;
extern jmethodID mid_InetSocketAddress_init;

extern jweak jw_IOException;

extern jweak jw_NormAckingStatus;
extern jmethodID mid_NormAckingStatus_values;

extern jweak jw_NormData;
extern jmethodID mid_NormData_init;

extern jweak jw_NormEvent;
extern jmethodID mid_NormEvent_init;
extern jfieldID fid_NormEvent_objectHandle;

extern jweak jw_NormEventType;
extern jmethodID mid_NormEventType_values;

extern jweak jw_NormFile;
extern jmethodID mid_NormFile_init;

extern jweak jw_NormFlushMode;
extern jmethodID mid_NormFlushMode_ordinal;

extern jweak jw_NormInstance;
extern jfieldID fid_NormInstance_handle;

extern jweak jw_NormNackingMode;
extern jmethodID mid_NormNackingMode_ordinal;

extern jweak jw_NormNode;
extern jfieldID fid_NormNode_handle;
extern jmethodID mid_NormNode_init;

extern jweak jw_NormObject;
extern jfieldID fid_NormObject_handle;

extern jweak jw_NormObjectType;
extern jmethodID mid_NormObjectType_values;

extern jweak jw_NormProbingMode;
extern jmethodID mid_NormProbingMode_ordinal;

extern jweak jw_NormRepairBoundary;
extern jmethodID mid_NormRepairBoundary_ordinal;

extern jweak jw_NormSession;
extern jfieldID fid_NormSession_handle;
extern jmethodID mid_NormSession_init;

extern jweak jw_NormStream;
extern jmethodID mid_NormStream_init;

extern jweak jw_NormSyncPolicy;
extern jmethodID mid_NormSyncPolicy_ordinal;

#ifdef __cplusplus
}
#endif

#endif
