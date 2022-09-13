"""
pynorm - Python wrapper for NRL's libnorm
By: Tom Wambold <wambold@itd.nrl.navy.mil>
"""

from __future__ import absolute_import

import ctypes
import enum
from pynorm.core import libnorm

#enum ProtoDebugLevel {PL_FATAL, PL_ERROR, PL_WARN, PL_INFO, PL_DEBUG, PL_TRACE, PL_DETAIL, PL_MAX, PL_ALWAYS}; 
class DebugLevel(enum.IntEnum):
    FATAL = 0
    ERROR = 1
    WARNNING = 2
    INFO  =3
    DEBUG =4
    TRACE =5 
    DETAIL=6
    MAX   =7
    ALWAYS=8 

# Constants
# enum NormObjectType
class ObjectType(enum.Enum):
    NONE   = 0
    DATA   = 1
    FILE   = 2
    STREAM = 3    

# enum NormFlushMode
class FlushMode(enum.Enum):
    '''
       NormStreamFlush
       NormStreamSetAutoFlush
    '''
    NONE    = 0
    PASSIVE = 1
    ACTIVE  = 2    

    
# enum NormNackingMode


class NackingMode(enum.Enum):
    '''
      only used for NormSetDefaultNackingMode
    '''
    NONE = 0
    INFO_ONLY = 1
    NORMAL = 2    

# enum NormAckingStatus

class AckingStatus(enum.Enum):
    '''
      the return value of NormGetAckingStatus
    '''
    INVALID = 0
    FAILURE = 1
    PENDING = 2
    SUCCESS = 3    

# enum NormProbingMode


class ProbingMode(enum.Enum):
    '''
    used in NormSetGrttProbingMode
    '''
    NONE    = 0
    PASSIVE = 1
    ACTIVE  = 2    

# enum NormSyncPolicy
    
class SyncPolicy(enum.Enum):
    '''
      used in NormSetDefaultSyncPolicy
    '''
    CURRENT = 0
    STREAM  = 1
    ALL     = 2     

class TrackingStatus(enum.Enum):    #enum NormTrackingStatus
    NONE =0 #NORM_TRACK_NONE,
    RECEIVERS =1  #NORM_TRACK_RECEIVERS,
    SENDERS =2 #NORM_TRACK_SENDERS,
    ALL = 3   #NORM_TRACK_ALL
    
# enum NormRepairBoundary
NORM_BOUNDARY_BLOCK  = 0
NORM_BOUNDARY_OBJECT = 1

class RepairBoundary( enum.Enum):
    BOUNDARY_BLOCK  = 0
    BOUNDARY_OBJECT = 1   
    
# enum NormEventType
class EventType(enum.Enum):
    EVENT_INVALID          = 0
    TX_QUEUE_VACANCY       = 1
    TX_QUEUE_EMPTY         = 2
    TX_FLUSH_COMPLETED     = 3
    TX_WATERMARK_COMPLETED = 4
    TX_CMD_SENT            = 5
    TX_OBJECT_SENT         = 6
    TX_OBJECT_PURGED       = 7
    TX_RATE_CHANGED        = 8
    LOCAL_SENDER_CLOSED    = 9
    REMOTE_SENDER_NEW      = 10
    REMOTE_SENDER_RESET    = 11
    REMOTE_SENDER_ADDRESS  = 12
    REMOTE_SENDER_ACTIVE   = 13
    REMOTE_SENDER_INACTIVE = 14
    REMOTE_SENDER_PURGED   = 15
    RX_CMD_NEW             = 16
    RX_OBJECT_NEW          = 17
    RX_OBJECT_INFO         = 18
    RX_OBJECT_UPDATED      = 19
    RX_OBJECT_COMPLETED    = 20
    RX_OBJECT_ABORTED      = 21
    RX_ACK_REQUEST         = 22
    GRTT_UPDATED           = 23
    CC_ACTIVE              = 24
    CC_INACTIVE            = 25
    ACKING_NODE_NEW        = 26
    SEND_ERROR             = 27
    USER_TIMEOUT           = 28    

NORM_INSTANCE_INVALID = ctypes.c_void_p.in_dll(libnorm, "NORM_INSTANCE_INVALID").value
NORM_SESSION_INVALID  = ctypes.c_void_p.in_dll(libnorm, "NORM_SESSION_INVALID").value
NORM_NODE_INVALID     = ctypes.c_void_p.in_dll(libnorm, "NORM_NODE_INVALID").value
NORM_NODE_NONE        = ctypes.c_uint32.in_dll(libnorm, "NORM_NODE_NONE").value
NORM_NODE_ANY         = ctypes.c_uint32.in_dll(libnorm, "NORM_NODE_ANY").value
NORM_OBJECT_INVALID   = ctypes.c_uint32.in_dll(libnorm, "NORM_OBJECT_INVALID").value
