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

type ConnState int

const (
	ConnStateClosed      ConnState = C.CONN_S_CLOSED
	ConnStateNew         ConnState = C.CONN_S_NEW
	ConnStateSimsynSent  ConnState = C.CONN_S_SIMSYN_SENT
	ConnStateSynRecv     ConnState = C.CONN_S_SYN_RECEIVED
	ConnStateEstablished ConnState = C.CONN_S_ESTABLISHED
	ConnStateFinSent     ConnState = C.CONN_S_FIN_SENT
	ConnStateFinRecv     ConnState = C.CONN_S_FIN_RECEIVED
	ConnStateCloseWait   ConnState = C.CONN_S_CLOSE_WAIT
	ConnStateFinWait     ConnState = C.CONN_S_FIN_WAIT
	ConnStateClosing     ConnState = C.CONN_S_CLOSING
	ConnStateLastAck     ConnState = C.CONN_S_LAST_ACK
	ConnStateTimeWait    ConnState = C.CONN_S_TIME_WAIT
)

// ConntrackFlow is a unidirectional connection flow.
type ConntrackFlow struct {
	Src   netip.Addr
	Dst   netip.Addr
	SrcID uint16
	DstID uint16
}

// Conntrack is a tracked connection entry.
type Conntrack struct {
	IfaceID    uint16
	AF         uint8
	Proto      uint8
	FwdFlow    ConntrackFlow
	RevFlow    ConntrackFlow
	LastUpdate int64
	ID         uint32
	State      ConnState
}

// ConntrackList lists all tracked connections.
func (c *Client) ConntrackList() ([]Conntrack, error) {
	var req C.struct_gr_empty

	var result []Conntrack
	err := c.streamForEach(
		C.GR_CONNTRACK_LIST,
		C.size_t(unsafe.Sizeof(req)),
		unsafe.Pointer(&req),
		func(ptr unsafe.Pointer) error {
			ct := (*C.struct_gr_conntrack)(ptr)
			entry := Conntrack{
				IfaceID:    uint16(ct.iface_id),
				AF:         uint8(ct.af),
				Proto:      uint8(ct.proto),
				LastUpdate: int64(ct.last_update),
				ID:         uint32(ct.id),
				State:      ConnState(ct.state),
			}
			entry.FwdFlow = conntrackFlowFromC(&ct.fwd_flow)
			entry.RevFlow = conntrackFlowFromC(&ct.rev_flow)
			result = append(result, entry)
			return nil
		},
	)
	return result, err
}

func conntrackFlowFromC(f *C.struct_gr_conntrack_flow) ConntrackFlow {
	return ConntrackFlow{
		Src:   ip4FromC(f.src),
		Dst:   ip4FromC(f.dst),
		SrcID: uint16(f.src_id),
		DstID: uint16(f.dst_id),
	}
}

// ConntrackFlush deletes all tracked connections.
func (c *Client) ConntrackFlush() error {
	var req C.struct_gr_empty
	return c.sendOnly(C.GR_CONNTRACK_FLUSH, C.size_t(unsafe.Sizeof(req)), unsafe.Pointer(&req))
}

// ConntrackConfig contains connection tracking configuration.
type ConntrackConfig struct {
	MaxCount              uint32
	TimeoutClosedSec      uint32
	TimeoutNewSec         uint32
	TimeoutUDPEstSec      uint32
	TimeoutTCPEstSec      uint32
	TimeoutHalfCloseSec   uint32
	TimeoutTimeWaitSec    uint32
	UsedCount             uint32
}

// ConntrackConfigGet returns connection tracking configuration.
func (c *Client) ConntrackConfigGet() (*ConntrackConfig, error) {
	var req C.struct_gr_empty
	ptr, err := c.sendRecv(C.GR_CONNTRACK_CONF_GET, C.size_t(unsafe.Sizeof(req)), unsafe.Pointer(&req))
	if err != nil {
		return nil, err
	}
	defer C.free(ptr)
	r := (*C.struct_gr_conntrack_conf_get_resp)(ptr)
	return &ConntrackConfig{
		MaxCount:            uint32(C.grout_ct_conf_get_max_count(r)),
		TimeoutClosedSec:    uint32(C.grout_ct_conf_get_timeout_closed(r)),
		TimeoutNewSec:       uint32(C.grout_ct_conf_get_timeout_new(r)),
		TimeoutUDPEstSec:    uint32(C.grout_ct_conf_get_timeout_udp(r)),
		TimeoutTCPEstSec:    uint32(C.grout_ct_conf_get_timeout_tcp(r)),
		TimeoutHalfCloseSec: uint32(C.grout_ct_conf_get_timeout_half_close(r)),
		TimeoutTimeWaitSec:  uint32(C.grout_ct_conf_get_timeout_time_wait(r)),
		UsedCount:           uint32(r.used_count),
	}, nil
}

// ConntrackConfigSet updates connection tracking configuration.
func (c *Client) ConntrackConfigSet(conf *ConntrackConfig) error {
	var req C.struct_gr_conntrack_conf_set_req
	C.grout_ct_conf_set_max_count(&req, C.uint32_t(conf.MaxCount))
	C.grout_ct_conf_set_timeout_closed(&req, C.uint32_t(conf.TimeoutClosedSec))
	C.grout_ct_conf_set_timeout_new(&req, C.uint32_t(conf.TimeoutNewSec))
	C.grout_ct_conf_set_timeout_udp(&req, C.uint32_t(conf.TimeoutUDPEstSec))
	C.grout_ct_conf_set_timeout_tcp(&req, C.uint32_t(conf.TimeoutTCPEstSec))
	C.grout_ct_conf_set_timeout_half_close(&req, C.uint32_t(conf.TimeoutHalfCloseSec))
	C.grout_ct_conf_set_timeout_time_wait(&req, C.uint32_t(conf.TimeoutTimeWaitSec))
	return c.sendOnly(C.GR_CONNTRACK_CONF_SET, C.size_t(unsafe.Sizeof(req)), unsafe.Pointer(&req))
}
