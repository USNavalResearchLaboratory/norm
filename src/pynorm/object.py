"""
pynorm - Python wrapper for NRL's libnorm
By: Tom Wambold <wambold@itd.nrl.navy.mil>
"""

from __future__ import absolute_import

import ctypes
from typing import Union
import locale
import pynorm.constants as c
from pynorm.core import libnorm, NormError
from pynorm.node import Node

class Object(object):
    """Represents a NORM object instance"""
    locale_encoding:str = locale.getpreferredencoding()

    ## Public functions
    def __init__(self, object_id:int):
        libnorm.NormObjectRetain(object_id)
        self._object:int = object_id # type NormObjectHandle
        
    def getType(self):
        return libnorm.NormObjectGetType(self)
    def getType(self) -> c.ObjectType:
        value = libnorm.NormObjectGetType(self)
        return c.ObjectType(value)
    def hasObjectInfo(self):
        return libnorm.NormObjectHasInfo(self)

    def getInfo(self):
        if not self.hasObjectInfo():
            raise NormError("No object info received yet.")

        length = libnorm.NormObjectGetInfoLength(self)
        if length == 0:
            raise NormError("No object info received yet.")

        buf = ctypes.create_string_buffer(length)
        recv = libnorm.NormObjectGetInfo(self, buf, length)
        if recv == 0:
            raise NormError("No object info received yet.")
        return buf.value

    def getSize(self) -> int:
        return libnorm.NormObjectGetSize(self)

    def getBytesPending(self) -> int:
        return libnorm.NormObjectGetBytesPending(self)

    def cancel(self) -> None:
        libnorm.NormObjectCancel(self)

    def getFileName(self) ->Union[str,bool]:
        buf = ctypes.create_string_buffer(100)
        success = libnorm.NormFileGetName(self, buf, ctypes.sizeof(buf))
        if success:
            return  buf.value.decode(Object.locale_encoding)
        else:
            return False

    def renameFile(self, name:str) -> bool:
        '''
            
        '''
        return libnorm.NormFileRename(self, name.encode( Object.locale_encoding ))

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

    def setNackingMode(self, mode:c.NackingMode):
        libnorm.NormObjectSetNackingMode(self, mode.value)

    ## Stream sending functions
    def streamClose(self, graceful=False):
        libnorm.NormStreamClose(self, graceful)

    def streamWrite(self, msg:bytes):
        return libnorm.NormStreamWrite(self, msg, len(msg))

    def streamFlush(self, eom=False, flushmode:c.FlushMode = c.FlushMode.PASSIVE):
        libnorm.NormStreamFlush(self, eom, flushmode.value)

    def streamSetAutoFlush(self, flushMode:c.FlushMode):
        libnorm.NormStreamSetAutoFlush(self, flushMode.value)

    def streamPushEnable(self, push:bool):
        libnorm.NormStreamSetPushEnable(self, push)

    def streamHasVacancy(self):
        return libnorm.NormStreamHasVacancy(self)

    def streamMarkEom(self):
        libnorm.NormStreamMarkEom(self)

    ## Stream receiving functions
    def streamRead(self, size) -> (int,bytes):
        buf = ctypes.create_string_buffer(size)
        numBytes = ctypes.c_uint(ctypes.sizeof(buf))
        libnorm.NormStreamRead(self, buf, ctypes.byref(numBytes))
        return (numBytes.value, buf)

    def streamSeekMsgStart(self):
        return libnorm.NormStreamSeekMsgStart(self)

    def streamGetReadOffset(self):
        return libnorm.NormStreamGetReadOffset(self)

    ## Properties
    type:c.ObjectType = property(getType)
    info:bytes = property(getInfo)
    size:int = property(getSize)
    bytesPending:int = property(getBytesPending)
    filename:bytes = property(getFileName, renameFile)
    sender:Node = property(getSender)
    handle:int = property( lambda self:self._object )

    ## Private functions
    def __del__(self):
        libnorm.NormObjectRelease(self)

    @property
    def _as_parameter_(self):
        return self._object

    def __str__(self):
        return self.type.name

    def __cmp__(self, other):
        def cmp(a, b):
            return (a > b) - (a < b)        
        return cmp(self._as_parameter_, other._as_parameter_)

    def __hash__(self):
        return hash(self._as_parameter_)
        
    def __equ__(self, other):
        return self._object == other._object
