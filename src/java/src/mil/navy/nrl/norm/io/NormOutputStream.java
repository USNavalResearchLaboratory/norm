
package mil.navy.nrl.norm.io;

import java.io.IOException;
import java.io.OutputStream;
import java.net.SocketException;
import java.util.LinkedList;
import java.util.List;

import mil.navy.nrl.norm.NormEvent;
import mil.navy.nrl.norm.NormInstance;
import mil.navy.nrl.norm.NormNode;
import mil.navy.nrl.norm.NormObject;
import mil.navy.nrl.norm.NormSession;
import mil.navy.nrl.norm.NormStream;
import mil.navy.nrl.norm.enums.NormEventType;
import mil.navy.nrl.norm.enums.NormFlushMode;

/**
 * @author Jason Rush
 */
public class NormOutputStream extends OutputStream {
  private NormInstance normInstance;
  private NormSession normSession;
  private NormStream normStream;

  private List<INormEventListener> normEventListeners;

  private boolean closed;
  private Object closeLock;

  private boolean bufferIsFull;

  public NormOutputStream(String address, int port) throws IOException {
    // Create the NORM instance
    normInstance = new NormInstance();

    // Create the NORM session
    normSession = normInstance.createSession(address, port,
        NormNode.NORM_NODE_ANY);

    normStream = null;

    normEventListeners = new LinkedList<INormEventListener>();

    closed = true;
    closeLock = new Object();

    bufferIsFull = false;
  }

  public synchronized void openDebugLog(String filename) throws IOException {
    normInstance.openDebugLog(filename);
  }

  public synchronized void closeDebugLog() {
    normInstance.closeDebugLog();
  }
  
  public synchronized void setDebugLevel(int level) {
    normInstance.setDebugLevel(level);
  }
  
  public synchronized void setMessageTrace(boolean messageTrace) {
    normSession.setMessageTrace(messageTrace);
  }

  public synchronized void setMulticastInterface(String multicastInterface)
      throws IOException {
    normSession.setMulticastInterface(multicastInterface);
  }

  public synchronized void setEcnSupport(boolean ecnEnable, boolean ignoreLoss)
      throws IOException {
    normSession.setEcnSupport(ecnEnable, ignoreLoss);
  }

  public synchronized void setTtl(byte ttl) throws IOException {
    normSession.setTTL(ttl);
  }

  public synchronized void setTos(byte tos) throws IOException {
    normSession.setTOS(tos);
  }

  public synchronized void setCongestionControl(boolean ccEnabled,
      boolean ccAdjustRate) {
    normSession.setCongestionControl(ccEnabled, ccAdjustRate);
  }

  public synchronized void setTxRateBounds(double minTxRate, double maxTxRate) {
    normSession.setTxRateBounds(minTxRate, maxTxRate);
  }

  public synchronized void setTxRate(double txRate) {
    normSession.setTxRate(txRate);
  }

  public synchronized void setGrttEstimate(double initialGrttEstimate) {
    normSession.setGrttEstimate(initialGrttEstimate);
  }

  public synchronized void setGroupSize(long groupSize) {
    normSession.setGroupSize(groupSize);
  }

  public synchronized void setAutoParity(short autoParity) {
    normSession.setAutoParity(autoParity);
  }

  public synchronized void setBackoffFactor(double backoffFactor) {
    normSession.setBackoffFactor(backoffFactor);
  }

  public synchronized double getTxRate() {
    return normSession.getTxRate();
  }

  public synchronized double getGrttEstimate() {
    return normSession.getGrttEstimate();
  }

  public synchronized void setAutoFlush(NormFlushMode flushMode) {
    if (normStream == null) {
      throw new IllegalStateException(
          "Can only set auto flush after the stream is open");
    }

    normStream.setAutoFlush(flushMode);
  }

  public synchronized void setPushEnable(boolean pushEnable) {
    if (normStream == null) {
      throw new IllegalStateException(
          "Can only set push enabled after the stream is open");
    }

    normStream.setPushEnable(pushEnable);
  }

  public synchronized void markEom() {
    if (normStream == null) {
      throw new IllegalStateException(
          "Can only mark EOM after the stream is open");
    }

    normStream.markEom();
  }

  /**
   * @param normEventListener The INormEventListener to add.
   */
  public void addNormEventListener(INormEventListener normEventListener) {
    synchronized (normEventListeners) {
      normEventListeners.add(normEventListener);
    }
  }

  /**
   * @param normEventListener The INormEventListener to add.
   */
  public void removeNormEventListener(INormEventListener normEventListener) {
    synchronized (normEventListeners) {
      normEventListeners.add(normEventListener);
    }
  }

  private void fireNormEventOccured(NormEvent normEvent) {
    synchronized (normEventListeners) {
      for (INormEventListener normEventListener : normEventListeners) {
        normEventListener.normEventOccurred(normEvent);
      }
    }
  }

  public void open(int sessionId, long bufferSpace, int segmentSize,
      short blockSize, short numParity, long repairWindow) throws IOException {
    synchronized (closeLock) {
      if (!isClosed()) {
        throw new SocketException("Stream is already open");
      }

      normSession.startSender(sessionId, bufferSpace, segmentSize, blockSize,
          numParity);

      // Open the stream
      normStream = normSession.streamOpen(repairWindow);

      closed = false;
    }
  }

  /**
   * @see java.io.OutputStream#close()
   */
  @Override
  public void close() throws IOException {
    synchronized (closeLock) {
      if (isClosed()) {
        return;
      }

      normStream.close(false);
      normSession.stopSender();
      normInstance.stopInstance();

      closed = true;
    }
  }

  /**
   * @return Returns the closed.
   */
  public boolean isClosed() {
    synchronized (closeLock) {
      return closed;
    }
  }

  /**
   * @see java.io.OutputStream#write(int)
   */
  @Override
  public synchronized void write(int b) throws IOException {
    if (isClosed()) {
      throw new IOException("Stream is closed");
    }

    byte buffer[] = new byte[1];
    buffer[0] = (byte)b;

    write(buffer);
  }

  /**
   * @see java.io.OutputStream#write(byte[], int, int)
   */
  @Override
  public synchronized void write(byte[] buffer, int offset, int length)
      throws IOException {
    int n;

    if (isClosed()) {
      throw new IOException("Stream is closed");
    }

    while (length > 0) {
      while (normInstance.hasNextEvent(0, 0)) {
        processEvent();
      }

      // Wait while the buffer is full
      while (bufferIsFull) {
        processEvent();
      }

      // Write some data
      if ((n = normStream.write(buffer, offset, length)) < 0) {
        throw new IOException("Failed to write to stream");
      }

      bufferIsFull = (n == 0);

      length -= n;
      offset += n;
    }
  }

  private void processEvent() throws IOException {
    // Retrieve the next event
    NormEvent normEvent = normInstance.getNextEvent();

    // Check if the stream was closed
    if (isClosed()) {
      throw new IOException("Stream closed");
    }

    if (normEvent != null) {
      // Process the event
      NormEventType eventType = normEvent.getType();
      switch (eventType) {
        case NORM_TX_QUEUE_VACANCY:
        case NORM_TX_QUEUE_EMPTY:
          NormObject object = normEvent.getObject();
          if (!object.equals(normStream)) {
            break;
          }

          // Signal that the buffer is not full
          bufferIsFull = false;
          break;

        case NORM_TX_OBJECT_SENT:
        case NORM_TX_OBJECT_PURGED:
          normStream = null;
          break;

        default:
          break;
      }

      // Notify listeners of the norm event
      fireNormEventOccured(normEvent);
    }
  }
}
