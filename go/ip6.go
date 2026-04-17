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

// IPv6IfAddr is an IPv6 address assigned to an interface.
type IPv6IfAddr struct {
	IfaceID uint16
	Addr    IPv6Net
}

// IPv6Route is an IPv6 routing table entry.
type IPv6Route struct {
	Dest   IPv6Net
	VRFID  uint16
	Origin NHOrigin
	NH     Nexthop
}

// IPv6RouteAdd adds an IPv6 route.
func (c *Client) IPv6RouteAdd(vrfID uint16, dest IPv6Net, nh netip.Addr, nhID uint32, origin NHOrigin, existOK bool) error {
	var req C.struct_gr_ip6_route_add_req
	req.vrf_id = C.uint16_t(vrfID)
	req.dest = ip6NetToC(dest)
	req.nh = ip6ToC(nh)
	req.nh_id = C.uint32_t(nhID)
	req.origin = C.gr_nh_origin_t(origin)
	req.exist_ok = boolToC(existOK)
	return c.sendOnly(C.GR_IP6_ROUTE_ADD, C.size_t(unsafe.Sizeof(req)), unsafe.Pointer(&req))
}

// IPv6RouteDel deletes an IPv6 route.
func (c *Client) IPv6RouteDel(vrfID uint16, dest IPv6Net, missingOK bool) error {
	var req C.struct_gr_ip6_route_del_req
	req.vrf_id = C.uint16_t(vrfID)
	req.dest = ip6NetToC(dest)
	req.missing_ok = boolToC(missingOK)
	return c.sendOnly(C.GR_IP6_ROUTE_DEL, C.size_t(unsafe.Sizeof(req)), unsafe.Pointer(&req))
}

// IPv6RouteGet looks up a route by destination (longest prefix match).
func (c *Client) IPv6RouteGet(vrfID uint16, dest netip.Addr) (*Nexthop, error) {
	var req C.struct_gr_ip6_route_get_req
	req.vrf_id = C.uint16_t(vrfID)
	req.dest = ip6ToC(dest)
	ptr, err := c.sendRecv(C.GR_IP6_ROUTE_GET, C.size_t(unsafe.Sizeof(req)), unsafe.Pointer(&req))
	if err != nil {
		return nil, err
	}
	defer C.free(ptr)
	resp := (*C.struct_gr_ip6_route_get_resp)(ptr)
	return nexthopFromC(&resp.nh), nil
}

// IPv6RouteList lists all IPv6 routes in a VRF.
func (c *Client) IPv6RouteList(vrfID uint16, maxCount uint16) ([]IPv6Route, error) {
	var req C.struct_gr_ip6_route_list_req
	req.vrf_id = C.uint16_t(vrfID)
	req.max_count = C.uint16_t(maxCount)

	var result []IPv6Route
	err := c.streamForEach(
		C.GR_IP6_ROUTE_LIST,
		C.size_t(unsafe.Sizeof(req)),
		unsafe.Pointer(&req),
		func(ptr unsafe.Pointer) error {
			r := (*C.struct_gr_ip6_route)(ptr)
			result = append(result, IPv6Route{
				Dest:   ip6NetFromC(&r.dest),
				VRFID:  uint16(r.vrf_id),
				Origin: NHOrigin(r.origin),
				NH:     *nexthopFromC(&r.nh),
			})
			return nil
		},
	)
	return result, err
}

// IPv6AddrAdd adds an IPv6 address to an interface.
func (c *Client) IPv6AddrAdd(ifaceID uint16, addr IPv6Net, existOK bool) error {
	var req C.struct_gr_ip6_addr_add_req
	req.addr.iface_id = C.uint16_t(ifaceID)
	req.addr.addr = ip6NetToC(addr)
	req.exist_ok = boolToC(existOK)
	return c.sendOnly(C.GR_IP6_ADDR_ADD, C.size_t(unsafe.Sizeof(req)), unsafe.Pointer(&req))
}

// IPv6AddrDel removes an IPv6 address from an interface.
func (c *Client) IPv6AddrDel(ifaceID uint16, addr IPv6Net, missingOK bool) error {
	var req C.struct_gr_ip6_addr_del_req
	req.addr.iface_id = C.uint16_t(ifaceID)
	req.addr.addr = ip6NetToC(addr)
	req.missing_ok = boolToC(missingOK)
	return c.sendOnly(C.GR_IP6_ADDR_DEL, C.size_t(unsafe.Sizeof(req)), unsafe.Pointer(&req))
}

// IPv6AddrList lists IPv6 addresses on interfaces.
func (c *Client) IPv6AddrList(vrfID, ifaceID uint16) ([]IPv6IfAddr, error) {
	var req C.struct_gr_ip6_addr_list_req
	req.vrf_id = C.uint16_t(vrfID)
	req.iface_id = C.uint16_t(ifaceID)

	var result []IPv6IfAddr
	err := c.streamForEach(
		C.GR_IP6_ADDR_LIST,
		C.size_t(unsafe.Sizeof(req)),
		unsafe.Pointer(&req),
		func(ptr unsafe.Pointer) error {
			a := (*C.struct_gr_ip6_ifaddr)(ptr)
			result = append(result, IPv6IfAddr{
				IfaceID: uint16(a.iface_id),
				Addr:    ip6NetFromC(&a.addr),
			})
			return nil
		},
	)
	return result, err
}

// IPv6AddrFlush removes all IPv6 addresses from an interface.
func (c *Client) IPv6AddrFlush(ifaceID uint16) error {
	var req C.struct_gr_ip6_addr_flush_req
	req.iface_id = C.uint16_t(ifaceID)
	return c.sendOnly(C.GR_IP6_ADDR_FLUSH, C.size_t(unsafe.Sizeof(req)), unsafe.Pointer(&req))
}

// FIB6Info contains FIB status for a VRF.
type FIB6Info struct {
	VRFID      uint16
	MaxRoutes  uint32
	UsedRoutes uint32
	NumTbl8    uint32
	UsedTbl8   uint32
}

// IPv6FIBInfoList lists FIB info for VRFs.
func (c *Client) IPv6FIBInfoList(vrfID uint16) ([]FIB6Info, error) {
	var req C.struct_gr_ip6_fib_info_list_req
	req.vrf_id = C.uint16_t(vrfID)

	var result []FIB6Info
	err := c.streamForEach(
		C.GR_IP6_FIB_INFO_LIST,
		C.size_t(unsafe.Sizeof(req)),
		unsafe.Pointer(&req),
		func(ptr unsafe.Pointer) error {
			f := (*C.struct_gr_fib6_info)(ptr)
			result = append(result, FIB6Info{
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

// IPv6RAConfig is the router advertisement configuration for an interface.
type IPv6RAConfig struct {
	Enabled  bool
	IfaceID  uint16
	Interval uint16
	Lifetime uint16
}

// IPv6RASet configures router advertisement on an interface.
func (c *Client) IPv6RASet(ifaceID uint16, interval, lifetime *uint16) error {
	var req C.struct_gr_ip6_ra_set_req
	req.iface_id = C.uint16_t(ifaceID)
	if interval != nil {
		C.grout_ra_set_interval(&req, C.uint16_t(*interval))
	}
	if lifetime != nil {
		C.grout_ra_set_lifetime(&req, C.uint16_t(*lifetime))
	}
	return c.sendOnly(C.GR_IP6_IFACE_RA_SET, C.size_t(unsafe.Sizeof(req)), unsafe.Pointer(&req))
}

// IPv6RAClear disables router advertisement on an interface.
func (c *Client) IPv6RAClear(ifaceID uint16) error {
	var req C.struct_gr_ip6_ra_clear_req
	req.iface_id = C.uint16_t(ifaceID)
	return c.sendOnly(C.GR_IP6_IFACE_RA_CLEAR, C.size_t(unsafe.Sizeof(req)), unsafe.Pointer(&req))
}

// IPv6RAShow returns RA configuration for interfaces.
func (c *Client) IPv6RAShow(ifaceID uint16) ([]IPv6RAConfig, error) {
	var req C.struct_gr_ip6_ra_show_req
	req.iface_id = C.uint16_t(ifaceID)

	var result []IPv6RAConfig
	err := c.streamForEach(
		C.GR_IP6_IFACE_RA_SHOW,
		C.size_t(unsafe.Sizeof(req)),
		unsafe.Pointer(&req),
		func(ptr unsafe.Pointer) error {
			r := (*C.struct_gr_ip6_ra_conf)(ptr)
			result = append(result, IPv6RAConfig{
				Enabled:  bool(r.enabled),
				IfaceID:  uint16(r.iface_id),
				Interval: uint16(r.interval),
				Lifetime: uint16(r.lifetime),
			})
			return nil
		},
	)
	return result, err
}
