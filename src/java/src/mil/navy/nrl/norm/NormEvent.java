package mil.navy.nrl.norm;

import mil.navy.nrl.norm.enums.NormEventType;

/**
 * The NormEvent encapsulates the type of event that occured along with the
 * session, node, and possibly object information.
 * 
 * @author Jason Rush
 */
public class NormEvent {
  private NormEventType type;
  private long sessionHandle;
  private long nodeHandle;
  @SuppressWarnings("unused")
  private long objectHandle;

  public NormEvent(NormEventType type, long sessionHandle, long nodeHandle,
      long objectHandle) {
    this.type = type;
    this.sessionHandle = sessionHandle;
    this.nodeHandle = nodeHandle;
    this.objectHandle = objectHandle;
  }

  /**
   * @return Returns the type.
   */
  public NormEventType getType() {
    return type;
  }

  /**
   * @return Returns the session.
   */
  public NormSession getSession() {
    if (sessionHandle == 0) {
      return null;
    }

    return NormSession.getSession(sessionHandle);
  }

  /**
   * @return Returns the node.
   */
  public NormNode getNode() {
    if (nodeHandle == 0) {
      return null;
    }
    return new NormNode(nodeHandle);
  }

  /**
   * @return Returns the object.
   */
  public native NormObject getObject();

  /**
   * @see java.lang.Object#toString()
   */
  public String toString() {
    return String.format("NormEvent [type=%s]", type);
  }
}
