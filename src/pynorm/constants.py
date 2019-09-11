"""
pynorm - Python wrapper for NRL's libnorm
By: Tom Wambold <wambold@itd.nrl.navy.mil>
"""

from __future__ import absolute_import

import ctypes

from pynorm.core import libnorm

# Constants
# enum NormObjectType
NORM_OBJECT_NONE   = 0
NORM_OBJECT_DATA   = 1
NORM_OBJECT_FILE   = 2
NORM_OBJECT_STREAM = 3

# enum NormFlushMode
NORM_FLUSH_NONE    = 0
NORM_FLUSH_PASSIVE = 1
NORM_FLUSH_ACTIVE  = 2
    
# enum NormNackingMode
NORM_NACK_NONE = 0
NORM_NACK_INFO_ONLY = 1
NORM_NACK_NORMAL = 2

# enum NormAckingStatus
NORM_ACK_INVALID = 0
NORM_ACK_FAILURE = 1
NORM_ACK_PENDING = 2
NORM_ACK_SUCCESS = 3

# enum NormProbingMode
NORM_PROBE_NONE    = 0
NORM_PROBE_PASSIVE = 1
NORM_PROBE_ACTIVE  = 2

# enum NormSyncPolicy
NORM_SYNC_CURRENT = 0
NORM_SYNC_STREAM  = 1
NORM_SYNC_ALL     = 2    
    
# enum NormRepairBoundary
NORM_BOUNDARY_BLOCK  = 0
NORM_BOUNDARY_OBJECT = 1
    
# enum NormEventType
NORM_EVENT_INVALID          = 0
NORM_TX_QUEUE_VACANCY       = 1
NORM_TX_QUEUE_EMPTY         = 2
NORM_TX_FLUSH_COMPLETED     = 3
NORM_TX_WATERMARK_COMPLETED = 4
NORM_TX_CMD_SENT            = 5
NORM_TX_OBJECT_SENT         = 6
NORM_TX_OBJECT_PURGED       = 7
NORM_TX_RATE_CHANGED        = 8
NORM_LOCAL_SENDER_CLOSED    = 9
NORM_REMOTE_SENDER_NEW      = 10
NORM_REMOTE_SENDER_RESET    = 11
NORM_REMOTE_SENDER_ADDRESS  = 12
NORM_REMOTE_SENDER_ACTIVE   = 13
NORM_REMOTE_SENDER_INACTIVE = 14
NORM_REMOTE_SENDER_PURGED   = 15
NORM_RX_CMD_NEW             = 16
NORM_RX_OBJECT_NEW          = 17
NORM_RX_OBJECT_INFO         = 18
NORM_RX_OBJECT_UPDATED      = 19
NORM_RX_OBJECT_COMPLETED    = 20
NORM_RX_OBJECT_ABORTED      = 21
NORM_RX_ACK_REQUEST         = 22
NORM_GRTT_UPDATED           = 23
NORM_CC_ACTIVE              = 24
NORM_CC_INACTIVE            = 25
NORM_ACKING_NODE_NEW        = 26
NORM_SEND_ERROR             = 27
NORM_USER_TIMEOUT           = 28

NORM_INSTANCE_INVALID = ctypes.c_void_p.in_dll(libnorm, "NORM_INSTANCE_INVALID").value
NORM_SESSION_INVALID  = ctypes.c_void_p.in_dll(libnorm, "NORM_SESSION_INVALID").value
NORM_NODE_INVALID     = ctypes.c_void_p.in_dll(libnorm, "NORM_NODE_INVALID").value
NORM_NODE_NONE        = ctypes.c_uint32.in_dll(libnorm, "NORM_NODE_NONE").value
NORM_NODE_ANY         = ctypes.c_uint32.in_dll(libnorm, "NORM_NODE_ANY").value
NORM_OBJECT_INVALID   = ctypes.c_uint32.in_dll(libnorm, "NORM_OBJECT_INVALID").value
