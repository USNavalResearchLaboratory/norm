"""
pynorm - Python wrapper for NRL's libnorm
By: Tom Wambold <wambold@itd.nrl.navy.mil>
"""

from __future__ import absolute_import

import ctypes

import pynorm.constants as c
from pynorm.core import libnorm, NormError

class Node(object):
    """Represents a NORM node instance"""

    ## Public functions
    def __init__(self, node):
        libnorm.NormNodeRetain(node)
        self._node = node

    def getId(self):
        id = libnorm.NormNodeGetId(self)
        if id == c.NORM_NODE_ANY:
            return None
        return id

    def getAddress(self):
        try:
            return self._address
        except AttributeError:
            port = ctypes.c_uint16()
            buf = ctypes.create_string_buffer(50)
            size = ctypes.c_uint(50)
            if not libnorm.NormNodeGetAddress(self, buf, ctypes.byref(size),
                    ctypes.byref(port)):
                raise NormError("Node getAddress failed")
            self._address = (buf.value, port.value)
            return self._address

    def getCommand(self, buf):
        return libnorm.NormNodeGetCommand(self, buf, len(buf))
    
    def getGrtt(self):
        grtt = libnorm.NormNodeGetGrtt(self)
        if grtt == -1.0:
            raise NormError("getGrtt failed")
        return grtt

    def setUnicastNack(self, mode):
        libnorm.NormNodeSetUnicastNack(self, mode)

    def setNackingMode(self, mode):
        libnorm.NormNodeSetNackingMode(self, mode)

    def setRepairBoundary(self, boundary):
        libnorm.NormNodeSetRepairBoundary(self, boundary)

    ## Properties
    id = property(getId)
    address = property(getAddress)
    grtt = property(getGrtt)

    ## Private Functions
    def __del__(self):
        libnorm.NormNodeRelease(self)

    @property
    def _as_parameter_(self):
        return self._node

    def __str__(self):
        return "Node - Id=%i" % self.id

    def __cmp__(self, other):
        return cmp(self._as_parameter_, other._as_parameter_)

    def __hash__(self):
        return self._as_parameter_
