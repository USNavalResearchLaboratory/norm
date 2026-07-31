package norm

import "errors"

// Common errors returned by the norm package. Operation-specific failures wrap
// these with context via fmt.Errorf.
var (
	// ErrOperationFailed indicates a NORM API call returned false/failure.
	ErrOperationFailed = errors.New("norm: operation failed")
	// ErrInvalidHandle indicates NORM returned an invalid (NULL) handle.
	ErrInvalidHandle = errors.New("norm: invalid handle")
	// ErrWrongObjectType indicates an operation was attempted on an object of
	// the wrong type (e.g. a stream operation on a data object).
	ErrWrongObjectType = errors.New("norm: wrong object type")
)
