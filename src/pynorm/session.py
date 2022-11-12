"""
pynorm - Python wrapper for NRL's libnorm
By: Tom Wambold <wambold@itd.nrl.navy.mil>
"""

from __future__ import absolute_import

import ctypes
import locale
from typing import Optional
import pynorm.constants as c
from pynorm.core import libnorm, NormError
from pynorm.object import Object

class Session(object):
    """This represents a session tied to a particular NORM instance"""
    locale_encoding:str = locale.getpreferredencoding()

    ## Public functions
    def __init__(self, instance, address:str, port:int, localId=c.NORM_NODE_ANY): 
        """
        instance - An instance of NormInstance
        address - String of multicast address to join
        port - valid unused port number
        localId - NormNodeId
        """
        self._instance = instance
        self._session:int = libnorm.NormCreateSession(instance, address.encode('utf-8'), port, localId)
        self.sendGracefulStop = False
        self.gracePeriod = 0

    def destroy(self):
        libnorm.NormDestroySession(self)
        del self._instance._sessions[self]

    def setUserData(self, data:str):
        """data should be a string"""
        libnorm.NormSetUserData(self, data.encode('utf-8'))

    def getUserData(self)->str:
        data = libnorm.NormGetUserData(self)
        return data.decode('utf-8') if data else None

    def getNodeId(self):
        return libnorm.NormGetLocalNodeId(self)

    def setTxPort(self, txPort:int, enableReuse:bool=False, txBindAddr:Optional[str]=None):
        libnorm.NormSetTxPort(self, txPort, enableReuse, 
                              txBindAddr.encode('utf-8') if txBindAddr else None )

    def setTxOnly(self, txOnly:bool=False, connectToSessionAddress:bool=False):
        libnorm.NormSetTxOnly(self, txOnly, connectToSessionAddress)


    def setRxPortReuse(self, enable:bool, rxBindAddr:Optional[str]=None, senderAddr:Optional[str]=None, senderPort:int=0):
        '''
        This function allows the user to control the port reuse and binding behavior for the receive socket used for the given NORM sessionHandle. 
        When the enablReuse parameter is set to true, reuse of the NormSession port number by multiple NORM instances or sessions is enabled.
        '''
        libnorm.NormSetRxPortReuse(self, enable, 
                                   rxBindAddr.encode('utf-8') if rxBindAddr else None,
                                   senderAddr.encode('utf-8') if senderAddr else None,
                                   senderPort)

    def setMulticastInterface(self, iface:str):
        libnorm.NormSetMulticastInterface(self, iface.encode('utf-8'))

    def setSSM(self, srcAddr:str):
        libnorm.NormSetSSM(self, srcAddr.encode('utf-8'))
        
    def setTTL(self, ttl) -> bool:
        return libnorm.NormSetTTL(self, ttl)

    def setTOS(self, tos) -> bool:
        return libnorm.NormSetTOS(self, tos)

    def setLoopback(self, loopbackEnable:bool=False):
        libnorm.NormSetLoopback(self, loopbackEnable)
        
    def setMulticastLoopback(self, enable:bool):
        libnorm.NormSetMulticastLoopback(self, enable)
        
    def setTxLoss(self, percent:float):
        libnorm.NormSetTxLoss(percent)
        
    def setRxLoss(self, percent:float):
        libnorm.NormSetRxLoss(percent)

    def getReportInterval(self) -> int:
        return libnorm.NormGetReportInterval(self)

    def setReportInterval(self, interval:int):
        libnorm.NormSetReportInterval(self, interval)

    ## Sender functions
    def startSender(self, sessionId:int, bufferSpace:int, segmentSize:int, blockSize:int, numParity:int, fecId=0) -> bool:
        return libnorm.NormStartSender(self, sessionId, bufferSpace, segmentSize, blockSize, numParity, fecId)

    def stopSender(self):
        libnorm.NormStopSender(self)

    def setTxRate(self, txRate:float):
        libnorm.NormSetTxRate(self, txRate)

    def getTxRate(self) -> float:
        return libnorm.NormGetTxRate(self)

    def setTxSocketBuffer(self, size:int):
        libnorm.NormSetTxSocketBuffer(self, size)

    def setCongestionControl(self, ccEnable:bool, adjustRate:bool=True):
        '''
            must called before startSender
        '''
        libnorm.NormSetCongestionControl(self, ccEnable, adjustRate)

    def setEcnSupport(self, ecnEnable, ignoreLoss=False, tolerateLoss=False):
        libnorm.NormSetEcnSupport(self, ecnEnable, ignoreLoss, tolerateLoss)

    def setFlowControl(self, flowControlFactor):
        libnorm.NormSetFlowControl(self, flowControlFactor)

    def setTxRateBounds(self, rateMin, rateMax) -> bool:
        '''
           If both rateMin and rateMax are greater than or equal to zero, 
           but (rateMax < rateMin), the rate bounds will remain unset or unchanged and the function will return false.
        '''
        if rateMin > rateMax:
            return False
        libnorm.NormSetTxRateBounds(self, rateMin, rateMax)
        return True

    def setTxCacheBounds(self, sizeMax:int, countMin:int, countMax:int):
        libnorm.NormSetTxCacheBounds(self, sizeMax, countMin, countMax)

    def setAutoParity(self, parity:int):
        libnorm.NormSetAutoParity(self, parity)

    def getGrttEstimate(self) -> float:
        return libnorm.NormGetGrttEstimate(self)

    def setGrttEstimate(self, grtt:float):
        return libnorm.NormSetGrttEstimate(self, grtt)

    def setGrttMax(self, grttMax:float):
        return libnorm.NormSetGrttMax(self, grttMax)

    def setGrttProbingMode(self, probingMode:c.ProbingMode):
        libnorm.NormSetGrttProbingMode(self, probingMode.value)

    def setGrttProbingInterval(self, intervalMin:int, intervalMax:int):
        libnorm.NormSetGrttProbingInterval(self, intervalMin, intervalMax)

    def setBackoffFactor(self, factor:int):
        libnorm.NormSetBackoffFactor(self, factor)

    def setGroupSize(self, size:int):
        libnorm.NormSetGroupSize(self, size)

    def setTxRobustFactor(self, robustFactor:int):
        libnorm.NormSetTxRobustFactor(self, robustFactor)

    def fileEnqueue(self, filename:str, info:bytes=b""):
        # TBD - allow for case of info being None?
        result = libnorm.NormFileEnqueue(self, filename.encode(self.locale_encoding), info, len(info))
        if ctypes.c_void_p.in_dll(libnorm, "NORM_OBJECT_INVALID") == result:
            return None; # enqueue not successful due to flow control or sender cache limit
        else:
            # Put a reference of the object in our instance "_objects" cache to avoid creation 
            # of duplicative Python NORM Object during event notification
            obj = self._instance._objects[result] = Object(result)
            return obj

    def dataEnqueue(self, data:bytes, info:bytes=b""):
        # TBD - allow for case of info being None?
        result = libnorm.NormDataEnqueue(self, data, len(data), info, len(info))
        if ctypes.c_void_p.in_dll(libnorm, "NORM_OBJECT_INVALID") == result:
            return None; # enqueue not successful due to flow control or sender cache limit
        else:
            # Put a reference of the object in our instance "_objects" cache to avoid creation 
            # of duplicative Python NORM Object during event notification
            obj = self._instance._objects[result] = Object(result)
            return obj

    def streamOpen(self, bufferSize:int, info=b""):
        # TBD - allow for case of info being None?
        result = libnorm.NormStreamOpen(self, bufferSize, info, len(info))
        if ctypes.c_void_p.in_dll(libnorm, "NORM_OBJECT_INVALID") == result:
            return None; # stream open/enqueue not successful due to flow control or sender cache limit
        else:
            # Put a reference of the object in our instance "_objects" cache to avoid creation 
            # of duplicative Python NORM Object during event notification
            obj = self._instance._objects[result] = Object(result)
            return obj
    
    def requeueObject(self, normObject):
        libnorm.NormRequeueObject(self, normObject)

    def sendCommand(self, cmdBuffer:bytes, robust:bool=False) -> bool:
        return libnorm.NormSendCommand(self, cmdBuffer, len(cmdBuffer), robust)

    def cancelCommand(self):
        libnorm.NormCancelCommand(self)

    def setWatermark(self, normObject:Object, overrideFlush=False) -> bool:
        return libnorm.NormSetWatermark(self, normObject, overrideFlush)

    def resetWatermark(self) -> bool:
        return libnorm.NormResetWatermark(self)

    def cancelWatermark(self) -> None:
        libnorm.NormCancelWatermark(self)

    def addAckingNode(self, nodeId:int) -> bool:
        return libnorm.NormAddAckingNode(self, nodeId)

    def removeAckingNode(self, nodeId:int) -> None:
        libnorm.NormRemoveAckingNode(self, nodeId)
        
    def setAutoAckingNodes(self, trackingStatus:c.TrackingStatus) -> None:
        libnorm.NormSetAutoAckingNodes(self, trackingStatus.value)
        
    def getNextAckingNode(self, nodeID:int=c.NORM_NODE_NONE) -> (bool, int,int):
        '''
            bool NormGetNextAckingNode(NormSessionHandle    sessionHandle,
                           NormNodeId*          nodeId,   
                           NormAckingStatus*    ackingStatus DEFAULT(0));
        '''
        ackingStatus = 0
        nodeIdBytes = ctypes.c_uint(ctypes.sizeof(nodeId))
        ackingStatusBytes = ctypes.c_uint(ctypes.sizeof(ackingStatus))
              
        isSuccess = libnorm.NormGetNextAckingNode(self, ctypes.byref(nodeIdBytes),ctypes.byref(ackingStatusBytes)  )
        return (isSuccess, nodeIdBytes.value, c.AckingStatus(ackingStatusBytes.value) if isSuccess else c.AckingStatus.INVALID)

    def getAckingStatus(self, nodeId:int=c.NORM_NODE_ANY) -> c.AckingStatus:
        return c.AckingStatus( libnorm.NormGetAckingStatus(self, nodeId) )

    ## Receiver functions
    def startReceiver(self, bufferSpace:int) -> bool:
        '''
        The bufferSpace parameter is used to set a limit on the amount of bufferSpace allocated by the receiver per active NormSender within the session.
        The appropriate bufferSpace to use is a function of expected network delay*bandwidth product and packet loss characteristics. 
        '''
        return libnorm.NormStartReceiver(self, bufferSpace)

    def stopReceiver(self) -> None:
        """This will be called automatically if the receiver is active"""
        libnorm.NormStopReceiver(self)

    def setRxCacheLimit(self, count:int):
        libnorm.NormSetRxCacheLimit(self, count)
    def setRxSocketBuffer(self, size:int):
        libnorm.NormSetRxSocketBuffer(self, size)

    def setSilentReceiver(self, silent:bool, maxDelay:Optional[int]=None):
        if maxDelay == None:
            maxDelay = -1
        libnorm.NormSetSilentReceiver(self, silent, maxDelay)

    def setDefaultUnicastNack(self, enable:bool):
        libnorm.NormSetDefaultUnicastNack(self, enable)

    def setDefaultSyncPolicy(self, policy:c.SyncPolicy):
        libnorm.NormSetDefaultSyncPolicy(self, policy.value)

    def setDefaultNackingMode(self, mode:c.NackingMode):
        libnorm.NormSetDefaultNackingMode(self, mode.value)

    def setDefaultRepairBoundary(self, boundary:c.RepairBoundary):
        libnorm.NormSetDefaultRepairBoundary(self, boundary.value)

    def setDefaultRxRobustFactor(self, rxRobustFactor:int):
        libnorm.NormSetDefaultRxRobustFactor(rxRobustFactor)
    def setMessageTrace(self, state):
        libnorm.NormSetMessageTrace(self, state)

    ## Properties
    nodeId:int = property(getNodeId)
    grtt:float = property(getGrttEstimate, setGrttEstimate)
    userData:str = property(getUserData, setUserData)
    reportInterval:int = property(getReportInterval, setReportInterval)

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
        def cmp(a, b):
            return (a > b) - (a < b)        
        try:
            return cmp(self._as_parameter_, other._as_parameter)
        except AttributeError:
            return cmp(self._as_parameter_, other)

    def __hash__(self):
        return self._as_parameter_
