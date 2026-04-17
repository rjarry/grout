// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Robin Jarry

package grout

import (
	"errors"
	"fmt"
	"syscall"
)

// Error is an API error with the underlying errno value.
type Error struct {
	Errno syscall.Errno
	Op    string
}

func (e *Error) Error() string {
	return fmt.Sprintf("%s: %v", e.Op, e.Errno)
}

func (e *Error) Unwrap() error {
	return e.Errno
}

func (e *Error) Is(target error) bool {
	var errno syscall.Errno
	if errors.As(target, &errno) {
		return e.Errno == errno
	}
	return false
}

func apiError(op string, ret int) error {
	return &Error{Errno: syscall.Errno(-ret), Op: op}
}
