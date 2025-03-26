namespace Mil.Navy.Nrl.Norm.Enums
{
    /// <summary>
    /// The type identifies the event with one of NORM protocol events.
    /// </summary>
    public enum NormEventType
    {
        /// <summary>
        /// This NormEventType indicates an invalid or "null" notification which should be ignored.
        /// </summary>
        NORM_EVENT_INVALID,
        /// <summary>
        /// This event indicates that there is room for additional transmit objects to be enqueued, or, 
        /// if the handle of NORM_OBJECT_STREAM is given in the corresponding
        /// event "object" field, the application may successfully write to the indicated stream
        /// object. Note this event is not dispatched until a call to NormFileEnqueue(),
        /// NormDataEnqueue(), or NormStreamWrite() fails because of a filled transmit
        /// cache or stream buffer.
        /// </summary>
        NORM_TX_QUEUE_VACANCY,
        /// <summary>
        /// This event indicates the NORM protocol engine has no new data pending transmission 
        /// and the application may enqueue additional objects for transmission.
        /// If the handle of a sender NORM_OBJECT_STREAM is given in the corresponding event
        /// "object" field, this indicates the stream transmit buffer has been emptied and the
        /// sender application may write to the stream (Use of NORM_TX_QUEUE_VACANCY may
        /// be preferred for this purpose since it allows the application to keep the NORM
        /// protocol engine busier sending data, resulting in higher throughput when attempting
        /// very high transfer rates).
        /// </summary>
        NORM_TX_QUEUE_EMPTY,
        /// <summary>
        /// This event indicates that the flushing process the NORM sender observes when
        /// it no longer has data ready for transmission has completed. The completion of the
        /// flushing process is a reasonable indicator (with a sufficient NORM "robust factor"
        /// value) that the receiver set no longer has any pending repair requests. Note the
        /// use of NORM's optional positive acknowledgement feature is more deterministic
        /// in this regards, but this notification is useful when there are non-acking (NACK-only) 
        /// receivers. The default NORM robust factor of 20 (20 flush messages are
        /// sent at end-of-transmission) provides a high assurance of reliable transmission,
        /// even with packet loss rates of 50%.
        /// </summary>
        NORM_TX_FLUSH_COMPLETED,
        /// <summary>
        /// This event indicates that the flushing process initiated by a prior application call
        /// to NormSetWatermark() has completed. The posting of this event indicates the 
        /// appropriate time for the application to make a call NormGetAckingStatus() to
        /// determine the results of the watermark flushing process.
        /// </summary>
        NORM_TX_WATERMARK_COMPLETED,
        /// <summary>
        /// This event indicates that an application-defined command previously enqueued
        /// with a call to NormSendCommand() has been transmitted, including any repetition.
        /// </summary>
        NORM_TX_CMD_SENT,
        /// <summary>
        /// This event indicates that the transport object referenced by the event's "object"
        /// field has completed at least one pass of total transmission. Note that this does not
        /// guarantee that reliable transmission has yet completed; only that the entire object
        /// content has been transmitted. Depending upon network behavior, several rounds
        /// of NACKing and repair transmissions may be required to complete reliable transfer.
        /// </summary>
        NORM_TX_OBJECT_SENT,
        /// <summary>
        /// This event indicates that the NORM protocol engine will no longer refer to the
        /// transport object identified by the event's "object" field. Typically, this will occur
        /// when the application has enqueued more objects than space available within the
        /// set sender  transmit  cache bounds. Posting of this notification means the application is 
        /// free to free any resources (memory, files, etc) associated with the indicated "object".  
        /// After this event, the given "object" handle (NormObjectHandle) is no longer valid unless 
        /// it is specifically retained by the application.
        /// </summary>
        NORM_TX_OBJECT_PURGED,
        /// <summary>
        /// This event indicates that NORM Congestion Control operation has adjusted the
        /// transmission rate. The NormGetTxRate() call may be used to retrieve the new
        /// corresponding transmission rate. Note that if NormSetCongestionControl() was
        /// called with its adjustRate parameter set to false, then no actual rate change has
        /// occurred and the rate value returned by NormGetTxRate() reflects a "suggested"
        /// rate and not the actual transmission rate.
        /// </summary>
        NORM_TX_RATE_CHANGED,
        /// <summary>
        /// This event is posted when the NORM protocol engine completes the "graceful
        /// shutdown" of its participation as a sender in the indicated "session".
        /// </summary>
        NORM_LOCAL_SENDER_CLOSED,
        /// <summary>
        /// This event is posted when a receiver first receives messages from a specific remote
        /// NORM sender. This marks the beginning of the interval during which the application
        /// may reference the provided "node" handle (NormNodeHandle).
        /// </summary>
        NORM_REMOTE_SENDER_NEW,
        /// <summary>
        /// Remote sender instanceId or FEC params changed.
        /// </summary>
        NORM_REMOTE_SENDER_RESET,
        /// <summary>
        /// Remote sender src addr and/or port changed.
        /// </summary>
        NORM_REMOTE_SENDER_ADDRESS,
        /// <summary>
        /// This event is posted when a previously inactive (or new) remote sender is detected
        /// operating as an active sender within the session.
        /// </summary>
        NORM_REMOTE_SENDER_ACTIVE,
        /// <summary>
        /// This event is posted after a significant period of inactivity (no sender messages
        /// received) of a specific NORM sender within the session. The NORM protocol
        /// engine frees buffering resources allocated for this sender when it becomes inactive.
        /// </summary>
        NORM_REMOTE_SENDER_INACTIVE,
        /// <summary>
        /// This event is posted when the NORM protocol engine frees resources for, and
        /// thus invalidates the indicated "node" handle.
        /// </summary>
        NORM_REMOTE_SENDER_PURGED,
        /// <summary>
        /// This event indicates that an application-defined command has been received from
        /// a remote sender. The NormEvent node element indicates the NormNodeHandle
        /// value associated with the given sender. The NormNodeGetCommand() call can be
        /// used to retrieve the received command content.
        /// </summary>
        NORM_RX_CMD_NEW,
        /// <summary>
        /// This event is posted when reception of a new transport object begins and marks
        /// the beginning of the interval during which the specified "object" (NormObjectHandle)
        /// is valid.
        /// </summary>
        NORM_RX_OBJECT_NEW,
        /// <summary>
        /// This notification is posted when the NORM_INFO content for the indicated "object" is received.
        /// </summary>
        NORM_RX_OBJECT_INFO,
        /// <summary>
        /// This event indicates that the identified receive "object" has newly received data content.
        /// </summary>
        NORM_RX_OBJECT_UPDATED,
        /// <summary>
        /// This event is posted when a receive object is completely received, including
        /// available NORM_INFO content. Unless the application specifically retains the "object"
        /// handle, the indicated NormObjectHandle becomes invalid and must no longer be
        /// referenced.
        /// </summary>
        NORM_RX_OBJECT_COMPLETED,
        /// <summary>
        /// This notification is posted when a pending receive object's transmission is aborted
        /// by the remote sender. Unless the application specifically retains the "object"
        /// handle, the indicated NormObjectHandle becomes invalid and must no longer be
        /// referenced.
        /// </summary>
        NORM_RX_OBJECT_ABORTED,
        /// <summary>
        /// Upon receipt of app-extended watermark ack request.
        /// </summary>
        NORM_RX_ACK_REQUEST,
        /// <summary>
        /// This notification indicates that either the local sender estimate of GRTT has
        /// changed, or that a remote sender's estimate of GRTT has changed. The "sender"
        /// member  of  the  NormEvent  is  set  to  NORM_NODE_INVALID  if  the  local  sender's
        /// GRTT estimate has changed or to the NormNodeHandle of the remote sender that
        /// has updated its estimate of GRTT
        /// </summary>
        NORM_GRTT_UPDATED,
        /// <summary>
        /// This event indicates that congestion control feedback from receivers has begun
        /// to be received (This also implies that receivers in the group are actually present
        /// and can be used as a cue to begin data transmission.). Note that congestion control
        /// must be enabled for this event to be posted.
        /// Congestion control feedback can be assumed to be received until a NORM_CC_INACTIVE event is posted.
        /// </summary>
        NORM_CC_ACTIVE,
        /// <summary>
        /// This event indicates there has been no recent congestion control feedback received
        /// from the receiver set and that the local NORM sender has reached its minimum
        /// transmit rate. Applications may wish to refrain from new data transmission until
        /// a NORM_CC_ACTIVE event is posted. This notification is only posted when congestion
        /// control operation is enabled and a previous NORM_CC_ACTIVE event has occurred.
        /// </summary>
        NORM_CC_INACTIVE,
        /// <summary>
        /// When NormSetAutoAcking.
        /// </summary>
        NORM_ACKING_NODE_NEW,
        /// <summary>
        /// ICMP error (e.g. destination unreachable).
        /// </summary>
        NORM_SEND_ERROR,
        /// <summary>
        /// Issues when timeout set by NormSetUserTimer() expires.
        /// </summary>
        NORM_USER_TIMEOUT
    }
}
