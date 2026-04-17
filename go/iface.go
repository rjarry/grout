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

type IfaceType uint8

const (
	IfaceTypeUndef  IfaceType = C.GR_IFACE_TYPE_UNDEF
	IfaceTypeVRF    IfaceType = C.GR_IFACE_TYPE_VRF
	IfaceTypePort   IfaceType = C.GR_IFACE_TYPE_PORT
	IfaceTypeVLAN   IfaceType = C.GR_IFACE_TYPE_VLAN
	IfaceTypeIPIP   IfaceType = C.GR_IFACE_TYPE_IPIP
	IfaceTypeBond   IfaceType = C.GR_IFACE_TYPE_BOND
	IfaceTypeBridge IfaceType = C.GR_IFACE_TYPE_BRIDGE
	IfaceTypeVXLAN  IfaceType = C.GR_IFACE_TYPE_VXLAN
)

type IfaceFlags uint16

const (
	IfaceFlagUp          IfaceFlags = C.GR_IFACE_F_UP
	IfaceFlagPromisc     IfaceFlags = C.GR_IFACE_F_PROMISC
	IfaceFlagPacketTrace IfaceFlags = C.GR_IFACE_F_PACKET_TRACE
	IfaceFlagSNATStatic  IfaceFlags = C.GR_IFACE_F_SNAT_STATIC
	IfaceFlagSNATDynamic IfaceFlags = C.GR_IFACE_F_SNAT_DYNAMIC
)

type IfaceState uint16

const (
	IfaceStateRunning      IfaceState = C.GR_IFACE_S_RUNNING
	IfaceStatePromiscFixed IfaceState = C.GR_IFACE_S_PROMISC_FIXED
	IfaceStateAllmulti     IfaceState = C.GR_IFACE_S_ALLMULTI
)

type IfaceMode uint8

const (
	IfaceModeVRF    IfaceMode = C.GR_IFACE_MODE_VRF
	IfaceModeXC     IfaceMode = C.GR_IFACE_MODE_XC
	IfaceModeBond   IfaceMode = C.GR_IFACE_MODE_BOND
	IfaceModeBridge IfaceMode = C.GR_IFACE_MODE_BRIDGE
)

// Iface set_attrs bitmask values.
const (
	IfaceSetFlags    uint64 = C.GR_IFACE_SET_FLAGS
	IfaceSetMTU      uint64 = C.GR_IFACE_SET_MTU
	IfaceSetName     uint64 = C.GR_IFACE_SET_NAME
	IfaceSetVRF      uint64 = C.GR_IFACE_SET_VRF
	IfaceSetDomain   uint64 = C.GR_IFACE_SET_DOMAIN
	IfaceSetDescr    uint64 = C.GR_IFACE_SET_DESCR
	PortSetNRxQs     uint64 = C.GR_PORT_SET_N_RXQS
	PortSetNTxQs     uint64 = C.GR_PORT_SET_N_TXQS
	PortSetQSize     uint64 = C.GR_PORT_SET_Q_SIZE
	PortSetMAC       uint64 = C.GR_PORT_SET_MAC
	VRFSetFIB        uint64 = C.GR_VRF_SET_FIB
	VLANSetParent    uint64 = C.GR_VLAN_SET_PARENT
	VLANSetVLAN      uint64 = C.GR_VLAN_SET_VLAN
	VLANSetMAC       uint64 = C.GR_VLAN_SET_MAC
	BondSetMode      uint64 = C.GR_BOND_SET_MODE
	BondSetPrimary   uint64 = C.GR_BOND_SET_PRIMARY
	BondSetMAC       uint64 = C.GR_BOND_SET_MAC
	BondSetAlgo      uint64 = C.GR_BOND_SET_ALGO
	BridgeSetAgeing  uint64 = C.GR_BRIDGE_SET_AGEING_TIME
	BridgeSetFlags   uint64 = C.GR_BRIDGE_SET_FLAGS
	BridgeSetMAC     uint64 = C.GR_BRIDGE_SET_MAC
	VXLANSetVNI      uint64 = C.GR_VXLAN_SET_VNI
	VXLANSetEncapVRF uint64 = C.GR_VXLAN_SET_ENCAP_VRF
	VXLANSetDstPort  uint64 = C.GR_VXLAN_SET_DST_PORT
	VXLANSetLocal    uint64 = C.GR_VXLAN_SET_LOCAL
	VXLANSetMAC      uint64 = C.GR_VXLAN_SET_MAC
	IPIPSetLocal     uint64 = C.GR_IPIP_SET_LOCAL
	IPIPSetRemote    uint64 = C.GR_IPIP_SET_REMOTE
)

type BondMode uint8

const (
	BondModeActiveBackup BondMode = C.GR_BOND_MODE_ACTIVE_BACKUP
	BondModeLACP         BondMode = C.GR_BOND_MODE_LACP
)

type BondAlgo uint8

const (
	BondAlgoRSS  BondAlgo = C.GR_BOND_ALGO_RSS
	BondAlgoL2   BondAlgo = C.GR_BOND_ALGO_L2
	BondAlgoL3L4 BondAlgo = C.GR_BOND_ALGO_L3_L4
)

// Iface represents a network interface with type-specific info.
type Iface struct {
	ID          uint16
	Type        IfaceType
	Mode        IfaceMode
	Flags       IfaceFlags
	State       IfaceState
	MTU         uint16
	VRFID       uint16
	DomainID    uint16
	Speed       uint32
	Name        string
	Description string
	PortInfo    *PortInfo
	VRFInfo     *VRFInfo
	VLANInfo    *VLANInfo
	BondInfo    *BondInfo
	BridgeInfo  *BridgeInfo
	IPIPInfo    *IPIPInfo
	VXLANInfo   *VXLANInfo
}

type PortInfo struct {
	NRxQ    uint16
	NTxQ    uint16
	RxQSize uint16
	TxQSize uint16
	MAC     EtherAddr
	Devargs string
	Driver  string
}

type VRFInfo struct {
	IPv4 FIBConfig
	IPv6 FIBConfig
}

type FIBConfig struct {
	MaxRoutes uint32
	NumTbl8   uint32
}

type VLANInfo struct {
	ParentID uint16
	VLANID   uint16
	MAC      EtherAddr
}

type BondMember struct {
	IfaceID uint16
	Active  bool
}

type BondInfo struct {
	Mode          BondMode
	Algo          BondAlgo
	MAC           EtherAddr
	PrimaryMember uint8
	Members       []BondMember
}

type BridgeFlags uint16

const (
	BridgeFlagNoFlood BridgeFlags = C.GR_BRIDGE_F_NO_FLOOD
	BridgeFlagNoLearn BridgeFlags = C.GR_BRIDGE_F_NO_LEARN
)

type BridgeInfo struct {
	AgeingTime uint16
	Flags      BridgeFlags
	MAC        EtherAddr
	Members    []uint16
}

type IPIPInfo struct {
	Local  netip.Addr
	Remote netip.Addr
}

type VXLANInfo struct {
	VNI        uint32
	EncapVRFID uint16
	DstPort    uint16
	Local      netip.Addr
	MAC        EtherAddr
}

func ifaceFromC(ci *C.struct_gr_iface) *Iface {
	iface := &Iface{
		ID:          uint16(C.grout_iface_get_id(ci)),
		Type:        IfaceType(C.grout_iface_get_type(ci)),
		Mode:        IfaceMode(C.grout_iface_get_mode(ci)),
		Flags:       IfaceFlags(C.grout_iface_get_flags(ci)),
		State:       IfaceState(C.grout_iface_get_state(ci)),
		MTU:         uint16(C.grout_iface_get_mtu(ci)),
		VRFID:       uint16(C.grout_iface_get_vrf_id(ci)),
		DomainID:    uint16(C.grout_iface_get_domain_id(ci)),
		Speed:       uint32(C.grout_iface_get_speed(ci)),
		Name:        C.GoString(&ci.name[0]),
		Description: C.GoString(&ci.description[0]),
	}

	switch iface.Type {
	case IfaceTypePort:
		p := C.grout_iface_port_info(ci)
		mac := C.grout_port_get_mac(p)
		iface.PortInfo = &PortInfo{
			NRxQ:    uint16(C.grout_port_get_n_rxq(p)),
			NTxQ:    uint16(C.grout_port_get_n_txq(p)),
			RxQSize: uint16(C.grout_port_get_rxq_size(p)),
			TxQSize: uint16(C.grout_port_get_txq_size(p)),
			MAC:     etherFromC(&mac),
			Devargs: C.GoString(&p.devargs[0]),
			Driver:  C.GoString(&p.driver_name[0]),
		}
	case IfaceTypeVRF:
		v := C.grout_iface_vrf_info(ci)
		iface.VRFInfo = &VRFInfo{
			IPv4: FIBConfig{
				MaxRoutes: uint32(v.ipv4.max_routes),
				NumTbl8:   uint32(v.ipv4.num_tbl8),
			},
			IPv6: FIBConfig{
				MaxRoutes: uint32(v.ipv6.max_routes),
				NumTbl8:   uint32(v.ipv6.num_tbl8),
			},
		}
	case IfaceTypeVLAN:
		v := C.grout_iface_vlan_info(ci)
		iface.VLANInfo = &VLANInfo{
			ParentID: uint16(v.parent_id),
			VLANID:   uint16(v.vlan_id),
			MAC:      etherFromC(&v.mac),
		}
	case IfaceTypeBond:
		b := C.grout_iface_bond_info(ci)
		info := &BondInfo{
			Mode:          BondMode(b.mode),
			Algo:          BondAlgo(b.algo),
			MAC:           etherFromC(&b.mac),
			PrimaryMember: uint8(b.primary_member),
		}
		n := int(b.n_members)
		info.Members = make([]BondMember, n)
		for i := 0; i < n; i++ {
			info.Members[i] = BondMember{
				IfaceID: uint16(b.members[i].iface_id),
				Active:  bool(b.members[i].active),
			}
		}
		iface.BondInfo = info
	case IfaceTypeBridge:
		b := C.grout_iface_bridge_info(ci)
		info := &BridgeInfo{
			AgeingTime: uint16(C.grout_bridge_get_ageing_time(b)),
			Flags:      BridgeFlags(C.grout_bridge_get_flags(b)),
		}
		mac := C.grout_bridge_get_mac(b)
		info.MAC = etherFromC(&mac)
		n := int(C.grout_bridge_get_n_members(b))
		info.Members = make([]uint16, n)
		for i := 0; i < n; i++ {
			info.Members[i] = uint16(b.members[i])
		}
		iface.BridgeInfo = info
	case IfaceTypeIPIP:
		p := C.grout_iface_ipip_info(ci)
		iface.IPIPInfo = &IPIPInfo{
			Local:  ip4FromC(p.local),
			Remote: ip4FromC(p.remote),
		}
	case IfaceTypeVXLAN:
		v := C.grout_iface_vxlan_info(ci)
		iface.VXLANInfo = &VXLANInfo{
			VNI:        uint32(v.vni),
			EncapVRFID: uint16(v.encap_vrf_id),
			DstPort:    uint16(v.dst_port),
			Local:      ip4FromC(v.local),
			MAC:        etherFromC(&v.mac),
		}
	}
	return iface
}

func ifaceToC(ci *C.struct_gr_iface, iface *Iface) {
	C.grout_iface_set_id(ci, C.uint16_t(iface.ID))
	C.grout_iface_set_type(ci, C.uint8_t(iface.Type))
	C.grout_iface_set_mode(ci, C.uint8_t(iface.Mode))
	C.grout_iface_set_flags(ci, C.uint16_t(iface.Flags))
	C.grout_iface_set_mtu(ci, C.uint16_t(iface.MTU))
	C.grout_iface_set_vrf_id(ci, C.uint16_t(iface.VRFID))
	C.grout_iface_set_domain_id(ci, C.uint16_t(iface.DomainID))
	C.grout_iface_set_speed(ci, C.uint32_t(iface.Speed))

	cname := C.CString(iface.Name)
	defer C.free(unsafe.Pointer(cname))
	C.strncpy(&ci.name[0], cname, C.IFNAMSIZ-1)

	cdescr := C.CString(iface.Description)
	defer C.free(unsafe.Pointer(cdescr))
	C.strncpy(&ci.description[0], cdescr, C.size_t(len(ci.description)-1))

	switch iface.Type {
	case IfaceTypePort:
		if p := iface.PortInfo; p != nil {
			cp := C.grout_iface_port_info_mut(ci)
			C.grout_port_set_n_rxq(cp, C.uint16_t(p.NRxQ))
			C.grout_port_set_n_txq(cp, C.uint16_t(p.NTxQ))
			C.grout_port_set_rxq_size(cp, C.uint16_t(p.RxQSize))
			C.grout_port_set_txq_size(cp, C.uint16_t(p.TxQSize))
			C.grout_port_set_mac(cp, etherToC(p.MAC))
			cdev := C.CString(p.Devargs)
			defer C.free(unsafe.Pointer(cdev))
			C.strncpy(&cp.devargs[0], cdev, C.GR_PORT_DEVARGS_SIZE-1)
		}
	case IfaceTypeVRF:
		if v := iface.VRFInfo; v != nil {
			cv := C.grout_iface_vrf_info_mut(ci)
			cv.ipv4.max_routes = C.uint32_t(v.IPv4.MaxRoutes)
			cv.ipv4.num_tbl8 = C.uint32_t(v.IPv4.NumTbl8)
			cv.ipv6.max_routes = C.uint32_t(v.IPv6.MaxRoutes)
			cv.ipv6.num_tbl8 = C.uint32_t(v.IPv6.NumTbl8)
		}
	case IfaceTypeVLAN:
		if v := iface.VLANInfo; v != nil {
			cv := C.grout_iface_vlan_info_mut(ci)
			cv.parent_id = C.uint16_t(v.ParentID)
			cv.vlan_id = C.uint16_t(v.VLANID)
			cv.mac = etherToC(v.MAC)
		}
	case IfaceTypeBond:
		if b := iface.BondInfo; b != nil {
			cb := C.grout_iface_bond_info_mut(ci)
			cb.mode = C.gr_bond_mode_t(b.Mode)
			cb.algo = C.gr_bond_algo_t(b.Algo)
			cb.mac = etherToC(b.MAC)
			cb.primary_member = C.uint8_t(b.PrimaryMember)
			cb.n_members = C.uint8_t(len(b.Members))
			for i, m := range b.Members {
				cb.members[i].iface_id = C.uint16_t(m.IfaceID)
				cb.members[i].active = C.bool(m.Active)
			}
		}
	case IfaceTypeBridge:
		if b := iface.BridgeInfo; b != nil {
			cb := C.grout_iface_bridge_info_mut(ci)
			C.grout_bridge_set_ageing_time(cb, C.uint16_t(b.AgeingTime))
			C.grout_bridge_set_flags(cb, C.uint16_t(b.Flags))
			C.grout_bridge_set_mac(cb, etherToC(b.MAC))
		}
	case IfaceTypeIPIP:
		if p := iface.IPIPInfo; p != nil {
			cp := C.grout_iface_ipip_info_mut(ci)
			cp.local = ip4ToC(p.Local)
			cp.remote = ip4ToC(p.Remote)
		}
	case IfaceTypeVXLAN:
		if v := iface.VXLANInfo; v != nil {
			cv := C.grout_iface_vxlan_info_mut(ci)
			cv.vni = C.uint32_t(v.VNI)
			cv.encap_vrf_id = C.uint16_t(v.EncapVRFID)
			cv.dst_port = C.uint16_t(v.DstPort)
			cv.local = ip4ToC(v.Local)
			cv.mac = etherToC(v.MAC)
		}
	}
}

// IfaceList returns all interfaces matching the given type.
// Use IfaceTypeUndef to list all interfaces.
func (c *Client) IfaceList(ifaceType IfaceType) ([]Iface, error) {
	var req C.struct_gr_iface_list_req
	req._type = C.gr_iface_type_t(ifaceType)

	var result []Iface
	err := c.streamForEach(
		C.GR_IFACE_LIST,
		C.size_t(unsafe.Sizeof(req)),
		unsafe.Pointer(&req),
		func(ptr unsafe.Pointer) error {
			ci := (*C.struct_gr_iface)(ptr)
			result = append(result, *ifaceFromC(ci))
			return nil
		},
	)
	return result, err
}

// IfaceGet returns a single interface by ID.
func (c *Client) IfaceGet(id uint16) (*Iface, error) {
	var req C.struct_gr_iface_get_req
	req.iface_id = C.uint16_t(id)

	ptr, err := c.sendRecv(C.GR_IFACE_GET, C.size_t(unsafe.Sizeof(req)), unsafe.Pointer(&req))
	if err != nil {
		return nil, err
	}
	defer C.free(ptr)
	resp := (*C.struct_gr_iface_get_resp)(ptr)
	return ifaceFromC(&resp.iface), nil
}

// IfaceGetByName returns a single interface by name.
func (c *Client) IfaceGetByName(name string) (*Iface, error) {
	var req C.struct_gr_iface_get_req
	cname := C.CString(name)
	defer C.free(unsafe.Pointer(cname))
	C.strncpy(&req.name[0], cname, C.IFNAMSIZ-1)

	ptr, err := c.sendRecv(C.GR_IFACE_GET, C.size_t(unsafe.Sizeof(req)), unsafe.Pointer(&req))
	if err != nil {
		return nil, err
	}
	defer C.free(ptr)
	resp := (*C.struct_gr_iface_get_resp)(ptr)
	return ifaceFromC(&resp.iface), nil
}

// IfaceAdd creates a new interface and returns the allocated ID.
func (c *Client) IfaceAdd(iface *Iface) (uint16, error) {
	var txLen C.size_t
	req := C.grout_iface_add_req_alloc(C.uint8_t(iface.Type), &txLen)
	if req == nil {
		return 0, apiError("alloc", -int(C.ENOMEM))
	}
	defer C.free(unsafe.Pointer(req))
	ifaceToC(&req.iface, iface)

	ptr, err := c.sendRecv(C.GR_IFACE_ADD, txLen, unsafe.Pointer(req))
	if err != nil {
		return 0, err
	}
	defer C.free(ptr)
	resp := (*C.struct_gr_iface_add_resp)(ptr)
	return uint16(resp.iface_id), nil
}

// IfaceDel deletes an interface by ID.
func (c *Client) IfaceDel(id uint16) error {
	var req C.struct_gr_iface_del_req
	req.iface_id = C.uint16_t(id)
	return c.sendOnly(C.GR_IFACE_DEL, C.size_t(unsafe.Sizeof(req)), unsafe.Pointer(&req))
}

// IfaceSet modifies an existing interface. Only fields indicated by
// set_attrs are changed.
func (c *Client) IfaceSet(attrs uint64, iface *Iface) error {
	var txLen C.size_t
	req := C.grout_iface_set_req_alloc(C.uint8_t(iface.Type), &txLen)
	if req == nil {
		return apiError("alloc", -int(C.ENOMEM))
	}
	defer C.free(unsafe.Pointer(req))
	req.set_attrs = C.uint64_t(attrs)
	ifaceToC(&req.iface, iface)
	return c.sendOnly(C.GR_IFACE_SET, txLen, unsafe.Pointer(req))
}

// IfaceStats contains per-interface traffic statistics.
type IfaceStats struct {
	IfaceID    uint16
	RxPackets  uint64
	RxBytes    uint64
	RxDrops    uint64
	TxPackets  uint64
	TxBytes    uint64
	TxErrors   uint64
	CpRxPackets uint64
	CpRxBytes   uint64
	CpTxPackets uint64
	CpTxBytes   uint64
}

// IfaceStatsGet returns statistics for all interfaces.
func (c *Client) IfaceStatsGet() ([]IfaceStats, error) {
	var req C.struct_gr_empty
	ptr, err := c.sendRecv(C.GR_IFACE_STATS_GET, C.size_t(unsafe.Sizeof(req)), unsafe.Pointer(&req))
	if err != nil {
		return nil, err
	}
	defer C.free(ptr)

	resp := (*C.struct_gr_iface_stats_get_resp)(ptr)
	n := int(resp.n_stats)
	stats := make([]IfaceStats, n)
	for i := 0; i < n; i++ {
		s := C.grout_iface_stats_at(resp, C.int(i))
		stats[i] = IfaceStats{
			IfaceID:     uint16(s.iface_id),
			RxPackets:   uint64(s.rx_packets),
			RxBytes:     uint64(s.rx_bytes),
			RxDrops:     uint64(s.rx_drops),
			TxPackets:   uint64(s.tx_packets),
			TxBytes:     uint64(s.tx_bytes),
			TxErrors:    uint64(s.tx_errors),
			CpRxPackets: uint64(s.cp_rx_packets),
			CpRxBytes:   uint64(s.cp_rx_bytes),
			CpTxPackets: uint64(s.cp_tx_packets),
			CpTxBytes:   uint64(s.cp_tx_bytes),
		}
	}
	return stats, nil
}
