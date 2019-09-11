
from threading import Thread, Lock
import sys
import random
import pynorm
import signal
import time
from collections import deque

MSG_HDR_SIZE = 2

class InputThread(Thread):
    """This thread reads 'messages" from STDIN and send them"""
    
    def __init__(self, parent, *args, **kwargs):
        super(InputThread, self).__init__(*args, **kwargs)
        self.setDaemon(True)  ;# this is "child" daemon thread
        self.msgr = parent
        
    def run(self):
        while True:
            try:
                msgHdr = bytearray(sys.stdin.read(MSG_HDR_SIZE))
            except:
                sys.stderr.write("normMsgr: input thread end-of-file 1 ...\n")
                return
            try:
                msgSize = 256*int(msgHdr[0]) + int(msgHdr[1])
                msgBuffer = sys.stdin.read(msgSize - 2)
            except:
                sys.stderr.write("normMsgr: input thread end-of-file 2 ...\n")
                return
            msgr.sendMessage(msgBuffer) ;# will block if NORM not "tx ready"

class OutputThread(Thread):
    """This thread writes received 'messages" to STDOUT."""
    
    def __init__(self, parent, *args, **kwargs):
        super(OutputThread, self).__init__(*args, **kwargs)
        self.setDaemon(True)  ;# this is "child" daemon thread
        self.msgr = parent
        
    def run(self):
        while True:
            msg = msgr.getRxMsg() ;# will block if none ready
            msgLen = len(msg) + MSG_HDR_SIZE
            msgHeader = bytearray(MSG_HDR_SIZE)
            msgHeader[0] = (msgLen >> 8) & 0x00ff
            msgHeader[1] = msgLen & 0x00ff
            sys.stdout.write(msgHeader)
            sys.stdout.write(msg)
            del msg
            

class NormMsgr:
    """This class keeps state for NORM tx/rx operations"""
    
    def __init__(self):
        self.normInstance = pynorm.Instance()
        self.normSession = None
        # Sender state members
        self.normTxLock = Lock() ;# for thread-safe access to NORM tx state variables
        self.normTxReady = Lock()
        self.norm_tx_vacancy = True
        self.norm_tx_queue_count = 0
        self.norm_tx_queue_max = 2048
        self.norm_tx_watermark_pending = False
        self.norm_acking = False        
        self.tx_msg_cache = {}
        # Receiver state members
        self.normRxLock = Lock()
        self.normRxReady = Lock()
        self.normRxReady.acquire() ;# no rx messages yet
        self.output_msg_queue = deque()
        random.seed(None)  ;# seeds with current time
        
    def openNormSession(self, addr, port, nodeId):
        # Create a NormSession and set some default parameters
        self.normSession = self.normInstance.createSession(addr, port, nodeId)
        self.normSession.setRxCacheLimit(2*self.norm_tx_queue_max) ;# we let the receiver track some extra objects
        self.normSession.setDefaultSyncPolicy(pynorm.NORM_SYNC_ALL);
        self.normSession.setDefaultUnicastNack(True);
        self.normSession.setTxCacheBounds(10*1024*1024, self.norm_tx_queue_max, self.norm_tx_queue_max);
        self.normSession.setCongestionControl(True, True);
        return self.normSession
        
    def addAckingNode(self, nodeId):
        self.normSession.addAckingNode(nodeId);
        self.norm_acking = True
        
    def setNormMulticastInterface(self, ifaceName):
        self.normSession.setMulticastInterface(ifaceName)
        
    def setNormCCMode(self, ccMode):
        if ccMode == "cc":
            self.normSession.setEcnSupport(False, False, False)
        elif ccMode == "cce":
            self.normSession.setEcnSupport(True, True)
        elif ccMode == "ccl":
            self.normSession.setEcnSupport(False, False, True)
        elif ccMode == "fixed":
            self.normSession.setEcnSupport(False, False, False)
        else:
            raise Exception("normMsgr: invalid ccMode \"%s\"" % ccMode)
        if ccMode != "fixed":
            self.normSession.setCongestionControl(True)
        else:
            self.normSession.setCongetstionControl(False)
            
    def setNormTxRate(self, bitsPerSecond):
        self.normSession.setTxRate(bitsPerSecond)
        
    def setNormDebugLevel(self, level):
        self.normInstance.setDebugLevel(level)  
        
    def setNormMessageTrace(self, state):
        self.normSession.setMessageTrace(state)
    
    def start(self, send, recv):
        if (recv):
            self.normSession.startReceiver(10*1024*1024)
        if (send):
            if (self.norm_acking):
                self.normSession.setFlowControl(0.0)
            # We use a random sender instanceId in case of stop/restart
            instanceId = random.randint(0, 0xffff)
            self.normSession.startSender(instanceId, 10*1024*1024, 1400, 16, 4);
    
    def stop(self):
        del self.normInstance
        self.normInstance = None
        
    def sendMessage(self, msgBuf):
        # caller will be blocked if NORM is (or becomes) not "tx ready"
        while not self.enqueueMessageObject(msgBuf):
            #sys.stderr.write("enqueue message was blocked\n")
            continue
            
    def enqueueMessageObject(self, msgBuf):
        self.normTxReady.acquire() ;# this blocks until NORM is "tx ready"
        with self.normTxLock:
            #sys.stderr.write("normMsgr: sending %d byte message payload ...\n" % len(msgBuf))
            obj = self.normSession.dataEnqueue(msgBuf)
            if obj is None:
                self.norm_tx_vacancy = False ;# will be cleared by NORM_TX_QUEUE_EMPTY, etc
                return False
            # cache the sent msgBuf until NORM_TX_OBJECT_PURGED
            self.tx_msg_cache[obj] = msgBuf    
            if (self.norm_acking):
                # Manage ack-based flow control state
                self.norm_tx_queue_count += 1
                if not self.norm_tx_watermark_pending:
                    if self.norm_tx_queue_count >= self.norm_tx_queue_max/2:
                        #sys.stderr.write("setting watermark ...\n")
                        self.normSession.setWatermark(obj)
                        #sys.stderr.write("watermark set.\n");
                        self.norm_tx_watermark_pending = True
                if self.norm_tx_queue_count >= self.norm_tx_queue_max:
                    # Don't release "normTxReady" since cache is filled
                    # (Will be released upon NORM_TX_WATERMARK_COMPLETED)
                    return True
            self.normTxReady.release()
            return True
        
    def onNormTxObjectPurged(self, obj):
        with self.normTxLock:
            if pynorm.NORM_OBJECT_DATA == obj.getType():
                del self.tx_msg_cache[obj]
                
    def onNormTxQueueVacancy(self):
        with self.normTxLock:
            wasTxReady = self.norm_tx_vacancy 
            if wasTxReady and self.norm_acking:
                wasTxReady = self.norm_tx_queue_count < self.norm_tx_queue_max
            self.norm_tx_vacancy = True
            if self.norm_acking:
                isTxReady = self.norm_tx_queue_count < self.norm_tx_queue_max
            else:
                isTxReady = False
            if isTxReady and not wasTxReady:
                if self.normTxReady.acquire(False):
                    sys.stderr.write("normMsgr onNormTxQueueVacancy() warning: normTxReady wasn't locked?!\n")
                #sys.stderr.write("tx vacancy releasing norm tx ready ...\n");
                self.normTxReady.release()
                
    def onNormTxWatermarkCompleted(self):
        with self.normTxLock:
            wasTxReady = self.norm_tx_vacancy 
            if wasTxReady and self.norm_acking:
                wasTxReady = self.norm_tx_queue_count < self.norm_tx_queue_max
            self.norm_tx_watermark_pending = False
            self.norm_tx_queue_count -= self.norm_tx_queue_max / 2
            isTxReady = self.norm_tx_vacancy
            if isTxReady and self.norm_acking:
                isTxReady = self.norm_tx_queue_count < self.norm_tx_queue_max
            else:
                isTxReady = False
            if isTxReady and not wasTxReady:
                if self.normTxReady.acquire(False):
                    sys.stderr.write("normMsgr onNormTxWatermarkCompleted() warning: normTxReady wasn't locked?!\n")
                #sys.stderr.write("watermark completion releasing norm tx ready ...\n")
                self.normTxReady.release()
   
    def onNormRxObjectCompleted(self, obj):
        with self.normRxLock:
            if pynorm.NORM_OBJECT_DATA == obj.getType():
                if 0 != len(self.output_msg_queue):
                    wasEmpty = False
                else:
                    wasEmpty = True
                msg = obj.getData()
                self.output_msg_queue.append(msg)
                if wasEmpty:
                    #sys.stderr.write("releasing normRxReady ...\n")
                    self.normRxReady.release() ;# unblocks waiting OutputThread
                    
    def getRxMsg(self):
        self.normRxReady.acquire() ;# blocks if output_msg_queue is empty
        with self.normRxLock:
            msg = self.output_msg_queue.popleft()
            if 0 != len(self.output_msg_queue):
                self.normRxReady.release() ;# not empty yet
            return msg
                
    def getNextNormEvent(self):
        if self.normInstance is None:
            return None
        else:
            return self.normInstance.getNextEvent()

class NormEventHandler(Thread):
    """This thread calls normInstance.getNextEvent() and handles the events"""
    
    def __init__(self, parentMsgr, *args, **kwargs):
        super(NormEventHandler, self).__init__(*args, **kwargs)
        self.setDaemon(True)  ;# this is "child" daemon thread
        self.lock = Lock()
        self.msgr = parentMsgr
        
    def run(self):
        self.lock.acquire()
        while True:
            try:
                event = self.msgr.getNextNormEvent()
            except:
                sys.stderr.write("get next event exception\n");
                self.lock.release()
                return
            if event is None:
                break
            if pynorm.NORM_EVENT_INVALID == event.type:
                continue
            elif pynorm.NORM_TX_QUEUE_EMPTY == event.type or pynorm.NORM_TX_QUEUE_VACANCY == event.type:
                msgr.onNormTxQueueVacancy()
            elif pynorm.NORM_TX_WATERMARK_COMPLETED == event.type:
                if pynorm.NORM_ACK_SUCCESS == event.session.getAckingStatus():
                    # All receivers acknowledged
                    msgr.onNormTxWatermarkCompleted()
                else:
                    # TBD - we could see who didn't ACK and possibly remove them
                    #       from our acking list.  For now, we are infinitely
                    #       persistent by resetting watermark ack request
                    event.session.resetWatermark()
            elif pynorm.NORM_TX_OBJECT_PURGED == event.type:
                msgr.onNormTxObjectPurged(event.object)
            elif pynorm.NORM_RX_OBJECT_COMPLETED == event.type:
                msgr.onNormRxObjectCompleted(event.object)
            #else:
            #    sys.stderr.write("normMsgr: NormEventHandler warning: unhandled event: %s\n" % str(event))
        sys.stderr.write("normMsgr: NormEventHandler thread exiting ...\n");
        self.lock.release()
  
def usage():
    sys.stderr.write("Usage: normMsgr.py id <nodeId> {send &| recv} [addr <addr>[/<port>]][ack <node1>[,<node2>,...]\n" +
                     "                   [cc|cce|ccl|rate <bitsPerSecond>][interface <name>][debug <level>][trace]\n")
  
# Default parameters
nodeId = None
sessionAddr = "224.1.2.3"
sessionPort = 6003
send = False
recv = False
ccMode = "cc"
txRate = None
ackerList = []
debugLevel = 3
normTrace = False
mcastIface = None

# Parse command-line
cmd = None
val = None
try:
    i = 1
    while i < len(sys.argv):
        cmd = sys.argv[i]
        i += 1
        if "id" == cmd:
            val = sys.argv[i]
            nodeId = int(val)
            i += 1
        elif "addr" == cmd:
            val = sys.argv[i]
            i += 1
            if "/" in val:
                field = val.split('/')
                sessionAddr = field[0]
                sessionPort = int(field[1])
            else:
                sessionAddr = val
        elif "send" == cmd:
            send = True
        elif "recv" == cmd:
            recv = True
        elif "cc" == cmd:
            ccMode = "cc"
        elif "cce" == cmd:
            ccMode = "cce"
        elif "ccl" == cmd:
            ccMode = "ccl"
        elif "rate" == cmd:
            val = sys.argv[i]
            rxRate = float(val)
            ccMode = "fixed"
            i += 1
        elif "ack" == cmd:
            alist = sys.argv[i].split(',')
            for val in alist:
                ackerList.append(int(val))
        elif "debug" == cmd:
            val = sys.argv[i]
            debugLevel = int(val)
            i += 1
        elif "trace" == cmd:
            normTrace = True
        else:
            sys.stderr.write("normMsgr error: invalid command \"%s\"\n" % cmd)
except Exception as e:
    sys.stderr.write("normMsgr \"" + cmd + " " + val + "\" argument error: " + e.__str__() + "\n")
    usage()
    sys.exit(-1)        
        
if not send and not recv:
    sys.stderr.write("normMsgr error: not configured to send or recv!\n")
    usage()
    sys.exit(-1)
    
if nodeId is None:
    sys.stderr.write("normMsgr error: no local 'id' provided!\n")
    usage()
    sys.exit(-1)
    
# Instantiate a NormMsgr and set its parameters
msgr = NormMsgr()
msgr.setNormDebugLevel(debugLevel)
sys.stderr.write("normMsgr: opening norm session ...\n")
msgr.openNormSession(sessionAddr, sessionPort, nodeId)
if mcastIface:
    msgr.setNormMulticastInterface(mcastIface)
for node in ackerList:
    msgr.addAckingNode(node)
msgr.setNormCCMode(ccMode);
if "fixed" == ccMode:
    msgr.setNormTxRate(txRate)
msgr.setNormMessageTrace(normTrace)
msgr.start(send, recv)

sys.stderr.write("normMsgr: starting NormEventHandler ...\n")
normEventHandler = NormEventHandler(msgr)
normEventHandler.start()

if send:
    sys.stderr.write("normMsgr: starting input thread ...\n")
    inputThread = InputThread(msgr)
    inputThread.start()

if recv:
    sys.stderr.write("normMsgr: starting output thread ...\n")
    outputThread = OutputThread(msgr)
    outputThread.start()

# The main thread just sits on a loop that sleeps and wakes up
# once in a while.  We could have made any of the other threads
# the main loop if we had wanted.  Since all the other threads
# were child "daemons", they will get killed when this main exits
# TBD - provide for a graceful/clean sender/receiver termination
try:
    sys.stderr.write("normMsgr: running (use Crtl-C to exit) ...\n")
    while True:
        time.sleep(5)
        #sys.stderr.write("woke up ...\n")
except KeyboardInterrupt:
    #sys.stderr.write("exception while waiting on input thread ..\n");
    pass
    
sys.stderr.write("normMsgr: Done.\n")
