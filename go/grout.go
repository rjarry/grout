// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Robin Jarry

// Package grout provides a Go client library for the grout network router API.
//
// The client communicates with the grout daemon over a UNIX socket using a
// binary protocol. It wraps the C client implementation via CGO.
//
// A Client is NOT safe for concurrent use. Use one Client per goroutine.
//
// Building this package requires the following environment variable:
//
//	CGO_CFLAGS_ALLOW="-fms-extensions"
package grout

/*
#cgo CFLAGS: -std=gnu2x -fms-extensions -Wno-microsoft -I${SRCDIR}
#include "shim.h"
*/
import "C"

import (
	"net/netip"
	"syscall"
	"unsafe"
)

const DefaultSockPath = C.GR_DEFAULT_SOCK_PATH

// Client is a connection to the grout API server.
type Client struct {
	c *C.struct_gr_api_client
}

// Connect creates a new client connection. If sockPath is empty, the
// default path (/run/grout.sock) is used.
func Connect(sockPath string) (*Client, error) {
	if sockPath == "" {
		sockPath = DefaultSockPath
	}
	cs := C.CString(sockPath)
	defer C.free(unsafe.Pointer(cs))

	c, err := C.gr_api_client_connect(cs)
	if c == nil {
		return nil, &Error{Errno: errnoFrom(err), Op: "connect"}
	}
	return &Client{c: c}, nil
}

// Close disconnects from the API server.
func (c *Client) Close() error {
	if c.c == nil {
		return nil
	}
	ret, _ := C.gr_api_client_disconnect(c.c)
	c.c = nil
	if ret < 0 {
		return apiError("close", int(ret))
	}
	return nil
}

func (c *Client) sendRecv(reqType C.uint32_t, txLen C.size_t, txData unsafe.Pointer) (unsafe.Pointer, error) {
	id, err := C.gr_api_client_send(c.c, reqType, txLen, txData)
	if id < 0 {
		return nil, &Error{Errno: errnoFrom(err), Op: "send"}
	}
	var ret C.int
	ptr := C.grout_recv(c.c, reqType, C.uint32_t(id), &ret)
	if ret < 0 {
		return nil, apiError("recv", int(ret))
	}
	return ptr, nil
}

func (c *Client) sendOnly(reqType C.uint32_t, txLen C.size_t, txData unsafe.Pointer) error {
	ptr, err := c.sendRecv(reqType, txLen, txData)
	C.free(ptr)
	return err
}

func (c *Client) streamForEach(
	reqType C.uint32_t,
	txLen C.size_t,
	txData unsafe.Pointer,
	fn func(ptr unsafe.Pointer) error,
) error {
	id, err := C.gr_api_client_send(c.c, reqType, txLen, txData)
	if id < 0 {
		return &Error{Errno: errnoFrom(err), Op: "send"}
	}
	for {
		var ret C.int
		ptr := C.grout_recv(c.c, reqType, C.uint32_t(id), &ret)
		if ret < 0 {
			return apiError("recv", int(ret))
		}
		if ptr == nil {
			break
		}
		err := fn(ptr)
		C.free(ptr)
		if err != nil {
			C.__gr_api_client_stream_drain(c.c, reqType, C.uint32_t(id))
			return err
		}
	}
	return nil
}

// helper to convert C errno to syscall.Errno
func errnoFrom(err error) syscall.Errno {
	if err != nil {
		if errno, ok := err.(syscall.Errno); ok {
			return errno
		}
	}
	return syscall.EINVAL
}

func ip4FromC(addr C.ip4_addr_t) netip.Addr {
	b := (*[4]byte)(unsafe.Pointer(&addr))
	return netip.AddrFrom4(*b)
}

func ip4ToC(addr netip.Addr) C.ip4_addr_t {
	b := addr.As4()
	return *(*C.ip4_addr_t)(unsafe.Pointer(&b))
}

func ip4NetFromC(net *C.struct_ip4_net) IPv4Net {
	return IPv4Net{
		IP:        ip4FromC(net.ip),
		PrefixLen: uint8(net.prefixlen),
	}
}

func ip4NetToC(n IPv4Net) C.struct_ip4_net {
	return C.struct_ip4_net{
		ip:        ip4ToC(n.IP),
		prefixlen: C.uint8_t(n.PrefixLen),
	}
}

func ip6FromC(addr *C.struct_rte_ipv6_addr) netip.Addr {
	b := (*[16]byte)(unsafe.Pointer(&addr.a))
	return netip.AddrFrom16(*b)
}

func ip6ToC(addr netip.Addr) C.struct_rte_ipv6_addr {
	b := addr.As16()
	var c C.struct_rte_ipv6_addr
	copy((*[16]byte)(unsafe.Pointer(&c.a))[:], b[:])
	return c
}

func ip6NetFromC(net *C.struct_ip6_net) IPv6Net {
	return IPv6Net{
		IP:        ip6FromC(&net.ip),
		PrefixLen: uint8(net.prefixlen),
	}
}

func ip6NetToC(n IPv6Net) C.struct_ip6_net {
	return C.struct_ip6_net{
		ip:        ip6ToC(n.IP),
		prefixlen: C.uint8_t(n.PrefixLen),
	}
}

func etherFromC(addr *C.struct_rte_ether_addr) EtherAddr {
	var a EtherAddr
	copy(a[:], (*[6]byte)(unsafe.Pointer(&addr.addr_bytes))[:])
	return a
}

func etherToC(addr EtherAddr) C.struct_rte_ether_addr {
	var c C.struct_rte_ether_addr
	copy((*[6]byte)(unsafe.Pointer(&c.addr_bytes))[:], addr[:])
	return c
}
