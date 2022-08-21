"""
pynorm - Python wrapper for NRL's libnorm
By: Tom Wambold <wambold@itd.nrl.navy.mil>
"""

from pynorm.instance import Instance
from pynorm.core import libnorm, NormError
from pynorm.constants import *

def setDebugLevel(level):
    libnorm.NormSetDebugLevel(level)

def getDebugLevel():
    return libnorm.NormGetDebugLevel()
