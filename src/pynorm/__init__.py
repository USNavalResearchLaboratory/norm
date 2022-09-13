"""
pynorm - Python wrapper for NRL's libnorm
By: Tom Wambold <wambold@itd.nrl.navy.mil>
"""

from pynorm.instance import Instance
from pynorm.core import libnorm, NormError
from pynorm.constants import *
from pynorm.event import Event
from pynorm.session import Session
from pynorm.object import Object

def setDebugLevel(level: DebugLevel):
    libnorm.NormSetDebugLevel(level.value)

def getDebugLevel() -> DebugLevel:
    return DebugLevel( libnorm.NormGetDebugLevel() )
