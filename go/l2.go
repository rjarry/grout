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

type FDBFlags uint8

const (
	FDBFlagStatic FDBFlags = C.GR_FDB_F_STATIC
	FDBFlagLearn  FDBFlags = C.GR_FDB_F_LEARN
	FDBFlagExtern FDBFlags = C.GR_FDB_F_EXTERN
)

// FDBEntry is a forwarding database entry.
type FDBEntry struct {
	BridgeID uint16
	MAC      EtherAddr
	VLANID   uint16
	IfaceID  uint16
	VTEP     netip.Addr
	Flags    FDBFlags
	LastSeen int64
}

// FDBAdd adds a forwarding database entry.
func (c *Client) FDBAdd(entry FDBEntry, existOK bool) error {
	var req C.struct_gr_fdb_add_req
	req.fdb.bridge_id = C.uint16_t(entry.BridgeID)
	req.fdb.mac = etherToC(entry.MAC)
	req.fdb.vlan_id = C.uint16_t(entry.VLANID)
	req.fdb.iface_id = C.uint16_t(entry.IfaceID)
	if entry.VTEP.IsValid() {
		req.fdb.vtep = ip4ToC(entry.VTEP)
	}
	req.fdb.flags = C.gr_fdb_flags_t(entry.Flags)
	req.exist_ok = C.bool(existOK)
	return c.sendOnly(C.GR_FDB_ADD, C.size_t(unsafe.Sizeof(req)), unsafe.Pointer(&req))
}

// FDBDel deletes a forwarding database entry.
func (c *Client) FDBDel(bridgeID uint16, mac EtherAddr, vlanID uint16, missingOK bool) error {
	var req C.struct_gr_fdb_del_req
	req.bridge_id = C.uint16_t(bridgeID)
	req.mac = etherToC(mac)
	req.vlan_id = C.uint16_t(vlanID)
	req.missing_ok = C.bool(missingOK)
	return c.sendOnly(C.GR_FDB_DEL, C.size_t(unsafe.Sizeof(req)), unsafe.Pointer(&req))
}

// FDBFlush flushes FDB entries matching the given filters.
func (c *Client) FDBFlush(bridgeID, ifaceID uint16, mac EtherAddr, flags FDBFlags) error {
	var req C.struct_gr_fdb_flush_req
	req.bridge_id = C.uint16_t(bridgeID)
	req.iface_id = C.uint16_t(ifaceID)
	req.mac = etherToC(mac)
	req.flags = C.gr_fdb_flags_t(flags)
	return c.sendOnly(C.GR_FDB_FLUSH, C.size_t(unsafe.Sizeof(req)), unsafe.Pointer(&req))
}

// FDBList lists forwarding database entries.
func (c *Client) FDBList(bridgeID, ifaceID uint16, flags FDBFlags) ([]FDBEntry, error) {
	var req C.struct_gr_fdb_list_req
	req.bridge_id = C.uint16_t(bridgeID)
	req.iface_id = C.uint16_t(ifaceID)
	req.flags = C.gr_fdb_flags_t(flags)

	var result []FDBEntry
	err := c.streamForEach(
		C.GR_FDB_LIST,
		C.size_t(unsafe.Sizeof(req)),
		unsafe.Pointer(&req),
		func(ptr unsafe.Pointer) error {
			e := (*C.struct_gr_fdb_entry)(ptr)
			result = append(result, FDBEntry{
				BridgeID: uint16(e.bridge_id),
				MAC:      etherFromC(&e.mac),
				VLANID:   uint16(e.vlan_id),
				IfaceID:  uint16(e.iface_id),
				VTEP:     ip4FromC(e.vtep),
				Flags:    FDBFlags(e.flags),
				LastSeen: int64(e.last_seen),
			})
			return nil
		},
	)
	return result, err
}

// FDBConfig contains FDB subsystem configuration.
type FDBConfig struct {
	MaxEntries  uint32
	UsedEntries uint32
}

// FDBConfigGet returns FDB configuration and usage.
func (c *Client) FDBConfigGet() (*FDBConfig, error) {
	var req C.struct_gr_empty
	ptr, err := c.sendRecv(C.GR_FDB_CONFIG_GET, C.size_t(unsafe.Sizeof(req)), unsafe.Pointer(&req))
	if err != nil {
		return nil, err
	}
	defer C.free(ptr)
	r := (*C.struct_gr_fdb_config_get_resp)(ptr)
	return &FDBConfig{
		MaxEntries:  uint32(r.max_entries),
		UsedEntries: uint32(r.used_entries),
	}, nil
}

// FDBConfigSet updates FDB configuration.
func (c *Client) FDBConfigSet(maxEntries uint32) error {
	var req C.struct_gr_fdb_config_set_req
	req.max_entries = C.uint32_t(maxEntries)
	return c.sendOnly(C.GR_FDB_CONFIG_SET, C.size_t(unsafe.Sizeof(req)), unsafe.Pointer(&req))
}

type FloodType uint8

const (
	FloodTypeVTEP FloodType = C.GR_FLOOD_T_VTEP
)

// FloodEntry is a flood list entry for BUM traffic.
type FloodEntry struct {
	Type  FloodType
	VRFID uint16
	VNI   uint32
	Addr  netip.Addr
}

// FloodAdd adds a flood list entry.
func (c *Client) FloodAdd(entry FloodEntry, existOK bool) error {
	var req C.struct_gr_flood_add_req
	req.entry._type = C.gr_flood_type_t(entry.Type)
	req.entry.vrf_id = C.uint16_t(entry.VRFID)
	C.grout_flood_set_vni(&req.entry, C.uint32_t(entry.VNI))
	C.grout_flood_set_addr(&req.entry, ip4ToC(entry.Addr))
	req.exist_ok = C.bool(existOK)
	return c.sendOnly(C.GR_FLOOD_ADD, C.size_t(unsafe.Sizeof(req)), unsafe.Pointer(&req))
}

// FloodDel removes a flood list entry.
func (c *Client) FloodDel(entry FloodEntry, missingOK bool) error {
	var req C.struct_gr_flood_del_req
	req.entry._type = C.gr_flood_type_t(entry.Type)
	req.entry.vrf_id = C.uint16_t(entry.VRFID)
	C.grout_flood_set_vni(&req.entry, C.uint32_t(entry.VNI))
	C.grout_flood_set_addr(&req.entry, ip4ToC(entry.Addr))
	req.missing_ok = C.bool(missingOK)
	return c.sendOnly(C.GR_FLOOD_DEL, C.size_t(unsafe.Sizeof(req)), unsafe.Pointer(&req))
}

// FloodList lists flood entries.
func (c *Client) FloodList(floodType FloodType, vrfID uint16) ([]FloodEntry, error) {
	var req C.struct_gr_flood_list_req
	req._type = C.gr_flood_type_t(floodType)
	req.vrf_id = C.uint16_t(vrfID)

	var result []FloodEntry
	err := c.streamForEach(
		C.GR_FLOOD_LIST,
		C.size_t(unsafe.Sizeof(req)),
		unsafe.Pointer(&req),
		func(ptr unsafe.Pointer) error {
			e := (*C.struct_gr_flood_entry)(ptr)
			result = append(result, FloodEntry{
				Type:  FloodType(e._type),
				VRFID: uint16(e.vrf_id),
				VNI:   uint32(C.grout_flood_get_vni(e)),
				Addr:  ip4FromC(C.grout_flood_get_addr(e)),
			})
			return nil
		},
	)
	return result, err
}
