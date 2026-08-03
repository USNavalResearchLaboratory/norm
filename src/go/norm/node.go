package norm

import "github.com/USNavalResearchLaboratory/norm/src/go/normsys"

// Node represents a NORM participant (typically a remote sender). Like Object,
// nodes delivered via events are unowned borrows; call Retain to keep one.
type Node struct {
	handle normsys.NodeHandle
	owned  bool
}

// Handle exposes the raw normsys handle for use with the low-level package.
func (n *Node) Handle() normsys.NodeHandle { return n.handle }

// Id returns the node's NORM node id.
func (n *Node) Id() NodeId { return normsys.NodeGetId(n.handle) }

// Address returns the node's source address and port.
func (n *Node) Address() (addr string, port uint16, ok bool) {
	return normsys.NodeGetAddress(n.handle)
}

// Grtt returns the node's estimated group round-trip time in seconds.
func (n *Node) Grtt() float64 { return normsys.NodeGetGrtt(n.handle) }

// SetUnicastNack controls whether repair requests to this sender are unicast.
func (n *Node) SetUnicastNack(on bool) { normsys.NodeSetUnicastNack(n.handle, on) }

// SetNackingMode sets the repair (NACK) behavior for this sender.
func (n *Node) SetNackingMode(mode NackingMode) { normsys.NodeSetNackingMode(n.handle, mode) }

// FreeBuffers releases buffers NORM holds for this sender.
func (n *Node) FreeBuffers() { normsys.NodeFreeBuffers(n.handle) }

// Retain increments the node reference count and returns an owned wrapper.
func (n *Node) Retain() *Node {
	normsys.NodeRetain(n.handle)
	return &Node{handle: n.handle, owned: true}
}

// Close releases the node reference if this wrapper owns it.
func (n *Node) Close() {
	if n.owned && n.handle != normsys.NodeInvalid {
		normsys.NodeRelease(n.handle)
		n.handle = normsys.NodeInvalid
	}
}
