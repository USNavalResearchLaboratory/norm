
package mil.navy.nrl.norm.io;

import java.io.IOException;

/**
 * @author Jason Rush
 */
public class StreamBreakException extends IOException {
  public StreamBreakException(String message) {
    super(message);
  }
}
