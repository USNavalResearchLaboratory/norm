package norm

import (
	"fmt"
	"sync"
	"unsafe"

	"github.com/USNavalResearchLaboratory/norm/src/go/normsys"
)

// Session is a NORM session: a sender and/or receiver bound to a session
// address and port. Create one with Instance.CreateSession and release it with
// Close.
type Session struct {
	handle   normsys.SessionHandle
	instance *Instance

	// pendingData tracks C-allocated buffers handed to NormDataEnqueue. NORM
	// retains these until it emits NORM_TX_OBJECT_PURGED for the object, at
	// which point ReleasePurged frees them. Keyed by the object handle.
	mu          sync.Mutex
	pendingData map[normsys.ObjectHandle]unsafe.Pointer
}

// Handle exposes the raw normsys handle for use with the low-level package.
func (s *Session) Handle() normsys.SessionHandle { return s.handle }

// Close destroys the session. It does not free the parent Instance. Any
// C-allocated enqueue buffers still pending are freed.
func (s *Session) Close() {
	if s.handle == normsys.SessionInvalid {
		return
	}
	normsys.DestroySession(s.handle)
	s.handle = normsys.SessionInvalid
	s.mu.Lock()
	for _, p := range s.pendingData {
		normsys.CFree(p)
	}
	s.pendingData = nil
	s.mu.Unlock()
}

// LocalNodeId returns this session's local node id.
func (s *Session) LocalNodeId() NodeId { return normsys.GetLocalNodeId(s.handle) }

// --- socket / multicast configuration ---

// SetTxPort sets the source port for transmitted packets.
func (s *Session) SetTxPort(port uint16, enableReuse bool, bindAddress string) error {
	return boolErr(normsys.SetTxPort(s.handle, port, enableReuse, bindAddress), "NormSetTxPort")
}

// SetRxPortReuse enables address/port reuse on the receive socket.
func (s *Session) SetRxPortReuse(enable bool) { normsys.SetRxPortReuse(s.handle, enable) }

// SetMulticastInterface binds multicast traffic to the named interface.
func (s *Session) SetMulticastInterface(iface string) error {
	return boolErr(normsys.SetMulticastInterface(s.handle, iface), "NormSetMulticastInterface")
}

// SetSSM configures source-specific multicast for the given source address.
func (s *Session) SetSSM(sourceAddress string) error {
	return boolErr(normsys.SetSSM(s.handle, sourceAddress), "NormSetSSM")
}

// SetTTL sets the multicast time-to-live.
func (s *Session) SetTTL(ttl uint8) error {
	return boolErr(normsys.SetTTL(s.handle, ttl), "NormSetTTL")
}

// SetTOS sets the IP type-of-service byte.
func (s *Session) SetTOS(tos uint8) error {
	return boolErr(normsys.SetTOS(s.handle, tos), "NormSetTOS")
}

// SetLoopback enables reception of the session's own transmissions.
func (s *Session) SetLoopback(on bool) error {
	return boolErr(normsys.SetLoopback(s.handle, on), "NormSetLoopback")
}

// SetMulticastLoopback enables multicast loopback delivery.
func (s *Session) SetMulticastLoopback(on bool) error {
	return boolErr(normsys.SetMulticastLoopback(s.handle, on), "NormSetMulticastLoopback")
}

// SetEcnSupport configures ECN-based congestion control.
func (s *Session) SetEcnSupport(enable, ignoreLoss, tolerateLoss bool) {
	normsys.SetEcnSupport(s.handle, enable, ignoreLoss, tolerateLoss)
}

// --- sender ---

// StartSender starts the session as a sender. sessionId identifies this sender
// instance; segmentSize/numData/numParity configure the FEC block coding.
func (s *Session) StartSender(sessionId SessionId, bufferSpace uint32, segmentSize, numData, numParity uint16) error {
	return boolErr(normsys.StartSender(s.handle, sessionId, bufferSpace, segmentSize, numData, numParity, 0), "NormStartSender")
}

// StopSender stops sending.
func (s *Session) StopSender() { normsys.StopSender(s.handle) }

// SetTxRate sets the transmission rate in bits per second.
func (s *Session) SetTxRate(bitsPerSecond float64) { normsys.SetTxRate(s.handle, bitsPerSecond) }

// TxRate returns the current transmission rate in bits per second.
func (s *Session) TxRate() float64 { return normsys.GetTxRate(s.handle) }

// SetTxSocketBuffer sets the send socket buffer size in bytes.
func (s *Session) SetTxSocketBuffer(size uint) error {
	return boolErr(normsys.SetTxSocketBuffer(s.handle, size), "NormSetTxSocketBuffer")
}

// SetFlowControl sets the flow-control factor (0 disables).
func (s *Session) SetFlowControl(factor float64) { normsys.SetFlowControl(s.handle, factor) }

// SetCongestionControl enables TCP-friendly congestion control.
func (s *Session) SetCongestionControl(enable, adjustRate bool) {
	normsys.SetCongestionControl(s.handle, enable, adjustRate)
}

// SetTxRateBounds sets min/max transmission rate bounds (bits/sec) for
// congestion control. A bound of 0 leaves that side unbounded.
func (s *Session) SetTxRateBounds(min, max float64) { normsys.SetTxRateBounds(s.handle, min, max) }

// SetGrttEstimate sets the initial group round-trip time estimate in seconds.
func (s *Session) SetGrttEstimate(grtt float64) { normsys.SetGrttEstimate(s.handle, grtt) }

// SetBackoffFactor sets the NACK backoff factor.
func (s *Session) SetBackoffFactor(factor float64) { normsys.SetBackoffFactor(s.handle, factor) }

// SetGroupSize sets the estimated receiver group size.
func (s *Session) SetGroupSize(size uint) { normsys.SetGroupSize(s.handle, size) }

// SetAutoParity sets the number of parity segments auto-transmitted per block.
func (s *Session) SetAutoParity(autoParity uint8) { normsys.SetAutoParity(s.handle, autoParity) }

// FileEnqueue queues a local file for transmission. info is optional
// application metadata (NORM_INFO) and may be nil.
func (s *Session) FileEnqueue(fileName string, info []byte) (*Object, error) {
	h := normsys.FileEnqueue(s.handle, fileName, info)
	if h == normsys.ObjectInvalid {
		return nil, fmt.Errorf("%w: NormFileEnqueue(%s)", ErrOperationFailed, fileName)
	}
	return &Object{handle: h, owned: true}, nil
}

// DataEnqueue queues an in-memory data object for transmission. The data is
// copied into C-managed memory that stays valid until NORM purges the object;
// call ReleasePurged when handling TxObjectPurged events to free those buffers.
// info is optional application metadata (NORM_INFO) and may be nil.
func (s *Session) DataEnqueue(data, info []byte) (*Object, error) {
	// NORM retains the data pointer (it does not copy), and cgo forbids handing
	// Go memory to C to hold across calls, so stage the payload in C memory.
	cptr := normsys.CBytes(data)
	h := normsys.DataEnqueue(s.handle, cptr, uint32(len(data)), info)
	if h == normsys.ObjectInvalid {
		normsys.CFree(cptr)
		return nil, fmt.Errorf("%w: NormDataEnqueue", ErrOperationFailed)
	}
	s.mu.Lock()
	if s.pendingData == nil {
		s.pendingData = make(map[normsys.ObjectHandle]unsafe.Pointer)
	}
	s.pendingData[h] = cptr
	s.mu.Unlock()
	return &Object{handle: h, owned: true}, nil
}

// ReleasePurged frees the C-allocated buffer associated with a data object that
// NORM has purged. Call it when handling a TxObjectPurged event, passing
// event.Object(). It is a no-op for objects without a tracked buffer (e.g. files
// or streams).
func (s *Session) ReleasePurged(o *Object) {
	if o == nil {
		return
	}
	s.mu.Lock()
	p, ok := s.pendingData[o.handle]
	if ok {
		delete(s.pendingData, o.handle)
	}
	s.mu.Unlock()
	if ok {
		normsys.CFree(p)
	}
}

// StreamOpen opens a send stream with the given buffer size. info is optional
// application metadata (NORM_INFO) and may be nil.
func (s *Session) StreamOpen(bufferSize uint32, info []byte) (*Object, error) {
	h := normsys.StreamOpen(s.handle, bufferSize, info)
	if h == normsys.ObjectInvalid {
		return nil, fmt.Errorf("%w: NormStreamOpen", ErrOperationFailed)
	}
	return &Object{handle: h, owned: true}, nil
}

// --- watermark / acking ---

// SetWatermark sets a transmission watermark at the given object; when all
// acking nodes acknowledge up to it, a TxWatermarkCompleted event is emitted.
func (s *Session) SetWatermark(o *Object, overrideFlush bool) error {
	return boolErr(normsys.SetWatermark(s.handle, o.handle, overrideFlush), "NormSetWatermark")
}

// ResetWatermark reactivates the previously set watermark.
func (s *Session) ResetWatermark() error {
	return boolErr(normsys.ResetWatermark(s.handle), "NormResetWatermark")
}

// CancelWatermark cancels a pending watermark.
func (s *Session) CancelWatermark() { normsys.CancelWatermark(s.handle) }

// AddAckingNode adds a node to the positive-acknowledgment list.
func (s *Session) AddAckingNode(id NodeId) error {
	return boolErr(normsys.AddAckingNode(s.handle, id), "NormAddAckingNode")
}

// RemoveAckingNode removes a node from the positive-acknowledgment list.
func (s *Session) RemoveAckingNode(id NodeId) { normsys.RemoveAckingNode(s.handle, id) }

// AckingStatus returns the acknowledgment status of an acking node (use NodeAny
// for the aggregate status).
func (s *Session) AckingStatus(id NodeId) AckingStatus {
	return normsys.GetAckingStatus(s.handle, id)
}

// SetAutoAckingNodes automatically tracks nodes matching the given status as
// acking nodes.
func (s *Session) SetAutoAckingNodes(status TrackingStatus) {
	normsys.SetAutoAckingNodes(s.handle, status)
}

// SendCommand transmits an application-defined command. cmd is copied by NORM.
func (s *Session) SendCommand(cmd []byte, robust bool) error {
	return boolErr(normsys.SendCommand(s.handle, cmd, robust), "NormSendCommand")
}

// CancelCommand cancels a pending robust command transmission.
func (s *Session) CancelCommand() { normsys.CancelCommand(s.handle) }

// --- receiver ---

// StartReceiver starts the session as a receiver with the given buffer space.
func (s *Session) StartReceiver(bufferSpace uint32) error {
	return boolErr(normsys.StartReceiver(s.handle, bufferSpace), "NormStartReceiver")
}

// StopReceiver stops receiving.
func (s *Session) StopReceiver() { normsys.StopReceiver(s.handle) }

// SetRxCacheLimit sets the maximum number of objects held in the receive cache.
func (s *Session) SetRxCacheLimit(countMax uint16) { normsys.SetRxCacheLimit(s.handle, countMax) }

// SetRxSocketBuffer sets the receive socket buffer size in bytes.
func (s *Session) SetRxSocketBuffer(size uint) error {
	return boolErr(normsys.SetRxSocketBuffer(s.handle, size), "NormSetRxSocketBuffer")
}

// SetSilentReceiver configures a non-NACKing (silent) receiver. maxDelay of -1
// uses the default.
func (s *Session) SetSilentReceiver(silent bool, maxDelay int) {
	normsys.SetSilentReceiver(s.handle, silent, maxDelay)
}

// SetDefaultUnicastNack sets whether repair requests are unicast by default.
func (s *Session) SetDefaultUnicastNack(on bool) { normsys.SetDefaultUnicastNack(s.handle, on) }

// SetDefaultSyncPolicy sets the default receiver sync policy.
func (s *Session) SetDefaultSyncPolicy(policy SyncPolicy) {
	normsys.SetDefaultSyncPolicy(s.handle, policy)
}

// SetDefaultNackingMode sets the default receiver NACK mode.
func (s *Session) SetDefaultNackingMode(mode NackingMode) {
	normsys.SetDefaultNackingMode(s.handle, mode)
}

// SetDefaultRepairBoundary sets the default repair boundary granularity.
func (s *Session) SetDefaultRepairBoundary(boundary RepairBoundary) {
	normsys.SetDefaultRepairBoundary(s.handle, boundary)
}
