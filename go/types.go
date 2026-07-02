// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Robin Jarry

package grout

import (
	"fmt"
	"net"
	"net/netip"
	"time"

	"golang.org/x/sys/unix"
)

// ClockNS is a nanosecond timestamp from CLOCK_MONOTONIC, matching
// gr_clock_ns_t in C. Use ClockNow() to get the current value and
// compare with timestamps returned by the API (e.g. FDBEntry.LastSeen).
type ClockNS = int64

func ClockNow() ClockNS {
	var ts unix.Timespec
	_ = unix.ClockGettime(unix.CLOCK_MONOTONIC, &ts)
	return ts.Nano()
}

func ClockSince(ns ClockNS) time.Duration {
	return time.Duration(ClockNow() - ns)
}

// CPUSet represents a Linux cpu_set_t (1024 bits = 128 bytes).
type CPUSet [128]byte

func (s CPUSet) IsSet(cpu int) bool {
	if cpu < 0 || cpu >= 1024 {
		return false
	}
	return s[cpu/8]&(1<<(cpu%8)) != 0
}

func (s *CPUSet) Set(cpu int) {
	if cpu >= 0 && cpu < 1024 {
		s[cpu/8] |= 1 << (cpu % 8)
	}
}

func (s *CPUSet) Clear(cpu int) {
	if cpu >= 0 && cpu < 1024 {
		s[cpu/8] &^= 1 << (cpu % 8)
	}
}

type EtherAddr [6]byte

func (a EtherAddr) String() string {
	return fmt.Sprintf("%02x:%02x:%02x:%02x:%02x:%02x",
		a[0], a[1], a[2], a[3], a[4], a[5])
}

func (a EtherAddr) HardwareAddr() net.HardwareAddr {
	return net.HardwareAddr(a[:])
}

func EtherAddrFrom(hw net.HardwareAddr) EtherAddr {
	var a EtherAddr
	copy(a[:], hw)
	return a
}

type IPv4Net struct {
	IP        netip.Addr
	PrefixLen uint8
}

func (n IPv4Net) Prefix() netip.Prefix {
	return netip.PrefixFrom(n.IP, int(n.PrefixLen))
}

func (n IPv4Net) String() string {
	return n.Prefix().String()
}

func IPv4NetFrom(p netip.Prefix) IPv4Net {
	return IPv4Net{IP: p.Addr(), PrefixLen: uint8(p.Bits())}
}

type IPv6Net struct {
	IP        netip.Addr
	PrefixLen uint8
}

func (n IPv6Net) Prefix() netip.Prefix {
	return netip.PrefixFrom(n.IP, int(n.PrefixLen))
}

func (n IPv6Net) String() string {
	return n.Prefix().String()
}

func IPv6NetFrom(p netip.Prefix) IPv6Net {
	return IPv6Net{IP: p.Addr(), PrefixLen: uint8(p.Bits())}
}
