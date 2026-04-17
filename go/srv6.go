// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Robin Jarry

package grout

/*
#include "shim.h"
*/
import "C"

import (
	"net/netip"
	"unsafe"
)

// SRv6TunSrcSet sets the global SRv6 tunnel source address.
func (c *Client) SRv6TunSrcSet(addr netip.Addr) error {
	var req C.struct_gr_srv6_tunsrc_set_req
	req.addr = ip6ToC(addr)
	return c.sendOnly(C.GR_SRV6_TUNSRC_SET, C.size_t(unsafe.Sizeof(req)), unsafe.Pointer(&req))
}

// SRv6TunSrcClear clears the global SRv6 tunnel source address.
func (c *Client) SRv6TunSrcClear() error {
	var req C.struct_gr_empty
	return c.sendOnly(C.GR_SRV6_TUNSRC_CLEAR, C.size_t(unsafe.Sizeof(req)), unsafe.Pointer(&req))
}

// SRv6TunSrcShow returns the current SRv6 tunnel source address.
func (c *Client) SRv6TunSrcShow() (netip.Addr, error) {
	var req C.struct_gr_empty
	ptr, err := c.sendRecv(C.GR_SRV6_TUNSRC_SHOW, C.size_t(unsafe.Sizeof(req)), unsafe.Pointer(&req))
	if err != nil {
		return netip.Addr{}, err
	}
	defer C.free(ptr)
	resp := (*C.struct_gr_srv6_tunsrc_show_resp)(ptr)
	return ip6FromC(&resp.addr), nil
}
