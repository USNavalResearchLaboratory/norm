"""
pynorm - Python wrapper for NRL's libnorm
By: Tom Wambold <wambold@itd.nrl.navy.mil>
"""

from __future__ import absolute_import

import ctypes

import pynorm.constants as c
from pynorm.session import Session
from pynorm.node import Node
from pynorm.object import Object

class Event(object):
    def __init__(self, eventType, session, sender, normObject):
        self._type = eventType
        self._session = session
        self._sender = sender
        self._object = normObject

    # Properties
    type = property(lambda self: self._type)
    session = property(lambda self: self._session)
    sender = property(lambda self: self._sender)
    object = property(lambda self: self._object)

    ## Private functions
    @property
    def _as_parameter_(self):
        """For Ctypes"""
        return self._event

    def __cmp__(self, type):
        """Does comparison by string event name, or by constant"""
        if not str(type).isdigit():
            try:
                type = getattr(c, type)
            except AttributeError:
                return -1
        return cmp(self.type, type)

    def __str__(self):
        if self.type == c.NORM_EVENT_INVALID:
            return "NORM_EVENT_INVALID"
        elif self.type == c.NORM_TX_QUEUE_VACANCY:
            return "NORM_TX_QUEUE_VACANCY"
        elif self.type == c.NORM_TX_QUEUE_EMPTY:
            return "NORM_TX_QUEUE_EMPTY"
        elif self.type == c.NORM_TX_FLUSH_COMPLETED:
            return "NORM_TX_FLUSH_COMPLETED"
        elif self.type == c.NORM_TX_WATERMARK_COMPLETED:
            return "NORM_TX_WATERMARK_COMPLETED"
        elif self.type == c.NORM_TX_CMD_SENT:
            return "NORM_TX_CMD_SENT"
        elif self.type == c.NORM_TX_OBJECT_SENT:
            return "NORM_TX_OBJECT_SENT"
        elif self.type == c.NORM_TX_OBJECT_PURGED:
            return "NORM_TX_OBJECT_PURGED"
        elif self.type == c.NORM_TX_RATE_CHANGED:
            return "NORM_TX_RATE_CHANGED"
        elif self.type == c.NORM_LOCAL_SENDER_CLOSED:
            return "NORM_LOCAL_SENDER_CLOSED"
        elif self.type == c.NORM_REMOTE_SENDER_NEW:
            return "NORM_REMOTE_SENDER_NEW"
        elif self.type == c.NORM_REMOTE_SENDER_RESET:
            return "NORM_REMOTE_SENDER_RESET"
        elif self.type == c.NORM_REMOTE_SENDER_ADDRESS:
            return "NORM_REMOTE_SENDER_ADDRESS"
        elif self.type == c.NORM_REMOTE_SENDER_ACTIVE:
            return "NORM_REMOTE_SENDER_ACTIVE"
        elif self.type == c.NORM_REMOTE_SENDER_INACTIVE:
            return "NORM_REMOTE_SENDER_INACTIVE"
        elif self.type == c.NORM_REMOTE_SENDER_PURGED:
            return "NORM_REMOTE_SENDER_PURGED"
        elif self.type == c.NORM_RX_CMD_NEW:
            return "NORM_RX_CMD_NEW"
        elif self.type == c.NORM_RX_OBJECT_NEW:
            return "NORM_RX_OBJECT_NEW"
        elif self.type == c.NORM_RX_OBJECT_INFO:
            return "NORM_RX_OBJECT_INFO"
        elif self.type == c.NORM_RX_OBJECT_UPDATED:
            return "NORM_RX_OBJECT_UPDATED"
        elif self.type == c.NORM_RX_OBJECT_COMPLETED:
            return "NORM_RX_OBJECT_COMPLETED"
        elif self.type == c.NORM_RX_OBJECT_ABORTED:
            return "NORM_RX_OBJECT_ABORTED"
        elif self.type == c.NORM_RX_ACK_REQUEST:
            return "NORM_RX_ACK_REQUEST"
        elif self.type == c.NORM_GRTT_UPDATED:
            return "NORM_GRTT_UPDATED"
        elif self.type == c.NORM_CC_ACTIVE:
            return "NORM_CC_ACTIVE"
        elif self.type == c.NORM_CC_INACTIVE:
            return "NORM_CC_INACTIVE"
        elif self.type == c.NORM_ACKING_NODE_NEW:
            return "NORM_ACKING_NODE_NEW"
        elif self.type == c.NORM_SEND_ERROR:
            return "NORM_SEND_ERROR"
        elif self.type == c.NORM_USER_TIMEOUT:
            return "NORM_USER_TIMEOUT"
        else:
            return "Unknown event type"
