package mil.navy.nrl.norm;

/**
 * This class contains information about a NORM Data Object.
 * 
 * @author Jason Rush
 */
public class NormData extends NormObject {

  NormData(long handle) {
    super(handle);
  }

  public native byte[] getData();
}
