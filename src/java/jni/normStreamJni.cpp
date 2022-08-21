#include "normJni.h"
#include "normStreamJni.h"

JNIEXPORT void JNICALL PKGNAME(NormStream_close)
    (JNIEnv *env, jobject obj, jboolean graceful) {
  NormObjectHandle objectHandle;

  objectHandle = (NormObjectHandle)env->GetLongField(obj,
    fid_NormObject_handle);

  NormStreamClose(objectHandle, graceful);
}

JNIEXPORT jint JNICALL PKGNAME(NormStream_write)
    (JNIEnv *env, jobject obj, jbyteArray buffer,
    jint offset, jint length) {
  NormObjectHandle objectHandle;
  jbyte *bytes;
  unsigned int n;

  objectHandle = (NormObjectHandle)env->GetLongField(obj,
    fid_NormObject_handle);
  bytes = env->GetByteArrayElements(buffer, 0);

  n = NormStreamWrite(objectHandle, (const char*)(bytes + offset), length);

  env->ReleaseByteArrayElements(buffer, bytes, JNI_ABORT);

  return (jint)n;
}

JNIEXPORT void JNICALL PKGNAME(NormStream_flush)
    (JNIEnv *env, jobject obj, jboolean eom, jobject flushMode) {
  NormObjectHandle objectHandle;
  NormFlushMode mode;

  objectHandle = (NormObjectHandle)env->GetLongField(obj,
    fid_NormObject_handle);
  mode = (NormFlushMode)env->CallIntMethod(flushMode,
    mid_NormFlushMode_ordinal);

  NormStreamFlush(objectHandle, eom, mode);
}

JNIEXPORT void JNICALL PKGNAME(NormStream_setAutoFlush)
    (JNIEnv *env, jobject obj, jobject flushMode) {
  NormObjectHandle objectHandle;
  NormFlushMode mode;

  objectHandle = (NormObjectHandle)env->GetLongField(obj,
    fid_NormObject_handle);
  mode = (NormFlushMode)env->CallIntMethod(flushMode,
    mid_NormFlushMode_ordinal);

  NormStreamSetAutoFlush(objectHandle, mode);
}

JNIEXPORT void JNICALL PKGNAME(NormStream_setPushEnable)
    (JNIEnv *env, jobject obj, jboolean pushEnable) {
  NormObjectHandle objectHandle;

  objectHandle = (NormObjectHandle)env->GetLongField(obj,
    fid_NormObject_handle);

  NormStreamSetPushEnable(objectHandle, pushEnable);
}

JNIEXPORT jboolean JNICALL PKGNAME(NormStream_hasVacancy)
    (JNIEnv *env, jobject obj) {
  NormObjectHandle objectHandle;

  objectHandle = (NormObjectHandle)env->GetLongField(obj,
    fid_NormObject_handle);

  return NormStreamHasVacancy(objectHandle);
}

JNIEXPORT void JNICALL PKGNAME(NormStream_markEom)
    (JNIEnv *env, jobject obj) {
  NormObjectHandle objectHandle;

  objectHandle = (NormObjectHandle)env->GetLongField(obj,
    fid_NormObject_handle);

  NormStreamMarkEom(objectHandle);
}

JNIEXPORT jint JNICALL PKGNAME(NormStream_read)
    (JNIEnv *env, jobject obj, jbyteArray buffer, jint offset, jint length) {
  NormObjectHandle objectHandle;
  jbyte *bytes;
  unsigned int n = length;

  objectHandle = (NormObjectHandle)env->GetLongField(obj,
    fid_NormObject_handle);
  bytes = env->GetByteArrayElements(buffer, 0);

  if (!NormStreamRead(objectHandle, (char*)(bytes + offset), &n)) {
    return -1;
  }

  env->ReleaseByteArrayElements(buffer, bytes, 0);

  return (jint)n;
}

JNIEXPORT jboolean JNICALL PKGNAME(NormStream_seekMsgStart)
    (JNIEnv *env, jobject obj) {
  NormObjectHandle objectHandle;

  objectHandle = (NormObjectHandle)env->GetLongField(obj,
    fid_NormObject_handle);

  return NormStreamSeekMsgStart(objectHandle);
}

JNIEXPORT jlong JNICALL PKGNAME(NormStream_getReadOffset)
    (JNIEnv *env, jobject obj) {
  NormObjectHandle objectHandle;

  objectHandle = (NormObjectHandle)env->GetLongField(obj,
    fid_NormObject_handle);

  return NormStreamGetReadOffset(objectHandle);
}

