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

// IPv4IfAddr is an IPv4 address assigned to an interface.
type IPv4IfAddr struct {
	IfaceID uint16
	Addr    IPv4Net
}

// IPv4Route is an IPv4 routing table entry.
type IPv4Route struct {
	Dest   IPv4Net
	VRFID  uint16
	Origin NHOrigin
	NH     Nexthop
}

// IPv4RouteAdd adds an IPv4 route.
func (c *Client) IPv4RouteAdd(vrfID uint16, dest IPv4Net, nh netip.Addr, nhID uint32, origin NHOrigin, existOK bool) error {
	var req C.struct_gr_ip4_route_add_req
	req.vrf_id = C.uint16_t(vrfID)
	req.dest = ip4NetToC(dest)
	req.nh = ip4ToC(nh)
	req.nh_id = C.uint32_t(nhID)
	req.origin = C.gr_nh_origin_t(origin)
	req.exist_ok = boolToC(existOK)
	return c.sendOnly(C.GR_IP4_ROUTE_ADD, C.size_t(unsafe.Sizeof(req)), unsafe.Pointer(&req))
}

// IPv4RouteDel deletes an IPv4 route.
func (c *Client) IPv4RouteDel(vrfID uint16, dest IPv4Net, missingOK bool) error {
	var req C.struct_gr_ip4_route_del_req
	req.vrf_id = C.uint16_t(vrfID)
	req.dest = ip4NetToC(dest)
	req.missing_ok = boolToC(missingOK)
	return c.sendOnly(C.GR_IP4_ROUTE_DEL, C.size_t(unsafe.Sizeof(req)), unsafe.Pointer(&req))
}

// IPv4RouteGet looks up a route by destination (longest prefix match).
func (c *Client) IPv4RouteGet(vrfID uint16, dest netip.Addr) (*Nexthop, error) {
	var req C.struct_gr_ip4_route_get_req
	req.vrf_id = C.uint16_t(vrfID)
	req.dest = ip4ToC(dest)
	ptr, err := c.sendRecv(C.GR_IP4_ROUTE_GET, C.size_t(unsafe.Sizeof(req)), unsafe.Pointer(&req))
	if err != nil {
		return nil, err
	}
	defer C.free(ptr)
	resp := (*C.struct_gr_ip4_route_get_resp)(ptr)
	return nexthopFromC(&resp.nh), nil
}

// IPv4RouteList lists all IPv4 routes in a VRF.
func (c *Client) IPv4RouteList(vrfID uint16, maxCount uint16) ([]IPv4Route, error) {
	var req C.struct_gr_ip4_route_list_req
	req.vrf_id = C.uint16_t(vrfID)
	req.max_count = C.uint16_t(maxCount)

	var result []IPv4Route
	err := c.streamForEach(
		C.GR_IP4_ROUTE_LIST,
		C.size_t(unsafe.Sizeof(req)),
		unsafe.Pointer(&req),
		func(ptr unsafe.Pointer) error {
			r := (*C.struct_gr_ip4_route)(ptr)
			result = append(result, IPv4Route{
				Dest:   ip4NetFromC(&r.dest),
				VRFID:  uint16(r.vrf_id),
				Origin: NHOrigin(r.origin),
				NH:     *nexthopFromC(&r.nh),
			})
			return nil
		},
	)
	return result, err
}

// IPv4AddrAdd adds an IPv4 address to an interface.
func (c *Client) IPv4AddrAdd(ifaceID uint16, addr IPv4Net, existOK bool) error {
	var req C.struct_gr_ip4_addr_add_req
	req.addr.iface_id = C.uint16_t(ifaceID)
	req.addr.addr = ip4NetToC(addr)
	req.exist_ok = boolToC(existOK)
	return c.sendOnly(C.GR_IP4_ADDR_ADD, C.size_t(unsafe.Sizeof(req)), unsafe.Pointer(&req))
}

// IPv4AddrDel removes an IPv4 address from an interface.
func (c *Client) IPv4AddrDel(ifaceID uint16, addr IPv4Net, missingOK bool) error {
	var req C.struct_gr_ip4_addr_del_req
	req.addr.iface_id = C.uint16_t(ifaceID)
	req.addr.addr = ip4NetToC(addr)
	req.missing_ok = boolToC(missingOK)
	return c.sendOnly(C.GR_IP4_ADDR_DEL, C.size_t(unsafe.Sizeof(req)), unsafe.Pointer(&req))
}

// IPv4AddrList lists IPv4 addresses on interfaces.
func (c *Client) IPv4AddrList(vrfID, ifaceID uint16) ([]IPv4IfAddr, error) {
	var req C.struct_gr_ip4_addr_list_req
	req.vrf_id = C.uint16_t(vrfID)
	req.iface_id = C.uint16_t(ifaceID)

	var result []IPv4IfAddr
	err := c.streamForEach(
		C.GR_IP4_ADDR_LIST,
		C.size_t(unsafe.Sizeof(req)),
		unsafe.Pointer(&req),
		func(ptr unsafe.Pointer) error {
			a := (*C.struct_gr_ip4_ifaddr)(ptr)
			result = append(result, IPv4IfAddr{
				IfaceID: uint16(a.iface_id),
				Addr:    ip4NetFromC(&a.addr),
			})
			return nil
		},
	)
	return result, err
}

// IPv4AddrFlush removes all IPv4 addresses from an interface.
func (c *Client) IPv4AddrFlush(ifaceID uint16) error {
	var req C.struct_gr_ip4_addr_flush_req
	req.iface_id = C.uint16_t(ifaceID)
	return c.sendOnly(C.GR_IP4_ADDR_FLUSH, C.size_t(unsafe.Sizeof(req)), unsafe.Pointer(&req))
}

// FIB4Info contains FIB status for a VRF.
type FIB4Info struct {
	VRFID      uint16
	MaxRoutes  uint32
	UsedRoutes uint32
	NumTbl8    uint32
	UsedTbl8   uint32
}

// IPv4FIBInfoList lists FIB info for VRFs.
func (c *Client) IPv4FIBInfoList(vrfID uint16) ([]FIB4Info, error) {
	var req C.struct_gr_ip4_fib_info_list_req
	req.vrf_id = C.uint16_t(vrfID)

	var result []FIB4Info
	err := c.streamForEach(
		C.GR_IP4_FIB_INFO_LIST,
		C.size_t(unsafe.Sizeof(req)),
		unsafe.Pointer(&req),
		func(ptr unsafe.Pointer) error {
			f := (*C.struct_gr_fib4_info)(ptr)
			result = append(result, FIB4Info{
				VRFID:      uint16(f.vrf_id),
				MaxRoutes:  uint32(f.max_routes),
				UsedRoutes: uint32(f.used_routes),
				NumTbl8:    uint32(f.num_tbl8),
				UsedTbl8:   uint32(f.used_tbl8),
			})
			return nil
		},
	)
	return result, err
}
