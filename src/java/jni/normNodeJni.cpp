#include "normJni.h"
#include "normNodeJni.h"

JNIEXPORT void JNICALL PKGNAME(NormNode_setUnicastNack)
    (JNIEnv *env, jobject obj, jboolean state) {
  NormNodeHandle nodeHandle;

  nodeHandle = (NormNodeHandle)env->GetLongField(obj, fid_NormNode_handle);

  NormNodeSetUnicastNack(nodeHandle, state);
}

JNIEXPORT void JNICALL PKGNAME(NormNode_setNackingMode)
    (JNIEnv *env, jobject obj, jobject nackingMode) {
  NormNodeHandle nodeHandle;
  NormNackingMode mode;

  nodeHandle = (NormNodeHandle)env->GetLongField(obj, fid_NormNode_handle);
  mode = (NormNackingMode)env->CallIntMethod(nackingMode,
    mid_NormNackingMode_ordinal);

  NormNodeSetNackingMode(nodeHandle, mode);
}

JNIEXPORT void JNICALL PKGNAME(NormNode_setRepairBoundary)
    (JNIEnv *env, jobject obj, jobject repairBoundary) {
  NormNodeHandle nodeHandle;
  NormRepairBoundary mode;

  nodeHandle = (NormNodeHandle)env->GetLongField(obj, fid_NormNode_handle);
  mode = (NormRepairBoundary)env->CallIntMethod(repairBoundary,
    mid_NormRepairBoundary_ordinal);

  NormNodeSetRepairBoundary(nodeHandle, mode);
}

JNIEXPORT void JNICALL PKGNAME(NormNode_setRxRobustFactor)
    (JNIEnv *env, jobject obj, jint robustFactor) {
  NormNodeHandle nodeHandle;

  nodeHandle = (NormNodeHandle)env->GetLongField(obj, fid_NormNode_handle);

  NormNodeSetRxRobustFactor(nodeHandle, robustFactor);
}

JNIEXPORT jlong JNICALL PKGNAME(NormNode_getId)
    (JNIEnv *env, jobject obj) {
  NormNodeHandle handle;

  handle = (NormNodeHandle)env->GetLongField(obj, fid_NormNode_handle);

  return (jlong)NormNodeGetId(handle);
}

JNIEXPORT jobject JNICALL PKGNAME(NormNode_getAddress)
    (JNIEnv *env, jobject obj) {
  NormNodeHandle handle;
  char buffer[256];
  unsigned int bufferLen;
  UINT16 port;
  jobject address;
  jbyteArray array;
  jbyte *ptr;

  handle = (NormNodeHandle)env->GetLongField(obj, fid_NormNode_handle);

  // Get the node address and port
  bufferLen = sizeof(buffer);
  if (!NormNodeGetAddress(handle, buffer, &bufferLen, &port)) {
    env->ThrowNew((jclass)env->NewLocalRef(jw_IOException), "Failed to get node address");
    return NULL;
  }

  // Copy the address into a jbyte*
  array = env->NewByteArray(bufferLen);
  ptr = env->GetByteArrayElements(array, 0);
  memcpy(ptr, buffer, bufferLen);
  env->ReleaseByteArrayElements(array, ptr, 0);

  // Create a new InetAddress
  address = env->CallStaticObjectMethod((jclass)env->NewLocalRef(jw_InetAddress),
    mid_InetAddress_getByAddress, array);

  // Create a new InetSocketAddress
  return env->NewObject((jclass)env->NewLocalRef(jw_InetSocketAddress), mid_InetSocketAddress_init,
    address, (jint)port);
}

JNIEXPORT jdouble JNICALL PKGNAME(NormNode_getGrtt)
    (JNIEnv *env, jobject obj) {
  NormNodeHandle nodeHandle;

  nodeHandle = (NormNodeHandle)env->GetLongField(obj, fid_NormNode_handle);

  return NormNodeGetGrtt(nodeHandle);
}

JNIEXPORT jint JNICALL PKGNAME(NormNode_getCommand)
    (JNIEnv *env, jobject obj, jbyteArray buffer, jint offset, jint length) {
  NormNodeHandle nodeHandle;
  jbyte *bytes;
  unsigned int n = length;

  nodeHandle = (NormNodeHandle)env->GetLongField(obj, fid_NormNode_handle);
  bytes = env->GetByteArrayElements(buffer, 0);

  if (!NormNodeGetCommand(nodeHandle, (char*)(bytes + offset), &n)) {
    env->ReleaseByteArrayElements(buffer, bytes, 0);
    env->ThrowNew((jclass)env->NewLocalRef(jw_IOException), "Failed to get command");
    return -1;
  }

  env->ReleaseByteArrayElements(buffer, bytes, 0);

  return (jint)n;
}

JNIEXPORT void JNICALL PKGNAME(NormNode_freeBuffers)
    (JNIEnv *env, jobject obj) {
  NormNodeHandle nodeHandle;

  nodeHandle = (NormNodeHandle)env->GetLongField(obj, fid_NormNode_handle);

  NormNodeFreeBuffers(nodeHandle);
}

JNIEXPORT void JNICALL PKGNAME(NormNode_retain)
    (JNIEnv *env, jobject obj) {
  NormNodeHandle nodeHandle;

  nodeHandle = (NormNodeHandle)env->GetLongField(obj, fid_NormNode_handle);

  NormNodeRetain(nodeHandle);
}

JNIEXPORT void JNICALL PKGNAME(NormNode_release)
    (JNIEnv *env, jobject obj) {
  NormNodeHandle nodeHandle;

  nodeHandle = (NormNodeHandle)env->GetLongField(obj, fid_NormNode_handle);

  NormNodeRelease(nodeHandle);
}

