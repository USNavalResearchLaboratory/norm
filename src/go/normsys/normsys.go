package normsys

/*
#cgo pkg-config: norm
#include <stdlib.h>
#include <normApi.h>
*/
import "C"

import "unsafe"

// This file wraps the NORM C API in Go-typed functions. Each wrapper is a thin
// translation: Go bool/string/slice <-> C bool/char*, and the pointer-backed
// handle types <-> the C handle typedefs. No lifetime management or error
// synthesis is done here; that belongs to the higher-level "norm" package.

// --- handle conversion helpers (kept unexported and tiny; cgo pointer casts) ---

func inst(h InstanceHandle) C.NormInstanceHandle { return C.NormInstanceHandle(unsafe.Pointer(h)) }
func sess(h SessionHandle) C.NormSessionHandle   { return C.NormSessionHandle(unsafe.Pointer(h)) }
func node(h NodeHandle) C.NormNodeHandle         { return C.NormNodeHandle(unsafe.Pointer(h)) }
func obj(h ObjectHandle) C.NormObjectHandle      { return C.NormObjectHandle(unsafe.Pointer(h)) }

// --- C memory helpers ---
//
// NORM retains the data buffer passed to DataEnqueue until the object is purged
// (it stores the pointer, it does not copy). Go heap memory cannot be handed to
// C to retain across calls, so callers must place enqueue data in C memory using
// CBytes and release it with CFree once NORM_TX_OBJECT_PURGED is observed.

// CBytes copies b into C-allocated memory and returns the pointer. Free with CFree.
func CBytes(b []byte) unsafe.Pointer {
	if len(b) == 0 {
		return C.malloc(1) // avoid a NULL data pointer for zero-length payloads
	}
	return C.CBytes(b)
}

// CFree releases memory returned by CBytes.
func CFree(p unsafe.Pointer) { C.free(p) }

// --- version ---

func GetVersion() (major, minor, patch int) {
	var maj, min, pat C.int
	C.NormGetVersion(&maj, &min, &pat)
	return int(maj), int(min), int(pat)
}

// --- instance / operation ---

func CreateInstance(priorityBoost bool) InstanceHandle {
	return InstanceHandle(unsafe.Pointer(C.NormCreateInstance(C.bool(priorityBoost))))
}

func DestroyInstance(h InstanceHandle)      { C.NormDestroyInstance(inst(h)) }
func StopInstance(h InstanceHandle)         { C.NormStopInstance(inst(h)) }
func RestartInstance(h InstanceHandle) bool { return bool(C.NormRestartInstance(inst(h))) }
func SuspendInstance(h InstanceHandle) bool { return bool(C.NormSuspendInstance(inst(h))) }
func ResumeInstance(h InstanceHandle)       { C.NormResumeInstance(inst(h)) }

func SetCacheDirectory(h InstanceHandle, path string) bool {
	cs := C.CString(path)
	defer C.free(unsafe.Pointer(cs))
	return bool(C.NormSetCacheDirectory(inst(h), cs))
}

// GetNextEvent blocks for the next event unless waitForEvent is false. The
// returned bool reports whether an event was retrieved.
func GetNextEvent(h InstanceHandle, waitForEvent bool) (Event, bool) {
	var ev C.NormEvent
	ok := bool(C.NormGetNextEvent(inst(h), &ev, C.bool(waitForEvent)))
	if !ok {
		return Event{}, false
	}
	return Event{
		Type:    EventType(ev._type),
		Session: SessionHandle(unsafe.Pointer(ev.session)),
		Sender:  NodeHandle(unsafe.Pointer(ev.sender)),
		Object:  ObjectHandle(unsafe.Pointer(ev.object)),
	}, true
}

// GetDescriptor returns the underlying descriptor (a file descriptor on Unix)
// usable with select()/poll for asynchronous event notification.
func GetDescriptor(h InstanceHandle) int {
	return int(C.NormGetDescriptor(inst(h)))
}

func OpenDebugLog(h InstanceHandle, path string) bool {
	cs := C.CString(path)
	defer C.free(unsafe.Pointer(cs))
	return bool(C.NormOpenDebugLog(inst(h), cs))
}
func CloseDebugLog(h InstanceHandle) { C.NormCloseDebugLog(inst(h)) }
func SetDebugLevel(level uint)       { C.NormSetDebugLevel(C.uint(level)) }
func GetDebugLevel() uint            { return uint(C.NormGetDebugLevel()) }

// --- session creation / control ---

func CreateSession(h InstanceHandle, address string, port uint16, localNodeId NodeId) SessionHandle {
	cs := C.CString(address)
	defer C.free(unsafe.Pointer(cs))
	return SessionHandle(unsafe.Pointer(C.NormCreateSession(inst(h), cs, C.UINT16(port), C.NormNodeId(localNodeId))))
}

func DestroySession(h SessionHandle) { C.NormDestroySession(sess(h)) }
func GetInstance(h SessionHandle) InstanceHandle {
	return InstanceHandle(unsafe.Pointer(C.NormGetInstance(sess(h))))
}
func GetLocalNodeId(h SessionHandle) NodeId { return NodeId(C.NormGetLocalNodeId(sess(h))) }

func IsUnicastAddress(address string) bool {
	cs := C.CString(address)
	defer C.free(unsafe.Pointer(cs))
	return bool(C.NormIsUnicastAddress(cs))
}

func SetTxPort(h SessionHandle, port uint16, enableReuse bool, txBindAddress string) bool {
	var cs *C.char
	if txBindAddress != "" {
		cs = C.CString(txBindAddress)
		defer C.free(unsafe.Pointer(cs))
	}
	return bool(C.NormSetTxPort(sess(h), C.UINT16(port), C.bool(enableReuse), cs))
}

func SetRxPortReuse(h SessionHandle, enable bool) {
	C.NormSetRxPortReuse(sess(h), C.bool(enable), nil, nil, 0)
}

func SetMulticastInterface(h SessionHandle, iface string) bool {
	cs := C.CString(iface)
	defer C.free(unsafe.Pointer(cs))
	return bool(C.NormSetMulticastInterface(sess(h), cs))
}

func SetSSM(h SessionHandle, sourceAddress string) bool {
	cs := C.CString(sourceAddress)
	defer C.free(unsafe.Pointer(cs))
	return bool(C.NormSetSSM(sess(h), cs))
}

func SetTTL(h SessionHandle, ttl uint8) bool    { return bool(C.NormSetTTL(sess(h), C.uchar(ttl))) }
func SetTOS(h SessionHandle, tos uint8) bool    { return bool(C.NormSetTOS(sess(h), C.uchar(tos))) }
func SetLoopback(h SessionHandle, on bool) bool { return bool(C.NormSetLoopback(sess(h), C.bool(on))) }
func SetMulticastLoopback(h SessionHandle, on bool) bool {
	return bool(C.NormSetMulticastLoopback(sess(h), C.bool(on)))
}
func SetEcnSupport(h SessionHandle, enable, ignoreLoss, tolerateLoss bool) {
	C.NormSetEcnSupport(sess(h), C.bool(enable), C.bool(ignoreLoss), C.bool(tolerateLoss))
}
func SetFragmentation(h SessionHandle, on bool) bool {
	return bool(C.NormSetFragmentation(sess(h), C.bool(on)))
}

// --- sender ---

func GetRandomSessionId() SessionId { return SessionId(C.NormGetRandomSessionId()) }

func StartSender(h SessionHandle, sessionId SessionId, bufferSpace uint32, segmentSize, numData, numParity uint16, fecId uint8) bool {
	return bool(C.NormStartSender(sess(h), C.NormSessionId(sessionId), C.UINT32(bufferSpace),
		C.UINT16(segmentSize), C.UINT16(numData), C.UINT16(numParity), C.UINT8(fecId)))
}
func StopSender(h SessionHandle) { C.NormStopSender(sess(h)) }

func SetTxRate(h SessionHandle, bitsPerSecond float64) {
	C.NormSetTxRate(sess(h), C.double(bitsPerSecond))
}
func GetTxRate(h SessionHandle) float64 { return float64(C.NormGetTxRate(sess(h))) }

func SetTxSocketBuffer(h SessionHandle, size uint) bool {
	return bool(C.NormSetTxSocketBuffer(sess(h), C.uint(size)))
}
func SetFlowControl(h SessionHandle, factor float64) {
	C.NormSetFlowControl(sess(h), C.double(factor))
}
func SetCongestionControl(h SessionHandle, enable, adjustRate bool) {
	C.NormSetCongestionControl(sess(h), C.bool(enable), C.bool(adjustRate))
}
func SetTxRateBounds(h SessionHandle, min, max float64) {
	C.NormSetTxRateBounds(sess(h), C.double(min), C.double(max))
}
func SetGrttEstimate(h SessionHandle, grtt float64) {
	C.NormSetGrttEstimate(sess(h), C.double(grtt))
}
func GetGrttEstimate(h SessionHandle) float64 { return float64(C.NormGetGrttEstimate(sess(h))) }
func SetGrttMax(h SessionHandle, grttMax float64) {
	C.NormSetGrttMax(sess(h), C.double(grttMax))
}
func SetGrttProbingMode(h SessionHandle, mode ProbingMode) {
	C.NormSetGrttProbingMode(sess(h), C.NormProbingMode(mode))
}
func SetBackoffFactor(h SessionHandle, factor float64) {
	C.NormSetBackoffFactor(sess(h), C.double(factor))
}
func SetGroupSize(h SessionHandle, size uint) {
	C.NormSetGroupSize(sess(h), C.uint(size))
}
func SetTxRobustFactor(h SessionHandle, factor int) {
	C.NormSetTxRobustFactor(sess(h), C.int(factor))
}
func SetAutoParity(h SessionHandle, autoParity uint8) {
	C.NormSetAutoParity(sess(h), C.uchar(autoParity))
}

// FileEnqueue queues a file for transmission. NORM reads the file itself, so no
// Go/C buffer lifetime concerns apply here.
func FileEnqueue(h SessionHandle, fileName string, info []byte) ObjectHandle {
	cs := C.CString(fileName)
	defer C.free(unsafe.Pointer(cs))
	iptr, ilen := infoArgs(info)
	defer freeInfo(iptr)
	return ObjectHandle(unsafe.Pointer(C.NormFileEnqueue(sess(h), cs, iptr, ilen)))
}

// DataEnqueue queues data for transmission. dataPtr MUST reference C memory (see
// CBytes) that stays valid until NORM_TX_OBJECT_PURGED; NORM retains it. The info
// buffer is copied by NORM during the call, so a Go slice is fine there.
func DataEnqueue(h SessionHandle, dataPtr unsafe.Pointer, dataLen uint32, info []byte) ObjectHandle {
	iptr, ilen := infoArgs(info)
	defer freeInfo(iptr)
	return ObjectHandle(unsafe.Pointer(C.NormDataEnqueue(sess(h), (*C.char)(dataPtr), C.UINT32(dataLen), iptr, ilen)))
}

func StreamOpen(h SessionHandle, bufferSize uint32, info []byte) ObjectHandle {
	iptr, ilen := infoArgs(info)
	defer freeInfo(iptr)
	return ObjectHandle(unsafe.Pointer(C.NormStreamOpen(sess(h), C.UINT32(bufferSize), iptr, ilen)))
}

func RequeueObject(h SessionHandle, o ObjectHandle) bool {
	return bool(C.NormRequeueObject(sess(h), obj(o)))
}

// --- watermark / acking ---

func SetWatermark(h SessionHandle, o ObjectHandle, overrideFlush bool) bool {
	return bool(C.NormSetWatermark(sess(h), obj(o), C.bool(overrideFlush)))
}
func ResetWatermark(h SessionHandle) bool { return bool(C.NormResetWatermark(sess(h))) }
func CancelWatermark(h SessionHandle)     { C.NormCancelWatermark(sess(h)) }
func AddAckingNode(h SessionHandle, id NodeId) bool {
	return bool(C.NormAddAckingNode(sess(h), C.NormNodeId(id)))
}
func RemoveAckingNode(h SessionHandle, id NodeId) {
	C.NormRemoveAckingNode(sess(h), C.NormNodeId(id))
}
func GetAckingStatus(h SessionHandle, id NodeId) AckingStatus {
	return AckingStatus(C.NormGetAckingStatus(sess(h), C.NormNodeId(id)))
}
func SetAutoAckingNodes(h SessionHandle, status TrackingStatus) {
	C.NormSetAutoAckingNodes(sess(h), C.NormTrackingStatus(status))
}

// SendCommand transmits application-defined command content. cmd is copied by
// NORM during the call.
func SendCommand(h SessionHandle, cmd []byte, robust bool) bool {
	var p *C.char
	if len(cmd) > 0 {
		p = (*C.char)(unsafe.Pointer(&cmd[0]))
	}
	return bool(C.NormSendCommand(sess(h), p, C.uint(len(cmd)), C.bool(robust)))
}
func CancelCommand(h SessionHandle) { C.NormCancelCommand(sess(h)) }

// --- stream (sender + receiver) ---

func StreamClose(o ObjectHandle, graceful bool) { C.NormStreamClose(obj(o), C.bool(graceful)) }

// StreamWrite writes data to a send stream, returning the number of bytes
// accepted (which may be less than len(data) under flow control). data is copied
// by NORM during the call.
func StreamWrite(o ObjectHandle, data []byte) uint {
	if len(data) == 0 {
		return 0
	}
	return uint(C.NormStreamWrite(obj(o), (*C.char)(unsafe.Pointer(&data[0])), C.uint(len(data))))
}
func StreamFlush(o ObjectHandle, eom bool, mode FlushMode) {
	C.NormStreamFlush(obj(o), C.bool(eom), C.NormFlushMode(mode))
}
func StreamSetAutoFlush(o ObjectHandle, mode FlushMode) {
	C.NormStreamSetAutoFlush(obj(o), C.NormFlushMode(mode))
}
func StreamSetPushEnable(o ObjectHandle, enable bool) {
	C.NormStreamSetPushEnable(obj(o), C.bool(enable))
}
func StreamHasVacancy(o ObjectHandle) bool { return bool(C.NormStreamHasVacancy(obj(o))) }
func StreamMarkEom(o ObjectHandle)         { C.NormStreamMarkEom(obj(o)) }

// StreamRead reads up to len(buf) bytes from a receive stream into buf. It
// returns the number of bytes read and whether the stream is still in sync
// (false indicates a break was detected; see NormStreamRead semantics).
func StreamRead(o ObjectHandle, buf []byte) (n uint, inSync bool) {
	if len(buf) == 0 {
		return 0, true
	}
	num := C.uint(len(buf))
	ok := bool(C.NormStreamRead(obj(o), (*C.char)(unsafe.Pointer(&buf[0])), &num))
	return uint(num), ok
}
func StreamSeekMsgStart(o ObjectHandle) bool { return bool(C.NormStreamSeekMsgStart(obj(o))) }
func StreamGetReadOffset(o ObjectHandle) uint32 {
	return uint32(C.NormStreamGetReadOffset(obj(o)))
}

// --- receiver ---

func StartReceiver(h SessionHandle, bufferSpace uint32) bool {
	return bool(C.NormStartReceiver(sess(h), C.UINT32(bufferSpace)))
}
func StopReceiver(h SessionHandle) { C.NormStopReceiver(sess(h)) }

func SetRxCacheLimit(h SessionHandle, countMax uint16) {
	C.NormSetRxCacheLimit(sess(h), C.ushort(countMax))
}
func SetRxSocketBuffer(h SessionHandle, size uint) bool {
	return bool(C.NormSetRxSocketBuffer(sess(h), C.uint(size)))
}
func SetSilentReceiver(h SessionHandle, silent bool, maxDelay int) {
	C.NormSetSilentReceiver(sess(h), C.bool(silent), C.int(maxDelay))
}
func SetDefaultUnicastNack(h SessionHandle, on bool) {
	C.NormSetDefaultUnicastNack(sess(h), C.bool(on))
}
func SetDefaultSyncPolicy(h SessionHandle, policy SyncPolicy) {
	C.NormSetDefaultSyncPolicy(sess(h), C.NormSyncPolicy(policy))
}
func SetDefaultNackingMode(h SessionHandle, mode NackingMode) {
	C.NormSetDefaultNackingMode(sess(h), C.NormNackingMode(mode))
}
func SetDefaultRepairBoundary(h SessionHandle, boundary RepairBoundary) {
	C.NormSetDefaultRepairBoundary(sess(h), C.NormRepairBoundary(boundary))
}
func SetDefaultRxRobustFactor(h SessionHandle, factor int) {
	C.NormSetDefaultRxRobustFactor(sess(h), C.int(factor))
}

// --- object ---

func ObjectGetType(o ObjectHandle) ObjectType { return ObjectType(C.NormObjectGetType(obj(o))) }
func ObjectHasInfo(o ObjectHandle) bool       { return bool(C.NormObjectHasInfo(obj(o))) }
func ObjectGetInfoLength(o ObjectHandle) uint16 {
	return uint16(C.NormObjectGetInfoLength(obj(o)))
}

// ObjectGetInfo returns the object's info (NORM_INFO) content, copied into Go memory.
func ObjectGetInfo(o ObjectHandle) []byte {
	n := C.NormObjectGetInfoLength(obj(o))
	if n == 0 {
		return nil
	}
	buf := make([]byte, int(n))
	got := C.NormObjectGetInfo(obj(o), (*C.char)(unsafe.Pointer(&buf[0])), C.UINT16(n))
	return buf[:int(got)]
}

func ObjectGetSize(o ObjectHandle) int64 { return int64(C.NormObjectGetSize(obj(o))) }
func ObjectGetBytesPending(o ObjectHandle) int64 {
	return int64(C.NormObjectGetBytesPending(obj(o)))
}
func ObjectCancel(o ObjectHandle)  { C.NormObjectCancel(obj(o)) }
func ObjectRetain(o ObjectHandle)  { C.NormObjectRetain(obj(o)) }
func ObjectRelease(o ObjectHandle) { C.NormObjectRelease(obj(o)) }
func ObjectGetSender(o ObjectHandle) NodeHandle {
	return NodeHandle(unsafe.Pointer(C.NormObjectGetSender(obj(o))))
}

// DataAccessData returns a pointer into NORM-managed memory holding a received
// data object's payload. The caller must copy out before the object is released.
// Use ObjectGetSize for the length.
func DataAccessData(o ObjectHandle) unsafe.Pointer {
	return unsafe.Pointer(C.NormDataAccessData(obj(o)))
}

// DataDetachData transfers ownership of a data object's buffer to the caller
// (NORM will no longer free it). Used on the sender side at NORM_TX_OBJECT_PURGED
// to reclaim the CBytes buffer, and on the receiver side to keep received data.
func DataDetachData(o ObjectHandle) unsafe.Pointer {
	return unsafe.Pointer(C.NormDataDetachData(obj(o)))
}

func FileGetName(o ObjectHandle) string {
	const maxPath = 4096
	buf := make([]byte, maxPath)
	if !bool(C.NormFileGetName(obj(o), (*C.char)(unsafe.Pointer(&buf[0])), C.uint(maxPath))) {
		return ""
	}
	return C.GoString((*C.char)(unsafe.Pointer(&buf[0])))
}
func FileRename(o ObjectHandle, name string) bool {
	cs := C.CString(name)
	defer C.free(unsafe.Pointer(cs))
	return bool(C.NormFileRename(obj(o), cs))
}

// --- node ---

func NodeGetId(n NodeHandle) NodeId { return NodeId(C.NormNodeGetId(node(n))) }

// NodeGetAddress returns the node's source address as a string and its port.
func NodeGetAddress(n NodeHandle) (addr string, port uint16, ok bool) {
	const maxAddr = 64
	buf := make([]byte, maxAddr)
	blen := C.uint(maxAddr)
	var p C.UINT16
	if !bool(C.NormNodeGetAddress(node(n), (*C.char)(unsafe.Pointer(&buf[0])), &blen, &p)) {
		return "", 0, false
	}
	return C.GoString((*C.char)(unsafe.Pointer(&buf[0]))), uint16(p), true
}

func NodeGetGrtt(n NodeHandle) float64 { return float64(C.NormNodeGetGrtt(node(n))) }
func NodeSetUnicastNack(n NodeHandle, on bool) {
	C.NormNodeSetUnicastNack(node(n), C.bool(on))
}
func NodeSetNackingMode(n NodeHandle, mode NackingMode) {
	C.NormNodeSetNackingMode(node(n), C.NormNackingMode(mode))
}
func NodeFreeBuffers(n NodeHandle) { C.NormNodeFreeBuffers(node(n)) }
func NodeRetain(n NodeHandle)      { C.NormNodeRetain(node(n)) }
func NodeRelease(n NodeHandle)     { C.NormNodeRelease(node(n)) }

// --- info argument helper ---
//
// Several enqueue/open calls take an optional (infoPtr, infoLen). NORM copies the
// info content during the call, so C memory allocated here is freed immediately
// by the caller's defer.

func infoArgs(info []byte) (*C.char, C.uint) {
	if len(info) == 0 {
		return nil, 0
	}
	return (*C.char)(C.CBytes(info)), C.uint(len(info))
}

func freeInfo(p *C.char) {
	if p != nil {
		C.free(unsafe.Pointer(p))
	}
}
