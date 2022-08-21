#include "normJni.h"
#include "normFileJni.h"

JNIEXPORT jstring JNICALL PKGNAME(NormFile_getName)
    (JNIEnv *env, jobject obj) {
  NormObjectHandle objectHandle;
  char path[FILENAME_MAX];

  objectHandle = (NormObjectHandle)env->GetLongField(obj, fid_NormObject_handle);

  // Get the path information from NORM
  if (!NormFileGetName(objectHandle, path, FILENAME_MAX)) {
    env->ThrowNew((jclass)env->NewLocalRef(jw_IOException), "Failed to get file name");
    return NULL;
  }

  // Create a new Java string from the path
  jstring str = env->NewStringUTF(path);

  return str;
}

JNIEXPORT void JNICALL PKGNAME(NormFile_rename)
    (JNIEnv *env, jobject obj, jstring filename) {
  NormObjectHandle objectHandle;
  const char *str;

  objectHandle = (NormObjectHandle)env->GetLongField(obj, fid_NormObject_handle);
  str = env->GetStringUTFChars(filename, NULL);

  if (!NormFileRename(objectHandle, str)) {
    env->ReleaseStringUTFChars(filename, str);
    env->ThrowNew((jclass)env->NewLocalRef(jw_IOException), "Failed to rename file");
    return;
  }

  env->ReleaseStringUTFChars(filename, str);
}

