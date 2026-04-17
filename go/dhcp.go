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

type DHCPState uint8

const (
	DHCPStateInit       DHCPState = C.DHCP_STATE_INIT
	DHCPStateSelecting  DHCPState = C.DHCP_STATE_SELECTING
	DHCPStateRequesting DHCPState = C.DHCP_STATE_REQUESTING
	DHCPStateBound      DHCPState = C.DHCP_STATE_BOUND
	DHCPStateRenewing   DHCPState = C.DHCP_STATE_RENEWING
	DHCPStateRebinding  DHCPState = C.DHCP_STATE_REBINDING
)

// DHCPStatus is the status of a DHCP client on an interface.
type DHCPStatus struct {
	IfaceID     uint16
	State       DHCPState
	ServerIP    netip.Addr
	AssignedIP  netip.Addr
	LeaseTime   uint32
	RenewalTime uint32
	RebindTime  uint32
}

// DHCPList lists all active DHCP clients.
func (c *Client) DHCPList() ([]DHCPStatus, error) {
	var req C.struct_gr_empty

	var result []DHCPStatus
	err := c.streamForEach(
		C.GR_DHCP_LIST,
		C.size_t(unsafe.Sizeof(req)),
		unsafe.Pointer(&req),
		func(ptr unsafe.Pointer) error {
			s := (*C.struct_gr_dhcp_status)(ptr)
			result = append(result, DHCPStatus{
				IfaceID:     uint16(s.iface_id),
				State:       DHCPState(s.state),
				ServerIP:    ip4FromC(s.server_ip),
				AssignedIP:  ip4FromC(s.assigned_ip),
				LeaseTime:   uint32(s.lease_time),
				RenewalTime: uint32(s.renewal_time),
				RebindTime:  uint32(s.rebind_time),
			})
			return nil
		},
	)
	return result, err
}

// DHCPStart starts the DHCP client on an interface.
func (c *Client) DHCPStart(ifaceID uint16) error {
	var req C.struct_gr_dhcp_start_req
	req.iface_id = C.uint16_t(ifaceID)
	return c.sendOnly(C.GR_DHCP_START, C.size_t(unsafe.Sizeof(req)), unsafe.Pointer(&req))
}

// DHCPStop stops the DHCP client on an interface.
func (c *Client) DHCPStop(ifaceID uint16) error {
	var req C.struct_gr_dhcp_stop_req
	req.iface_id = C.uint16_t(ifaceID)
	return c.sendOnly(C.GR_DHCP_STOP, C.size_t(unsafe.Sizeof(req)), unsafe.Pointer(&req))
}
