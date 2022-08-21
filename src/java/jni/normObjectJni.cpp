#include "normJni.h"
#include "normObjectJni.h"

JNIEXPORT void JNICALL PKGNAME(NormObject_setNackingMode)
    (JNIEnv *env, jobject obj, jobject nackingMode) {
  NormObjectHandle objectHandle;
  NormNackingMode mode;

  objectHandle = (NormObjectHandle)env->GetLongField(obj,
    fid_NormObject_handle);
  mode = (NormNackingMode)env->CallIntMethod(nackingMode,
    mid_NormNackingMode_ordinal);

  NormObjectSetNackingMode(objectHandle, mode);
}

JNIEXPORT jobject JNICALL PKGNAME(NormObject_getType)
    (JNIEnv *env, jobject obj) {
  NormObjectHandle objectHandle;
  NormObjectType type;

  objectHandle = (NormObjectHandle)env->GetLongField(obj, fid_NormObject_handle);

  type = NormObjectGetType(objectHandle);

  // Get the enum value from the native value
  jobjectArray array = (jobjectArray)env->CallStaticObjectMethod(
    (jclass)env->NewLocalRef(jw_NormObjectType), mid_NormObjectType_values);
  return env->GetObjectArrayElement(array, type);
}

JNIEXPORT jbyteArray JNICALL PKGNAME(NormObject_getInfo)
    (JNIEnv *env, jobject obj) {
  NormObjectHandle objectHandle;

  objectHandle = (NormObjectHandle)env->GetLongField(obj,
    fid_NormObject_handle);

  // Check if there is any info attached
  if (!NormObjectHasInfo(objectHandle)) {
    return NULL;
  }

  int length = NormObjectGetInfoLength(objectHandle);

  // Create a new java array to hold the info
  jbyteArray info = env->NewByteArray(length);
  jbyte *ptr = env->GetByteArrayElements(info, NULL);

  // Get the info from the NormObject
  NormObjectGetInfo(objectHandle, (char*)ptr, length);

  // Release the jbyte* to copy them back to the jbyteArray
  env->ReleaseByteArrayElements(info, ptr, 0);

  return info;
}

JNIEXPORT jlong JNICALL PKGNAME(NormObject_getSize)
    (JNIEnv *env, jobject obj) {
  NormObjectHandle objectHandle;

  objectHandle = (NormObjectHandle)env->GetLongField(obj,
    fid_NormObject_handle);

  return (jlong)NormObjectGetSize(objectHandle);
}

JNIEXPORT jlong JNICALL PKGNAME(NormObject_getBytesPending)
    (JNIEnv *env, jobject obj) {
  NormObjectHandle objectHandle;

  objectHandle = (NormObjectHandle)env->GetLongField(obj,
    fid_NormObject_handle);

  return (jlong)NormObjectGetBytesPending(objectHandle);
}

JNIEXPORT void JNICALL PKGNAME(NormObject_cancel)
    (JNIEnv *env, jobject obj) {
  NormObjectHandle objectHandle;

  objectHandle = (NormObjectHandle)env->GetLongField(obj,
    fid_NormObject_handle);

  NormObjectCancel(objectHandle);
}

JNIEXPORT void JNICALL PKGNAME(NormObject_retain)
    (JNIEnv *env, jobject obj) {
  NormObjectHandle objectHandle;

  objectHandle = (NormObjectHandle)env->GetLongField(obj,
    fid_NormObject_handle);

  NormObjectRetain(objectHandle);
}

JNIEXPORT void JNICALL PKGNAME(NormObject_release)
    (JNIEnv *env, jobject obj) {
  NormObjectHandle objectHandle;

  objectHandle = (NormObjectHandle)env->GetLongField(obj,
    fid_NormObject_handle);

  NormObjectRelease(objectHandle);
}

JNIEXPORT jobject JNICALL PKGNAME(NormObject_getSender)
    (JNIEnv *env, jobject obj) {
  NormObjectHandle objectHandle;
  NormNodeHandle nodeHandle;

  objectHandle = (NormObjectHandle)env->GetLongField(obj,
    fid_NormObject_handle);

  nodeHandle = NormObjectGetSender(objectHandle);
  if (nodeHandle == NORM_NODE_INVALID) {
    env->ThrowNew((jclass)env->NewLocalRef(jw_IOException), "Locally originated sender object");
    return NULL;
  }

  // Create the NormNode
  return env->NewObject((jclass)env->NewLocalRef(jw_NormNode), mid_NormNode_init,
    (jlong)nodeHandle);
}

