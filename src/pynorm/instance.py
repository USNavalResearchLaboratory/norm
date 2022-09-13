"""
pynorm - Python wrapper for NRL's libnorm
By: Tom Wambold <wambold@itd.nrl.navy.mil>
"""

from __future__ import absolute_import
from typing import Hashable
import ctypes
from select import select
from weakref import WeakValueDictionary
from platform import system
import sys

if system() == 'Windows':
    import win32event

import pynorm.constants as c
from pynorm.core import libnorm, NormError, NormEventStruct
from pynorm.session import Session
from pynorm.event import Event
from pynorm.node import Node
from pynorm.object import Object

class Instance(object):
    """Represents an instance of the NORM protocal engine"""

    def __init__(self, priorityBoost=False):
        """Creates a new instance of the NORM protocol engine"""
        self._instance = libnorm.NormCreateInstance(priorityBoost)
        self._sessions:dict[int,Session] = dict() #sessionID -> Session
        self._senders = WeakValueDictionary()
        self._objects = WeakValueDictionary()
        self._estruct = NormEventStruct()
        self._index2Session:dict[Hashable, Session] = {}
        

        if system() == 'Windows':
            self._select = self._select_windows
        else:
            self._select = self._select_everythingelse

    ## Public functions
    def destroy(self):
        libnorm.NormDestroyInstance(self)
        
    def destroySession(self, sessionHandle:int):
        '''
        
        '''
        if session:=self._sessions.get(sessionHandle):
            #libnorm.NormDestroySession(sessionHandle)
            self._sessions.pop(sessionHandle)
            if session._index:
                self._index2Session.pop(session._index)
            return session
        
            
    def destroySessionByIndex(self, index:Hashable):
        if session:=self._index2Session.get(index):
            self._index2Session.pop(index)
            #libnorm.NormDestroySession(session)
            self._sessions.pop(session._session)
            return session
                      
            
    def stop(self):
        libnorm.NormStopInstance(self)

    def restart(self):
        libnorm.NormRestartInstance(self)

    def setCacheDirectory(self, path:str):
        libnorm.NormSetCacheDirectory(self, path.encode() )

    def setDebugLevel(self, level:c.DebugLevel):
        libnorm.NormSetDebugLevel(level.value)

    def openDebugLog(self, path:str):
        libnorm.NormOpenDebugLog(self, path.encode() )

    def closeDebugLog(self):
        libnorm.NormCloseDebugLog(self)

    def openDebugPipe(self, pipeName:str):
        libnorm.NormOpenDebugPipe(self, pipeName.encode() )

    def getNextEvent(self, timeout=None):
        # Use python's select because letting the C NormGetNextEvent block
        # seems to stop signals (CTRL+C) from killing the process
        if not self._select(timeout):
            return None

        result = libnorm.NormGetNextEvent(self, ctypes.byref(self._estruct), True)

        if not result:
            sys.stderr.write("NormInstance.getNextEvent() warning: no more NORM events\n")
            return False
            
        # Note a NORM_EVENT_INVALID can be OK (with NORM_SESSION_INVALID)    
        if self._estruct.type == c.EventType.EVENT_INVALID: 
            return Event(c.EventType.EVENT_INVALID, None, None, None)
        
        
        if self._estruct.session == c.NORM_SESSION_INVALID:
            raise NormError("No new event")
        sender = None
        sender_id = self._estruct.sender
        if sender_id:
            sender = self._senders.get(sender_id)
            if sender is None:
                sender = self._senders[sender_id] = Node(sender_id)
            
        obj = None 
        obj_id = self._estruct.object
        if obj_id:            
            obj =  self._objects.get(obj_id)
            if obj is None:
                obj = self._objects[obj_id] = Object(obj_id)
        return Event( c.EventType(self._estruct.type), self._sessions[self._estruct.session]  , sender, obj)

    def getDescriptor(self) -> int:
        return libnorm.NormGetDescriptor(self)

    def fileno(self) -> int:
        return libnorm.NormGetDescriptor(self)

    def createSession(self, address:str, port:int, localId:int=None, index:Hashable=None ):
        if localId == None:
            localId = c.NORM_NODE_ANY
        session:Session = Session(self, address, port, localId,index)
        self._sessions[session._session] = session
        if index:
            self._index2Session[index] = session
        return session

    ## Properties
    descriptor = property(getDescriptor)

    ## Private functions
    def __del__(self):
        del self._sessions
        self.destroy()

    def _select_windows(self, timeout):
        if timeout is None:
            timeout = win32event.INFINITE
        else:
            # Windows wants milliseconds...
            timeout *= 1000
        rv = win32event.WaitForSingleObject(self.getDescriptor(), int(timeout) )
        return True if rv == win32event.WAIT_OBJECT_0 else False

    def _select_everythingelse(self, timeout):
        return True if select([self], [], [], timeout)[0] else False

    @property
    def _as_parameter_(self):
        """This is for ctypes, so we can pass this object into C functions"""
        return self._instance

    def __iter__(self):
        return self

    def __next__(self):
        """So you can iterate over the events."""
        try:
          return self.getNextEvent()
        except NormError:
          raise StopIteration
          
    def next(self):
        return __next__()

    def __cmp__(self, other):
        def cmp(a, b):
            return (a > b) - (a < b)         
        return cmp(self._as_parameter_, other._as_parameter)

    def __hash__(self):
        return self._as_parameter_
