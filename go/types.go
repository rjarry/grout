// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Robin Jarry

package grout

import (
	"fmt"
	"net"
	"net/netip"
)

// EtherAddr is an Ethernet MAC address.
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

// IPv4Net is an IPv4 network prefix.
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

// IPv6Net is an IPv6 network prefix.
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
