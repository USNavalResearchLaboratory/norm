"""
pynorm - Python wrapper for NRL's libnorm
By: Tom Wambold <wambold@itd.nrl.navy.mil>
"""

from __future__ import absolute_import

import ctypes

import pynorm.constants as c
from pynorm.core import libnorm, NormError
from pynorm.node import Node

class Object(object):
    """Represents a NORM object instance"""

    ## Public functions
    def __init__(self, object):
        libnorm.NormObjectRetain(object)
        self._object = object
        
    def getType(self):
        return libnorm.NormObjectGetType(self)

    def hasObjectInfo(self):
        return libnorm.NormObjectHasInfo(self)

    def getInfo(self):
        if not self.hasObjectInfo():
            raise NormError("No object info received yet.")

        length = libnorm.NormObjectGetInfoLength(self)
        if length == 0:
            raise NormError("No object info received yet.")

        buffer = ctypes.create_string_buffer(length)
        recv = libnorm.NormObjectGetInfo(self, buffer, length)
        if recv == 0:
            raise NormError("No object info received yet.")
        return buffer.value

    def getSize(self):
        return libnorm.NormObjectGetSize(self)

    def getBytesPending(self):
        return libnorm.NormObjectGetBytesPending(self)

    def cancel(self):
        libnorm.NormObjectCancel(self)

    def getFileName(self):
        buffer = ctypes.create_string_buffer(100)
        libnorm.NormFileGetName(self, buffer, ctypes.sizeof(buffer))
        return buffer.value

    def renameFile(self, name):
        libnorm.NormFileRename(self, name)

    # Because 'ctypes.string_at()' makes a _copy_ of the data, we don't
    # support the usual NORM accessData / detachData options.  We use
    # NormDataAccessData() to get a pointer to the data we copy and
    # let the underlying NORM release the received object and free the
    # memory.
    def getData(self):
        return ctypes.string_at(libnorm.NormDataAccessData(self), self.size)

    #def accessData(self):
    #    return ctypes.string_at(libnorm.NormDataAccessData(self), self.size)
    #def detachData(self):
    #    return ctypes.string_at(libnorm.NormDataDetachData(self), self.size)
        
    def getSender(self):
        return Node(libnorm.NormObjectGetSender(self))

    def setNackingMode(self, mode):
        libnorm.NormObjectSetNackingMode(self, mode)

    ## Stream sending functions
    def streamClose(self, graceful=False):
        libnorm.NormStreamClose(self, graceful)

    def streamWrite(self, msg):
        return libnorm.NormStreamWrite(self, msg, len(msg))

    def streamFlush(self, eom=False, flushmode=c.NORM_FLUSH_PASSIVE):
        libnorm.NormStreamFlush(self, eom, flushmode)

    def streamSetAutoFlush(self, mode):
        libnorm.NormStreamSetAutoFlush(self, mode)

    def streamPushEnable(self, push):
        libnorm.NormStreamSetPushEnable(self, push)

    def streamHasVacancy(self):
        return libnorm.NormStreamHasVacancy(self)

    def streamMarkEom(self):
        libnorm.NormStreamMarkEom(self)

    ## Stream receiving functions
    def streamRead(self, size):
        buffer = ctypes.create_string_buffer(size)
        numBytes = ctypes.c_uint(ctypes.sizeof(buffer))
        libnorm.NormStreamRead(self, buffer, ctypes.byref(numBytes))
        return (numBytes, buffer.value)

    def streamSeekMsgStart(self):
        return libnorm.NormStreamSeekMsgStart(self)

    def streamGetReadOffset(self):
        return libnorm.NormStreamGetReadOffset(self)

    ## Properties
    type = property(getType)
    info = property(getInfo)
    size = property(getSize)
    bytesPending = property(getBytesPending)
    filename = property(getFileName, renameFile)
    sender = property(getSender)

    ## Private functions
    def __del__(self):
        libnorm.NormObjectRelease(self)

    @property
    def _as_parameter_(self):
        return self._object

    def __str__(self):
        if self.type == c.NORM_OBJECT_DATA:
            return "NORM_OBJECT_DATA"
        elif self.type == c.NORM_OBJECT_FILE:
            return "NORM_OBJECT_FILE"
        elif self.type == c.NORM_OBJECT_STREAM:
            return "NORM_OBJECT_STREAM"
        elif self.type == c.NORM_OBJECT_NONE:
            return "NORM_OBJECT_NONE"

    def __cmp__(self, other):
        return cmp(self._as_parameter_, other._as_parameter_)

    def __hash__(self):
        return hash(self._as_parameter_)
        
    def __equ__(self, other):
        return self._object == other._object
