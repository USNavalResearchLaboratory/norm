package mil.navy.nrl.norm;

import java.io.IOException;

/**
 * This class maps to a native instance of a NORM protocol engine.
 *
 * @author Jason Rush
 */
public class NormInstance {
  /*
   * This version string is used to validate the compatibility between the Java
   * and C native libraries. Update this string along with it's counterpart in
   * the normJni.h file whenever the native API changes.
   */
  private static final String VERSION = "20130415-0927";

  static {
    System.loadLibrary("mil_navy_nrl_norm");
  }

  @SuppressWarnings("unused")
  private long handle; // The pointer to the native NormInstanceHandle

  public NormInstance() throws IOException {
    createInstance(false);
  }

  public NormInstance(boolean priorityBoost) throws IOException {
    createInstance(priorityBoost);
  }

  private native void createInstance(boolean priorityBoost) throws IOException;

  public native void destroyInstance();

  public native void stopInstance();

  public native boolean restartInstance();

  public native boolean suspendInstance();

  public native void resumeInstance();

  public native void setCacheDirectory(String cachePath) throws IOException;

  public native void openDebugLog(String filename) throws IOException;

  public native void closeDebugLog();

  public native void openDebugPipe(String pipename) throws IOException;

  public native void setDebugLevel(int level);

  public native int getDebugLevel();

  public native boolean hasNextEvent(int sec, int usec) throws IOException;

  public native NormEvent getNextEvent() throws IOException;

  public native NormSession createSession(String address, int port,
      long localNodeId) throws IOException;
}
