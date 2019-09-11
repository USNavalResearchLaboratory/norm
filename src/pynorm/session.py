"""
pynorm - Python wrapper for NRL's libnorm
By: Tom Wambold <wambold@itd.nrl.navy.mil>
"""

from __future__ import absolute_import

import ctypes

import pynorm.constants as c
from pynorm.core import libnorm, NormError
from pynorm.object import Object

class Session(object):
    """This represents a session tied to a particular NORM instance"""

    ## Public functions
    def __init__(self, instance, address, port, localId=c.NORM_NODE_ANY):
        """
        instance - An instance of NormInstance
        address - String of multicast address to join
        port - valid unused port number
        localId - NormNodeId
        """
        self._instance = instance
        self._session = libnorm.NormCreateSession(instance, address, port,
                localId)
        self.sendGracefulStop = False
        self.gracePeriod = 0

    def destroy(self):
        libnorm.NormDestroySession(self)

    def setUserData(self, data):
        """data should be a string"""
        libnorm.NormSetUserData(self, data)

    def getUserData(self):
        return libnorm.NormGetUserData(self)

    def getNodeId(self):
        return libnorm.NormGetLocalNodeId(self)

    def setTxPort(self, port):
        libnorm.NormSetTxPort(self, port)

    def setRxPortReuse(self, enable, bindToSessionAddr=True):
        libnorm.NormSetRxPortReuse(self, enable, bindToSessionAddr)

    def setMulticastInterface(self, iface):
        libnorm.NormSetMulticastInterface(self, iface)

    def setTTL(self, ttl):
        libnorm.NormSetTTL(self, ttl)

    def setTOS(self, tos):
        libnorm.NormSetTOS(self, tos)

    def setLoopback(self, loop):
        libnorm.NormSetLoopback(self, loop)

    def getReportInterval(self):
        libnorm.NormGetReportInterval(self)

    def setReportInterval(self, interval):
        libnorm.NormSetReportInterval(self, interval)

    ## Sender functions
    def startSender(self, sessionId, bufferSpace, segmentSize, blockSize,
            numParity):
        libnorm.NormStartSender(self, sessionId, bufferSpace, segmentSize,
                blockSize, numParity)

    def stopSender(self, graceful=False):
        libnorm.NormStopSender(self, graceful)

    def setTxRate(self, rate):
        libnorm.NormSetTxRate(self, rate)

    def setTxSocketBuffer(self, size):
        libnorm.NormSetTxSocketBuffer(self, size)

    def setCongestionControl(self, cc):
        libnorm.NormSetCongestionControl(self, cc)

    def setTxRateBounds(self, min, max):
        libnorm.NormSetTxRateBounds(self, min, max)

    def setTxCacheBounds(self, sizeMax, countMin, countMax):
        libnorm.NormSetTxCacheBounds(self, sizeMax, countMin, countMax)

    def setAutoParity(self, parity):
        libnorm.NormSetAutoParity(self, parity)

    def getGrttEstimate(self):
        return libnorm.NormGetGrttEstimate(self)

    def setGrttEstimate(self, grtt):
        libnorm.NormSetGrttEstimate(self, grtt)

    def setGrttMax(self, max):
        libnorm.NormSetGrttMax(self, max)

    def setGrttProbingMode(self, mode):
        libnorm.NormSetGrttProbingMode(self, mode)

    def setGrttProbingInterval(self, min, max):
        libnorm.NormSetGrttProbingInterval(self, min, max)

    def setBackoffFactor(self, factor):
        libnorm.NormSetBackoffFactor(self, factor)

    def setGroupSize(self, size):
        libnorm.NormSetGroupSize(self, size)

    def fileEnqueue(self, filename, info=""):
        return Object(libnorm.NormFileEnqueue(self, filename, info,
            len(info)))

    def dataEnqueue(self, data, info=""):
        return Object(libnorm.NormDataEnqueue(self, data,
            len(data), info, len(info)))

    def requeueObject(self, object):
        libnorm.NormRequeueObject(self, object)

    def streamOpen(self, bufferSize, info=""):
        return Object(libnorm.NormStreamOpen(self, bufferSize, info,
            len(info)))

    def setWatermark(self, object):
        libnorm.NormSetWatermark(self, object)

    def addAckingNode(self, nodeId):
        libnorm.NormAddAckingNode(self, nodeId)

    def removeAckingNode(self, nodeId):
        libnorm.NormRemoveAckingNode(self, nodeId)

    def getAckingStatus(self, nodeId=c.NORM_NODE_ANY):
        return libnorm.NormGetAckingStatus(self, nodeId)

    ## Receiver functions
    def startReceiver(self, bufferSpace):
        libnorm.NormStartReceiver(self, bufferSpace)

    def stopReceiver(self, gracePeriod=0):
        """This will be called automatically if the receiver is active"""
        libnorm.NormStopReceiver(self, gracePeriod)

    def setRxSocketBuffer(self, size):
        libnorm.NormSetRxSocketBuffer(self, size)

    def setSilentReceiver(self, silent, maxDelay=None):
        if maxDelay == None:
            maxDelay = -1
        libnorm.NormSetSilentReceiver(self, silent, maxDelay)

    def setDefaultUnicastNack(self, mode):
        libnorm.NormSetDefaultUnicastNack(self, mode)

    def setDefaultNackingMode(self, mode):
        libnorm.NormSetDefaultNackingMode(self, mode)

    def setDefaultRepairBoundary(self, boundary):
        libnorm.NormSetDefaultRepairBoundary(self, boundary)

    ## Properties
    nodeId = property(getNodeId)
    grtt = property(getGrttEstimate, setGrttEstimate)
    userData = property(getUserData, setUserData)
    reportInterval = property(getReportInterval, setReportInterval)

    ## Private functions
    def __del__(self):
        self.stopReceiver(self.gracePeriod)
        self.stopSender(self.sendGracefulStop)
        libnorm.NormDestroySession(self)

    @property
    def _as_parameter_(self):
        """Used when passing this object to ctypes functions"""
        return self._session

    def __cmp__(self, other):
        try:
            return cmp(self._as_parameter_, other._as_parameter)
        except AttributeError:
            return cmp(self._as_parameter_, other)

    def __hash__(self):
        return self._as_parameter_
