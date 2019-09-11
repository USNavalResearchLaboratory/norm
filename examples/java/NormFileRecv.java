import mil.navy.nrl.norm.NormEvent;
import mil.navy.nrl.norm.NormFile;
import mil.navy.nrl.norm.NormInstance;
import mil.navy.nrl.norm.NormNode;
import mil.navy.nrl.norm.NormObject;
import mil.navy.nrl.norm.NormSession;
import mil.navy.nrl.norm.enums.NormEventType;
import mil.navy.nrl.norm.enums.NormObjectType;

/**
 * Example code to receive a file from the NormFileSend example.
 *
 * @author Jason Rush
 */
public class NormFileRecv {
  public static void main(String[] args) throws Throwable {
    if (args.length != 1) {
      System.err.println("Usage: NormFileRecv <rxCachePath>");
      return;
    }

    NormInstance instance = new NormInstance();
    instance.setCacheDirectory(args[0]);

    NormSession session = instance.createSession("224.1.2.3", 6003,
        NormNode.NORM_NODE_ANY);
    session.startReceiver(1024 * 1024);

    NormEvent event;
    while ((event = instance.getNextEvent()) != null) {
      NormEventType eventType = event.getType();
      NormObject normObject = event.getObject();

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
    session.destroySession();
    instance.destroyInstance();
  }
}
