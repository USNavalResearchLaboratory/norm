/* DO NOT EDIT THIS FILE - it is machine generated */
#include <jni.h>
/* Header for class mil_navy_nrl_norm_NormSession */

#ifndef _Included_mil_navy_nrl_norm_NormSession
#define _Included_mil_navy_nrl_norm_NormSession
#ifdef __cplusplus
extern "C" {
#endif
/*
 * Class:     mil_navy_nrl_norm_NormSession
 * Method:    destroySessionNative
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_mil_navy_nrl_norm_NormSession_destroySessionNative
  (JNIEnv *, jobject);

/*
 * Class:     mil_navy_nrl_norm_NormSession
 * Method:    getLocalNodeId
 * Signature: ()J
 */
JNIEXPORT jlong JNICALL Java_mil_navy_nrl_norm_NormSession_getLocalNodeId
  (JNIEnv *, jobject);

/*
 * Class:     mil_navy_nrl_norm_NormSession
 * Method:    setTxPort
 * Signature: (IZLjava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_mil_navy_nrl_norm_NormSession_setTxPort
  (JNIEnv *, jobject, jint, jboolean, jstring);

/*
 * Class:     mil_navy_nrl_norm_NormSession
 * Method:    setRxPortReuse
 * Signature: (ZLjava/lang/String;Ljava/lang/String;I)V
 */
JNIEXPORT void JNICALL Java_mil_navy_nrl_norm_NormSession_setRxPortReuse
  (JNIEnv *, jobject, jboolean, jstring, jstring, jint);

/*
 * Class:     mil_navy_nrl_norm_NormSession
 * Method:    setEcnSupport
 * Signature: (ZZ)V
 */
JNIEXPORT void JNICALL Java_mil_navy_nrl_norm_NormSession_setEcnSupport
  (JNIEnv *, jobject, jboolean, jboolean);

/*
 * Class:     mil_navy_nrl_norm_NormSession
 * Method:    setMulticastInterface
 * Signature: (Ljava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_mil_navy_nrl_norm_NormSession_setMulticastInterface
  (JNIEnv *, jobject, jstring);

/*
 * Class:     mil_navy_nrl_norm_NormSession
 * Method:    setTTL
 * Signature: (B)V
 */
JNIEXPORT void JNICALL Java_mil_navy_nrl_norm_NormSession_setTTL
  (JNIEnv *, jobject, jbyte);

/*
 * Class:     mil_navy_nrl_norm_NormSession
 * Method:    setTOS
 * Signature: (B)V
 */
JNIEXPORT void JNICALL Java_mil_navy_nrl_norm_NormSession_setTOS
  (JNIEnv *, jobject, jbyte);

/*
 * Class:     mil_navy_nrl_norm_NormSession
 * Method:    setLoopback
 * Signature: (Z)V
 */
JNIEXPORT void JNICALL Java_mil_navy_nrl_norm_NormSession_setLoopback
  (JNIEnv *, jobject, jboolean);

/*
 * Class:     mil_navy_nrl_norm_NormSession
 * Method:    setMessageTrace
 * Signature: (Z)V
 */
JNIEXPORT void JNICALL Java_mil_navy_nrl_norm_NormSession_setMessageTrace
  (JNIEnv *, jobject, jboolean);

/*
 * Class:     mil_navy_nrl_norm_NormSession
 * Method:    setTxLoss
 * Signature: (D)V
 */
JNIEXPORT void JNICALL Java_mil_navy_nrl_norm_NormSession_setTxLoss
  (JNIEnv *, jobject, jdouble);

/*
 * Class:     mil_navy_nrl_norm_NormSession
 * Method:    setRxLoss
 * Signature: (D)V
 */
JNIEXPORT void JNICALL Java_mil_navy_nrl_norm_NormSession_setRxLoss
  (JNIEnv *, jobject, jdouble);

/*
 * Class:     mil_navy_nrl_norm_NormSession
 * Method:    setReportInterval
 * Signature: (D)V
 */
JNIEXPORT void JNICALL Java_mil_navy_nrl_norm_NormSession_setReportInterval
  (JNIEnv *, jobject, jdouble);

/*
 * Class:     mil_navy_nrl_norm_NormSession
 * Method:    getReportInterval
 * Signature: ()D
 */
JNIEXPORT jdouble JNICALL Java_mil_navy_nrl_norm_NormSession_getReportInterval
  (JNIEnv *, jobject);

/*
 * Class:     mil_navy_nrl_norm_NormSession
 * Method:    startSender
 * Signature: (IJISS)V
 */
JNIEXPORT void JNICALL Java_mil_navy_nrl_norm_NormSession_startSender
  (JNIEnv *, jobject, jint, jlong, jint, jshort, jshort);

/*
 * Class:     mil_navy_nrl_norm_NormSession
 * Method:    stopSender
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_mil_navy_nrl_norm_NormSession_stopSender
  (JNIEnv *, jobject);

/*
 * Class:     mil_navy_nrl_norm_NormSession
 * Method:    setTxOnly
 * Signature: (Z)V
 */
JNIEXPORT void JNICALL Java_mil_navy_nrl_norm_NormSession_setTxOnly
  (JNIEnv *, jobject, jboolean);

/*
 * Class:     mil_navy_nrl_norm_NormSession
 * Method:    getTxRate
 * Signature: ()D
 */
JNIEXPORT jdouble JNICALL Java_mil_navy_nrl_norm_NormSession_getTxRate
  (JNIEnv *, jobject);

/*
 * Class:     mil_navy_nrl_norm_NormSession
 * Method:    setTxRate
 * Signature: (D)V
 */
JNIEXPORT void JNICALL Java_mil_navy_nrl_norm_NormSession_setTxRate
  (JNIEnv *, jobject, jdouble);

/*
 * Class:     mil_navy_nrl_norm_NormSession
 * Method:    setFlowControl
 * Signature: (D)V
 */
JNIEXPORT void JNICALL Java_mil_navy_nrl_norm_NormSession_setFlowControl
  (JNIEnv *, jobject, jdouble);

/*
 * Class:     mil_navy_nrl_norm_NormSession
 * Method:    setTxSocketBuffer
 * Signature: (J)V
 */
JNIEXPORT void JNICALL Java_mil_navy_nrl_norm_NormSession_setTxSocketBuffer
  (JNIEnv *, jobject, jlong);

/*
 * Class:     mil_navy_nrl_norm_NormSession
 * Method:    setCongestionControl
 * Signature: (ZZ)V
 */
JNIEXPORT void JNICALL Java_mil_navy_nrl_norm_NormSession_setCongestionControl
  (JNIEnv *, jobject, jboolean, jboolean);

/*
 * Class:     mil_navy_nrl_norm_NormSession
 * Method:    setTxRateBounds
 * Signature: (DD)V
 */
JNIEXPORT void JNICALL Java_mil_navy_nrl_norm_NormSession_setTxRateBounds
  (JNIEnv *, jobject, jdouble, jdouble);

/*
 * Class:     mil_navy_nrl_norm_NormSession
 * Method:    setTxCacheBounds
 * Signature: (JJJ)V
 */
JNIEXPORT void JNICALL Java_mil_navy_nrl_norm_NormSession_setTxCacheBounds
  (JNIEnv *, jobject, jlong, jlong, jlong);

/*
 * Class:     mil_navy_nrl_norm_NormSession
 * Method:    setAutoParity
 * Signature: (S)V
 */
JNIEXPORT void JNICALL Java_mil_navy_nrl_norm_NormSession_setAutoParity
  (JNIEnv *, jobject, jshort);

/*
 * Class:     mil_navy_nrl_norm_NormSession
 * Method:    setGrttEstimate
 * Signature: (D)V
 */
JNIEXPORT void JNICALL Java_mil_navy_nrl_norm_NormSession_setGrttEstimate
  (JNIEnv *, jobject, jdouble);

/*
 * Class:     mil_navy_nrl_norm_NormSession
 * Method:    getGrttEstimate
 * Signature: ()D
 */
JNIEXPORT jdouble JNICALL Java_mil_navy_nrl_norm_NormSession_getGrttEstimate
  (JNIEnv *, jobject);

/*
 * Class:     mil_navy_nrl_norm_NormSession
 * Method:    setGrttMax
 * Signature: (D)V
 */
JNIEXPORT void JNICALL Java_mil_navy_nrl_norm_NormSession_setGrttMax
  (JNIEnv *, jobject, jdouble);

/*
 * Class:     mil_navy_nrl_norm_NormSession
 * Method:    setGrttProbingMode
 * Signature: (Lmil/navy/nrl/norm/enums/NormProbingMode;)V
 */
JNIEXPORT void JNICALL Java_mil_navy_nrl_norm_NormSession_setGrttProbingMode
  (JNIEnv *, jobject, jobject);

/*
 * Class:     mil_navy_nrl_norm_NormSession
 * Method:    setGrttProbingInterval
 * Signature: (DD)V
 */
JNIEXPORT void JNICALL Java_mil_navy_nrl_norm_NormSession_setGrttProbingInterval
  (JNIEnv *, jobject, jdouble, jdouble);

/*
 * Class:     mil_navy_nrl_norm_NormSession
 * Method:    setBackoffFactor
 * Signature: (D)V
 */
JNIEXPORT void JNICALL Java_mil_navy_nrl_norm_NormSession_setBackoffFactor
  (JNIEnv *, jobject, jdouble);

/*
 * Class:     mil_navy_nrl_norm_NormSession
 * Method:    setGroupSize
 * Signature: (J)V
 */
JNIEXPORT void JNICALL Java_mil_navy_nrl_norm_NormSession_setGroupSize
  (JNIEnv *, jobject, jlong);

/*
 * Class:     mil_navy_nrl_norm_NormSession
 * Method:    setTxRobustFactor
 * Signature: (I)V
 */
JNIEXPORT void JNICALL Java_mil_navy_nrl_norm_NormSession_setTxRobustFactor
  (JNIEnv *, jobject, jint);

/*
 * Class:     mil_navy_nrl_norm_NormSession
 * Method:    fileEnqueue
 * Signature: (Ljava/lang/String;[BII)Lmil/navy/nrl/norm/NormFile;
 */
JNIEXPORT jobject JNICALL Java_mil_navy_nrl_norm_NormSession_fileEnqueue
  (JNIEnv *, jobject, jstring, jbyteArray, jint, jint);

/*
 * Class:     mil_navy_nrl_norm_NormSession
 * Method:    dataEnqueue
 * Signature: (Ljava/nio/ByteBuffer;II[BII)Lmil/navy/nrl/norm/NormData;
 */
JNIEXPORT jobject JNICALL Java_mil_navy_nrl_norm_NormSession_dataEnqueue
  (JNIEnv *, jobject, jobject, jint, jint, jbyteArray, jint, jint);

/*
 * Class:     mil_navy_nrl_norm_NormSession
 * Method:    requeueObject
 * Signature: (Lmil/navy/nrl/norm/NormObject;)V
 */
JNIEXPORT void JNICALL Java_mil_navy_nrl_norm_NormSession_requeueObject
  (JNIEnv *, jobject, jobject);

/*
 * Class:     mil_navy_nrl_norm_NormSession
 * Method:    streamOpen
 * Signature: (J[BII)Lmil/navy/nrl/norm/NormStream;
 */
JNIEXPORT jobject JNICALL Java_mil_navy_nrl_norm_NormSession_streamOpen
  (JNIEnv *, jobject, jlong, jbyteArray, jint, jint);

/*
 * Class:     mil_navy_nrl_norm_NormSession
 * Method:    setWatermark
 * Signature: (Lmil/navy/nrl/norm/NormObject;Z)V
 */
JNIEXPORT void JNICALL Java_mil_navy_nrl_norm_NormSession_setWatermark
  (JNIEnv *, jobject, jobject, jboolean);

/*
 * Class:     mil_navy_nrl_norm_NormSession
 * Method:    cancelWatermark
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_mil_navy_nrl_norm_NormSession_cancelWatermark
  (JNIEnv *, jobject);

/*
 * Class:     mil_navy_nrl_norm_NormSession
 * Method:    addAckingNode
 * Signature: (J)V
 */
JNIEXPORT void JNICALL Java_mil_navy_nrl_norm_NormSession_addAckingNode
  (JNIEnv *, jobject, jlong);

/*
 * Class:     mil_navy_nrl_norm_NormSession
 * Method:    removeAckingNode
 * Signature: (J)V
 */
JNIEXPORT void JNICALL Java_mil_navy_nrl_norm_NormSession_removeAckingNode
  (JNIEnv *, jobject, jlong);

/*
 * Class:     mil_navy_nrl_norm_NormSession
 * Method:    getAckingStatus
 * Signature: (J)Lmil/navy/nrl/norm/enums/NormAckingStatus;
 */
JNIEXPORT jobject JNICALL Java_mil_navy_nrl_norm_NormSession_getAckingStatus
  (JNIEnv *, jobject, jlong);

/*
 * Class:     mil_navy_nrl_norm_NormSession
 * Method:    sendCommand
 * Signature: ([BIIZ)V
 */
JNIEXPORT void JNICALL Java_mil_navy_nrl_norm_NormSession_sendCommand
  (JNIEnv *, jobject, jbyteArray, jint, jint, jboolean);

/*
 * Class:     mil_navy_nrl_norm_NormSession
 * Method:    cancelCommand
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_mil_navy_nrl_norm_NormSession_cancelCommand
  (JNIEnv *, jobject);

/*
 * Class:     mil_navy_nrl_norm_NormSession
 * Method:    startReceiver
 * Signature: (J)V
 */
JNIEXPORT void JNICALL Java_mil_navy_nrl_norm_NormSession_startReceiver
  (JNIEnv *, jobject, jlong);

/*
 * Class:     mil_navy_nrl_norm_NormSession
 * Method:    stopReceiver
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_mil_navy_nrl_norm_NormSession_stopReceiver
  (JNIEnv *, jobject);

/*
 * Class:     mil_navy_nrl_norm_NormSession
 * Method:    setRxCacheLimit
 * Signature: (I)V
 */
JNIEXPORT void JNICALL Java_mil_navy_nrl_norm_NormSession_setRxCacheLimit
  (JNIEnv *, jobject, jint);

/*
 * Class:     mil_navy_nrl_norm_NormSession
 * Method:    setRxSocketBuffer
 * Signature: (J)V
 */
JNIEXPORT void JNICALL Java_mil_navy_nrl_norm_NormSession_setRxSocketBuffer
  (JNIEnv *, jobject, jlong);

/*
 * Class:     mil_navy_nrl_norm_NormSession
 * Method:    setSilentReceiver
 * Signature: (ZI)V
 */
JNIEXPORT void JNICALL Java_mil_navy_nrl_norm_NormSession_setSilentReceiver
  (JNIEnv *, jobject, jboolean, jint);

/*
 * Class:     mil_navy_nrl_norm_NormSession
 * Method:    setDefaultUnicastNack
 * Signature: (Z)V
 */
JNIEXPORT void JNICALL Java_mil_navy_nrl_norm_NormSession_setDefaultUnicastNack
  (JNIEnv *, jobject, jboolean);

/*
 * Class:     mil_navy_nrl_norm_NormSession
 * Method:    setDefaultSyncPolicy
 * Signature: (Lmil/navy/nrl/norm/enums/NormSyncPolicy;)V
 */
JNIEXPORT void JNICALL Java_mil_navy_nrl_norm_NormSession_setDefaultSyncPolicy
  (JNIEnv *, jobject, jobject);

/*
 * Class:     mil_navy_nrl_norm_NormSession
 * Method:    setDefaultNackingMode
 * Signature: (Lmil/navy/nrl/norm/enums/NormNackingMode;)V
 */
JNIEXPORT void JNICALL Java_mil_navy_nrl_norm_NormSession_setDefaultNackingMode
  (JNIEnv *, jobject, jobject);

/*
 * Class:     mil_navy_nrl_norm_NormSession
 * Method:    setDefaultRepairBoundary
 * Signature: (Lmil/navy/nrl/norm/enums/NormRepairBoundary;)V
 */
JNIEXPORT void JNICALL Java_mil_navy_nrl_norm_NormSession_setDefaultRepairBoundary
  (JNIEnv *, jobject, jobject);

/*
 * Class:     mil_navy_nrl_norm_NormSession
 * Method:    setDefaultRxRobustFactor
 * Signature: (I)V
 */
JNIEXPORT void JNICALL Java_mil_navy_nrl_norm_NormSession_setDefaultRxRobustFactor
  (JNIEnv *, jobject, jint);

#ifdef __cplusplus
}
#endif
#endif
