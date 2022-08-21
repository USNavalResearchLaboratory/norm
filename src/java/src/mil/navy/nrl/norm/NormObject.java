package mil.navy.nrl.norm;

import java.io.IOException;

import mil.navy.nrl.norm.enums.NormNackingMode;
import mil.navy.nrl.norm.enums.NormObjectType;

/**
 * Interface to tag all NORM objects (data, file, etc).
 * 
 * @author Jason Rush
 */
public class NormObject {
  private long handle; // The pointer to the native NormObjectHandle

  NormObject(long handle) {
    this.handle = handle;
  }

  public native void setNackingMode(NormNackingMode nackingMode);

  public native NormObjectType getType();

  // TODO add hasInfo(), getInfoLength() methods?

  public native byte[] getInfo();

  public native long getSize();

  public native long getBytesPending();

  public native void cancel();

  public native void retain();

  public native void release();

  public native NormNode getSender() throws IOException;

  /**
   * @see java.lang.Object#hashCode()
   */
  public int hashCode() {
    return (int)handle;
  }

  /**
   * @see java.lang.Object#equals(java.lang.Object)
   */
  public boolean equals(Object obj) {
    if (obj instanceof NormObject) {
      return (handle == ((NormObject)obj).handle);
    }
    return false;
  }
}
