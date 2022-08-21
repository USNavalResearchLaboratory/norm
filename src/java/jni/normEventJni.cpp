#include "normJni.h"
#include "normEventJni.h"

JNIEXPORT jobject JNICALL PKGNAME(NormEvent_getObject)
    (JNIEnv *env, jobject obj) {
  NormObjectHandle objectHandle;
  jobject object = NULL;

  objectHandle = (NormObjectHandle)env->GetLongField(obj, fid_NormEvent_objectHandle);

  switch (NormObjectGetType(objectHandle)) {
    case NORM_OBJECT_DATA: 
      object = env->NewObject((jclass)env->NewLocalRef(jw_NormData), mid_NormData_init,
        (jlong)objectHandle);
      break;

    case NORM_OBJECT_FILE:
      object = env->NewObject((jclass)env->NewLocalRef(jw_NormFile), mid_NormFile_init,
        (jlong)objectHandle);
      break;

    case NORM_OBJECT_STREAM:
      object = env->NewObject((jclass)env->NewLocalRef(jw_NormStream), mid_NormStream_init,
        (jlong)objectHandle);
      break;

    default:
    case NORM_OBJECT_NONE:
      break;
  }

  return object;
}

