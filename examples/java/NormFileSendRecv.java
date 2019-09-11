import mil.navy.nrl.norm.*;
import mil.navy.nrl.norm.enums.NormEventType;
import mil.navy.nrl.norm.enums.NormObjectType;

/**
 * The ... class ...
 * <p/>
 * Created by scmijt
 * Date: Feb 14, 2010
 * Time: 11:10:50 AM
 */
public class NormFileSendRecv extends Thread {
    NormInstance instance;
    NormSession session;
    String fileToSend;

    public NormFileSendRecv(String cacheDirectory, String fileToSend) throws Throwable {
        instance = new NormInstance();
        instance.setCacheDirectory(cacheDirectory);

        session = instance.createSession("224.1.2.3", 6003,
                NormNode.NORM_NODE_ANY);
        session.setLoopback(true);

        this.fileToSend=fileToSend;

        start();

        sendFile();
        
        session.destroySession();
        instance.destroyInstance();

    }

    public void run() {
        try {
            receive();
        } catch (Throwable throwable) {
            throwable.printStackTrace();  //To change body of catch statement use File | Settings | File Templates.
        }
    }

    public void receive() throws Throwable {
        session.startReceiver(1024 * 1024);
        NormEvent event;
        while ((event = instance.getNextEvent()) != null) {
            NormEventType eventType = event.getType();
            NormObject normObject = event.getObject();

            System.out.println("NORM: Received something !!!!");

            System.out.println(eventType);

            switch (eventType) {
                case NORM_RX_OBJECT_INFO:
                    byte[] info = normObject.getInfo();
                    String infoStr = new String(info, "US-ASCII");
                    System.out.println("Info: " + infoStr);
                    break;

                case NORM_RX_OBJECT_COMPLETED:
                    if (normObject.getType() == NormObjectType.NORM_OBJECT_FILE) {
                        NormFile normFile = (NormFile)normObject;
                        String filename = normFile.getName();

                        System.out.println("NormFileObject: " + filename);
                    }
                    break;
            }
        }

        session.stopReceiver();

    }

    public void sendFile()  throws Throwable {
        session.startSender(1, 1024 * 1024, 1400, (short)64, (short)16);

        // Enqueue some data
      //  byte buffer[] = "Hello to the other norm node!!!!!!".getBytes();
      //  session.dataEnqueue(buffer, 0, buffer.length);

        // Enqueue a file
        session.fileEnqueue(fileToSend);

        NormEvent event;
        while ((event = instance.getNextEvent()) != null) {
            NormEventType eventType = event.getType();
            System.out.println(eventType);
        }

        session.stopSender();
    }

    public static void main(String[] args) throws Throwable {
        if (args.length != 2) {
            System.err.println("Usage: NormFileSendRecv <rxCachePath> <sendFile>");
            return;
        }
        new NormFileSendRecv(args[0], args[1]);
    }
}
