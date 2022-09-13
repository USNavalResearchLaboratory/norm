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
    def __init__(self, eventType:c.EventType, session:Session, sender, normObject):
        self._type:c.EventType = eventType
        self._session:Session = session
        self._sender = sender
        self._object:Object = normObject

    # Properties
    type:c.EventType = property(lambda self: self._type)
    session:Session = property(lambda self: self._session)
    sender:Node = property(lambda self: self._sender)
    object:Object = property(lambda self: self._object)

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
        return self._type.name
