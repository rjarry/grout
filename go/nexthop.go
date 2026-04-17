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

type NHType uint8

const (
	NHTypeL3        NHType = C.GR_NH_T_L3
	NHTypeSR6Output NHType = C.GR_NH_T_SR6_OUTPUT
	NHTypeSR6Local  NHType = C.GR_NH_T_SR6_LOCAL
	NHTypeDNAT      NHType = C.GR_NH_T_DNAT
	NHTypeBlackhole NHType = C.GR_NH_T_BLACKHOLE
	NHTypeReject    NHType = C.GR_NH_T_REJECT
	NHTypeGroup     NHType = C.GR_NH_T_GROUP
)

type NHState uint8

const (
	NHStateNew       NHState = C.GR_NH_S_NEW
	NHStatePending   NHState = C.GR_NH_S_PENDING
	NHStateReachable NHState = C.GR_NH_S_REACHABLE
	NHStateStale     NHState = C.GR_NH_S_STALE
	NHStateFailed    NHState = C.GR_NH_S_FAILED
)

type NHFlags uint8

const (
	NHFlagStatic  NHFlags = C.GR_NH_F_STATIC
	NHFlagLocal   NHFlags = C.GR_NH_F_LOCAL
	NHFlagGateway NHFlags = C.GR_NH_F_GATEWAY
	NHFlagLink    NHFlags = C.GR_NH_F_LINK
	NHFlagMcast   NHFlags = C.GR_NH_F_MCAST
)

type NHOrigin uint8

const (
	NHOriginUnspec   NHOrigin = C.GR_NH_ORIGIN_UNSPEC
	NHOriginRedirect NHOrigin = C.GR_NH_ORIGIN_REDIRECT
	NHOriginLink     NHOrigin = C.GR_NH_ORIGIN_LINK
	NHOriginStatic   NHOrigin = C.GR_NH_ORIGIN_STATIC
	NHOriginZebra    NHOrigin = C.GR_NH_ORIGIN_ZEBRA
	NHOriginDHCP     NHOrigin = C.GR_NH_ORIGIN_DHCP
	NHOriginBGP      NHOrigin = C.GR_NH_ORIGIN_BGP
	NHOriginISIS     NHOrigin = C.GR_NH_ORIGIN_ISIS
	NHOriginOSPF     NHOrigin = C.GR_NH_ORIGIN_OSPF
	NHOriginRIP      NHOrigin = C.GR_NH_ORIGIN_RIP
	NHOriginInternal NHOrigin = C.GR_NH_ORIGIN_INTERNAL
)

// Nexthop represents a routing nexthop entry.
type Nexthop struct {
	Type    NHType
	Origin  NHOrigin
	IfaceID uint16
	VRFID   uint16
	NHID    uint32
	L3Info  *NHL3Info
	Group   *NHGroupInfo
}

// NHL3Info contains L3 forwarding information for a nexthop.
type NHL3Info struct {
	State     NHState
	Flags     NHFlags
	AF        uint8
	PrefixLen uint8
	IPv4      netip.Addr
	IPv6      netip.Addr
	MAC       EtherAddr
}

type NHGroupMember struct {
	NHID   uint32
	Weight uint32
}

type NHGroupInfo struct {
	Members []NHGroupMember
}

func nexthopFromC(cn *C.struct_gr_nexthop) *Nexthop {
	nh := &Nexthop{
		Type:    NHType(C.grout_nh_get_type(cn)),
		Origin:  NHOrigin(C.grout_nh_get_origin(cn)),
		IfaceID: uint16(C.grout_nh_get_iface_id(cn)),
		VRFID:   uint16(C.grout_nh_get_vrf_id(cn)),
		NHID:    uint32(C.grout_nh_get_nh_id(cn)),
	}

	switch nh.Type {
	case NHTypeL3:
		l3 := C.grout_nh_l3_info(cn)
		info := &NHL3Info{
			State:     NHState(l3.state),
			Flags:     NHFlags(l3.flags),
			AF:        uint8(l3.af),
			PrefixLen: uint8(l3.prefixlen),
			MAC:       etherFromC(&l3.mac),
		}
		switch info.AF {
		case C.GR_AF_IP4:
			info.IPv4 = ip4FromC(C.grout_nh_l3_get_ipv4(l3))
		case C.GR_AF_IP6:
			v6 := C.grout_nh_l3_get_ipv6(l3)
			info.IPv6 = ip6FromC(&v6)
		}
		nh.L3Info = info
	case NHTypeGroup:
		g := C.grout_nh_group_info(cn)
		info := &NHGroupInfo{}
		n := int(g.n_members)
		info.Members = make([]NHGroupMember, n)
		members := C.grout_nh_group_members(g)
		for i := range n {
			m := (*C.struct_gr_nexthop_group_member)(
				unsafe.Pointer(uintptr(unsafe.Pointer(members)) +
					uintptr(i)*unsafe.Sizeof(C.struct_gr_nexthop_group_member{})))
			info.Members[i] = NHGroupMember{
				NHID:   uint32(m.nh_id),
				Weight: uint32(m.weight),
			}
		}
		nh.Group = info
	}
	return nh
}

// NHList returns nexthops matching the given filters.
func (c *Client) NHList(vrfID uint16, nhType NHType, maxCount uint16, includeInternal bool) ([]Nexthop, error) {
	var req C.struct_gr_nh_list_req
	req.vrf_id = C.uint16_t(vrfID)
	req._type = C.gr_nh_type_t(nhType)
	req.max_count = C.uint16_t(maxCount)
	req.include_internal = C.bool(includeInternal)

	var result []Nexthop
	err := c.streamForEach(
		C.GR_NH_LIST,
		C.size_t(unsafe.Sizeof(req)),
		unsafe.Pointer(&req),
		func(ptr unsafe.Pointer) error {
			result = append(result, *nexthopFromC((*C.struct_gr_nexthop)(ptr)))
			return nil
		},
	)
	return result, err
}

// NHGet returns a nexthop by ID.
func (c *Client) NHGet(nhID uint32) (*Nexthop, error) {
	var req C.struct_gr_nh_get_req
	req.nh_id = C.uint32_t(nhID)
	ptr, err := c.sendRecv(C.GR_NH_GET, C.size_t(unsafe.Sizeof(req)), unsafe.Pointer(&req))
	if err != nil {
		return nil, err
	}
	defer C.free(ptr)
	return nexthopFromC((*C.struct_gr_nexthop)(ptr)), nil
}

// NHAdd creates a new L3 nexthop.
func (c *Client) NHAdd(nh *Nexthop, existOK bool) error {
	var txLen C.size_t
	req := C.grout_nh_add_req_alloc(C.uint8_t(nh.Type), &txLen)
	if req == nil {
		return apiError("alloc", -int(C.ENOMEM))
	}
	defer C.free(unsafe.Pointer(req))
	req.exist_ok = boolToC(existOK)
	C.grout_nh_set_type(&req.nh, C.uint8_t(nh.Type))
	C.grout_nh_set_origin(&req.nh, C.uint8_t(nh.Origin))
	C.grout_nh_set_iface_id(&req.nh, C.uint16_t(nh.IfaceID))
	C.grout_nh_set_vrf_id(&req.nh, C.uint16_t(nh.VRFID))
	C.grout_nh_set_nh_id(&req.nh, C.uint32_t(nh.NHID))

	if nh.Type == NHTypeL3 && nh.L3Info != nil {
		l3 := C.grout_nh_l3_info_mut(&req.nh)
		l3.flags = C.gr_nh_flags_t(nh.L3Info.Flags)
		l3.af = C.addr_family_t(nh.L3Info.AF)
		l3.prefixlen = C.uint8_t(nh.L3Info.PrefixLen)
		l3.mac = etherToC(nh.L3Info.MAC)
		switch nh.L3Info.AF {
		case C.GR_AF_IP4:
			C.grout_nh_l3_set_ipv4(l3, ip4ToC(nh.L3Info.IPv4))
		case C.GR_AF_IP6:
			C.grout_nh_l3_set_ipv6(l3, ip6ToC(nh.L3Info.IPv6))
		}
	}
	return c.sendOnly(C.GR_NH_ADD, txLen, unsafe.Pointer(req))
}

// NHDel deletes a nexthop by ID.
func (c *Client) NHDel(nhID uint32, missingOK bool) error {
	var req C.struct_gr_nh_del_req
	req.nh_id = C.uint32_t(nhID)
	req.missing_ok = boolToC(missingOK)
	return c.sendOnly(C.GR_NH_DEL, C.size_t(unsafe.Sizeof(req)), unsafe.Pointer(&req))
}

// NHConfig contains nexthop subsystem configuration.
type NHConfig struct {
	MaxCount             uint32
	LifetimeReachableSec uint32
	LifetimeUnreachSec   uint32
	MaxHeldPkts          uint16
	MaxUcastProbes       uint8
	MaxBcastProbes       uint8
	UsedCount            uint32
}

// NHConfigGet returns the nexthop subsystem configuration.
func (c *Client) NHConfigGet() (*NHConfig, error) {
	var req C.struct_gr_empty
	ptr, err := c.sendRecv(C.GR_NH_CONFIG_GET, C.size_t(unsafe.Sizeof(req)), unsafe.Pointer(&req))
	if err != nil {
		return nil, err
	}
	defer C.free(ptr)
	r := (*C.struct_gr_nh_config_get_resp)(ptr)
	return &NHConfig{
		MaxCount:             uint32(C.grout_nh_conf_get_max_count(r)),
		LifetimeReachableSec: uint32(C.grout_nh_conf_get_lifetime_reachable(r)),
		LifetimeUnreachSec:   uint32(C.grout_nh_conf_get_lifetime_unreachable(r)),
		MaxHeldPkts:          uint16(C.grout_nh_conf_get_max_held_pkts(r)),
		MaxUcastProbes:       uint8(C.grout_nh_conf_get_max_ucast_probes(r)),
		MaxBcastProbes:       uint8(C.grout_nh_conf_get_max_bcast_probes(r)),
		UsedCount:            uint32(r.used_count),
	}, nil
}

// NHConfigSet updates the nexthop subsystem configuration.
// Only non-zero values are applied.
func (c *Client) NHConfigSet(conf *NHConfig) error {
	var req C.struct_gr_nh_config_set_req
	C.grout_nh_conf_set_max_count(&req, C.uint32_t(conf.MaxCount))
	C.grout_nh_conf_set_lifetime_reachable(&req, C.uint32_t(conf.LifetimeReachableSec))
	C.grout_nh_conf_set_lifetime_unreachable(&req, C.uint32_t(conf.LifetimeUnreachSec))
	C.grout_nh_conf_set_max_held_pkts(&req, C.uint16_t(conf.MaxHeldPkts))
	C.grout_nh_conf_set_max_ucast_probes(&req, C.uint8_t(conf.MaxUcastProbes))
	C.grout_nh_conf_set_max_bcast_probes(&req, C.uint8_t(conf.MaxBcastProbes))
	return c.sendOnly(C.GR_NH_CONFIG_SET, C.size_t(unsafe.Sizeof(req)), unsafe.Pointer(&req))
}

func boolToC(b bool) C.uint8_t {
	if b {
		return 1
	}
	return 0
}
