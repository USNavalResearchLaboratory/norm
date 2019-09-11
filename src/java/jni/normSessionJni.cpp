#include "normJni.h"
#include "normSessionJni.h"

JNIEXPORT void JNICALL PKGNAME(NormSession_destroySessionNative)
    (JNIEnv *env, jobject obj) {
  NormSessionHandle session;

  session = (NormSessionHandle)env->GetLongField(obj, fid_NormSession_handle);

  NormDestroySession(session);
}

JNIEXPORT jlong JNICALL PKGNAME(NormSession_getLocalNodeId)
    (JNIEnv *env, jobject obj) {
  NormSessionHandle session;

  session = (NormSessionHandle)env->GetLongField(obj, fid_NormSession_handle);

  return NormGetLocalNodeId(session);
}

JNIEXPORT void JNICALL PKGNAME(NormSession_setTxPort)
    (JNIEnv *env, jobject obj, jint port, jboolean enableReuse, jstring txBindAddress) {
  NormSessionHandle session;
  const char *str = NULL;

  session = (NormSessionHandle)env->GetLongField(obj, fid_NormSession_handle);

  if (txBindAddress != NULL) {
    str = env->GetStringUTFChars(txBindAddress, NULL);
  }

  if (!NormSetTxPort(session, port, enableReuse, str)) {
    if (txBindAddress != NULL) {
      env->ReleaseStringUTFChars(txBindAddress, str);
    }
    env->ThrowNew((jclass)env->NewLocalRef(jw_IOException), "Invalid tx bind address");
    return;
  }

  if (txBindAddress != NULL) {
    env->ReleaseStringUTFChars(txBindAddress, str);
  }
}

JNIEXPORT void JNICALL PKGNAME(NormSession_setRxPortReuse)
    (JNIEnv *env, jobject obj, jboolean enable, jstring rxBindAddress, jstring senderAddress, jint senderPort) {
  NormSessionHandle session;
  const char *str1 = NULL;
  const char *str2 = NULL;

  session = (NormSessionHandle)env->GetLongField(obj, fid_NormSession_handle);

  if (rxBindAddress != NULL) {
    str1 = env->GetStringUTFChars(rxBindAddress, NULL);
  }
  if (senderAddress != NULL) {
    str2 = env->GetStringUTFChars(senderAddress, NULL);
  }

  NormSetRxPortReuse(session, enable, str1, str2, senderPort);

  if (rxBindAddress != NULL) {
    env->ReleaseStringUTFChars(rxBindAddress, str1);
  }
  if (senderAddress != NULL) {
    env->ReleaseStringUTFChars(senderAddress, str2);
  }
}

JNIEXPORT void JNICALL PKGNAME(NormSession_setEcnSupport)
    (JNIEnv *env, jobject obj, jboolean ecnEnable, jboolean ignoreLoss, jboolean tolerateLoss) {
  NormSessionHandle session;

  session = (NormSessionHandle)env->GetLongField(obj, fid_NormSession_handle);

  NormSetEcnSupport(session, ecnEnable, ignoreLoss, tolerateLoss);
}

JNIEXPORT void JNICALL PKGNAME(NormSession_setMulticastInterface)
    (JNIEnv *env, jobject obj, jstring interfaceName) {
  NormSessionHandle session;
  const char *str;

  session = (NormSessionHandle)env->GetLongField(obj, fid_NormSession_handle);

  str = env->GetStringUTFChars(interfaceName, NULL);

  if (!NormSetMulticastInterface(session, str)) {
    env->ReleaseStringUTFChars(interfaceName, str);
    env->ThrowNew((jclass)env->NewLocalRef(jw_IOException), "Failed to set multicast interface");
    return;
  }

  env->ReleaseStringUTFChars(interfaceName, str);
}


JNIEXPORT void JNICALL PKGNAME(NormSession_setSSM)
    (JNIEnv *env, jobject obj, jstring sourceAddr) {
  NormSessionHandle session;
  const char *str;

  session = (NormSessionHandle)env->GetLongField(obj, fid_NormSession_handle);

  str = env->GetStringUTFChars(sourceAddr, NULL);

  if (!NormSetSSM(session, str)) {
    env->ReleaseStringUTFChars(sourceAddr, str);
    env->ThrowNew((jclass)env->NewLocalRef(jw_IOException), "Failed to set SSM source address");
    return;
  }

  env->ReleaseStringUTFChars(sourceAddr, str);
}


JNIEXPORT void JNICALL PKGNAME(NormSession_setTTL)
    (JNIEnv *env, jobject obj, jbyte ttl) {
  NormSessionHandle session;

  session = (NormSessionHandle)env->GetLongField(obj, fid_NormSession_handle);

  if (!NormSetTTL(session, ttl)) {
    env->ThrowNew((jclass)env->NewLocalRef(jw_IOException), "Failed to set TTL");
    return;
  }
}

JNIEXPORT void JNICALL PKGNAME(NormSession_setTOS)
    (JNIEnv *env, jobject obj, jbyte tos) {
  NormSessionHandle session;

  session = (NormSessionHandle)env->GetLongField(obj, fid_NormSession_handle);

  if (!NormSetTOS(session, tos)) {
    env->ThrowNew((jclass)env->NewLocalRef(jw_IOException), "Failed to set TOS");
    return;
  }
}

JNIEXPORT void JNICALL PKGNAME(NormSession_setLoopback)
    (JNIEnv *env, jobject obj, jboolean loopbackEnable) {
  NormSessionHandle session;

  session = (NormSessionHandle)env->GetLongField(obj, fid_NormSession_handle);

  if (!NormSetLoopback(session, loopbackEnable)) {
    env->ThrowNew((jclass)env->NewLocalRef(jw_IOException), "Failed to set loopback");
    return;
  }
}

JNIEXPORT void JNICALL PKGNAME(NormSession_setMessageTrace)
    (JNIEnv *env, jobject obj, jboolean flag) {
  NormSessionHandle session;

  session = (NormSessionHandle)env->GetLongField(obj, fid_NormSession_handle);

  NormSetMessageTrace(session, flag);
}

JNIEXPORT void JNICALL PKGNAME(NormSession_setTxLoss)
    (JNIEnv *env, jobject obj, jdouble percent) {
  NormSessionHandle session;

  session = (NormSessionHandle)env->GetLongField(obj, fid_NormSession_handle);

  NormSetTxLoss(session, percent);
}

JNIEXPORT void JNICALL PKGNAME(NormSession_setRxLoss)
    (JNIEnv *env, jobject obj, jdouble percent) {
  NormSessionHandle session;

  session = (NormSessionHandle)env->GetLongField(obj, fid_NormSession_handle);

  NormSetRxLoss(session, percent);
}

JNIEXPORT void JNICALL PKGNAME(NormSession_setReportInterval)
    (JNIEnv *env, jobject obj, jdouble interval) {
  NormSessionHandle session;

  session = (NormSessionHandle)env->GetLongField(obj, fid_NormSession_handle);

  NormSetReportInterval(session, interval);
}

JNIEXPORT jdouble JNICALL PKGNAME(NormSession_getReportInterval)
    (JNIEnv *env, jobject obj) {
  NormSessionHandle session;

  session = (NormSessionHandle)env->GetLongField(obj, fid_NormSession_handle);

  return NormGetReportInterval(session);
}

JNIEXPORT void JNICALL PKGNAME(NormSession_startSender)
    (JNIEnv *env, jobject obj, jint sessionId, jlong bufferSpace,
    jint segmentSize, jshort blockSize, jshort numParity) {
  NormSessionHandle session;

  session = (NormSessionHandle)env->GetLongField(obj, fid_NormSession_handle);

  if (!NormStartSender(session, sessionId, bufferSpace, segmentSize,
      blockSize, numParity)) {
    env->ThrowNew((jclass)env->NewLocalRef(jw_IOException), "Failed to start sender");
    return;
  }
}

JNIEXPORT void JNICALL PKGNAME(NormSession_stopSender)
    (JNIEnv *env, jobject obj) {
  NormSessionHandle session;

  session = (NormSessionHandle)env->GetLongField(obj, fid_NormSession_handle);

  NormStopSender(session);
}

JNIEXPORT void JNICALL PKGNAME(NormSession_setTxOnly)
    (JNIEnv *env, jobject obj, jboolean txOnly) {
  NormSessionHandle session;

  session = (NormSessionHandle)env->GetLongField(obj, fid_NormSession_handle);

  NormSetTxOnly(session, txOnly);
}

JNIEXPORT jdouble JNICALL PKGNAME(NormSession_getTxRate)
    (JNIEnv *env, jobject obj) {
  NormSessionHandle session;

  session = (NormSessionHandle)env->GetLongField(obj, fid_NormSession_handle);

  return NormGetTxRate(session);
}

JNIEXPORT void JNICALL PKGNAME(NormSession_setTxRate)
    (JNIEnv *env, jobject obj, jdouble rate) {
  NormSessionHandle session;

  session = (NormSessionHandle)env->GetLongField(obj, fid_NormSession_handle);

  NormSetTxRate(session, rate);
}

JNIEXPORT void JNICALL PKGNAME(NormSession_setFlowControl)
    (JNIEnv *env, jobject obj, jdouble flowControlFactor) {
  NormSessionHandle session;

  session = (NormSessionHandle)env->GetLongField(obj, fid_NormSession_handle);

  NormSetFlowControl(session, flowControlFactor);
}

JNIEXPORT void JNICALL PKGNAME(NormSession_setTxSocketBuffer)
    (JNIEnv *env, jobject obj, jlong bufferSize) {
  NormSessionHandle session;

  session = (NormSessionHandle)env->GetLongField(obj, fid_NormSession_handle);

  if (!NormSetTxSocketBuffer(session, bufferSize)) {
    env->ThrowNew((jclass)env->NewLocalRef(jw_IOException), "Failed to set tx socket buffer");
    return;
  }
}

JNIEXPORT void JNICALL PKGNAME(NormSession_setCongestionControl)
    (JNIEnv *env, jobject obj, jboolean enable, jboolean adjustRate) {
  NormSessionHandle session;

  session = (NormSessionHandle)env->GetLongField(obj, fid_NormSession_handle);

  NormSetCongestionControl(session, enable, adjustRate);
}

JNIEXPORT void JNICALL PKGNAME(NormSession_setTxRateBounds)
    (JNIEnv *env, jobject obj, jdouble rateMin, jdouble rateMax) {
  NormSessionHandle session;

  session = (NormSessionHandle)env->GetLongField(obj, fid_NormSession_handle);

  NormSetTxRateBounds(session, rateMin, rateMax);
}

JNIEXPORT void JNICALL PKGNAME(NormSession_setTxCacheBounds)
    (JNIEnv *env, jobject obj, jlong sizeMax, jlong countMin, jlong countMax) {
  NormSessionHandle session;

  session = (NormSessionHandle)env->GetLongField(obj, fid_NormSession_handle);

  NormSetTxCacheBounds(session, (NormSize)sizeMax, countMin, countMax);
}

JNIEXPORT void JNICALL PKGNAME(NormSession_setAutoParity)
    (JNIEnv *env, jobject obj, jshort autoParity) {
  NormSessionHandle session;

  session = (NormSessionHandle)env->GetLongField(obj, fid_NormSession_handle);

  NormSetAutoParity(session, autoParity);
}

JNIEXPORT void JNICALL PKGNAME(NormSession_setGrttEstimate)
    (JNIEnv *env, jobject obj, jdouble grtt) {
  NormSessionHandle session;

  session = (NormSessionHandle)env->GetLongField(obj, fid_NormSession_handle);

  NormSetGrttEstimate(session, grtt);
}

JNIEXPORT jdouble JNICALL PKGNAME(NormSession_getGrttEstimate)
    (JNIEnv *env, jobject obj) {
  NormSessionHandle session;

  session = (NormSessionHandle)env->GetLongField(obj, fid_NormSession_handle);

  return NormGetGrttEstimate(session);
}

JNIEXPORT void JNICALL PKGNAME(NormSession_setGrttMax)
    (JNIEnv *env, jobject obj, jdouble grttMax) {
  NormSessionHandle session;

  session = (NormSessionHandle)env->GetLongField(obj, fid_NormSession_handle);

  NormSetGrttMax(session, grttMax);
}

JNIEXPORT void JNICALL PKGNAME(NormSession_setGrttProbingMode)
    (JNIEnv *env, jobject obj, jobject probingMode) {
  NormSessionHandle session;
  NormProbingMode mode;

  session = (NormSessionHandle)env->GetLongField(obj, fid_NormSession_handle);
  mode = (NormProbingMode)env->CallIntMethod(probingMode,
    mid_NormProbingMode_ordinal);

  NormSetGrttProbingMode(session, mode);
}

JNIEXPORT void JNICALL PKGNAME(NormSession_setGrttProbingInterval)
    (JNIEnv *env, jobject obj, jdouble intervalMin, jdouble intervalMax) {
  NormSessionHandle session;

  session = (NormSessionHandle)env->GetLongField(obj, fid_NormSession_handle);

  NormSetGrttProbingInterval(session, intervalMin, intervalMax);
}

JNIEXPORT void JNICALL PKGNAME(NormSession_setBackoffFactor)
    (JNIEnv *env, jobject obj, jdouble backoffFactor) {
  NormSessionHandle session;

  session = (NormSessionHandle)env->GetLongField(obj, fid_NormSession_handle);

  NormSetBackoffFactor(session, backoffFactor);
}

JNIEXPORT void JNICALL PKGNAME(NormSession_setGroupSize)
    (JNIEnv *env, jobject obj, jlong groupSize) {
  NormSessionHandle session;

  session = (NormSessionHandle)env->GetLongField(obj, fid_NormSession_handle);

  NormSetGroupSize(session, groupSize);
}

JNIEXPORT void JNICALL PKGNAME(NormSession_setTxRobustFactor)
    (JNIEnv *env, jobject obj, jint robustFactor) {
  NormSessionHandle session;

  session = (NormSessionHandle)env->GetLongField(obj, fid_NormSession_handle);

  NormSetTxRobustFactor(session, robustFactor);
}

JNIEXPORT jobject JNICALL PKGNAME(NormSession_fileEnqueue)
    (JNIEnv *env, jobject obj, jstring filename, jbyteArray info,
    jint infoOffset, jint infoLength) {
  NormSessionHandle session;
  NormObjectHandle objectHandle;
  const char *str;
  jbyte *infoBytes = NULL;

  session = (NormSessionHandle)env->GetLongField(obj, fid_NormSession_handle);
  str = env->GetStringUTFChars(filename, NULL);
  if (info != NULL) {
    infoBytes = env->GetByteArrayElements(info, 0);
  }

  objectHandle = NormFileEnqueue(session, str,
    (const char*)(infoBytes + infoOffset), infoLength);

  env->ReleaseStringUTFChars(filename, str);
  if (info != NULL) {
    env->ReleaseByteArrayElements(info, infoBytes, JNI_ABORT);
  }

  if (objectHandle == NORM_OBJECT_INVALID) {
    env->ThrowNew((jclass)env->NewLocalRef(jw_IOException), "Failed to enqueue file");
    return NULL;
  }

  // Create the NormObject
  return env->NewObject((jclass)env->NewLocalRef(jw_NormFile), mid_NormFile_init,
    (jlong)objectHandle);
}

JNIEXPORT jobject JNICALL PKGNAME(NormSession_dataEnqueue)
(JNIEnv *env, jobject obj, jobject dataBuffer, jint dataOffset,
    jint dataLength, jbyteArray info, jint infoOffset, jint infoLength) {
  NormSessionHandle session;
  NormObjectHandle objectHandle;
  char *dataPtr;
  jbyte *infoBytes;

  session = (NormSessionHandle)env->GetLongField(obj, fid_NormSession_handle);

  // Get the data ByteBuffer's address
  dataPtr = (char*)env->GetDirectBufferAddress(dataBuffer);
  if (dataPtr == NULL) {
    env->ThrowNew((jclass)env->NewLocalRef(jw_IOException), "Cannot access direct ByteBuffer address");
    return NULL;
  }
  dataPtr = dataPtr + dataOffset;

  // Get the info byte array
  if (info != NULL) {
    infoBytes = env->GetByteArrayElements(info, 0);
  } else {
    infoBytes = NULL;
    infoOffset = 0;
    infoLength = 0;
  }

  objectHandle = NormDataEnqueue(session, dataPtr, dataLength,
      (char*)infoBytes + infoOffset, infoLength);

  // Release the info byte array
  if (info != NULL) {
    env->ReleaseByteArrayElements(info, infoBytes, JNI_ABORT);
  }

  if (objectHandle == NORM_OBJECT_INVALID) {
    env->ThrowNew((jclass)env->NewLocalRef(jw_IOException), "Failed to enqueue data");
    return NULL;
  }

  // Create the NormObject
  return env->NewObject((jclass)env->NewLocalRef(jw_NormData), mid_NormData_init,
    (jlong)objectHandle);
}

JNIEXPORT void JNICALL PKGNAME(NormSession_requeueObject)
    (JNIEnv *env, jobject obj, jobject object) {
  NormSessionHandle session;
  NormObjectHandle objectHandle;

  session = (NormSessionHandle)env->GetLongField(obj, fid_NormSession_handle);
  objectHandle = (NormObjectHandle)env->GetLongField(object, fid_NormObject_handle);

  if (!NormRequeueObject(session, objectHandle)) {
    env->ThrowNew((jclass)env->NewLocalRef(jw_IOException), "Failed to requeue object");
    return;
  }
}

JNIEXPORT jobject JNICALL PKGNAME(NormSession_streamOpen)
    (JNIEnv *env, jobject obj, jlong bufferSize,
    jbyteArray info, jint infoOffset, jint infoLength) {
  NormSessionHandle session;
  NormObjectHandle objectHandle;
  jbyte *infoBytes;

  session = (NormSessionHandle)env->GetLongField(obj, fid_NormSession_handle);

  // Get the info byte array
  if (info != NULL) {
    infoBytes = env->GetByteArrayElements(info, 0);
  } else {
    infoBytes = NULL;
    infoOffset = 0;
    infoLength = 0;
  }

  objectHandle = NormStreamOpen(session, bufferSize,
    (char*)infoBytes + infoOffset, infoLength);

  // Release the info byte array
  if (info != NULL) {
    env->ReleaseByteArrayElements(info, infoBytes, JNI_ABORT);
  }

  if (objectHandle == NORM_OBJECT_INVALID) {
    env->ThrowNew((jclass)env->NewLocalRef(jw_IOException), "Failed to open stream");
    return NULL;
  }

  // Create the NormObject
  return env->NewObject((jclass)env->NewLocalRef(jw_NormStream), mid_NormStream_init,
    (jlong)objectHandle);
}

JNIEXPORT void JNICALL PKGNAME(NormSession_setWatermark)
    (JNIEnv *env, jobject obj, jobject object, jboolean overrideFlush) {
  NormSessionHandle session;
  NormObjectHandle objectHandle;

  session = (NormSessionHandle)env->GetLongField(obj, fid_NormSession_handle);
  objectHandle = (NormObjectHandle)env->GetLongField(object,
    fid_NormObject_handle);

  if (!NormSetWatermark(session, objectHandle, overrideFlush)) {
    env->ThrowNew((jclass)env->NewLocalRef(jw_IOException), "Failed to set watermark");
    return;
  }
}

JNIEXPORT void JNICALL PKGNAME(NormSession_cancelWatermark)
    (JNIEnv *env, jobject obj) {
  NormSessionHandle session;

  session = (NormSessionHandle)env->GetLongField(obj, fid_NormSession_handle);

  NormCancelWatermark(session);
}

JNIEXPORT void JNICALL PKGNAME(NormSession_resetWatermark)
    (JNIEnv *env, jobject obj) {
  NormSessionHandle session;

  session = (NormSessionHandle)env->GetLongField(obj, fid_NormSession_handle);

  NormResetWatermark(session);
}

JNIEXPORT void JNICALL PKGNAME(NormSession_addAckingNode)
    (JNIEnv *env, jobject obj, jlong nodeId) {
  NormSessionHandle session;

  session = (NormSessionHandle)env->GetLongField(obj, fid_NormSession_handle);

  if (!NormAddAckingNode(session, nodeId)) {
    env->ThrowNew((jclass)env->NewLocalRef(jw_IOException), "Failed to add acking node");
    return;
  }
}

JNIEXPORT void JNICALL PKGNAME(NormSession_removeAckingNode)
    (JNIEnv *env, jobject obj, jlong nodeId) {
  NormSessionHandle session;

  session = (NormSessionHandle)env->GetLongField(obj, fid_NormSession_handle);

  NormRemoveAckingNode(session, nodeId);
}

JNIEXPORT jobject JNICALL PKGNAME(NormSession_getAckingStatus)
    (JNIEnv *env, jobject obj, jlong nodeId) {
  NormSessionHandle session;
  NormAckingStatus ackingStatus;

  session = (NormSessionHandle)env->GetLongField(obj, fid_NormSession_handle);

  ackingStatus = NormGetAckingStatus(session, nodeId);

  // get the enum value from the native value
  jclass LocalNormAckingStatus = (jclass)env->NewLocalRef(jw_NormAckingStatus);
  jobjectArray array = (jobjectArray)env->CallStaticObjectMethod(
    LocalNormAckingStatus, mid_NormAckingStatus_values);
  return env->GetObjectArrayElement(array, ackingStatus);
}

JNIEXPORT void JNICALL PKGNAME(NormSession_sendCommand)
    (JNIEnv *env, jobject obj, jbyteArray cmdBuffer, jint cmdOffset,
    jint cmdLength, jboolean robust) {
  NormSessionHandle session;
  jbyte *cmdBytes;

  session = (NormSessionHandle)env->GetLongField(obj, fid_NormSession_handle);
  cmdBytes = env->GetByteArrayElements(cmdBuffer, 0);

  if (!NormSendCommand(session, (const char*)(cmdBytes + cmdOffset), cmdLength, robust)) {
    env->ReleaseByteArrayElements(cmdBuffer, cmdBytes, JNI_ABORT);
    env->ThrowNew((jclass)env->NewLocalRef(jw_IOException), "Failed to send command");
    return;
  }

  env->ReleaseByteArrayElements(cmdBuffer, cmdBytes, JNI_ABORT);
}

JNIEXPORT void JNICALL PKGNAME(NormSession_cancelCommand)
    (JNIEnv *env, jobject obj) {
  NormSessionHandle session;

  session = (NormSessionHandle)env->GetLongField(obj, fid_NormSession_handle);

  NormCancelCommand(session);
}

JNIEXPORT void JNICALL PKGNAME(NormSession_startReceiver)
    (JNIEnv *env, jobject obj, jlong bufferSpace) {
  NormSessionHandle session;

  session = (NormSessionHandle)env->GetLongField(obj, fid_NormSession_handle);

  if (!NormStartReceiver(session, bufferSpace)) {
    env->ThrowNew((jclass)env->NewLocalRef(jw_IOException), "Failed to start receiver");
    return;
  }
}

JNIEXPORT void JNICALL PKGNAME(NormSession_stopReceiver)
    (JNIEnv *env, jobject obj) {
  NormSessionHandle session;

  session = (NormSessionHandle)env->GetLongField(obj, fid_NormSession_handle);

  NormStopReceiver(session);
}

JNIEXPORT void JNICALL PKGNAME(NormSession_setRxCacheLimit)
    (JNIEnv *env, jobject obj, jint countMax) {
  NormSessionHandle session;

  session = (NormSessionHandle)env->GetLongField(obj, fid_NormSession_handle);

  NormSetRxCacheLimit(session, countMax);
}

JNIEXPORT void JNICALL PKGNAME(NormSession_setRxSocketBuffer)
    (JNIEnv *env, jobject obj, jlong bufferSize) {
  NormSessionHandle session;

  session = (NormSessionHandle)env->GetLongField(obj, fid_NormSession_handle);

  if (!NormSetRxSocketBuffer(session, bufferSize)) {
    env->ThrowNew((jclass)env->NewLocalRef(jw_IOException), "Failed to set rx socket buffer");
    return;
  }
}

JNIEXPORT void JNICALL PKGNAME(NormSession_setSilentReceiver)
    (JNIEnv *env, jobject obj, jboolean silent, jint maxDelay) {
  NormSessionHandle session;

  session = (NormSessionHandle)env->GetLongField(obj, fid_NormSession_handle);

  NormSetSilentReceiver(session, silent, maxDelay);
}

JNIEXPORT void JNICALL PKGNAME(NormSession_setDefaultUnicastNack)
    (JNIEnv *env, jobject obj, jboolean enable) {
  NormSessionHandle session;

  session = (NormSessionHandle)env->GetLongField(obj, fid_NormSession_handle);

  NormSetDefaultUnicastNack(session, enable);
}

JNIEXPORT void JNICALL  PKGNAME(NormSession_setDefaultSyncPolicy)
    (JNIEnv *env, jobject obj, jobject syncPolicy) {
  NormSessionHandle session;
  NormSyncPolicy policy;

  session = (NormSessionHandle)env->GetLongField(obj, fid_NormSession_handle);
  policy = (NormSyncPolicy)env->CallIntMethod(syncPolicy,
    mid_NormSyncPolicy_ordinal);

  NormSetDefaultSyncPolicy(session, policy);
}

JNIEXPORT void JNICALL PKGNAME(NormSession_setDefaultNackingMode)
    (JNIEnv *env, jobject obj, jobject nackingMode) {
  NormSessionHandle session;
  NormNackingMode mode;

  session = (NormSessionHandle)env->GetLongField(obj, fid_NormSession_handle);
  mode = (NormNackingMode)env->CallIntMethod(nackingMode,
    mid_NormNackingMode_ordinal);

  NormSetDefaultNackingMode(session, mode);
}

JNIEXPORT void JNICALL PKGNAME(NormSession_setDefaultRepairBoundary)
    (JNIEnv *env, jobject obj, jobject repairBoundary) {
  NormSessionHandle session;
  NormRepairBoundary mode;

  session = (NormSessionHandle)env->GetLongField(obj, fid_NormSession_handle);
  mode = (NormRepairBoundary)env->CallIntMethod(repairBoundary,
    mid_NormRepairBoundary_ordinal);

  NormSetDefaultRepairBoundary(session, mode);
}

JNIEXPORT void JNICALL PKGNAME(NormSession_setDefaultRxRobustFactor)
    (JNIEnv *env, jobject obj, jint robustFactor) {
  NormSessionHandle session;

  session = (NormSessionHandle)env->GetLongField(obj, fid_NormSession_handle);

  NormSetDefaultRxRobustFactor(session, robustFactor);
}

