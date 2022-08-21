package mil.navy.nrl.norm;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.HashMap;
import java.util.Map;

import mil.navy.nrl.norm.enums.NormAckingStatus;
import mil.navy.nrl.norm.enums.NormNackingMode;
import mil.navy.nrl.norm.enums.NormProbingMode;
import mil.navy.nrl.norm.enums.NormRepairBoundary;
import mil.navy.nrl.norm.enums.NormSyncPolicy;

/**
 * This class maps to a native instance of a NORM protocol session.
 * 
 * @author Jason Rush
 */
public class NormSession {
  private static Map<Long, NormSession> normSessions = new HashMap<Long, NormSession>();

  private long handle; // The pointer to the native NormSessionHandle

  /**
   * Package protected constructor is invoked by NormInstance
   */
  NormSession(long handle) {
    this.handle = handle;

    normSessions.put(handle, this);
  }

  static synchronized NormSession getSession(long handle) {
    return normSessions.get(handle);
  }

  /* NORM Session Creation and Control Functions */

  public void destroySession() {
    normSessions.remove(handle);
    destroySessionNative();
  }

  private native void destroySessionNative();

  public native long getLocalNodeId();

  public void setTxPort(int port) {
    setTxPort(port, false, null);
  }
  public native void setTxPort(int port, boolean enableReuse, String txAddress);

  public void setRxPortReuse(boolean enable) {
    setRxPortReuse(enable, null, null, 0);
  }
  public native void setRxPortReuse(boolean enable, String rxBindAddress, String senderAddress, int senderPort);

  public void setEcnSupport(boolean ecnEnable, boolean ignoreLoss) {
    setEcnSupport(ecnEnable, ignoreLoss, false);
  }
  public native void setEcnSupport(boolean ecnEnable, boolean ignoreLoss, boolean tolerateLoss);

  public native void setMulticastInterface(String interfaceName)
      throws IOException;
  
  public native void setSSM(String sourceAddr)
      throws IOException;
      
  public native void setTTL(byte ttl) throws IOException;

  public native void setTOS(byte tos) throws IOException;

  public native void setLoopback(boolean loopbackEnable) throws IOException;

  /* Special functions for debug support */
  public native void setMessageTrace(boolean flag);

  public native void setTxLoss(double percent);

  public native void setRxLoss(double percent);

  public native void setReportInterval(double interval);

  public native double getReportInterval();

  /* NORM Sender Functions */

  public native void startSender(int sessionId, long bufferSpace,
      int segmentSize, short blockSize, short numParity) throws IOException;

  public native void stopSender();

  public native void setTxOnly(boolean txOnly);

  public native double getTxRate();

  public native void setTxRate(double rate);

  public native void setFlowControl(double flowControlFactor);

  public native void setTxSocketBuffer(long bufferSize) throws IOException;

  public void setCongestionControl(boolean enable) {
    setCongestionControl(enable, true);
  }
  public native void setCongestionControl(boolean enable, boolean adjustRate);

  public native void setTxRateBounds(double rateMin, double rateMax);

  public native void setTxCacheBounds(long sizeMax, long countMin, long countMax);

  public native void setAutoParity(short autoParity);

  public native void setGrttEstimate(double grtt);

  public native double getGrttEstimate();

  public native void setGrttMax(double grttMax);

  public native void setGrttProbingMode(NormProbingMode probingMode);

  public native void setGrttProbingInterval(double intervalMin,
      double intervalMax);

  public native void setBackoffFactor(double backoffFactor);

  public native void setGroupSize(long groupSize);

  public native void setTxRobustFactor(int robustFactor);

  public NormFile fileEnqueue(String filename) throws IOException {
    byte[] info = filename.getBytes("US-ASCII");
    return fileEnqueue(filename, info, 0, info.length);
  }

  public native NormFile fileEnqueue(String filename, byte info[],
      int infoOffset, int infoLength) throws IOException;

  public NormData dataEnqueue(ByteBuffer dataBuffer, int dataOffset,
      int dataLength) throws IOException {
    return dataEnqueue(dataBuffer, dataOffset, dataLength, null, 0, 0);
  }

  public native NormData dataEnqueue(ByteBuffer dataBuffer, int dataOffset,
      int dataLength, byte info[], int infoOffset, int infoLength)
      throws IOException;

  public native void requeueObject(NormObject object) throws IOException;

  public NormStream streamOpen(long bufferSize) throws IOException {
    return streamOpen(bufferSize, null, 0, 0);
  }

  public native NormStream streamOpen(long bufferSize, byte info[],
      int infoOffset, int infoLength) throws IOException;

  public void setWatermark(NormObject object) throws IOException {
    setWatermark(object, false);
  }

  public native void setWatermark(NormObject object, boolean overrideFlush)
      throws IOException;

  public native void cancelWatermark();
  
  public native void resetWatermark();

  public native void addAckingNode(long nodeId) throws IOException;

  public native void removeAckingNode(long nodeId);

  public native NormAckingStatus getAckingStatus(long nodeId);

  public native void sendCommand(byte cmdBuffer[], int cmdOffset,
      int cmdLength, boolean robust) throws IOException;

  public native void cancelCommand();

  /* NORM Receiver Functions */

  public native void startReceiver(long bufferSpace) throws IOException;

  public native void stopReceiver();

  public native void setRxCacheLimit(int countMax);

  public native void setRxSocketBuffer(long bufferSize) throws IOException;

  public native void setSilentReceiver(boolean silent, int maxDelay);

  public native void setDefaultUnicastNack(boolean enabled);

  public native void setDefaultSyncPolicy(NormSyncPolicy syncPolicy);

  public native void setDefaultNackingMode(NormNackingMode nackingMode);

  public native void setDefaultRepairBoundary(NormRepairBoundary repairBoundary);

  public native void setDefaultRxRobustFactor(int robustFactor);
}
