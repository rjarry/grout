// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Robin Jarry

package grout

import (
	"encoding/binary"
	"net/netip"
)

func getU8(b []byte, off int) uint8 {
	return b[off]
}

func putU8(b []byte, off int, v uint8) {
	b[off] = v
}

func getU16(b []byte, off int) uint16 {
	return binary.NativeEndian.Uint16(b[off:])
}

func putU16(b []byte, off int, v uint16) {
	binary.NativeEndian.PutUint16(b[off:], v)
}

func getU32(b []byte, off int) uint32 {
	return binary.NativeEndian.Uint32(b[off:])
}

func putU32(b []byte, off int, v uint32) {
	binary.NativeEndian.PutUint32(b[off:], v)
}

func getU64(b []byte, off int) uint64 {
	return binary.NativeEndian.Uint64(b[off:])
}

func putU64(b []byte, off int, v uint64) {
	binary.NativeEndian.PutUint64(b[off:], v)
}

func getBool(b []byte, off int) bool {
	return b[off] != 0
}

func putBool(b []byte, off int, v bool) {
	if v {
		b[off] = 1
	} else {
		b[off] = 0
	}
}

func getString(b []byte, off, maxLen int) string {
	s := b[off : off+maxLen]
	for i, c := range s {
		if c == 0 {
			return string(s[:i])
		}
	}
	return string(s)
}

func putString(b []byte, off, maxLen int, s string) {
	dst := b[off : off+maxLen]
	for i := range dst {
		dst[i] = 0
	}
	copy(dst, s)
}

func getIPv4(b []byte, off int) netip.Addr {
	return netip.AddrFrom4([4]byte(b[off : off+4]))
}

func putIPv4(b []byte, off int, addr netip.Addr) {
	a4 := addr.As4()
	copy(b[off:], a4[:])
}

func getIPv6(b []byte, off int) netip.Addr {
	return netip.AddrFrom16([16]byte(b[off : off+16]))
}

func putIPv6(b []byte, off int, addr netip.Addr) {
	a16 := addr.As16()
	copy(b[off:], a16[:])
}

func getEther(b []byte, off int) EtherAddr {
	var a EtherAddr
	copy(a[:], b[off:off+6])
	return a
}

func putEther(b []byte, off int, a EtherAddr) {
	copy(b[off:], a[:])
}

func getIPv4Net(b []byte, ipOff, prefixOff int) IPv4Net {
	return IPv4Net{
		IP:        getIPv4(b, ipOff),
		PrefixLen: getU8(b, prefixOff),
	}
}

func putIPv4Net(b []byte, ipOff, prefixOff int, n IPv4Net) {
	putIPv4(b, ipOff, n.IP)
	putU8(b, prefixOff, n.PrefixLen)
}

func getIPv6Net(b []byte, ipOff, prefixOff int) IPv6Net {
	return IPv6Net{
		IP:        getIPv6(b, ipOff),
		PrefixLen: getU8(b, prefixOff),
	}
}

func putIPv6Net(b []byte, ipOff, prefixOff int, n IPv6Net) {
	putIPv6(b, ipOff, n.IP)
	putU8(b, prefixOff, n.PrefixLen)
}

func getCPUSet(b []byte, off int) CPUSet {
	var s CPUSet
	copy(s[:], b[off:off+len(s)])
	return s
}

func putCPUSet(b []byte, off int, s CPUSet) {
	copy(b[off:], s[:])
}

// l3_addr is a discriminated union: uint8 af at offset 0, then either
// 4 bytes IPv4 at offset 4 or 16 bytes IPv6 at offset 4.
// Total wire size: 20 bytes (1 af + 3 pad + 16 ipv6).
func getL3Addr(b []byte, off int) netip.Addr {
	af := b[off]
	switch af {
	case 2: // AF_INET
		return netip.AddrFrom4([4]byte(b[off+4 : off+8]))
	case 10: // AF_INET6
		return netip.AddrFrom16([16]byte(b[off+4 : off+20]))
	}
	return netip.Addr{}
}

func putL3Addr(b []byte, off int, addr netip.Addr) {
	switch {
	case addr.Is4():
		b[off] = 2 // AF_INET
		a4 := addr.As4()
		copy(b[off+4:], a4[:])
	case addr.Is6():
		b[off] = 10 // AF_INET6
		a16 := addr.As16()
		copy(b[off+4:], a16[:])
	}
}

type marshaler interface {
	marshal() []byte
}

type unmarshaler interface {
	unmarshal([]byte) error
}
