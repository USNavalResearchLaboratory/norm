import java.nio.ByteBuffer;

import mil.navy.nrl.norm.NormEvent;
import mil.navy.nrl.norm.NormInstance;
import mil.navy.nrl.norm.NormNode;
import mil.navy.nrl.norm.NormSession;
import mil.navy.nrl.norm.enums.NormEventType;

/**
 * Example code to send a file to the NormFileRecv example.
 *
 * @author Jason Rush
 */
public class NormFileSend {
  public static void main(String[] args) throws Throwable {
    if (args.length != 1) {
      System.err.println("Usage: NormFileSend <filename>");
      return;
    }

    NormInstance instance = new NormInstance();

    NormSession session = instance.createSession("224.1.2.3", 6003,
        NormNode.NORM_NODE_ANY);
    session.startSender(1, 1024 * 1024, 1400, (short)64, (short)16);

    // Enqueue some data
    ByteBuffer byteBuffer = ByteBuffer.allocateDirect(1024);
    session.dataEnqueue(byteBuffer, 0, 1024);

    // Enqueue a file
    session.fileEnqueue(args[0]);

    NormEvent event;
    while ((event = instance.getNextEvent()) != null) {
      NormEventType eventType = event.getType();
      System.out.println(eventType);
    }

    session.stopSender();
    session.destroySession();
    instance.destroyInstance();
  }
}
