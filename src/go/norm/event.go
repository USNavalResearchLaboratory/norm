package norm

import "github.com/USNavalResearchLaboratory/norm/src/go/normsys"

// Event is a NORM protocol event delivered by Instance.NextEvent / Events.
//
// The Sender and Object handles it carries are borrowed (unowned): they are
// valid only for the handling of this event and must NOT be released. Wrap them
// with Session.ObjectFrom / NodeFrom (which create unowned wrappers) to inspect
// them; to keep a received object beyond the event, call Object.Retain.
type Event struct {
	Type    EventType
	session normsys.SessionHandle
	sender  normsys.NodeHandle
	object  normsys.ObjectHandle
}

func eventFromRaw(raw normsys.Event) Event {
	return Event{
		Type:    raw.Type,
		session: raw.Session,
		sender:  raw.Sender,
		object:  raw.Object,
	}
}

// Object returns an (unowned) wrapper for the object this event refers to, or
// nil if the event carries no object.
func (e Event) Object() *Object {
	if e.object == normsys.ObjectInvalid {
		return nil
	}
	return &Object{handle: e.object, owned: false}
}

// Sender returns an (unowned) wrapper for the remote sender node this event
// refers to, or nil if the event carries no node.
func (e Event) Sender() *Node {
	if e.sender == normsys.NodeInvalid {
		return nil
	}
	return &Node{handle: e.sender, owned: false}
}

// String returns the event type name.
func (e Event) String() string { return EventTypeString(e.Type) }
