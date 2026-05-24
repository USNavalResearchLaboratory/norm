// Allow non-uppercase globals from FFI bindings
// The bindgen-generated constants use lowercase prefixes, which is acceptable for FFI code
#![allow(non_upper_case_globals)]

use norm_sys::*;
use std::fmt;

/// NORM node identifier type
pub type NodeId = u32;

/// NORM session identifier type
pub type SessionId = u16;

/// NORM object transport identifier type
pub type ObjectTransportId = u16;

/// NORM size type for file and object sizes
#[cfg(unix)]
pub type Size = i64;

/// NORM size type for file and object sizes
#[cfg(windows)]
pub type Size = i64;

/// NORM object types
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
#[repr(u32)]
pub enum ObjectType {
    /// Placeholder for no object type
    None = NormObjectType_NORM_OBJECT_NONE as u32,
    /// Data object (memory buffer)
    Data = NormObjectType_NORM_OBJECT_DATA as u32,
    /// File object
    File = NormObjectType_NORM_OBJECT_FILE as u32,
    /// Stream object
    Stream = NormObjectType_NORM_OBJECT_STREAM as u32,
}

impl From<NormObjectType> for ObjectType {
    fn from(t: NormObjectType) -> Self {
        match t {
            NormObjectType_NORM_OBJECT_NONE => ObjectType::None,
            NormObjectType_NORM_OBJECT_DATA => ObjectType::Data,
            NormObjectType_NORM_OBJECT_FILE => ObjectType::File,
            NormObjectType_NORM_OBJECT_STREAM => ObjectType::Stream,
            _ => ObjectType::None,
        }
    }
}

impl From<ObjectType> for NormObjectType {
    fn from(t: ObjectType) -> Self {
        match t {
            ObjectType::None => NormObjectType_NORM_OBJECT_NONE,
            ObjectType::Data => NormObjectType_NORM_OBJECT_DATA,
            ObjectType::File => NormObjectType_NORM_OBJECT_FILE,
            ObjectType::Stream => NormObjectType_NORM_OBJECT_STREAM,
        }
    }
}

/// NORM flush modes
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
#[repr(u32)]
pub enum FlushMode {
    /// No flush
    None = NormFlushMode_NORM_FLUSH_NONE as u32,
    /// Passive flush - minimal delay
    Passive = NormFlushMode_NORM_FLUSH_PASSIVE as u32,
    /// Active flush - actively seek acknowledgment
    Active = NormFlushMode_NORM_FLUSH_ACTIVE as u32,
}

impl From<NormFlushMode> for FlushMode {
    fn from(m: NormFlushMode) -> Self {
        match m {
            NormFlushMode_NORM_FLUSH_NONE => FlushMode::None,
            NormFlushMode_NORM_FLUSH_PASSIVE => FlushMode::Passive,
            NormFlushMode_NORM_FLUSH_ACTIVE => FlushMode::Active,
            _ => FlushMode::None,
        }
    }
}

impl From<FlushMode> for NormFlushMode {
    fn from(m: FlushMode) -> Self {
        match m {
            FlushMode::None => NormFlushMode_NORM_FLUSH_NONE,
            FlushMode::Passive => NormFlushMode_NORM_FLUSH_PASSIVE,
            FlushMode::Active => NormFlushMode_NORM_FLUSH_ACTIVE,
        }
    }
}

/// NORM nacking modes
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
#[repr(u32)]
pub enum NackingMode {
    /// No NACKs
    None = NormNackingMode_NORM_NACK_NONE as u32,
    /// Info-only NACKs
    InfoOnly = NormNackingMode_NORM_NACK_INFO_ONLY as u32,
    /// Normal NACKs
    Normal = NormNackingMode_NORM_NACK_NORMAL as u32,
}

impl From<NormNackingMode> for NackingMode {
    fn from(m: NormNackingMode) -> Self {
        match m {
            NormNackingMode_NORM_NACK_NONE => NackingMode::None,
            NormNackingMode_NORM_NACK_INFO_ONLY => NackingMode::InfoOnly,
            NormNackingMode_NORM_NACK_NORMAL => NackingMode::Normal,
            _ => NackingMode::None,
        }
    }
}

impl From<NackingMode> for NormNackingMode {
    fn from(m: NackingMode) -> Self {
        match m {
            NackingMode::None => NormNackingMode_NORM_NACK_NONE,
            NackingMode::InfoOnly => NormNackingMode_NORM_NACK_INFO_ONLY,
            NackingMode::Normal => NormNackingMode_NORM_NACK_NORMAL,
        }
    }
}

/// NORM acking status
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
#[repr(u32)]
pub enum AckingStatus {
    /// Invalid ack status
    Invalid = NormAckingStatus_NORM_ACK_INVALID as u32,
    /// Ack failure
    Failure = NormAckingStatus_NORM_ACK_FAILURE as u32,
    /// Ack pending
    Pending = NormAckingStatus_NORM_ACK_PENDING as u32,
    /// Ack success
    Success = NormAckingStatus_NORM_ACK_SUCCESS as u32,
}

impl From<NormAckingStatus> for AckingStatus {
    fn from(s: NormAckingStatus) -> Self {
        match s {
            NormAckingStatus_NORM_ACK_INVALID => AckingStatus::Invalid,
            NormAckingStatus_NORM_ACK_FAILURE => AckingStatus::Failure,
            NormAckingStatus_NORM_ACK_PENDING => AckingStatus::Pending,
            NormAckingStatus_NORM_ACK_SUCCESS => AckingStatus::Success,
            _ => AckingStatus::Invalid,
        }
    }
}

impl From<AckingStatus> for NormAckingStatus {
    fn from(s: AckingStatus) -> Self {
        match s {
            AckingStatus::Invalid => NormAckingStatus_NORM_ACK_INVALID,
            AckingStatus::Failure => NormAckingStatus_NORM_ACK_FAILURE,
            AckingStatus::Pending => NormAckingStatus_NORM_ACK_PENDING,
            AckingStatus::Success => NormAckingStatus_NORM_ACK_SUCCESS,
        }
    }
}

/// NORM tracking status
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
#[repr(u32)]
pub enum TrackingStatus {
    /// No tracking
    None = NormTrackingStatus_NORM_TRACK_NONE as u32,
    /// Track receivers
    Receivers = NormTrackingStatus_NORM_TRACK_RECEIVERS as u32,
    /// Track senders
    Senders = NormTrackingStatus_NORM_TRACK_SENDERS as u32,
    /// Track all
    All = NormTrackingStatus_NORM_TRACK_ALL as u32,
}

impl From<NormTrackingStatus> for TrackingStatus {
    fn from(s: NormTrackingStatus) -> Self {
        match s {
            NormTrackingStatus_NORM_TRACK_NONE => TrackingStatus::None,
            NormTrackingStatus_NORM_TRACK_RECEIVERS => TrackingStatus::Receivers,
            NormTrackingStatus_NORM_TRACK_SENDERS => TrackingStatus::Senders,
            NormTrackingStatus_NORM_TRACK_ALL => TrackingStatus::All,
            _ => TrackingStatus::None,
        }
    }
}

impl From<TrackingStatus> for NormTrackingStatus {
    fn from(s: TrackingStatus) -> Self {
        match s {
            TrackingStatus::None => NormTrackingStatus_NORM_TRACK_NONE,
            TrackingStatus::Receivers => NormTrackingStatus_NORM_TRACK_RECEIVERS,
            TrackingStatus::Senders => NormTrackingStatus_NORM_TRACK_SENDERS,
            TrackingStatus::All => NormTrackingStatus_NORM_TRACK_ALL,
        }
    }
}

/// NORM probing mode
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
#[repr(u32)]
pub enum ProbingMode {
    /// No probing
    None = NormProbingMode_NORM_PROBE_NONE as u32,
    /// Passive probing
    Passive = NormProbingMode_NORM_PROBE_PASSIVE as u32,
    /// Active probing
    Active = NormProbingMode_NORM_PROBE_ACTIVE as u32,
}

impl From<NormProbingMode> for ProbingMode {
    fn from(m: NormProbingMode) -> Self {
        match m {
            NormProbingMode_NORM_PROBE_NONE => ProbingMode::None,
            NormProbingMode_NORM_PROBE_PASSIVE => ProbingMode::Passive,
            NormProbingMode_NORM_PROBE_ACTIVE => ProbingMode::Active,
            _ => ProbingMode::None,
        }
    }
}

impl From<ProbingMode> for NormProbingMode {
    fn from(m: ProbingMode) -> Self {
        match m {
            ProbingMode::None => NormProbingMode_NORM_PROBE_NONE,
            ProbingMode::Passive => NormProbingMode_NORM_PROBE_PASSIVE,
            ProbingMode::Active => NormProbingMode_NORM_PROBE_ACTIVE,
        }
    }
}

/// NORM sync policy
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
#[repr(u32)]
pub enum SyncPolicy {
    /// Sync to current data (join mid-stream)
    Current = NormSyncPolicy_NORM_SYNC_CURRENT as u32,
    /// Sync to stream from beginning
    Stream = NormSyncPolicy_NORM_SYNC_STREAM as u32,
    /// Sync to all data, old and new
    All = NormSyncPolicy_NORM_SYNC_ALL as u32,
}

impl From<NormSyncPolicy> for SyncPolicy {
    fn from(p: NormSyncPolicy) -> Self {
        match p {
            NormSyncPolicy_NORM_SYNC_CURRENT => SyncPolicy::Current,
            NormSyncPolicy_NORM_SYNC_STREAM => SyncPolicy::Stream,
            NormSyncPolicy_NORM_SYNC_ALL => SyncPolicy::All,
            _ => SyncPolicy::Current,
        }
    }
}

impl From<SyncPolicy> for NormSyncPolicy {
    fn from(p: SyncPolicy) -> Self {
        match p {
            SyncPolicy::Current => NormSyncPolicy_NORM_SYNC_CURRENT,
            SyncPolicy::Stream => NormSyncPolicy_NORM_SYNC_STREAM,
            SyncPolicy::All => NormSyncPolicy_NORM_SYNC_ALL,
        }
    }
}

/// NORM repair boundary
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
#[repr(u32)]
pub enum RepairBoundary {
    /// Block boundary
    Block = NormRepairBoundary_NORM_BOUNDARY_BLOCK as u32,
    /// Object boundary
    Object = NormRepairBoundary_NORM_BOUNDARY_OBJECT as u32,
}

impl From<NormRepairBoundary> for RepairBoundary {
    fn from(b: NormRepairBoundary) -> Self {
        match b {
            NormRepairBoundary_NORM_BOUNDARY_BLOCK => RepairBoundary::Block,
            NormRepairBoundary_NORM_BOUNDARY_OBJECT => RepairBoundary::Object,
            _ => RepairBoundary::Block,
        }
    }
}

impl From<RepairBoundary> for NormRepairBoundary {
    fn from(b: RepairBoundary) -> Self {
        match b {
            RepairBoundary::Block => NormRepairBoundary_NORM_BOUNDARY_BLOCK,
            RepairBoundary::Object => NormRepairBoundary_NORM_BOUNDARY_OBJECT,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_object_type_conversions() {
        // Test From<NormObjectType> for ObjectType
        let data_type: ObjectType = NormObjectType_NORM_OBJECT_DATA.into();
        assert_eq!(data_type, ObjectType::Data);

        let file_type: ObjectType = NormObjectType_NORM_OBJECT_FILE.into();
        assert_eq!(file_type, ObjectType::File);

        let stream_type: ObjectType = NormObjectType_NORM_OBJECT_STREAM.into();
        assert_eq!(stream_type, ObjectType::Stream);

        // Test From<ObjectType> for NormObjectType
        let norm_data: NormObjectType = ObjectType::Data.into();
        assert_eq!(norm_data, NormObjectType_NORM_OBJECT_DATA);
    }

    #[test]
    fn test_flush_mode_conversions() {
        assert_eq!(FlushMode::from(NormFlushMode_NORM_FLUSH_NONE), FlushMode::None);
        assert_eq!(FlushMode::from(NormFlushMode_NORM_FLUSH_PASSIVE), FlushMode::Passive);
        assert_eq!(FlushMode::from(NormFlushMode_NORM_FLUSH_ACTIVE), FlushMode::Active);

        let norm_active: NormFlushMode = FlushMode::Active.into();
        assert_eq!(norm_active, NormFlushMode_NORM_FLUSH_ACTIVE);
    }

    #[test]
    fn test_event_type_conversions() {
        let tx_event: EventType = NormEventType_NORM_TX_QUEUE_EMPTY.into();
        assert_eq!(tx_event, EventType::TxQueueEmpty);

        let rx_event: EventType = NormEventType_NORM_RX_OBJECT_COMPLETED.into();
        assert_eq!(rx_event, EventType::RxObjectCompleted);

        let norm_event: NormEventType = EventType::TxObjectSent.into();
        assert_eq!(norm_event, NormEventType_NORM_TX_OBJECT_SENT);
    }

    #[test]
    fn test_nacking_mode_conversions() {
        assert_eq!(NackingMode::from(NormNackingMode_NORM_NACK_NONE), NackingMode::None);
        assert_eq!(NackingMode::from(NormNackingMode_NORM_NACK_NORMAL), NackingMode::Normal);
    }

    #[test]
    fn test_acking_status_conversions() {
        assert_eq!(AckingStatus::from(NormAckingStatus_NORM_ACK_SUCCESS), AckingStatus::Success);
        assert_eq!(AckingStatus::from(NormAckingStatus_NORM_ACK_FAILURE), AckingStatus::Failure);
        assert_eq!(AckingStatus::from(NormAckingStatus_NORM_ACK_PENDING), AckingStatus::Pending);
    }
}

/// NORM event type
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
#[repr(u32)]
pub enum EventType {
    /// Invalid event
    Invalid = NormEventType_NORM_EVENT_INVALID as u32,
    /// Transmit queue vacancy
    TxQueueVacancy = NormEventType_NORM_TX_QUEUE_VACANCY as u32,
    /// Transmit queue empty
    TxQueueEmpty = NormEventType_NORM_TX_QUEUE_EMPTY as u32,
    /// Transmit flush completed
    TxFlushCompleted = NormEventType_NORM_TX_FLUSH_COMPLETED as u32,
    /// Transmit watermark completed
    TxWatermarkCompleted = NormEventType_NORM_TX_WATERMARK_COMPLETED as u32,
    /// Transmit command sent
    TxCmdSent = NormEventType_NORM_TX_CMD_SENT as u32,
    /// Transmit object sent
    TxObjectSent = NormEventType_NORM_TX_OBJECT_SENT as u32,
    /// Transmit object purged
    TxObjectPurged = NormEventType_NORM_TX_OBJECT_PURGED as u32,
    /// Transmit rate changed
    TxRateChanged = NormEventType_NORM_TX_RATE_CHANGED as u32,
    /// Local sender closed
    LocalSenderClosed = NormEventType_NORM_LOCAL_SENDER_CLOSED as u32,
    /// Remote sender new
    RemoteSenderNew = NormEventType_NORM_REMOTE_SENDER_NEW as u32,
    /// Remote sender reset
    RemoteSenderReset = NormEventType_NORM_REMOTE_SENDER_RESET as u32,
    /// Remote sender address
    RemoteSenderAddress = NormEventType_NORM_REMOTE_SENDER_ADDRESS as u32,
    /// Remote sender active
    RemoteSenderActive = NormEventType_NORM_REMOTE_SENDER_ACTIVE as u32,
    /// Remote sender inactive
    RemoteSenderInactive = NormEventType_NORM_REMOTE_SENDER_INACTIVE as u32,
    /// Remote sender purged
    RemoteSenderPurged = NormEventType_NORM_REMOTE_SENDER_PURGED as u32,
    /// Receive command new
    RxCmdNew = NormEventType_NORM_RX_CMD_NEW as u32,
    /// Receive object new
    RxObjectNew = NormEventType_NORM_RX_OBJECT_NEW as u32,
    /// Receive object info
    RxObjectInfo = NormEventType_NORM_RX_OBJECT_INFO as u32,
    /// Receive object updated
    RxObjectUpdated = NormEventType_NORM_RX_OBJECT_UPDATED as u32,
    /// Receive object completed
    RxObjectCompleted = NormEventType_NORM_RX_OBJECT_COMPLETED as u32,
    /// Receive object aborted
    RxObjectAborted = NormEventType_NORM_RX_OBJECT_ABORTED as u32,
    /// Receive ack request
    RxAckRequest = NormEventType_NORM_RX_ACK_REQUEST as u32,
    /// GRTT updated
    GrttUpdated = NormEventType_NORM_GRTT_UPDATED as u32,
    /// CC active
    CcActive = NormEventType_NORM_CC_ACTIVE as u32,
    /// CC inactive
    CcInactive = NormEventType_NORM_CC_INACTIVE as u32,
    /// Acking node new
    AckingNodeNew = NormEventType_NORM_ACKING_NODE_NEW as u32,
    /// Send error
    SendError = NormEventType_NORM_SEND_ERROR as u32,
    /// User timeout
    UserTimeout = NormEventType_NORM_USER_TIMEOUT as u32,
}

impl From<NormEventType> for EventType {
    fn from(t: NormEventType) -> Self {
        match t {
            NormEventType_NORM_EVENT_INVALID => EventType::Invalid,
            NormEventType_NORM_TX_QUEUE_VACANCY => EventType::TxQueueVacancy,
            NormEventType_NORM_TX_QUEUE_EMPTY => EventType::TxQueueEmpty,
            NormEventType_NORM_TX_FLUSH_COMPLETED => EventType::TxFlushCompleted,
            NormEventType_NORM_TX_WATERMARK_COMPLETED => EventType::TxWatermarkCompleted,
            NormEventType_NORM_TX_CMD_SENT => EventType::TxCmdSent,
            NormEventType_NORM_TX_OBJECT_SENT => EventType::TxObjectSent,
            NormEventType_NORM_TX_OBJECT_PURGED => EventType::TxObjectPurged,
            NormEventType_NORM_TX_RATE_CHANGED => EventType::TxRateChanged,
            NormEventType_NORM_LOCAL_SENDER_CLOSED => EventType::LocalSenderClosed,
            NormEventType_NORM_REMOTE_SENDER_NEW => EventType::RemoteSenderNew,
            NormEventType_NORM_REMOTE_SENDER_RESET => EventType::RemoteSenderReset,
            NormEventType_NORM_REMOTE_SENDER_ADDRESS => EventType::RemoteSenderAddress,
            NormEventType_NORM_REMOTE_SENDER_ACTIVE => EventType::RemoteSenderActive,
            NormEventType_NORM_REMOTE_SENDER_INACTIVE => EventType::RemoteSenderInactive,
            NormEventType_NORM_REMOTE_SENDER_PURGED => EventType::RemoteSenderPurged,
            NormEventType_NORM_RX_CMD_NEW => EventType::RxCmdNew,
            NormEventType_NORM_RX_OBJECT_NEW => EventType::RxObjectNew,
            NormEventType_NORM_RX_OBJECT_INFO => EventType::RxObjectInfo,
            NormEventType_NORM_RX_OBJECT_UPDATED => EventType::RxObjectUpdated,
            NormEventType_NORM_RX_OBJECT_COMPLETED => EventType::RxObjectCompleted,
            NormEventType_NORM_RX_OBJECT_ABORTED => EventType::RxObjectAborted,
            NormEventType_NORM_RX_ACK_REQUEST => EventType::RxAckRequest,
            NormEventType_NORM_GRTT_UPDATED => EventType::GrttUpdated,
            NormEventType_NORM_CC_ACTIVE => EventType::CcActive,
            NormEventType_NORM_CC_INACTIVE => EventType::CcInactive,
            NormEventType_NORM_ACKING_NODE_NEW => EventType::AckingNodeNew,
            NormEventType_NORM_SEND_ERROR => EventType::SendError,
            NormEventType_NORM_USER_TIMEOUT => EventType::UserTimeout,
            _ => EventType::Invalid,
        }
    }
}

impl From<EventType> for NormEventType {
    fn from(t: EventType) -> Self {
        match t {
            EventType::Invalid => NormEventType_NORM_EVENT_INVALID,
            EventType::TxQueueVacancy => NormEventType_NORM_TX_QUEUE_VACANCY,
            EventType::TxQueueEmpty => NormEventType_NORM_TX_QUEUE_EMPTY,
            EventType::TxFlushCompleted => NormEventType_NORM_TX_FLUSH_COMPLETED,
            EventType::TxWatermarkCompleted => NormEventType_NORM_TX_WATERMARK_COMPLETED,
            EventType::TxCmdSent => NormEventType_NORM_TX_CMD_SENT,
            EventType::TxObjectSent => NormEventType_NORM_TX_OBJECT_SENT,
            EventType::TxObjectPurged => NormEventType_NORM_TX_OBJECT_PURGED,
            EventType::TxRateChanged => NormEventType_NORM_TX_RATE_CHANGED,
            EventType::LocalSenderClosed => NormEventType_NORM_LOCAL_SENDER_CLOSED,
            EventType::RemoteSenderNew => NormEventType_NORM_REMOTE_SENDER_NEW,
            EventType::RemoteSenderReset => NormEventType_NORM_REMOTE_SENDER_RESET,
            EventType::RemoteSenderAddress => NormEventType_NORM_REMOTE_SENDER_ADDRESS,
            EventType::RemoteSenderActive => NormEventType_NORM_REMOTE_SENDER_ACTIVE,
            EventType::RemoteSenderInactive => NormEventType_NORM_REMOTE_SENDER_INACTIVE,
            EventType::RemoteSenderPurged => NormEventType_NORM_REMOTE_SENDER_PURGED,
            EventType::RxCmdNew => NormEventType_NORM_RX_CMD_NEW,
            EventType::RxObjectNew => NormEventType_NORM_RX_OBJECT_NEW,
            EventType::RxObjectInfo => NormEventType_NORM_RX_OBJECT_INFO,
            EventType::RxObjectUpdated => NormEventType_NORM_RX_OBJECT_UPDATED,
            EventType::RxObjectCompleted => NormEventType_NORM_RX_OBJECT_COMPLETED,
            EventType::RxObjectAborted => NormEventType_NORM_RX_OBJECT_ABORTED,
            EventType::RxAckRequest => NormEventType_NORM_RX_ACK_REQUEST,
            EventType::GrttUpdated => NormEventType_NORM_GRTT_UPDATED,
            EventType::CcActive => NormEventType_NORM_CC_ACTIVE,
            EventType::CcInactive => NormEventType_NORM_CC_INACTIVE,
            EventType::AckingNodeNew => NormEventType_NORM_ACKING_NODE_NEW,
            EventType::SendError => NormEventType_NORM_SEND_ERROR,
            EventType::UserTimeout => NormEventType_NORM_USER_TIMEOUT,
        }
    }
}

impl fmt::Display for EventType {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            EventType::Invalid => write!(f, "Invalid"),
            EventType::TxQueueVacancy => write!(f, "TxQueueVacancy"),
            EventType::TxQueueEmpty => write!(f, "TxQueueEmpty"),
            EventType::TxFlushCompleted => write!(f, "TxFlushCompleted"),
            EventType::TxWatermarkCompleted => write!(f, "TxWatermarkCompleted"),
            EventType::TxCmdSent => write!(f, "TxCmdSent"),
            EventType::TxObjectSent => write!(f, "TxObjectSent"),
            EventType::TxObjectPurged => write!(f, "TxObjectPurged"),
            EventType::TxRateChanged => write!(f, "TxRateChanged"),
            EventType::LocalSenderClosed => write!(f, "LocalSenderClosed"),
            EventType::RemoteSenderNew => write!(f, "RemoteSenderNew"),
            EventType::RemoteSenderReset => write!(f, "RemoteSenderReset"),
            EventType::RemoteSenderAddress => write!(f, "RemoteSenderAddress"),
            EventType::RemoteSenderActive => write!(f, "RemoteSenderActive"),
            EventType::RemoteSenderInactive => write!(f, "RemoteSenderInactive"),
            EventType::RemoteSenderPurged => write!(f, "RemoteSenderPurged"),
            EventType::RxCmdNew => write!(f, "RxCmdNew"),
            EventType::RxObjectNew => write!(f, "RxObjectNew"),
            EventType::RxObjectInfo => write!(f, "RxObjectInfo"),
            EventType::RxObjectUpdated => write!(f, "RxObjectUpdated"),
            EventType::RxObjectCompleted => write!(f, "RxObjectCompleted"),
            EventType::RxObjectAborted => write!(f, "RxObjectAborted"),
            EventType::RxAckRequest => write!(f, "RxAckRequest"),
            EventType::GrttUpdated => write!(f, "GrttUpdated"),
            EventType::CcActive => write!(f, "CcActive"),
            EventType::CcInactive => write!(f, "CcInactive"),
            EventType::AckingNodeNew => write!(f, "AckingNodeNew"),
            EventType::SendError => write!(f, "SendError"),
            EventType::UserTimeout => write!(f, "UserTimeout"),
        }
    }
}