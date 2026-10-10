// Package norm provides idiomatic Go bindings for the NORM (NACK-Oriented
// Reliable Multicast) protocol library. It wraps the low-level normsys package
// with resource-owning types (Instance, Session, Object, Node), Go errors, and
// io.Reader/io.Writer stream adapters.
//
// The typical object hierarchy is Instance -> Session -> Object. An Instance
// owns the protocol engine and event queue; Sessions are created from it and
// send/receive Objects. See API_GUIDE.md for a walkthrough.
package norm

import "github.com/USNavalResearchLaboratory/norm/src/go/normsys"

// Enum types are re-exported from normsys so callers depend only on this package.
type (
	ObjectType     = normsys.ObjectType
	FlushMode      = normsys.FlushMode
	NackingMode    = normsys.NackingMode
	AckingStatus   = normsys.AckingStatus
	TrackingStatus = normsys.TrackingStatus
	ProbingMode    = normsys.ProbingMode
	SyncPolicy     = normsys.SyncPolicy
	RepairBoundary = normsys.RepairBoundary
	EventType      = normsys.EventType
	NodeId         = normsys.NodeId
	SessionId      = normsys.SessionId
)

// Re-exported enum constants.
const (
	ObjectNone   = normsys.ObjectNone
	ObjectData   = normsys.ObjectData
	ObjectFile   = normsys.ObjectFile
	ObjectStream = normsys.ObjectStream

	FlushNone    = normsys.FlushNone
	FlushPassive = normsys.FlushPassive
	FlushActive  = normsys.FlushActive

	NackNone     = normsys.NackNone
	NackInfoOnly = normsys.NackInfoOnly
	NackNormal   = normsys.NackNormal

	AckInvalid = normsys.AckInvalid
	AckFailure = normsys.AckFailure
	AckPending = normsys.AckPending
	AckSuccess = normsys.AckSuccess

	TrackNone      = normsys.TrackNone
	TrackReceivers = normsys.TrackReceivers
	TrackSenders   = normsys.TrackSenders
	TrackAll       = normsys.TrackAll

	ProbeNone    = normsys.ProbeNone
	ProbePassive = normsys.ProbePassive
	ProbeActive  = normsys.ProbeActive

	SyncCurrent = normsys.SyncCurrent
	SyncStream  = normsys.SyncStream
	SyncAll     = normsys.SyncAll

	BoundaryBlock  = normsys.BoundaryBlock
	BoundaryObject = normsys.BoundaryObject

	NodeNone = normsys.NodeNone
	NodeAny  = normsys.NodeAny
)

// Event type constants.
const (
	EventInvalid         = normsys.EventInvalid
	TxQueueVacancy       = normsys.TxQueueVacancy
	TxQueueEmpty         = normsys.TxQueueEmpty
	TxFlushCompleted     = normsys.TxFlushCompleted
	TxWatermarkCompleted = normsys.TxWatermarkCompleted
	TxCmdSent            = normsys.TxCmdSent
	TxObjectSent         = normsys.TxObjectSent
	TxObjectPurged       = normsys.TxObjectPurged
	TxRateChanged        = normsys.TxRateChanged
	LocalSenderClosed    = normsys.LocalSenderClosed
	RemoteSenderNew      = normsys.RemoteSenderNew
	RemoteSenderReset    = normsys.RemoteSenderReset
	RemoteSenderAddress  = normsys.RemoteSenderAddress
	RemoteSenderActive   = normsys.RemoteSenderActive
	RemoteSenderInactive = normsys.RemoteSenderInactive
	RemoteSenderPurged   = normsys.RemoteSenderPurged
	RxCmdNew             = normsys.RxCmdNew
	RxObjectNew          = normsys.RxObjectNew
	RxObjectInfo         = normsys.RxObjectInfo
	RxObjectUpdated      = normsys.RxObjectUpdated
	RxObjectCompleted    = normsys.RxObjectCompleted
	RxObjectAborted      = normsys.RxObjectAborted
	RxAckRequest         = normsys.RxAckRequest
	GrttUpdated          = normsys.GrttUpdated
	CCActive             = normsys.CCActive
	CCInactive           = normsys.CCInactive
	AckingNodeNew        = normsys.AckingNodeNew
	SendError            = normsys.SendError
	UserTimeout          = normsys.UserTimeout
)

// Version returns the NORM library version this binding is linked against.
func Version() (major, minor, patch int) {
	return normsys.GetVersion()
}

// IsUnicastAddress reports whether the given address string is a unicast address.
func IsUnicastAddress(address string) bool {
	return normsys.IsUnicastAddress(address)
}

// ObjectTypeString returns a human-readable name for an ObjectType.
func ObjectTypeString(t ObjectType) string {
	switch t {
	case ObjectNone:
		return "NONE"
	case ObjectData:
		return "DATA"
	case ObjectFile:
		return "FILE"
	case ObjectStream:
		return "STREAM"
	default:
		return "UNKNOWN"
	}
}

// EventTypeString returns a human-readable name for an EventType.
func EventTypeString(t EventType) string {
	if name, ok := eventNames[t]; ok {
		return name
	}
	return "UNKNOWN"
}

var eventNames = map[EventType]string{
	EventInvalid:         "EVENT_INVALID",
	TxQueueVacancy:       "TX_QUEUE_VACANCY",
	TxQueueEmpty:         "TX_QUEUE_EMPTY",
	TxFlushCompleted:     "TX_FLUSH_COMPLETED",
	TxWatermarkCompleted: "TX_WATERMARK_COMPLETED",
	TxCmdSent:            "TX_CMD_SENT",
	TxObjectSent:         "TX_OBJECT_SENT",
	TxObjectPurged:       "TX_OBJECT_PURGED",
	TxRateChanged:        "TX_RATE_CHANGED",
	LocalSenderClosed:    "LOCAL_SENDER_CLOSED",
	RemoteSenderNew:      "REMOTE_SENDER_NEW",
	RemoteSenderReset:    "REMOTE_SENDER_RESET",
	RemoteSenderAddress:  "REMOTE_SENDER_ADDRESS",
	RemoteSenderActive:   "REMOTE_SENDER_ACTIVE",
	RemoteSenderInactive: "REMOTE_SENDER_INACTIVE",
	RemoteSenderPurged:   "REMOTE_SENDER_PURGED",
	RxCmdNew:             "RX_CMD_NEW",
	RxObjectNew:          "RX_OBJECT_NEW",
	RxObjectInfo:         "RX_OBJECT_INFO",
	RxObjectUpdated:      "RX_OBJECT_UPDATED",
	RxObjectCompleted:    "RX_OBJECT_COMPLETED",
	RxObjectAborted:      "RX_OBJECT_ABORTED",
	RxAckRequest:         "RX_ACK_REQUEST",
	GrttUpdated:          "GRTT_UPDATED",
	CCActive:             "CC_ACTIVE",
	CCInactive:           "CC_INACTIVE",
	AckingNodeNew:        "ACKING_NODE_NEW",
	SendError:            "SEND_ERROR",
	UserTimeout:          "USER_TIMEOUT",
}
