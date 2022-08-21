#include "normJni.h"
#include "normInstanceJni.h"

#ifndef WIN32
#include <sys/select.h>
#endif

JNIEXPORT void JNICALL PKGNAME(NormInstance_createInstance)
    (JNIEnv *env, jobject obj, jboolean priorityBoost) {
  NormInstanceHandle handle;

  handle = NormCreateInstance(priorityBoost);
  if (handle == NORM_INSTANCE_INVALID) {
    env->ThrowNew((jclass)env->NewLocalRef(jw_IOException), "Failed to create NORM instance");
    return;
  }

  env->SetLongField(obj, fid_NormInstance_handle, (jlong)handle);
}

JNIEXPORT void JNICALL PKGNAME(NormInstance_destroyInstance)
    (JNIEnv *env, jobject obj) {
  NormInstanceHandle handle;

  handle = (NormInstanceHandle)env->GetLongField(obj, fid_NormInstance_handle);

  NormDestroyInstance(handle);
}

JNIEXPORT void JNICALL PKGNAME(NormInstance_stopInstance)
    (JNIEnv *env, jobject obj) {
  NormInstanceHandle handle;

  handle = (NormInstanceHandle)env->GetLongField(obj, fid_NormInstance_handle);

  NormStopInstance(handle);
}

JNIEXPORT jboolean JNICALL PKGNAME(NormInstance_restartInstance)
    (JNIEnv *env, jobject obj) {
  NormInstanceHandle handle;

  handle = (NormInstanceHandle)env->GetLongField(obj, fid_NormInstance_handle);

  return NormRestartInstance(handle);
}

JNIEXPORT jboolean JNICALL PKGNAME(NormInstance_suspendInstance)
    (JNIEnv *env, jobject obj) {
  NormInstanceHandle handle;

  handle = (NormInstanceHandle)env->GetLongField(obj, fid_NormInstance_handle);

  return NormSuspendInstance(handle);
}

JNIEXPORT void JNICALL PKGNAME(NormInstance_resumeInstance)
    (JNIEnv *env, jobject obj) {
  NormInstanceHandle handle;

  handle = (NormInstanceHandle)env->GetLongField(obj, fid_NormInstance_handle);

  NormResumeInstance(handle);
}

JNIEXPORT void JNICALL PKGNAME(NormInstance_setCacheDirectory)
    (JNIEnv *env, jobject obj, jstring cachePath) {
  NormInstanceHandle handle;
  const char *str;

  handle = (NormInstanceHandle)env->GetLongField(obj, fid_NormInstance_handle);
  str = env->GetStringUTFChars(cachePath, NULL);

  if (!NormSetCacheDirectory(handle, str)) {
    env->ReleaseStringUTFChars(cachePath, str);
    env->ThrowNew((jclass)env->NewLocalRef(jw_IOException), "Failed to set the cache directory");
    return;
  }

  env->ReleaseStringUTFChars(cachePath, str);
}

JNIEXPORT void JNICALL PKGNAME(NormInstance_openDebugLog)
    (JNIEnv *env, jobject obj, jstring filename) {
  NormInstanceHandle handle;
  const char *str;

  handle = (NormInstanceHandle)env->GetLongField(obj, fid_NormInstance_handle);
  str = env->GetStringUTFChars(filename, NULL);

  if (!NormOpenDebugLog(handle, str)) {
    env->ReleaseStringUTFChars(filename, str);
    env->ThrowNew((jclass)env->NewLocalRef(jw_IOException), "Failed to open debug log");
    return;
  }

  env->ReleaseStringUTFChars(filename, str);
}

JNIEXPORT void JNICALL PKGNAME(NormInstance_closeDebugLog)
    (JNIEnv *env, jobject obj) {
  NormInstanceHandle handle;
  
  handle = (NormInstanceHandle)env->GetLongField(obj, fid_NormInstance_handle);
  
  NormCloseDebugLog(handle);
}

JNIEXPORT void JNICALL PKGNAME(NormInstance_openDebugPipe)
    (JNIEnv *env, jobject obj, jstring pipename) {
  NormInstanceHandle handle;
  const char *str;
  
  handle = (NormInstanceHandle)env->GetLongField(obj, fid_NormInstance_handle);
  str = env->GetStringUTFChars(pipename, NULL);

  if (!NormOpenDebugLog(handle, str)) {
    env->ReleaseStringUTFChars(pipename, str);
    env->ThrowNew((jclass)env->NewLocalRef(jw_IOException), "Failed to open debug log");
    return;
  }

  env->ReleaseStringUTFChars(pipename, str);
}

JNIEXPORT void JNICALL PKGNAME(NormInstance_setDebugLevel)
    (JNIEnv *env, jobject obj, jint level) {
  NormSetDebugLevel(level);
}

JNIEXPORT jint JNICALL PKGNAME(NormInstance_getDebugLevel)
    (JNIEnv *env, jobject obj) {
  return NormGetDebugLevel();
}

JNIEXPORT jboolean JNICALL PKGNAME(NormInstance_hasNextEvent)
    (JNIEnv *env, jobject obj, jint sec, jint usec) {
  NormInstanceHandle handle;
  NormDescriptor nd;

  handle = (NormInstanceHandle)env->GetLongField(obj, fid_NormInstance_handle);
  nd = NormGetDescriptor(handle);
#ifndef WIN32
  struct timeval tv;
  fd_set fds;
  int rv;

  FD_ZERO(&fds);
  FD_SET(nd, &fds);

  tv.tv_sec = sec;
  tv.tv_usec = usec;

  rv = select(nd + 1, &fds, NULL, NULL, &tv);

  if (rv == -1) {
    env->ThrowNew((jclass)env->NewLocalRef(jw_IOException), "Failed to perform select");
    return false;
  }

  return rv != 0 ? true : false;
#else
  return WaitForSingleObject((HANDLE)nd, sec * 1000 + usec / 1000) != WAIT_FAILED;
#endif
}

JNIEXPORT jobject JNICALL PKGNAME(NormInstance_getNextEvent)
    (JNIEnv *env, jobject obj) {
  NormInstanceHandle handle;
  NormEvent event;

  handle = (NormInstanceHandle)env->GetLongField(obj, fid_NormInstance_handle);

  if (!NormGetNextEvent(handle, &event)) {
    //env->ThrowNew((jclass)env->NewLocalRef(jw_IOException), "Failed to get next event");
    return NULL;
  }

  // Get the event type
  jobjectArray array = (jobjectArray)env->CallStaticObjectMethod(
    (jclass)env->NewLocalRef(jw_NormEventType), mid_NormEventType_values);
  
  if (env->GetArrayLength(array) <= event.type) {
    env->ThrowNew((jclass)env->NewLocalRef(jw_IOException), "Invalid NORM event type (NormEventType.java out of sync with NORM API event header?)");
    return NULL;
  }
      
  jobject type = env->GetObjectArrayElement(array, event.type);

  // Create the event
  return env->NewObject((jclass)env->NewLocalRef(jw_NormEvent), mid_NormEvent_init, type,
      (jlong)event.session, (jlong)event.sender, (jlong)event.object);
}

JNIEXPORT jobject JNICALL PKGNAME(NormInstance_createSession)
    (JNIEnv *env, jobject obj, jstring address, jint port, jlong localNodeId) {
  NormInstanceHandle handle;
  NormSessionHandle session;
  const char *str;

  handle = (NormInstanceHandle)env->GetLongField(obj, fid_NormInstance_handle);
  str = env->GetStringUTFChars(address, NULL);

  session = NormCreateSession(handle, str, port, (NormNodeId)localNodeId);

  // Release the string  
  env->ReleaseStringUTFChars(address, str);

  if (session == NORM_SESSION_INVALID) {
    env->ThrowNew((jclass)env->NewLocalRef(jw_IOException), "Failed to create session");
    return NULL;
  }

  return env->NewObject((jclass)env->NewLocalRef(jw_NormSession), mid_NormSession_init, (jlong)session);
}

