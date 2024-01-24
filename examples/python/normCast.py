'''

LD_LIBRARY_PATH=/home/honglei/norm_test/

'''
# sender/Win10: 
#--id 34252 --send "C:\Users\Admin\AppData\Roaming\Wing Pro 9" --repeat 1 --addr 224.1.2.4/6003 --txaddr 10.65.39.191/8002 --cc rate 5000 --ack  auto --grttprobing active --debug 4
# receiver/Debian10 
#--id 172042016 --recv "~/recvFiles" --addr 224.1.2.4/6003  --interface ens33  --unicast_nack --debug 4
import sys
import os
if os.name =='nt':
    os.add_dll_directory(os.path.dirname(__file__))
import argparse
import traceback
import logging
from typing import Optional

import enum

class CCEnum(enum.Enum):
    CC = "cc"
    CCE = "cce"
    CCL = "ccl"
    Fixed = "rate"

class FlushEnum(enum.Enum):
    none='none'
    passive='passive'
    active='active'

class GrttEnum(enum.Enum):
    none='none'
    passive='passive'
    active='active'  


class EnumAction(argparse.Action):
    """
    Argparse action for handling Enums
    from https://stackoverflow.com/questions/43968006/support-for-enum-arguments-in-argparse
    """
    def __init__(self, **kwargs):
        # Pop off the type value
        enum_type = kwargs.pop("type", None)

        # Ensure an Enum subclass is provided
        if enum_type is None:
            raise ValueError("type must be assigned an Enum when using EnumAction")
        if not issubclass(enum_type, enum.Enum):
            raise TypeError("type must be an Enum when using EnumAction")

        # Generate choices from the Enum
        #kwargs.setdefault("choices", tuple(e.value for e in enum_type)) #if e!=CCEnum.rate else "rate <bitsPerSecond>" 

        super(EnumAction, self).__init__(**kwargs)

        self._enum = enum_type

    def __call__(self, parser, namespace, values:list|str, option_string=None):
        # Convert value back into an Enum
        if isinstance(values,list):
            if len(values) ==2:
                value = int(values[1])
            elif len(values) ==1:
                value = self._enum( values[0].lower())
        else:
            value = self._enum(values.lower() )
        setattr(namespace, self.dest, value)

'''
    norm.cpp: 

    fprintf(stderr, "Usage: normCast {send <file/dir list> &| recv <rxCacheDir>} [silent {on|off}]\n"
                    "                [repeat <interval> [updatesOnly]] [id <nodeIdInteger>]\n"
                    "                [addr <addr>[/<port>]][txaddr <addr>[/<port>]][txport <port>]\n"
                    "                [interface <name>][reuse][loopback]\n"
                    "                [ack auto|<node1>[,<node2>,...]] [segment <bytes>]\n"
                    "                [block <count>] [parity <count>] [auto <count>]\n"
                    "                [cc|cce|ccl|rate <bitsPerSecond>] [rxloss <lossFraction>]\n"
                    "                [txloss <lossFraction>] [flush {none|passive|active}]\n"
                    "                [grttprobing {none|passive|active}] [grtt <secs>]\n"
                    "                [ptos <value>] [processor <processorCmdLine>] [saveaborts]\n"
                    "                [sentprocessor <processorCmdLine>]\n"
                    "                [purgeprocessor <processorCmdLine>] [buffer <bytes>]\n"
                    "                [txsockbuffer <bytes>] [rxsockbuffer <bytes>]\n"
                    "                [debug <level>] [trace] [log <logfile>]\n");

'''
# Instantiate the parser

def get_arg_options():
    parser = argparse.ArgumentParser(description='Optional app description')

    ##    Required positional argument
    parser.add_argument('--id', type=int, required=True,
                        help='id <nodeIdInteger>')

    parser.add_argument('--send', '-S',nargs='+',
                        help='send <file/dir list>')

    parser.add_argument('--recv', '-R',
                        help='recv <rxCacheDir>')

    parser.add_argument('--repeat','-r', type=int, default=60,
                        help='[repeat <interval> [updatesOnly]]')    

    parser.add_argument('--interface', '-i',
                        help='addr <addr>[/<port>]')

    parser.add_argument('--addr', '-a',
                        help='addr <addr>[/<port>]')

    parser.add_argument('--txaddr', '-t',
                        help='txaddr <addr>[/<port>]')
    parser.add_argument('--txport', type=int, 
                        help='txport <port>')

    parser.add_argument('--ack', nargs='+', #nargs='?',
                        help='ack auto|<node1>[,<node2>,...')


    #To create an option that needs no value, set the action [docs] of it to 'store_const', 'store_true' or 'store_false'
    parser.add_argument('--loopback', action='store_true',help='')
    parser.add_argument('--segment', type=int,
                        help='segment <bytes>')
    parser.add_argument('--block', type=int,
                        help='[block <count>] ')
    parser.add_argument('--parity', type=int,
                        help='[parity <count>]')
    parser.add_argument('--auto', type=int,
                        help='[auto <count>]')


    parser.add_argument('--grttprobing', type=GrttEnum,  action=EnumAction,
                        help='[grttprobing {none|passive|active}]') 
    parser.add_argument('--grtt', type=int, help='[grtt <secs>]')    

    parser.add_argument('--ptos', type=int,
                        help='[ptos <value>]')

    parser.add_argument('--flush', type=FlushEnum, action=EnumAction,
                        help='[flush {none|passive|active}]')

    parser.add_argument('--silent', action='store_true',help='')
    parser.add_argument('--unicast_nack', action='store_true', help='')

    parser.add_argument('--txloss', type=float, help='[txloss <lossFraction>]')
    parser.add_argument('--rxloss', type=float, help='[rxloss <lossFraction>]')

    parser.add_argument('--buffer', type=int, help='[buffer <bytes>]')
    parser.add_argument('--txsockbuffer', type=int, help=' [txsockbuffer <bytes>]')
    parser.add_argument('--rxsockbuffer', type=int, help='[rxsockbuffer <bytes>]')
    parser.add_argument('--debug', type=int, help='[debug <level>]')         
    parser.add_argument('--cc',type=CCEnum, nargs='+', action=EnumAction,
                        help='[cc|cce|ccl|rate <bitsPerSecond>] '
                        )

    args = parser.parse_args()
    return args

import pynorm

def create_session(instance:pynorm.Instance, opts:argparse.Namespace):
    '''
       use opts to create a NORM session
    '''
    sessionAddr ='224.1.2.3'
    sessionPort = 6003

    if opts.addr:
        args = opts.addr.split('/')
        if len(args) >0:
            sessionAddr = args[0]
        if len(args) ==2:
            sessionPort = int(args[1])
    session = instance.createSession(sessionAddr, sessionPort, localId=opts.id)


    sessionTxAddr:str = None
    sessionTxPort = 8002
    if opts.txaddr:
        args = opts.txaddr.split('/')
        if len(args) >0:
            sessionTxAddr = args[0]
        if len(args) ==2:
            sessionTxPort = int(args[1])

    if sessionTxAddr:        
        session.setTxPort(txPort=sessionTxPort, txBindAddr=sessionTxAddr)

    if opts.interface:
        session.setMulticastInterface(opts.interface)    
    if opts.loopback:
        session.setLoopback(loopbackEnable=True)

    if opts.silent:
        session.setSilentReceiver(True)

    if opts.unicast_nack:
        session.setDefaultUnicastNack(enable=True)

    autoAck:bool = False  
    ackingNodeList:list[int] =[]
    if opts.ack:
        if 'auto' == opts.ack[0]:
            autoAck = True
            session.setAutoAckingNodes(pynorm.TrackingStatus.RECEIVERS)
        else:
            ackingNodeList = [ int(i) for i in opts.ack]

            for ackingNondeID in ackingNodeList:
                session.addAckingNode(ackingNondeID)

    if opts.txloss and opts.txloss>0:
        session.setTxLoss(opts.txloss)

    if opts.rxloss and opts.rxloss>0:
        session.setRxLoss(opts.rxloss)    

    # Congestion Control
    if opts.cc:
        if isinstance(opts.cc,int):
            session.setTxRate(opts.cc)
            session.setEcnSupport(ecnEnable=False)
        else:
            if opts.cc == CCEnum.CC:  # default TCP-friendly congestion control
                session.setEcnSupport(ecnEnable=False)
            elif opts.cc == CCEnum.CCE: #"wireless-ready" ECN-only congestion control
                session.setEcnSupport(ecnEnable=True, ignoreLoss=True)
            elif opts.cc == CCEnum.CCL: # "loss tolerant", non-ECN congestion control
                session.setEcnSupport(ecnEnable=False, ignoreLoss=False, tolerateLoss=True)            

    defaultBufferSpace = 64*1024*1024
    defaultTxSocketBufferSize = 4*1024*1024
    defaultRxSocketBufferSize = 6*1024*1024

    bufferSpace = opts.buffer if opts.buffer else defaultBufferSpace
    if opts.send:
        if opts.ack:
            session.setFlowControl(flowControlFactor=0) #// ack-based flow control enabled on command-line, so disable timer-based flow control
        session.setBackoffFactor(0)
        #FEC
        session.startSender(sessionId=opts.id, 
                            bufferSpace=bufferSpace, 
                            segmentSize=opts.segment if opts.segment else 1400, 
                            blockSize=opts.block if opts.block else 64, 
                            numParity=opts.parity if opts.parity else 0,
                            fecId=0)

        if opts.auto:
            session.setAutoParity(opts.auto)
        session.setTxSocketBuffer(opts.txsockbuffer if opts.txsockbuffer else defaultTxSocketBufferSize )

    if opts.recv:
        session.startReceiver(bufferSpace=bufferSpace)
        session.setRxSocketBuffer(opts.rxsockbuffer if opts.rxsockbuffer else defaultRxSocketBufferSize )

    if opts.grttprobing:
        session.setGrttProbingMode(pynorm.ProbingMode[ opts.grttprobing.value.upper() ]  )    

    return session

from pynorm import EventType   

from typing import Iterable
def listFiles( dirFileList:list[str]) -> Iterable[str]:
    for dirFile in dirFileList:
        if os.path.isfile(dirFile):
            yield dirFile
        elif os.path.isdir(dirFile):
            for filename in os.scandir(dirFile):
                if filename.is_file():
                    yield filename.path       
        else:
            raise FileExistsError(f"{dirFile} is not a valid file/dir")

import logging
import ipaddress

class NormCaster():
    def __init__(self,instance:pynorm.Instance, opts:argparse.Namespace ):
        self.opts:argparse.Namespace  = opts
        self.recvDir:Optional[str]  = os.path.expanduser(opts.recv) if opts.recv else None

        self.instance:pynorm.Instance = instance

        self.session:pynorm.Session = create_session(instance, opts)
        self.fileIterator:Iterable[str] = listFiles(opts.send)
        self.is_running:bool = True
        self.pendingSendFilePath:Optional[str] = None #  

    def addOneFile(self,session):
        '''

        '''
        if self.pendingSendFilePath is None:
            try:
                self.pendingSendFilePath:str = self.fileIterator.__next__()
            except StopIteration:
                self.is_running = False
                return 
        
        lastSlash = self.pendingSendFilePath.rfind('/')
        filename = self.pendingSendFilePath[lastSlash+1:]
        obj:Optional[pynorm.Object] = session.fileEnqueue(self.pendingSendFilePath, info= filename.encode() )
        if obj:
            if self.opts.ack:
                session.setWatermark(obj,True)
            logging.info(f"add file:{self.pendingSendFilePath}  {obj._object}")
            self.pendingSendFilePath = None # succeed enqued
        else:
            logging.warning(f"fileEnqueue: {self.pendingSendFilePath} failure!")

    def handle_norm_event(self, event:pynorm.Event):
        session:pynorm.Session = event.session
        
        evtType:pynorm.EventType = event.type
        logging.info(evtType )
        if evtType in (EventType.TX_QUEUE_EMPTY, EventType.TX_QUEUE_VACANCY):
            self.addOneFile(session)
        elif evtType == EventType.GRTT_UPDATED:
            pass
        elif evtType  == EventType.TX_WATERMARK_COMPLETED:
            if pynorm.AckingStatus.SUCCESS == session.getAckingStatus():
                logging.warning( "normCast: NORM_TX_WATERMARK_COMPLETED, NORM_ACK_SUCCESS");
                self.addOneFile(session)
            else:
                logging.warning( "normCast: NORM_TX_WATERMARK_COMPLETED, _NOT_ NORM_ACK_SUCCESS");
            obj = event.object
            if obj is None:
                session.resetWatermark()
        elif evtType  == EventType.TX_FLUSH_COMPLETED:
            pass
        elif evtType  == EventType.TX_OBJECT_PURGED:
            obj = event.object
            if obj and obj.type== pynorm.ObjectType.FILE:
                logging.info(f"normCast: send file purged: {obj.info.decode()}")
        elif evtType  == EventType.TX_OBJECT_SENT:
            obj = event.object
            if obj and obj.type== pynorm.ObjectType.FILE:
                logging.info(f"initial send complete: {obj.info.decode()}")
                self.addOneFile(session)
        elif evtType  == EventType.ACKING_NODE_NEW:

            sender = event.sender
            logging.info(f"normCast: new acking node: {sender.id} IP address{sender.address}")
        elif evtType  == EventType.REMOTE_SENDER_INACTIVE:
            pass
        elif evtType == EventType.RX_OBJECT_ABORTED:
            obj = event.object
            if obj and obj.type is not pynorm.ObjectType.FILE:
                logging.error("normCast: received invalid object type?!")
                return
            filePath = event.object.filename
            event.object.cancel()
            #remove temparary file if recv aborted.
            os.remove(filePath) 
            logging.info("")

        elif evtType == EventType.RX_OBJECT_INFO:
            pass
        elif evtType == EventType.RX_OBJECT_COMPLETED:
            obj = event.object
            if obj and obj.type is not pynorm.ObjectType.FILE:
                logging.error("normCast: received invalid object type?!")
                return 
            file_path = event.object.info.decode()
            fileName = os.path.split(file_path)[-1]
            path = os.path.join( self.recvDir,  fileName )
            oldPath = event.object.filename

            logging.debug (f"{oldPath=}")
            try:
                if os.path.isfile(path):
                    os.remove(path)
                os.rename(src=oldPath, dst=path)    
            except Exception as e:
                logging.error ( traceback.format_exc() )           


    def getAckNodesStatus(self,session):
        isSuccess, nodeID, nodeAckStatus = session.getNextAckingNode()
        while isSuccess:
            ip=ipaddress.IPv4Address(nodeID)
            if pynorm.AckingStatus.SUCCESS == nodeAckStatus:
                logging.info( f"normCast: node {nodeID} (IP address: {str(ip)}) acnkowledged.")
            else:
                logging.info( f"normCast: node {nodeID} (IP address: {str(ip)}) failed to acnkowledge.")    
            isSuccess, nodeID, nodeAckStatus = session.getNextAckingNode()    

    def run(self):
        while self.is_running:
            event: Optional[pynorm.Event,bool,None] = self.instance.getNextEvent(timeout=self.opts.repeat)
            while event:
                self.handle_norm_event(event) 
                event = self.instance.getNextEvent(timeout=self.opts.repeat)
            if event is None and self.opts.send:
                self.addOneFile(self.session) # means timeout 
        logging.warning("normCast exits!")         


def main(argv):
    opts:argparse.Namespace = get_arg_options()
    instance = pynorm.Instance()
    if opts.debug:
        instance.setDebugLevel(level=pynorm.DebugLevel(opts.debug) )
    if opts.recv:
        recvPath:str = os.path.expanduser( opts.recv  ) 
        if not os.path.isdir(recvPath): #create  is not exists!
            os.makedirs( recvPath )
        instance.setCacheDirectory( recvPath )

    normCast:NormCaster = NormCaster(instance, opts)
    if opts.send:
        normCast.addOneFile(normCast.session)

    normCast.run()




if __name__ == '__main__':
    logging.getLogger().setLevel(logging.INFO)
    sys.exit(main(sys.argv))
