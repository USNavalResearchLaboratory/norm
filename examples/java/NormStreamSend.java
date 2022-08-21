import java.io.IOException;
import mil.navy.nrl.norm.NormEvent;
import mil.navy.nrl.norm.NormInstance;
import mil.navy.nrl.norm.NormNode;
import mil.navy.nrl.norm.NormSession;
import mil.navy.nrl.norm.NormStream;
import mil.navy.nrl.norm.enums.NormEventType;

public class NormStreamSend {
	static final long REPAIR_WINDOW_SIZE = 1024 * 1024;
	static final long SESSION_BUFFER_SIZE = 1024 * 1024;
	static final int SEGMENT_SIZE = 1400;
	static final int BLOCK_SIZE = 64;
	static final int PARITY_SEGMENTS = 16;
	static final String DEST_ADDRESS = "224.1.2.3";
	static final int DEST_PORT = 6003;

	public static void main(String[] args) {
		NormInstance instance = null;
		NormSession session = null;
		NormStream stream = null;
		String destAddress = DEST_ADDRESS;
		int destPort = DEST_PORT;

		try {
			int length = 0;
			int offset = 0;
			byte[] buf = new byte[65536];

			if (args.length > 0) {
				destAddress = args[0];
			}

			if (args.length > 1) {
				destPort = Integer.parseInt(args[1]);
			}

			instance = new NormInstance();
			session = instance.createSession(destAddress, destPort,
											 NormNode.NORM_NODE_ANY);

			String ccStr = System.getProperty("Norm.CC", "off");
			if (ccStr.equalsIgnoreCase("on")) {
				session.setCongestionControl(true, true);
				System.out.println("Set Congestion Control to " + ccStr);
			}

			session.startSender(1, SESSION_BUFFER_SIZE, 
								SEGMENT_SIZE, BLOCK_SIZE, PARITY_SEGMENTS);
			stream = session.streamOpen(REPAIR_WINDOW_SIZE);

			while (-1 != (length = System.in.read(buf, 0, buf.length))) {
				int numWritten = 0;
				offset = 0;

				while (length != (numWritten = stream.write(buf, offset, length))) {
					length -= numWritten;
					offset += numWritten;

					NormEvent event = instance.getNextEvent();
					NormEventType eventType = event.getType();

					while ((eventType != NormEventType.NORM_TX_QUEUE_EMPTY) && 
						   (eventType != NormEventType.NORM_TX_QUEUE_VACANCY)) {
						event = instance.getNextEvent();
						eventType = event.getType();
					}
				}

				stream.markEom();

				//System.err.println("Wrote " + numWritten);
				//System.err.println("... Done!");

				// TODO: Create a new buf each time I'm successful writing
				// all of it?
			}

			stream.flush();
			System.err.println("End of file!");
		}
		catch (IOException ex) {
			System.err.println(ex);
		}
		catch (NumberFormatException ex) {
			System.err.println("Usage: NormStreamSend [host-name [port]]");
			System.err.println("Default host-name: " + DEST_ADDRESS);
			System.err.println("Default port: " + DEST_PORT);
		}

		if (null != stream) {
			System.err.println("Closing stream");
			stream.close(true);
		}

		if (null != session) {
			System.err.println("Stopping sender");
			session.stopSender();
			System.err.println("Destroying session");
			session.destroySession();
		}

		if (null != instance) {
			System.err.println("Destroying instance");
			instance.destroyInstance();
		}

		System.err.println("That's all folks!");
	}
}