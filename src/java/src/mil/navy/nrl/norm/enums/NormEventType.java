package mil.navy.nrl.norm.enums;

/**
 * Enumeration of the different types on NORM events. This enumeration maps to
 * the native implementation and therefore is tied to the specific version of
 * NORM.
 * 
 * @author Jason Rush and Brian Adamson
 */
public enum NormEventType {                 
  NORM_EVENT_INVALID,
  NORM_TX_QUEUE_VACANCY,
  NORM_TX_QUEUE_EMPTY,
  NORM_TX_FLUSH_COMPLETED,
  NORM_TX_WATERMARK_COMPLETED,
  NORM_TX_CMD_SENT,
  NORM_TX_OBJECT_SENT,
  NORM_TX_OBJECT_PURGED,
  NORM_TX_RATE_CHANGED,
  NORM_LOCAL_SENDER_CLOSED,
  NORM_REMOTE_SENDER_NEW,
  NORM_REMOTE_SENDER_RESET,    
  NORM_REMOTE_SENDER_ADDRESS,  
  NORM_REMOTE_SENDER_ACTIVE,
  NORM_REMOTE_SENDER_INACTIVE,
  NORM_REMOTE_SENDER_PURGED,   
  NORM_RX_CMD_NEW,             
  NORM_RX_OBJECT_NEW,
  NORM_RX_OBJECT_INFO,
  NORM_RX_OBJECT_UPDATED,
  NORM_RX_OBJECT_COMPLETED,
  NORM_RX_OBJECT_ABORTED,
  NORM_RX_ACK_REQUEST,         
  NORM_GRTT_UPDATED,
  NORM_CC_ACTIVE,
  NORM_CC_INACTIVE,
  NORM_ACKING_NODE_NEW,        
  NORM_SEND_ERROR,             
  NORM_USER_TIMEOUT            
}
