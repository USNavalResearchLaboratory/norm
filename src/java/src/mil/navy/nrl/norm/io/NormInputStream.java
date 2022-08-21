package mil.navy.nrl.norm.io;

import java.io.IOException;
import java.io.InputStream;
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
import mil.navy.nrl.norm.enums.NormObjectType;

/**
 * @author Jason Rush
 */
public class NormInputStream extends InputStream {
  private NormInstance normInstance;
  private NormSession normSession;
  private NormStream normStream;

  private List<INormEventListener> normEventListeners;

  private boolean closed;
  private Object closeLock;

  private boolean bufferIsEmpty;
  private boolean receivedEof;

  public NormInputStream(String address, int port) throws IOException {
    // Create the NORM instance
    normInstance = new NormInstance();

    // Create the NORM session
    normSession = normInstance.createSession(address, port,
        NormNode.NORM_NODE_ANY);

    normStream = null;

    normEventListeners = new LinkedList<INormEventListener>();

    closed = true;
    closeLock = new Object();

    bufferIsEmpty = true;
    receivedEof = false;
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

  public synchronized void setSilentReceiver(boolean silent, int maxDelay) {
    normSession.setSilentReceiver(silent, maxDelay);
  }

  public synchronized void setDefaultUnicastNack(boolean defaultUnicastNack) {
    normSession.setDefaultUnicastNack(defaultUnicastNack);
  }

  public synchronized void seekMsgStart() {
    if (normStream == null) {
      throw new IllegalStateException(
          "Can only seek msg start after the stream is connected");
    }

    normStream.seekMsgStart();
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
   * @param normEventListener The INormEventListener to remove.
   */
  public void removeNormEventListener(INormEventListener normEventListener) {
    synchronized (normEventListeners) {
      normEventListeners.remove(normEventListener);
    }
  }

  private void fireNormEventOccured(NormEvent normEvent) {
    synchronized (normEventListeners) {
      for (INormEventListener normEventListener : normEventListeners) {
        normEventListener.normEventOccurred(normEvent);
      }
    }
  }

  public void open(long bufferSpace) throws IOException {
    synchronized (closeLock) {
      if (!isClosed()) {
        throw new SocketException("Stream is already open");
      }

      normSession.startReceiver(bufferSpace);

      closed = false;
    }
  }

  /**
   * @see java.io.InputStream#close()
   */
  @Override
  public void close() throws IOException {
    synchronized (closeLock) {
      if (isClosed()) {
        return;
      }

      normSession.stopReceiver();
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
   * @see java.io.InputStream#read()
   */
  @Override
  public synchronized int read() throws IOException {
    byte buffer[] = new byte[1];

    if (isClosed()) {
      throw new IOException("Stream is closed");
    }

    if (read(buffer) < 0) {
      return -1;
    }

    return buffer[0];
  }

  /**
   * @see java.io.InputStream#read(byte[], int, int)
   */
  @Override
  public synchronized int read(byte[] buffer, int offset, int length)
      throws IOException {
    int n;

    if (isClosed()) {
      throw new IOException("Stream is closed");
    }

    do {
      while (bufferIsEmpty || normInstance.hasNextEvent(0, 0)) {
        processEvent();

        if (receivedEof) {
          return -1;
        }
      }

      if (normStream == null) {
        return -1;
      }

      // Read from the stream
      if ((n = normStream.read(buffer, offset, length)) < 0) {
        throw new StreamBreakException("Break in stream integrity");
      }

      bufferIsEmpty = (n == 0);
    } while (bufferIsEmpty);

    return n;
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
        case NORM_RX_OBJECT_NEW:
          NormObject normObject = normEvent.getObject();
          if (normObject.getType() == NormObjectType.NORM_OBJECT_STREAM) {
            normStream = (NormStream)normObject;
          }
          break;

        case NORM_RX_OBJECT_UPDATED:
          NormObject object = normEvent.getObject();
          if (!object.equals(normStream)) {
            break;
          }

          // Signal that the buffer is not empty
          bufferIsEmpty = false;
          break;

        case NORM_RX_OBJECT_ABORTED:
        case NORM_RX_OBJECT_COMPLETED:
          normStream = null;

          // Signal that the stream has ended
          receivedEof = true;
          break;

        default:
          break;
      }

      // Notify listeners of the norm event
      fireNormEventOccured(normEvent);
    }
  }
}
