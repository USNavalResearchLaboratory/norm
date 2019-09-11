/*
 * Copyright (c) 2010 by General Dynamics Advanced Information Systems
 * Classification: UNCLASSIFIED
 */
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
