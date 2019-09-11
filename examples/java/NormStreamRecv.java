import java.io.IOException;
import java.net.InetAddress;
import mil.navy.nrl.norm.NormEvent;
import mil.navy.nrl.norm.NormInstance;
import mil.navy.nrl.norm.NormNode;
import mil.navy.nrl.norm.NormObject;
import mil.navy.nrl.norm.NormSession;
import mil.navy.nrl.norm.NormStream;
import mil.navy.nrl.norm.enums.NormEventType;

public class NormStreamRecv {
	static final long SESSION_BUFFER_SIZE = 1024 * 1024;
	static final int SEGMENT_SIZE = 1400;
	static final int BLOCK_SIZE = 64;
	static final int PARITY_SEGMENTS = 16;
	static final String DEST_ADDRESS = "224.1.2.3";
	static final int DEST_PORT = 6003;

	public static void main(String[] args) {
		NormInstance instance = null;
		NormSession session = null;
		String destAddress = DEST_ADDRESS;
		int destPort = DEST_PORT;

		try {
			int length = 0;
			int offset = 0;
			byte[] buf = new byte[65536];
			boolean useUnicastNACKs = false;
			
			if (args.length > 0) {
				// dest addr is arg 1
				InetAddress mcastAddr = InetAddress.getByName(args[0]);
				useUnicastNACKs = ! mcastAddr.isMulticastAddress();
				destAddress = args[0];
				if (useUnicastNACKs) 
					System.err.println("Using unicast NACKs");
			}

			if (args.length > 1) {
				// port is arg 2
				destPort = Integer.parseInt(args[1]);
			}

			instance = new NormInstance();
			session = instance.createSession(destAddress, destPort,
											 NormNode.NORM_NODE_ANY);
			session.setDefaultUnicastNack(useUnicastNACKs);
			session.startReceiver(SESSION_BUFFER_SIZE);
			boolean streamIsAlive = true;
			NormEvent event;

			while ((null != (event = instance.getNextEvent())) && streamIsAlive) {
				NormEventType eventType = event.getType();
				NormObject normObject = event.getObject();

				//System.err.println(eventType);

				switch (eventType) {
				case NORM_RX_OBJECT_NEW:
					//System.err.println("New stream");
					break;

                case NORM_RX_OBJECT_UPDATED: // Stream updated = data to read ....
					//System.err.println("An update!");

                    if (normObject instanceof NormStream) {
						int numRead = 0;
                        NormStream normStreamobj;
                        normStreamobj = (NormStream)normObject;
						// Read as much as possible, writing
						// everything to System.out.
						while (0 < (numRead = normStreamobj.read(buf, 0, buf.length))) {
							if (-1 != numRead) {
								System.out.write(buf, 0, numRead);
							}
						}
                    } else {
						System.err.print("Expected NormStream.  Got ");
						System.err.println(normObject);
					}
                    break;

				case NORM_RX_OBJECT_COMPLETED:
					//System.err.println("Stream end");
					streamIsAlive = false;
					break;
				}
			}
		}
		catch (IOException ex) {
			System.err.println(ex);
		}
		catch (NumberFormatException ex) {
			System.err.println("Usage: NormStreamRecv [host-name [port]]");
			System.err.println("Default host-name: " + DEST_ADDRESS);
			System.err.println("Default port: " + DEST_PORT);
		}

		if (null != session) {
			session.stopReceiver();
			session.destroySession();
		}

		if (null != instance) {
			instance.destroyInstance();
		}
	}
}