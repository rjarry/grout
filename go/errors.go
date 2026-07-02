// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Robin Jarry

package grout

import (
	"fmt"
	"syscall"
)

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
	if t, ok := target.(*Error); ok {
		return e.Errno == t.Errno
	}
	return false
}
