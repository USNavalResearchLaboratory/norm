
package mil.navy.nrl.norm.io;

import mil.navy.nrl.norm.NormEvent;

/**
 * @author Jason Rush
 */
public interface INormEventListener {
  public void normEventOccurred(NormEvent normEvent);
}
