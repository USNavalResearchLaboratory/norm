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
        self._session = libnorm.NormCreateSession(instance, address.encode('utf-8'), port, localId)
        self.sendGracefulStop = False
        self.gracePeriod = 0

    def destroy(self):
        libnorm.NormDestroySession(self)
        del self._instance._sessions[self]

    def setUserData(self, data):
        """data should be a string"""
        libnorm.NormSetUserData(self, data.encode('utf-8'))

    def getUserData(self):
        data = libnorm.NormGetUserData(self)
        return data.decode('utf-8') if data else None

    def getNodeId(self):
        return libnorm.NormGetLocalNodeId(self)

    def setTxPort(self, txPort, enableReuse=False, txBindAddr=None):
        libnorm.NormSetTxPort(self, txPort, enableReuse, txBindAddr)

    def setRxPortReuse(self, enable, rxBindAddr=None, senderAddr=None, senderPort=0):
        libnorm.NormSetRxPortReuse(self, enable, rxBindAddr.encode('utf-8'), senderAddr.encode('utf-8'), senderPort)

    def setMulticastInterface(self, iface):
        libnorm.NormSetMulticastInterface(self, iface.encode('utf-8'))
    
    def setSSM(self, srcAddr):
        libnorm.NormSetSSM(self, srcAddr.encode('utf-8'))
        
    def setTTL(self, ttl):
        libnorm.NormSetTTL(self, ttl)

    def setTOS(self, tos):
        libnorm.NormSetTOS(self, tos)

    def setLoopback(self, loop):
        libnorm.NormSetLoopback(self, loop)

    def getReportInterval(self):
        return libnorm.NormGetReportInterval(self)

    def setReportInterval(self, interval):
        libnorm.NormSetReportInterval(self, interval)

    ## Sender functions
    def startSender(self, sessionId, bufferSpace, segmentSize, blockSize, numParity, fecId=0):
        libnorm.NormStartSender(self, sessionId, bufferSpace, segmentSize, blockSize, numParity, fecId)

    def stopSender(self):
        libnorm.NormStopSender(self)

    def setTxRate(self, rate):
        libnorm.NormSetTxRate(self, rate)

    def setTxSocketBuffer(self, size):
        libnorm.NormSetTxSocketBuffer(self, size)

    def setCongestionControl(self, ccEnable, adjustRate=True):
        libnorm.NormSetCongestionControl(self, ccEnable, adjustRate)
        
    def setEcnSupport(self, ecnEnable, ignoreLoss=False, tolerateLoss=False):
        libnorm.NormSetEcnSupport(self, ecnEnable, ignoreLoss, tolerateLoss)
        
    def setFlowControl(self, flowControlFactor):
        libnorm.NormSetFlowControl(self, flowControlFactor)

    def setTxRateBounds(self, rateMin, rateMax):
        libnorm.NormSetTxRateBounds(self, rateMin, rateMax)

    def setTxCacheBounds(self, sizeMax, countMin, countMax):
        libnorm.NormSetTxCacheBounds(self, sizeMax, countMin, countMax)

    def setAutoParity(self, parity):
        libnorm.NormSetAutoParity(self, parity)

    def getGrttEstimate(self):
        return libnorm.NormGetGrttEstimate(self)

    def setGrttEstimate(self, grttMax):
        libnorm.NormSetGrttEstimate(self, grtt)

    def setGrttMax(self, max):
        libnorm.NormSetGrttMax(self, grttMax)

    def setGrttProbingMode(self, mode):
        libnorm.NormSetGrttProbingMode(self, mode)

    def setGrttProbingInterval(self, intervalMin, intervalMax):
        libnorm.NormSetGrttProbingInterval(self, intervalMin, intervalMax)

    def setBackoffFactor(self, factor):
        libnorm.NormSetBackoffFactor(self, factor)

    def setGroupSize(self, size):
        libnorm.NormSetGroupSize(self, size)

    def fileEnqueue(self, filename, info=""):
        # TBD - allow for case of info being None?
        result = libnorm.NormFileEnqueue(self, filename.encode('utf-8'), info.encode('utf-8'), len(info))
        if ctypes.c_void_p.in_dll(libnorm, "NORM_OBJECT_INVALID") == result:
            return None; # enqueue not successful due to flow control or sender cache limit
        else
            # Put a reference of the object in our instance "_objects" cache to avoid creation 
            # of duplicative Python NORM Object during event notification
            obj = self._instance._objects[self._estruct.object] = Object(result)
            return obj

    def dataEnqueue(self, data, info=""):
        # TBD - allow for case of info being None?
        result = libnorm.NormDataEnqueue(self, data.encode('utf-8'), len(data), info.encode('utf-8'), len(info))
        if ctypes.c_void_p.in_dll(libnorm, "NORM_OBJECT_INVALID") == result:
            return None; # enqueue not successful due to flow control or sender cache limit
        else
            # Put a reference of the object in our instance "_objects" cache to avoid creation 
            # of duplicative Python NORM Object during event notification
            obj = self._instance._objects[self._estruct.object] = Object(result)
            return obj

    def streamOpen(self, bufferSize, info=""):
        # TBD - allow for case of info being None?
        result = libnorm.NormStreamOpen(self, bufferSize, info.encode('utf-8'), len(info))
        if ctypes.c_void_p.in_dll(libnorm, "NORM_OBJECT_INVALID") == result:
            return None; # stream open/enqueue not successful due to flow control or sender cache limit
        else
            # Put a reference of the object in our instance "_objects" cache to avoid creation 
            # of duplicative Python NORM Object during event notification
            obj = self._instance._objects[self._estruct.object] = Object(result)
            return obj
    
    def requeueObject(self, normObject):
        libnorm.NormRequeueObject(self, normObject)

    def sendCommand(self, cmdBuffer, robust=False):
        return libnorm.NormSendCommand(self, cmdBuffer.encode('utf-8'), len(cmdBuffer), robust)
	
    def cancelCommand(self):
        libnorm.NormCancelCommand(self)

    def setWatermark(self, normObject, overrideFlush=False):
        libnorm.NormSetWatermark(self, normObject, overrideFlush)
        
    def resetWatermark(self):
        libnorm.NormResetWatermark(self)
        
    def cancelWatermark(self):
        libnorm.NormCancelWatermark(self)

    def addAckingNode(self, nodeId):
        libnorm.NormAddAckingNode(self, nodeId)

    def removeAckingNode(self, nodeId):
        libnorm.NormRemoveAckingNode(self, nodeId)

    def getAckingStatus(self, nodeId=c.NORM_NODE_ANY):
        return libnorm.NormGetAckingStatus(self, nodeId)

    ## Receiver functions
    def startReceiver(self, bufferSpace):
        libnorm.NormStartReceiver(self, bufferSpace)

    def stopReceiver(self):
        """This will be called automatically if the receiver is active"""
        libnorm.NormStopReceiver(self)

    def setRxCacheLimit(self, count):
        libnorm.NormSetRxCacheLimit(self, count)
        
    def setRxSocketBuffer(self, size):
        libnorm.NormSetRxSocketBuffer(self, size)

    def setSilentReceiver(self, silent, maxDelay=None):
        if maxDelay == None:
            maxDelay = -1
        libnorm.NormSetSilentReceiver(self, silent, maxDelay)

    def setDefaultUnicastNack(self, mode):
        libnorm.NormSetDefaultUnicastNack(self, mode)
        
    def setDefaultSyncPolicy(self, policy):
        libnorm.NormSetDefaultSyncPolicy(self, policy)

    def setDefaultNackingMode(self, mode):
        libnorm.NormSetDefaultNackingMode(self, mode)

    def setDefaultRepairBoundary(self, boundary):
        libnorm.NormSetDefaultRepairBoundary(self, boundary)
        
    def setMessageTrace(self, state):
        libnorm.NormSetMessageTrace(self, state)

    ## Properties
    nodeId = property(getNodeId)
    grtt = property(getGrttEstimate, setGrttEstimate)
    userData = property(getUserData, setUserData)
    reportInterval = property(getReportInterval, setReportInterval)

    ## Private functions
    def __del__(self):
        self.stopReceiver()
        self.stopSender()
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
