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

// DNAT44Policy is a stateless DNAT44 translation policy.
type DNAT44Policy struct {
	IfaceID uint16
	Match   netip.Addr
	Replace netip.Addr
}

// DNAT44Add adds a stateless DNAT44 policy.
func (c *Client) DNAT44Add(policy DNAT44Policy, existOK bool) error {
	var req C.struct_gr_dnat44_add_req
	req.policy.iface_id = C.uint16_t(policy.IfaceID)
	req.policy.match = ip4ToC(policy.Match)
	req.policy.replace = ip4ToC(policy.Replace)
	req.exist_ok = C.bool(existOK)
	return c.sendOnly(C.GR_DNAT44_ADD, C.size_t(unsafe.Sizeof(req)), unsafe.Pointer(&req))
}

// DNAT44Del deletes a DNAT44 policy.
func (c *Client) DNAT44Del(ifaceID uint16, match netip.Addr, missingOK bool) error {
	var req C.struct_gr_dnat44_del_req
	req.iface_id = C.uint16_t(ifaceID)
	req.match = ip4ToC(match)
	req.missing_ok = C.bool(missingOK)
	return c.sendOnly(C.GR_DNAT44_DEL, C.size_t(unsafe.Sizeof(req)), unsafe.Pointer(&req))
}

// DNAT44List lists DNAT44 policies.
func (c *Client) DNAT44List(vrfID uint16) ([]DNAT44Policy, error) {
	var req C.struct_gr_dnat44_list_req
	req.vrf_id = C.uint16_t(vrfID)

	var result []DNAT44Policy
	err := c.streamForEach(
		C.GR_DNAT44_LIST,
		C.size_t(unsafe.Sizeof(req)),
		unsafe.Pointer(&req),
		func(ptr unsafe.Pointer) error {
			p := (*C.struct_gr_dnat44_policy)(ptr)
			result = append(result, DNAT44Policy{
				IfaceID: uint16(p.iface_id),
				Match:   ip4FromC(p.match),
				Replace: ip4FromC(p.replace),
			})
			return nil
		},
	)
	return result, err
}

// SNAT44Policy is a stateful SNAT44 translation policy.
type SNAT44Policy struct {
	IfaceID uint16
	Net     IPv4Net
	Replace netip.Addr
}

// SNAT44Add adds a SNAT44 policy.
func (c *Client) SNAT44Add(policy SNAT44Policy, existOK bool) error {
	var req C.struct_gr_snat44_add_req
	req.policy.iface_id = C.uint16_t(policy.IfaceID)
	req.policy.net = ip4NetToC(policy.Net)
	req.policy.replace = ip4ToC(policy.Replace)
	req.exist_ok = C.bool(existOK)
	return c.sendOnly(C.GR_SNAT44_ADD, C.size_t(unsafe.Sizeof(req)), unsafe.Pointer(&req))
}

// SNAT44Del deletes a SNAT44 policy.
func (c *Client) SNAT44Del(policy SNAT44Policy, missingOK bool) error {
	var req C.struct_gr_snat44_del_req
	req.policy.iface_id = C.uint16_t(policy.IfaceID)
	req.policy.net = ip4NetToC(policy.Net)
	req.policy.replace = ip4ToC(policy.Replace)
	req.missing_ok = C.bool(missingOK)
	return c.sendOnly(C.GR_SNAT44_DEL, C.size_t(unsafe.Sizeof(req)), unsafe.Pointer(&req))
}

// SNAT44List lists all SNAT44 policies.
func (c *Client) SNAT44List() ([]SNAT44Policy, error) {
	var req C.struct_gr_empty

	var result []SNAT44Policy
	err := c.streamForEach(
		C.GR_SNAT44_LIST,
		C.size_t(unsafe.Sizeof(req)),
		unsafe.Pointer(&req),
		func(ptr unsafe.Pointer) error {
			p := (*C.struct_gr_snat44_policy)(ptr)
			result = append(result, SNAT44Policy{
				IfaceID: uint16(p.iface_id),
				Net:     ip4NetFromC(&p.net),
				Replace: ip4FromC(p.replace),
			})
			return nil
		},
	)
	return result, err
}
