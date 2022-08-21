package mil.navy.nrl.norm.enums;

/**
 * Enumeration of the different types on NORM acking statuses. This enumeration
 * maps to the native implementation and therefore is tied to the specific
 * version of NORM.
 * 
 * @author Jason Rush
 */
public enum NormAckingStatus {
  NORM_ACK_INVALID,
  NORM_ACK_FAILURE,
  NORM_ACK_PENDING,
  NORM_ACK_SUCCESS;
}
