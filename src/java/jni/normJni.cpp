#include "normJni.h"

#ifdef WIN32
#define snprintf _snprintf
#endif

jweak jw_NormInstance;
jfieldID fid_NormInstance_handle;

jweak jw_NormAckingStatus;
jmethodID mid_NormAckingStatus_values;

jweak jw_NormData;
jmethodID mid_NormData_init;

jweak jw_NormEvent;
jmethodID mid_NormEvent_init;
jfieldID fid_NormEvent_objectHandle;

jweak jw_NormEventType;
jmethodID mid_NormEventType_values;

jweak jw_NormFile;
jmethodID mid_NormFile_init;

jweak jw_NormFlushMode;
jmethodID mid_NormFlushMode_ordinal;

jweak jw_NormNackingMode;
jmethodID mid_NormNackingMode_ordinal;

jweak jw_NormNode;
jfieldID fid_NormNode_handle;
jmethodID mid_NormNode_init;

jweak jw_NormObject;
jfieldID fid_NormObject_handle;

jweak jw_NormObjectType;
jmethodID mid_NormObjectType_values;

jweak jw_NormProbingMode;
jmethodID mid_NormProbingMode_ordinal;

jweak jw_NormRepairBoundary;
jmethodID mid_NormRepairBoundary_ordinal;

jweak jw_NormSession;
jfieldID fid_NormSession_handle;
jmethodID mid_NormSession_init;

jweak jw_NormStream;
jmethodID mid_NormStream_init;

jweak jw_NormSyncPolicy;
jmethodID mid_NormSyncPolicy_ordinal;

jweak jw_InetAddress;
jmethodID mid_InetAddress_getByAddress;

jweak jw_InetSocketAddress;
jmethodID mid_InetSocketAddress_init;

jweak jw_IOException;

void check_version(JNIEnv *env);

/* Called by the JVM when the library is loaded */
JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
  JNIEnv *env;

  if (vm->GetEnv((void **)&env, JNI_VERSION_1_4)) {
    return JNI_ERR; /* JNI version not supported */
  }

  /* Load the NormInstance first so we can compare the versions */
  jclass NormInstanceClass = env->FindClass("mil/navy/nrl/norm/NormInstance");
  jw_NormInstance = env->NewWeakGlobalRef(NormInstanceClass);
  fid_NormInstance_handle = env->GetFieldID(NormInstanceClass, "handle", "J");

  /* Check the version */
  check_version(env);

  jclass NormAckingStatusClass = env->FindClass("mil/navy/nrl/norm/enums/NormAckingStatus");
  jw_NormAckingStatus = env->NewWeakGlobalRef(NormAckingStatusClass);
  mid_NormAckingStatus_values = env->GetStaticMethodID(NormAckingStatusClass,
    "values", "()[Lmil/navy/nrl/norm/enums/NormAckingStatus;");

  jclass NormDataClass = env->FindClass("mil/navy/nrl/norm/NormData");
  jw_NormData = env->NewWeakGlobalRef(NormDataClass);
  mid_NormData_init = env->GetMethodID(NormDataClass, "<init>", "(J)V");

  jclass NormEventClass = env->FindClass("mil/navy/nrl/norm/NormEvent");
  jw_NormEvent = env->NewWeakGlobalRef(NormEventClass);
  fid_NormEvent_objectHandle = env->GetFieldID(NormEventClass,"objectHandle", "J");
  mid_NormEvent_init = env->GetMethodID(NormEventClass, "<init>",
    "(Lmil/navy/nrl/norm/enums/NormEventType;JJJ)V");

  jclass NormEventTypeClass = env->FindClass("mil/navy/nrl/norm/enums/NormEventType");
  jw_NormEventType = env->NewWeakGlobalRef(NormEventTypeClass);
  mid_NormEventType_values = env->GetStaticMethodID(NormEventTypeClass,
    "values", "()[Lmil/navy/nrl/norm/enums/NormEventType;");

  jclass NormFileClass = env->FindClass("mil/navy/nrl/norm/NormFile");
  jw_NormFile = env->NewWeakGlobalRef(NormFileClass);
  mid_NormFile_init = env->GetMethodID(NormFileClass, "<init>", "(J)V");

  jclass NormFlushModeClass = env->FindClass("mil/navy/nrl/norm/enums/NormFlushMode");
  jw_NormFlushMode = env->NewWeakGlobalRef(NormFlushModeClass);
  mid_NormFlushMode_ordinal = env->GetMethodID(NormFlushModeClass,
    "ordinal", "()I");

  jclass NormNackingModeClass = env->FindClass("mil/navy/nrl/norm/enums/NormNackingMode");
  jw_NormNackingMode = env->NewWeakGlobalRef(NormNackingModeClass);
  mid_NormNackingMode_ordinal = env->GetMethodID(NormNackingModeClass,
    "ordinal", "()I");

  jclass NormNodeClass = env->FindClass("mil/navy/nrl/norm/NormNode");
  jw_NormNode = env->NewWeakGlobalRef(NormNodeClass);
  fid_NormNode_handle = env->GetFieldID(NormNodeClass, "handle", "J");
  mid_NormNode_init = env->GetMethodID(NormNodeClass, "<init>", "(J)V");

  jclass NormObjectClass = env->FindClass("mil/navy/nrl/norm/NormObject");
  jw_NormObject = env->NewWeakGlobalRef(NormObjectClass);
  fid_NormObject_handle = env->GetFieldID(NormObjectClass, "handle", "J");

  jclass NormObjectTypeClass = env->FindClass("mil/navy/nrl/norm/enums/NormObjectType");
  jw_NormObjectType = env->NewWeakGlobalRef(NormObjectTypeClass);
  mid_NormObjectType_values = env->GetStaticMethodID(NormObjectTypeClass,
    "values", "()[Lmil/navy/nrl/norm/enums/NormObjectType;");

  jclass NormProbingModeClass = env->FindClass("mil/navy/nrl/norm/enums/NormProbingMode");
  jw_NormProbingMode = env->NewWeakGlobalRef(NormProbingModeClass);
  mid_NormProbingMode_ordinal = env->GetMethodID(NormProbingModeClass,
    "ordinal", "()I");

  jclass NormRepairBoundaryClass = env->FindClass("mil/navy/nrl/norm/enums/NormRepairBoundary");
  jw_NormRepairBoundary = env->NewWeakGlobalRef(NormRepairBoundaryClass);
  mid_NormRepairBoundary_ordinal = env->GetMethodID(NormRepairBoundaryClass,
    "ordinal", "()I");

  jclass NormSessionClass = env->FindClass("mil/navy/nrl/norm/NormSession");
  jw_NormSession = env->NewWeakGlobalRef(NormSessionClass);
  fid_NormSession_handle = env->GetFieldID(NormSessionClass, "handle", "J");
  mid_NormSession_init = env->GetMethodID(NormSessionClass, "<init>", "(J)V");

  jclass NormStreamClass = env->FindClass("mil/navy/nrl/norm/NormStream");
  jw_NormStream = env->NewWeakGlobalRef(NormStreamClass);
  mid_NormStream_init = env->GetMethodID(NormStreamClass, "<init>", "(J)V");

  jclass NormSyncPolicyClass = env->FindClass("mil/navy/nrl/norm/enums/NormSyncPolicy");
  jw_NormSyncPolicy = env->NewWeakGlobalRef(NormSyncPolicyClass);
  mid_NormSyncPolicy_ordinal = env->GetMethodID(NormSyncPolicyClass,
    "ordinal", "()I");

  jclass InetAddressClass = env->FindClass("java/net/InetAddress");
  jw_InetAddress = env->NewWeakGlobalRef(InetAddressClass);
  mid_InetAddress_getByAddress = env->GetStaticMethodID(InetAddressClass,
    "getByAddress", "([B)Ljava/net/InetAddress;");

  jclass InetSocketAddressClass = env->FindClass("java/net/InetSocketAddress");
  jw_InetSocketAddress = env->NewWeakGlobalRef(InetSocketAddressClass);
  mid_InetSocketAddress_init = env->GetMethodID(InetSocketAddressClass,
    "<init>", "(Ljava/net/InetAddress;I)V");

  jw_IOException = (jclass)env->NewWeakGlobalRef(
    env->FindClass("java/io/IOException"));

  return JNI_VERSION_1_4;
}

void check_version(JNIEnv *env) {
  jfieldID fid;
  jstring versionString;
  const char *version;

  jclass LocalNormInstance = (jclass)env->NewLocalRef(jw_NormInstance);
  fid = env->GetStaticFieldID(LocalNormInstance, "VERSION", "Ljava/lang/String;");
  if (fid == NULL) {
    env->FatalError("Could not obtain version from NormInstance class."
      "  The native library and jar file are not compatible.\n");
  }

  versionString = (jstring)env->GetStaticObjectField(LocalNormInstance, fid);
  version = env->GetStringUTFChars(versionString, NULL);

  if (strcmp(VERSION, version) != 0) {
    char str[1024];
    snprintf(str, 1024, "Warning: NORM JNI versions do not match %s != %s."
      "  The native library and jar file are not compatible.\n",
      VERSION, version);
    env->FatalError(str);
  }

  env->ReleaseStringUTFChars(versionString, version);
}

/* Called by the JVM when the library is unloaded */
JNIEXPORT void JNICALL JNI_OnUnload(JavaVM *vm, void *reserved) {
  JNIEnv *env;

  if (vm->GetEnv((void **)&env, JNI_VERSION_1_2)) {
    return;
  }

  env->DeleteWeakGlobalRef(jw_InetAddress);
  env->DeleteWeakGlobalRef(jw_InetSocketAddress);
  env->DeleteWeakGlobalRef(jw_IOException);
  env->DeleteWeakGlobalRef(jw_NormAckingStatus);
  env->DeleteWeakGlobalRef(jw_NormData);
  env->DeleteWeakGlobalRef(jw_NormEvent);
  env->DeleteWeakGlobalRef(jw_NormEventType);
  env->DeleteWeakGlobalRef(jw_NormFile);
  env->DeleteWeakGlobalRef(jw_NormFlushMode);
  env->DeleteWeakGlobalRef(jw_NormInstance);
  env->DeleteWeakGlobalRef(jw_NormNackingMode);
  env->DeleteWeakGlobalRef(jw_NormNode);
  env->DeleteWeakGlobalRef(jw_NormObject);
  env->DeleteWeakGlobalRef(jw_NormObjectType);
  env->DeleteWeakGlobalRef(jw_NormProbingMode);
  env->DeleteWeakGlobalRef(jw_NormRepairBoundary);
  env->DeleteWeakGlobalRef(jw_NormSession);
  env->DeleteWeakGlobalRef(jw_NormStream);
  env->DeleteWeakGlobalRef(jw_NormSyncPolicy);
}

