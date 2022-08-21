
// This is a Java implementation of the same
// NORM "messenger" approach as normMsgr.cpp

import java.util.concurrent.Semaphore;
import java.nio.ByteBuffer;
import java.util.Random;
import java.util.HashMap;
import java.util.LinkedList;

import mil.navy.nrl.norm.NormEvent;
import mil.navy.nrl.norm.NormInstance;
import mil.navy.nrl.norm.NormNode;
import mil.navy.nrl.norm.NormSession;
import mil.navy.nrl.norm.NormObject;
import mil.navy.nrl.norm.NormData;
import mil.navy.nrl.norm.enums.NormEventType;
import mil.navy.nrl.norm.enums.NormObjectType;
import mil.navy.nrl.norm.enums.NormAckingStatus;
import mil.navy.nrl.norm.enums.NormSyncPolicy;

public class NormMsgr
{
    static final int MSG_SIZE_MAX = 65536;
    static final int MSG_HDR_SIZE = 2;
    
    private static Random randGen = new Random(System.currentTimeMillis()); 
    
    // These members track NORM's "tx ready" status
    private Semaphore normTxLock;   // this acts as a mutex to manage "tx ready" status in a thread safe way
    private Semaphore normTxReady;  // this blocks data enqueuing until tx ready
    private boolean norm_tx_vacancy;
    private int norm_tx_queue_count;
    private int norm_tx_queue_max;
    private boolean norm_tx_watermark_pending;
    
    public NormInstance normInstance;
    private NormSession normSession;
    private boolean norm_acking;
    
    // This HashMap is where we cache enqueued Messages until NORM_TX_OBJECT_PURGED
    // (In the C++ version we used the NormObjectSetUserData() which isn't available in NORM Java API)
    private HashMap<NormData, Message> input_msg_list = new HashMap<NormData, Message>();
    
    // Messages received from NORM are enqueued here until
    // retrieved by the OutputWriter thread
    private MessageQueue output_msg_queue;
    private Semaphore normRxLock;  // protects inter-thread access to output_msg_queue
    private Semaphore normRxReady;  // blocks OutputWriter when output_msg_queue is empty
    
    // Porbably will build these congestion control modes into the NORM API
    public enum NormCCMode
    {
        NORM_FIXED, 
        NORM_CC, 
        NORM_CCE, 
        NORM_CCL;
    }   
    
    // Constructor
    public NormMsgr() throws java.io.IOException, InterruptedException
    {
        normTxLock = new Semaphore(1);
        normTxReady = new Semaphore(1);
        try {
            normInstance = new NormInstance();
            normInstance.setDebugLevel(3);
        }
        catch (java.io.IOException ex)  {
            System.err.println(ex);
            throw(ex);
        }
        // Default parameter values
        norm_tx_vacancy = true;
        norm_tx_queue_max = 2048;    // this is set largish, since I'm testing with _small_ messages
        norm_tx_queue_count = 0;
        norm_tx_watermark_pending = false;
        norm_acking = false;
        
        normRxLock = new Semaphore(1);
        normRxReady = new Semaphore(1);
        output_msg_queue = new MessageQueue();
        normRxReady.acquire();  // nothing received yet
    }
    
    public boolean openNormSession(String addr, short port, long nodeId)
    {
        try 
        {
            normSession = normInstance.createSession(addr, port, nodeId);
            if (null == normSession)
            {
                System.err.println("normMsgr error: unable to create NORM session");
                return false;
            }
            // Set some default parameters (maybe we should put parameter setting in Start())
            normSession.setRxCacheLimit(2*norm_tx_queue_max);  // we let the receiver track some extra objects
            normSession.setDefaultSyncPolicy(NormSyncPolicy.NORM_SYNC_ALL);
            normSession.setDefaultUnicastNack(true);
            normSession.setTxCacheBounds(10*1024*1024, norm_tx_queue_max, norm_tx_queue_max);

            normSession.setCongestionControl(true, true);
            
            //normSession.setMessageTrace(true);
        }
        catch (java.io.IOException ex) 
        {
            System.err.println(ex);
        }
        return true;
    }  // end NormMsgr::openNormSession()
    
    public void addAckingNode(long nodeId)
    {
        try {
            normSession.addAckingNode(nodeId);
        }
        catch (java.io.IOException ex) {
            System.err.println(ex);
            return;
        }
        norm_acking = true;
    }  // end NormMsgr::addAckingNode()
    
    public boolean setNormMulticastInterface(String ifaceName)
    {
        try {
            normSession.setMulticastInterface(ifaceName);
        }
        catch (java.io.IOException ex) {
            System.err.println(ex);
            return false;
        }
        return true;
    }  // end NormMsgr::setMulticastInterface()
    
    public void setNormCCMode(NormCCMode ccMode)
    {
        switch (ccMode)
        {
            case NORM_CC:  // default TCP-friendly congestion control
                normSession.setEcnSupport(false, false, false);
                break;
            case NORM_CCE: // "wireless-ready" ECN-only congestion control
                normSession.setEcnSupport(true, true);
                break;
            case NORM_CCL: // "loss tolerant", non-ECN congestion control
                normSession.setEcnSupport(false, false, true);
                break;
            case NORM_FIXED:
                normSession.setEcnSupport(false, false, false);
                break;
        }
        if (NormCCMode.NORM_FIXED != ccMode)
            normSession.setCongestionControl(true);
        else
            normSession.setCongestionControl(false);
    }  // end NormMsgr::setNormCCMode()
    
    public void setNormTxRate(double bitsPerSecond)
    {
        normSession.setTxRate(bitsPerSecond);
    }
    
    public void setNormDebugLevel(int level)
        {normInstance.setDebugLevel(level);}
    
    public void setNormMessageTrace(boolean state)
        {normSession.setMessageTrace(state);}
        
    public boolean start(boolean send, boolean recv)
    {
        // Start NORM sender and/or receiver operation
        boolean recvStarted = false;
        try 
        {
            if (recv)
            {
                normSession.startReceiver(10*1024*1024);
                recvStarted = true;
            }
            if (send)
            {
                if (norm_acking)
                {   
                    // ack-based flow control enabled on command-line, 
                    // so disable timer-based flow control
                    normSession.setFlowControl(0.0);
                }
                // Pick a random instance id for now
                int instanceId = randGen.nextInt();
                normSession.startSender(instanceId, 10*1024*1024, 1400, (short)16, (short)4);
            }
        }
        catch (java.io.IOException ex)
        {
            System.err.println(ex);
            if (recvStarted) normSession.stopReceiver();
            return false;
        }
        return true;
    }  // end NormMsgr::start()
    
    public boolean sendMessage(Message msg)
    { 
        // Future version will support NORM_OBJECT_STREAM as an option
        while (!enqueueMessageObject(msg));  // keep trying until success (we're blocked by "normTxReady" semaphore)
        return true;
    }  // end NormMsgr::sendMessage()
    
    public boolean enqueueMessageObject(Message msg)
    {
        try
        {
            normTxReady.acquire();  // caller will be blocked if NORM is not "tx ready"
            normTxLock.acquire();   // this guarantees protected access to "tx ready" state variables
        }
        catch (InterruptedException ex)
        {
            System.err.println(ex);
            if (!normTxReady.tryAcquire())
                normTxReady.release();
            return false;
        }
        NormData obj = null;
        try {
            obj = normSession.dataEnqueue(msg.getBuffer(), 0, msg.getSize());
        }
        catch (java.io.IOException ex) {
            //System.err.println(ex);
            obj = null;
        }
        if (null == obj)
        {
            // Note we don't call normTxReady.release() here, it's up to the 
            // NormEventHandler to do that upon NORM_TX_QUEUE_EMPTY (or VACANCY for streams)
            //System.err.println("NormMsgr::SendMessage() warning: data enqueue was blocked.");
            norm_tx_vacancy = false;  // there was no room at the inn
            normTxLock.release();
            return false;
        }
        // System.err.println("caching msg for object " + obj + "\n");
        // Cache the msg associated with the resultant tx object.  We use the
        // input_msg_list HashMap so we can remove the msg upon NORM_TX_OBJECT_PURGED
        input_msg_list.put(obj, msg);
        
        if (norm_acking)
        {
            norm_tx_queue_count++;
            if (!norm_tx_watermark_pending && (norm_tx_queue_count >= (norm_tx_queue_max / 2)))
            {
                try {
                    normSession.setWatermark(obj);
                    norm_tx_watermark_pending = true;
                }
                catch (java.io.IOException ex) {
                    System.err.println(ex);
                }
            }
            if (norm_tx_queue_count >= norm_tx_queue_max)
            {
                // We've filled our tx cache, so don't call normTxReady.release()
                // NormEventHandler will do this upon watermark completion
                normTxLock.release();
                return true;
            }
        }
        normTxReady.release();
        normTxLock.release();
        return true;
    }
    
    public void onNormTxObjectPurged(NormObject normObject)
    {
        try {
            normTxLock.acquire();
        }
        catch (InterruptedException ex) {
            System.err.println(ex);
            return;  // Should kill program here (or maybe we should acquire uninterruptably)?
        }
        if (NormObjectType.NORM_OBJECT_DATA == normObject.getType())
        {
            // removed "msg" will get garbage-collected
            //System.err.println("purging msg for object " + (NormData)normObject + "\n");
            Message msg = input_msg_list.remove((NormData)normObject);
            if (null == msg) // Shouldn't happen (and it doesn't after adding normTxLock.acquire())
                System.err.println("normMsgr warning: purged invalid object?!");
        }
        normTxLock.release();
    }  // end NormMsgr::onNormTxObjectPurged()
    
    // These next two methods are used by the NormEventHandler to update the 
    // NORM "tx ready" status variables in a thread-safe manner.  Release of
    // the "normTxReady" semaphore will unblock the InputThread if it was blocked
    // due filling up the NORM tx queue.
    // (The "!wasTxReady" check in these avoids any race condition with the normTxReady semaphore
    public void onNormTxQueueVacancy()
    {
        try {
            normTxLock.acquire();
        }
        catch (InterruptedException ex) {
            System.err.println(ex);
            return;  // Should kill program here (maybe we should acquire uninterruptably)
        }
        boolean wasTxReady = norm_tx_vacancy && (norm_acking ? (norm_tx_queue_count < norm_tx_queue_max) : true);
        norm_tx_vacancy = true;
        boolean isTxReady = norm_acking ? (norm_tx_queue_count < norm_tx_queue_max) : true;
        if (!wasTxReady && isTxReady)
        {
            if (normTxReady.tryAcquire())
                System.err.println("NormMsgr::setNormTxVacancy() warning:  normTxReady wasn't locked?!");
            normTxReady.release(); 
        }
        normTxLock.release();
    }  // end NormMsgr::onNormTxQueueVacancy()
    
    public void onNormTxWatermarkCompleted()
    {
        try {
            normTxLock.acquire();
        }
        catch (InterruptedException ex) {
            System.err.println(ex);
            return;  // Should kill program here (or maybe we should acquire uninterruptably)?
        }
        boolean wasTxReady = norm_tx_vacancy && (norm_acking ? (norm_tx_queue_count < norm_tx_queue_max) : true);
        norm_tx_queue_count -= (norm_tx_queue_max / 2);
        norm_tx_watermark_pending = false;
        boolean isTxReady = norm_tx_vacancy && (norm_acking ? (norm_tx_queue_count < norm_tx_queue_max) : true);
        if (!wasTxReady && isTxReady)
        {
            if (normTxReady.tryAcquire())
                System.err.println("NormMsgr::decrementNormTxQueueCount() warning:  normTxReady wasn't locked?!");
            normTxReady.release(); 
        }
        normTxLock.release();
    }  // end NormMsgr::onNormTxQueueVacancy()
    
    public void onNormRxObjectCompleted(NormObject obj)
    {
        try {
            normRxLock.acquire();
        }   
        catch (InterruptedException ex) {
            System.err.println(ex);
            return;  // Should kill program here (or maybe we should acquire uninterruptably)?
        }
        if (NormObjectType.NORM_OBJECT_DATA == obj.getType())
        {
            // It's a message, so put it in the output_msg_queue for the OutputWriter thread
            boolean wasEmpty = output_msg_queue.isEmpty();
            Message msg = new Message(((NormData)obj).getData(), (int)obj.getSize());
            output_msg_queue.append(msg);
            if (wasEmpty) normRxReady.release();  // this will unblock the waiting OutputWriter
        }
        normRxLock.release();
    }  // end NormMsgr::onNormRxObjectCompleted()
    
    // Called by OutputWriter thread to fetch received messages
    public Message getRxMsg()
    {
        try {
            normRxReady.acquire();  // will block if output_msg_queue is empty
            normRxLock.acquire();
        }
        catch (InterruptedException ex) {
            System.err.println(ex);
            return null;
        }
        Message msg = output_msg_queue.removeHead();
        if (!output_msg_queue.isEmpty())
            normRxReady.release();
        normRxLock.release();
        if (null == msg)  // shouldn't happen
            System.err.println("NormMsgr warning: output_msg_queue unexpectedly empty?!");
        return msg;
    }
    
    public void RunThreads()
	{
        InputReader inputReader = new InputReader(this);
        System.err.println("main thread starting input thread ...");
        inputReader.start();
        
        NormEventHandler normEventHandler = new NormEventHandler(this);
        System.err.println("main thread starting norm event handler thread ...");
        normEventHandler.start();
        
        OutputWriter outputWriter = new OutputWriter(this);
        System.err.println("main thread starting output thread ...");
        outputWriter.start();
        
        // For now, we put the "inputReader" thread in the driver's seat.
        // In the future, we'll be acquiring a semaphore shared among the
        // threads and determine what thread(s) are signaling the parent
        // and their status(es) 
        System.err.println("main thread waiting on input thread ...");
        inputReader.acquireLock(); // when acquired, indicates thread is done
        
        try {
            inputReader.join();
            normInstance.stopInstance();
            normInstance.destroyInstance();
            normEventHandler.join();
        }
        catch (InterruptedException ex) {
            System.err.println(ex);
        }
        System.err.println("main thread exiting ...");
    }
    
    public class Message
    {
        private ByteBuffer msgBuffer;
        
        // Constructors
        public Message(int size)
        {
            msgBuffer = ByteBuffer.allocateDirect(size);
        }
        public Message(byte[] buffer, int size)
        {
            msgBuffer = ByteBuffer.wrap(buffer, 0, size);
        }
        
        public int getSize()
            {return msgBuffer.capacity();}
        public ByteBuffer getBuffer()
            {return msgBuffer;}
            
    }  // end class NormMsgr::Message
    
    public class MessageQueue extends LinkedList<Message>
    {
        public void append(Message msg)
            {addLast(msg);}
        public void prepend(Message msg)
            {addFirst(msg);}
        public Message removeHead()
            {return removeFirst();}
        public Message removeTail()
            {return removeLast();}
        public boolean isEmpty()
        {
            if (null == peekFirst())
                return true;
            else 
                return false;
        }
    }  // end class NormMsgr::MessageQueue
    
    private class InputReader extends Thread
	{
        private NormMsgr    parentMsgr;
        private boolean     inputDone;
        private Semaphore   threadLock;
        
        public InputReader(NormMsgr parent)
        {
            parentMsgr = parent;
            inputDone = false;
            threadLock = new Semaphore(1);
        }
        
        public boolean acquireLock()
        {
            try
            {
                threadLock.acquire();
                return true;
            }
            catch (InterruptedException ex)
            {
                System.err.println(ex);
                return false;
            }
        }
        
        @Override
		public void run()
        {
            System.err.println("input thread acquiring its lock ...");
            try 
            {
                threadLock.acquire();
            }
            catch (InterruptedException ex) 
            {
                System.err.println(ex);
                return;
            }
            System.err.println("input thread entering its main loop ...");
            byte[] msgHeader = new byte[MSG_HDR_SIZE];
            byte[] tmpBuffer = new byte[MSG_SIZE_MAX];
            while (!inputDone)
            {
                // Read in one message at time.
                int want = MSG_HDR_SIZE;
                int got = 0;
                while (got < want)
                {
                    try 
                    {
                        int result = System.in.read(msgHeader, got, want - got);
                        if (-1 == result)
                        {
                            System.err.println("normMsgr: input end-of-file!");
                            inputDone = true;
                            threadLock.release();
                            System.err.println("input thread exiting 1 ...");
                            return;
                        }
                        //System.err.println("input thread read " + result + " bytes of msg header\n");
                        got += result;
                    }
                    catch (java.io.IOException ex) 
                    {
			            System.err.println(ex);
		            }
                }
                int msgSize = 256*msgHeader[0] + msgHeader[1];
                //System.err.println("msg header is (256 * " + msgHeader[0] + ") + " + msgHeader[1] + " = " + msgSize);
                got = 0;
                msgSize -= 2;  // already read header
                while (got < msgSize)
                {
                    try 
                    {
                        int result = System.in.read(tmpBuffer, got, msgSize - got);
                        if (-1 == result)
                        {
                            System.err.println("normMsgr: input end-of-file!");
                            inputDone = true;
                            threadLock.release();
                            System.err.println("input thread exiting 2 ...");
                            return;
                        }
                        //System.err.println("input thread read " + result + "bytes of message data\n");
                        got += result;
                    }
                    catch (java.io.IOException ex) 
                    {
			            System.err.println(ex);
		            }
                }
                int totalSize = msgSize + 2;
                //System.err.println("normMsgr: read a " + totalSize + " byte message ...\n");
                
                Message msg = new Message(msgSize);
                msg.getBuffer().put(tmpBuffer, 0, msgSize);
                parentMsgr.sendMessage(msg);
                
            }
            System.err.println("input thread releasing its lock ...");
            threadLock.release();
            System.err.println("input thread exiting 3 ...");
            
        }  // end InputReader::run()
	}  // end class NormMsgr::InputReader
 
    
    private class NormEventHandler extends Thread
    {
        private NormMsgr parentMsgr;
        
        public NormEventHandler(NormMsgr parent)
        {
            parentMsgr = parent;
        }
        
        @Override
        public void run()
        {
            try
            {
                System.err.println("entering NormEventHandler loop ...");
                NormEvent event;
                while (null != (event = normInstance.getNextEvent()))
                {
                    //System.err.println(event);
                    NormSession session = event.getSession();
                    switch (event.getType())
                    {
                        case NORM_TX_QUEUE_EMPTY:
                        case NORM_TX_QUEUE_VACANCY:
                            // This will unblock the InputThread if it was blocked
                            parentMsgr.onNormTxQueueVacancy();
                            break;
                            
                        case NORM_TX_WATERMARK_COMPLETED:
                            if (NormAckingStatus.NORM_ACK_SUCCESS == session.getAckingStatus(NormNode.NORM_NODE_ANY))
                            {
                                // This will unblock the InputThread if it was blocked
                                //System.err.println("sender tx watermark completed ...");
                                parentMsgr.onNormTxWatermarkCompleted();
                            }
                            else
                            {
                                // TBD - we could see who didn't ACK and possibly remove them
                                //       from our acking list.  For now, we are infinitely
                                //       persistent by resetting watermark ack request
                               session.resetWatermark();
                            }
                            break;
                            
                        case NORM_TX_OBJECT_PURGED:
                            parentMsgr.onNormTxObjectPurged(event.getObject());
                            break;
                            
                        case NORM_RX_OBJECT_COMPLETED:
                            parentMsgr.onNormRxObjectCompleted(event.getObject());
                            break;
                            
                        case NORM_REMOTE_SENDER_INACTIVE:
                            // optionally free memory resources that were in
                            // use by this remote sender
                            break;
                            
                        default:
                            break;
                    }
                }
                System.err.println("NormEventHandler got null event ...");
            }
            catch (java.io.IOException ex)
            {
                System.err.println("NormGetNextEvent IOException:");
                System.err.println(ex);
            }
            System.err.println("exiting NormEventHandler ...");
        }  // end NormEventHandler::run()
    }  // end class NormMsgr::NormEventHandler
    
    private class OutputWriter extends Thread
    {
        private NormMsgr parentMsgr;
        boolean outputDone = false;
        
        // Constructors
        public OutputWriter(NormMsgr parent)
        {
            parentMsgr = parent;
        }
        
        @Override
        public void run()
        {
            while (!outputDone)
            {
                Message msg = parentMsgr.getRxMsg();  // this will block if none available
                // Make and write 2-byte length header (adding to the length to account for header, too)
                int msgSize = msg.getSize() + MSG_HDR_SIZE;
                byte[] msgHeader = new byte[MSG_HDR_SIZE];
                // This currently assumes 2-byte MSG_HDR_SIZE
                msgHeader[0] = (byte)((msgSize >> 8) & 0xff);
                msgHeader[1] = (byte)(msgSize & 0xff);
                System.out.write(msgHeader, 0, MSG_HDR_SIZE);
                // Write message content
                System.out.write(msg.getBuffer().array(), 0, msg.getSize());
            }
            System.err.println("NormMsgr output thread exiting ...");
        }
    }  // end class NormMsgr::OutputWriter()
    
    public static void usage()
    {
        System.err.println("Usage: normMsgr id <nodeId> {send &| recv} [addr <addr>[/<port>]][ack <node1>[,<node2>,...]\n" +
                           "                [cc|cce|ccl|rate <bitsPerSecond>][interface <name>][debug <level>][trace]\n");
    }
    
    public static void main(String[] args) throws Throwable 
	{
        // Default parameters
        String sessionAddr = "224.1.2.3";
        short sessionPort = 6003;
        long nodeId = 0;
        boolean send = false;
        boolean recv = false;
        String[] ackerList = null;
        String mcastIface = null;
        int debugLevel = 0;
        boolean normTrace = false;
        NormCCMode ccMode = NormCCMode.NORM_CC;
        double txRate = 1.0e+06;  // only applies for NORM_FIXED ccMode
        
        int i = 0;
        while (i < args.length)
        {   
            String cmd = args[i++];
            if (cmd.equals("id"))
            {
                nodeId = Integer.parseInt(args[i++]);
            }
            else if (cmd.equals("send"))
            {
                send = true;
            }
            else if (cmd.equals("recv"))
            {
                recv = true;
            }
            else if (cmd.equals("addr"))
            {
                String[] items = args[i++].split("/");
                sessionAddr = items[0];
                if (items.length > 1)
                    sessionPort = (short)Integer.parseInt(items[1]);
            }
            else if (cmd.equals("interface"))
            {
                mcastIface = args[i++];
            }
            else if (cmd.equals("cce"))
            {
                ccMode = NormCCMode.NORM_CCE;
            }
            else if (cmd.equals("cce"))
            {
                ccMode = NormCCMode.NORM_CCL;
            }
            else if (cmd.equals("rate"))
            {
                ccMode = NormCCMode.NORM_FIXED;
                txRate = Double.parseDouble(args[i++]);
            }
            else if (cmd.equals("ack"))
            {
               ackerList = args[i++].split(",");
            }
            else if (cmd.equals("debug"))
            {
               debugLevel = Integer.parseInt(args[i++]);
            }
            else if (cmd.equals("trace"))
            {
               normTrace = true;
            }
            else
            {
                System.err.println("normMsgr error: invalid command '" + cmd + "'");
                usage();
                return;
            }   
        }
        if (!send && !recv)
        {
            System.err.println("normMsgr error: not configured to send or recv!");
            usage();
            return;
        }
        if (0 == nodeId)
        {
            System.err.println("normMsgr error: no local 'id' provided!");
            usage();
            return;
        }
        
        NormMsgr msgr = new NormMsgr();
        
        msgr.setNormDebugLevel(debugLevel);
        
        msgr.openNormSession(sessionAddr, sessionPort, nodeId);
        
        if (null != mcastIface)
            msgr.setNormMulticastInterface(mcastIface);
        
        if (null != ackerList)
        {
            for (i = 0; i < ackerList.length; i++)
                msgr.addAckingNode(Integer.parseInt(ackerList[i]));
        }
        
        msgr.setNormCCMode(ccMode);
        if (NormCCMode.NORM_FIXED == ccMode)
            msgr.setNormTxRate(txRate);
        
        msgr.setNormMessageTrace(normTrace);
        
        msgr.start(send, recv);
        
        msgr.RunThreads();
        
        System.err.println("normMsgr: main() exiting ...");
    }
    
	
}  // end class NormMsgr
