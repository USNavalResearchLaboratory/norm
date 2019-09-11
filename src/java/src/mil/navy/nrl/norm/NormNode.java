package mil.navy.nrl.norm;

import java.io.IOException;
import java.net.InetSocketAddress;

import mil.navy.nrl.norm.enums.NormNackingMode;
import mil.navy.nrl.norm.enums.NormRepairBoundary;

/**
 * This class contains the information about a NORM node.
 * 
 * @author Jason Rush
 */
public class NormNode {
  public static final long NORM_NODE_ANY = 0xffffffff;

  @SuppressWarnings("unused")
  private long handle;

  NormNode(long handle) {
    this.handle = handle;
  }

  public native void setUnicastNack(boolean state);

  public native void setNackingMode(NormNackingMode nackingMode);

  public native void setRepairBoundary(NormRepairBoundary repairBoundary);

  public native void setRxRobustFactor(int robustFactor);

  public native long getId();

  public native InetSocketAddress getAddress() throws IOException;

  public native double getGrtt();

  public native int getCommand(byte buffer[], int offset, int length)
      throws IOException;

  public native void freeBuffers();

  public native void retain();

  public native void release();
}
