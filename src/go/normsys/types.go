// Package normsys is a low-level, near 1:1 binding of the NORM C API
// (include/normApi.h). It is the Go analog of the Rust "norm-sys" crate.
//
// Signatures are expressed in Go types (handles as pointer-backed named types,
// Go strings/slices/bools) rather than raw cgo C.* types. This is deliberate:
// cgo gives every package its own distinct set of C.* types, so exposing C types
// here would prevent the higher-level "norm" package (and downstream callers)
// from consuming this one without their own cgo layer. Keeping the surface in Go
// types makes normsys a usable escape hatch for API calls the idiomatic package
// does not yet wrap.
package normsys

/*
#cgo pkg-config: norm
#include <normApi.h>
*/
import "C"

import "unsafe"

// Opaque NORM handles. The underlying memory is owned by the native library, not
// the Go heap, so representing them as unsafe.Pointer is GC-safe: the collector
// never manages these and the C library guarantees their lifetime until the
// corresponding Destroy/Release call. Distinct named types prevent accidentally
// passing, say, a session handle where an object handle is expected.
type (
	InstanceHandle unsafe.Pointer
	SessionHandle  unsafe.Pointer
	NodeHandle     unsafe.Pointer
	ObjectHandle   unsafe.Pointer
)

// Invalid handle sentinels. All NORM handles are NULL when invalid
// (see src/common/normApi.cpp).
var (
	InstanceInvalid InstanceHandle = nil
	SessionInvalid  SessionHandle  = nil
	NodeInvalid     NodeHandle     = nil
	ObjectInvalid   ObjectHandle   = nil
)

// NodeId identifies a NORM participant.
type NodeId uint32

const (
	NodeNone NodeId = 0x00000000
	NodeAny  NodeId = 0xffffffff
)

// SessionId is a sender instance identifier passed to StartSender.
type SessionId uint16

// ObjectTransportId identifies an object within a session's transport sequence.
type ObjectTransportId uint16

// ObjectType enumerates the kinds of transported objects.
type ObjectType int32

const (
	ObjectNone   ObjectType = C.NORM_OBJECT_NONE
	ObjectData   ObjectType = C.NORM_OBJECT_DATA
	ObjectFile   ObjectType = C.NORM_OBJECT_FILE
	ObjectStream ObjectType = C.NORM_OBJECT_STREAM
)

// FlushMode controls stream flush behavior.
type FlushMode int32

const (
	FlushNone    FlushMode = C.NORM_FLUSH_NONE
	FlushPassive FlushMode = C.NORM_FLUSH_PASSIVE
	FlushActive  FlushMode = C.NORM_FLUSH_ACTIVE
)

// NackingMode controls receiver repair request behavior.
type NackingMode int32

const (
	NackNone     NackingMode = C.NORM_NACK_NONE
	NackInfoOnly NackingMode = C.NORM_NACK_INFO_ONLY
	NackNormal   NackingMode = C.NORM_NACK_NORMAL
)

// AckingStatus reports the result of a watermark/ack request for a node.
type AckingStatus int32

const (
	AckInvalid AckingStatus = C.NORM_ACK_INVALID
	AckFailure AckingStatus = C.NORM_ACK_FAILURE
	AckPending AckingStatus = C.NORM_ACK_PENDING
	AckSuccess AckingStatus = C.NORM_ACK_SUCCESS
)

// TrackingStatus selects which nodes are auto-added as acking nodes.
type TrackingStatus int32

const (
	TrackNone      TrackingStatus = C.NORM_TRACK_NONE
	TrackReceivers TrackingStatus = C.NORM_TRACK_RECEIVERS
	TrackSenders   TrackingStatus = C.NORM_TRACK_SENDERS
	TrackAll       TrackingStatus = C.NORM_TRACK_ALL
)

// ProbingMode controls GRTT probing behavior.
type ProbingMode int32

const (
	ProbeNone    ProbingMode = C.NORM_PROBE_NONE
	ProbePassive ProbingMode = C.NORM_PROBE_PASSIVE
	ProbeActive  ProbingMode = C.NORM_PROBE_ACTIVE
)

// SyncPolicy controls how a receiver synchronizes to a sender's stream.
type SyncPolicy int32

const (
	SyncCurrent SyncPolicy = C.NORM_SYNC_CURRENT
	SyncStream  SyncPolicy = C.NORM_SYNC_STREAM
	SyncAll     SyncPolicy = C.NORM_SYNC_ALL
)

// RepairBoundary controls the granularity of repair requests.
type RepairBoundary int32

const (
	BoundaryBlock  RepairBoundary = C.NORM_BOUNDARY_BLOCK
	BoundaryObject RepairBoundary = C.NORM_BOUNDARY_OBJECT
)

// EventType enumerates the NORM protocol events delivered by GetNextEvent.
type EventType int32

const (
	EventInvalid         EventType = C.NORM_EVENT_INVALID
	TxQueueVacancy       EventType = C.NORM_TX_QUEUE_VACANCY
	TxQueueEmpty         EventType = C.NORM_TX_QUEUE_EMPTY
	TxFlushCompleted     EventType = C.NORM_TX_FLUSH_COMPLETED
	TxWatermarkCompleted EventType = C.NORM_TX_WATERMARK_COMPLETED
	TxCmdSent            EventType = C.NORM_TX_CMD_SENT
	TxObjectSent         EventType = C.NORM_TX_OBJECT_SENT
	TxObjectPurged       EventType = C.NORM_TX_OBJECT_PURGED
	TxRateChanged        EventType = C.NORM_TX_RATE_CHANGED
	LocalSenderClosed    EventType = C.NORM_LOCAL_SENDER_CLOSED
	RemoteSenderNew      EventType = C.NORM_REMOTE_SENDER_NEW
	RemoteSenderReset    EventType = C.NORM_REMOTE_SENDER_RESET
	RemoteSenderAddress  EventType = C.NORM_REMOTE_SENDER_ADDRESS
	RemoteSenderActive   EventType = C.NORM_REMOTE_SENDER_ACTIVE
	RemoteSenderInactive EventType = C.NORM_REMOTE_SENDER_INACTIVE
	RemoteSenderPurged   EventType = C.NORM_REMOTE_SENDER_PURGED
	RxCmdNew             EventType = C.NORM_RX_CMD_NEW
	RxObjectNew          EventType = C.NORM_RX_OBJECT_NEW
	RxObjectInfo         EventType = C.NORM_RX_OBJECT_INFO
	RxObjectUpdated      EventType = C.NORM_RX_OBJECT_UPDATED
	RxObjectCompleted    EventType = C.NORM_RX_OBJECT_COMPLETED
	RxObjectAborted      EventType = C.NORM_RX_OBJECT_ABORTED
	RxAckRequest         EventType = C.NORM_RX_ACK_REQUEST
	GrttUpdated          EventType = C.NORM_GRTT_UPDATED
	CCActive             EventType = C.NORM_CC_ACTIVE
	CCInactive           EventType = C.NORM_CC_INACTIVE
	AckingNodeNew        EventType = C.NORM_ACKING_NODE_NEW
	SendError            EventType = C.NORM_SEND_ERROR
	UserTimeout          EventType = C.NORM_USER_TIMEOUT
)

// Event mirrors the C NormEvent struct returned by GetNextEvent.
type Event struct {
	Type    EventType
	Session SessionHandle
	Sender  NodeHandle
	Object  ObjectHandle
}
