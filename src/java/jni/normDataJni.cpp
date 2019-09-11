#include "normJni.h"
#include "normDataJni.h"

JNIEXPORT jbyteArray JNICALL PKGNAME(NormData_getData)
    (JNIEnv *env, jobject obj) {
  NormObjectHandle objectHandle;
  jbyteArray data;
  jbyte *ptr;
  const char *buffer;
  int length;

  objectHandle = (NormObjectHandle)env->GetLongField(obj,
    fid_NormObject_handle);

  // Get the data information from NORM
  buffer = NormDataAccessData(objectHandle);
  length = NormObjectGetSize(objectHandle);

  // Create a new java array to hold the data
  data = env->NewByteArray(length);

  // Get the element array and copy the data into it
  ptr = env->GetByteArrayElements(data, NULL);
  memcpy(ptr, buffer, length);

  // Release the array, commiting the changes back to the java array
  env->ReleaseByteArrayElements(data, ptr, 0);

  return data;
}

