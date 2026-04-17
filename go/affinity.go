// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Robin Jarry

package grout

/*
#include "shim.h"
*/
import "C"

import "unsafe"

// RxQMap is a port RX queue to CPU mapping.
type RxQMap struct {
	IfaceID uint16
	RxQID   uint16
	CPUID   uint16
	Enabled bool
}

// AffinityRxQList lists RX queue to CPU mappings.
func (c *Client) AffinityRxQList() ([]RxQMap, error) {
	var req C.struct_gr_empty

	var result []RxQMap
	err := c.streamForEach(
		C.GR_AFFINITY_RXQ_LIST,
		C.size_t(unsafe.Sizeof(req)),
		unsafe.Pointer(&req),
		func(ptr unsafe.Pointer) error {
			m := (*C.struct_gr_port_rxq_map)(ptr)
			result = append(result, RxQMap{
				IfaceID: uint16(m.iface_id),
				RxQID:   uint16(m.rxq_id),
				CPUID:   uint16(m.cpu_id),
				Enabled: m.enabled != 0,
			})
			return nil
		},
	)
	return result, err
}

// AffinityRxQSet sets one RX queue to CPU mapping.
func (c *Client) AffinityRxQSet(ifaceID, rxqID, cpuID uint16) error {
	var req C.struct_gr_affinity_rxq_set_req
	req.iface_id = C.uint16_t(ifaceID)
	req.rxq_id = C.uint16_t(rxqID)
	req.cpu_id = C.uint16_t(cpuID)
	return c.sendOnly(C.GR_AFFINITY_RXQ_SET, C.size_t(unsafe.Sizeof(req)), unsafe.Pointer(&req))
}
