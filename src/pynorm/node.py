"""
pynorm - Python wrapper for NRL's libnorm
By: Tom Wambold <wambold@itd.nrl.navy.mil>
"""

from __future__ import absolute_import

import ctypes

from typing import Tuple
import ipaddress
import pynorm.constants as c
from pynorm.core import libnorm, NormError

class Node(object):
    """Represents a NORM node instance"""

    ## Public functions
    def __init__(self, node):
        libnorm.NormNodeRetain(node)
        self._node = node

    def getId(self) -> int:
        id = libnorm.NormNodeGetId(self)
        if id == c.NORM_NODE_ANY:
            return None
        return id

    def getAddress(self) -> (str,int):
        try:
            return self._address
        except AttributeError:
            port = ctypes.c_uint16()
            buf = ctypes.create_string_buffer(50)
            size = ctypes.c_uint(50)
            if not libnorm.NormNodeGetAddress(self, buf, ctypes.byref(size),
                    ctypes.byref(port)):
                raise NormError("Node getAddress failed")
            if len(buf.value)==4:
                ip = ipaddress.IPv4Address(buf.value)
            elif len(buf.value) == 16: #ipv6 is 128bit 
                ip = ipaddress.IPv6Address(buf.value)
            self._address = (str(ip), port.value)
            return self._address

    def getCommand(self, buf:bytes) -> bool:
        return libnorm.NormNodeGetCommand(self, buf, len(buf))
    
    def getGrtt(self) -> float:
        grtt = libnorm.NormNodeGetGrtt(self)
        if grtt == -1.0:
            raise NormError("getGrtt failed")
        return grtt

    def setUnicastNack(self, mode:bool):
        libnorm.NormNodeSetUnicastNack(self, mode)

    def setNackingMode(self, mode:c.NackingMode):
        libnorm.NormNodeSetNackingMode(self, mode.value)

    def setRepairBoundary(self, boundary:c.RepairBoundary):
        libnorm.NormNodeSetRepairBoundary(self, boundary.value)

    ## Properties
    id:int = property(getId)
    address:Tuple[str,int] = property(getAddress)
    grtt:float = property(getGrtt)

    ## Private Functions
    def __del__(self):
        libnorm.NormNodeRelease(self._node)

    @property
    def _as_parameter_(self):
        return self._node

    def __str__(self):
        return "Node - Id=%i" % self.id

    def __cmp__(self, other):
        def cmp(a, b):
            return (a > b) - (a < b)         
        return cmp(self._as_parameter_, other._as_parameter_)

    def __hash__(self):
        return self._as_parameter_
