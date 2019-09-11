package mil.navy.nrl.norm.enums;

/**
 * Enumeration of the different types on NORM nacking modes. This enumeration
 * maps to the native implementation and therefore is tied to the specific
 * version of NORM.
 * 
 * @author Jason Rush
 */
public enum NormNackingMode {
  NORM_NACK_NONE,
  NORM_NACK_INFO_ONLY,
  NORM_NACK_NORMAL;
}
