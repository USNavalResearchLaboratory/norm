/*
 * Copyright (c) 2010 by General Dynamics Advanced Information Systems
 * Classification: UNCLASSIFIED
 */
package mil.navy.nrl.norm.io;

import mil.navy.nrl.norm.NormEvent;

/**
 * @author Jason Rush
 */
public interface INormEventListener {
  public void normEventOccurred(NormEvent normEvent);
}
