package mil.navy.nrl.norm;

import mil.navy.nrl.norm.enums.NormFlushMode;

/**
 * This class contains information about a NORM Stream Object.
 * 
 * @author Jason Rush
 */
public class NormStream extends NormObject {

  NormStream(long handle) {
    super(handle);
  }

  public void close() {
    close(false);
  }

  public native void close(boolean graceful);

  public native int write(byte buffer[], int offset, int length);

  public void flush() {
    flush(false, NormFlushMode.NORM_FLUSH_PASSIVE);
  }

  public native void flush(boolean eom, NormFlushMode flushMode);

  public native void setAutoFlush(NormFlushMode flushMode);

  public native void setPushEnable(boolean pushEnable);

  public native boolean hasVacancy();

  public native void markEom();

  public native int read(byte buffer[], int offset, int length);

  public native boolean seekMsgStart();

  public native long getReadOffset();
}
