package mil.navy.nrl.norm;

import java.io.IOException;

/**
 * This class contains information about a NORM File Object.
 * 
 * @author Jason Rush
 */
public class NormFile extends NormObject {

  NormFile(long handle) {
    super(handle);
  }

  public native String getName() throws IOException;

  public native void rename(String filename) throws IOException;
}
