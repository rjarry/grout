// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Robin Jarry

package grout

/*
#include "shim.h"
*/
import "C"

import "unsafe"

// Event types from all modules.
const (
	EventIfaceAdd         uint32 = C.GR_EVENT_IFACE_ADD
	EventIfacePostAdd     uint32 = C.GR_EVENT_IFACE_POST_ADD
	EventIfacePreRemove   uint32 = C.GR_EVENT_IFACE_PRE_REMOVE
	EventIfaceRemove      uint32 = C.GR_EVENT_IFACE_REMOVE
	EventIfacePostReconfig uint32 = C.GR_EVENT_IFACE_POST_RECONFIG
	EventIfaceStatusUp    uint32 = C.GR_EVENT_IFACE_STATUS_UP
	EventIfaceStatusDown  uint32 = C.GR_EVENT_IFACE_STATUS_DOWN
	EventIfaceMACChange   uint32 = C.GR_EVENT_IFACE_MAC_CHANGE

	EventNHNew    uint32 = C.GR_EVENT_NEXTHOP_NEW
	EventNHDelete uint32 = C.GR_EVENT_NEXTHOP_DELETE
	EventNHUpdate uint32 = C.GR_EVENT_NEXTHOP_UPDATE

	EventIPAddrAdd  uint32 = C.GR_EVENT_IP_ADDR_ADD
	EventIPAddrDel  uint32 = C.GR_EVENT_IP_ADDR_DEL
	EventIPRouteAdd uint32 = C.GR_EVENT_IP_ROUTE_ADD
	EventIPRouteDel uint32 = C.GR_EVENT_IP_ROUTE_DEL

	EventIP6AddrAdd  uint32 = C.GR_EVENT_IP6_ADDR_ADD
	EventIP6AddrDel  uint32 = C.GR_EVENT_IP6_ADDR_DEL
	EventIP6RouteAdd uint32 = C.GR_EVENT_IP6_ROUTE_ADD
	EventIP6RouteDel uint32 = C.GR_EVENT_IP6_ROUTE_DEL

	EventFDBAdd    uint32 = C.GR_EVENT_FDB_ADD
	EventFDBDel    uint32 = C.GR_EVENT_FDB_DEL
	EventFDBUpdate uint32 = C.GR_EVENT_FDB_UPDATE
	EventFloodAdd  uint32 = C.GR_EVENT_FLOOD_ADD
	EventFloodDel  uint32 = C.GR_EVENT_FLOOD_DEL

	EventAll uint32 = C.GR_EVENT_ALL
)

// EventSubscribe subscribes to events of a given type.
func (c *Client) EventSubscribe(evType uint32, suppressSelf bool) error {
	var req C.struct_gr_event_subscribe_req
	req.ev_type = C.uint32_t(evType)
	req.suppress_self_events = C.bool(suppressSelf)
	return c.sendOnly(C.GR_EVENT_SUBSCRIBE, C.size_t(unsafe.Sizeof(req)), unsafe.Pointer(&req))
}

// EventUnsubscribe removes all event subscriptions.
func (c *Client) EventUnsubscribe() error {
	var req C.struct_gr_empty
	return c.sendOnly(C.GR_EVENT_UNSUBSCRIBE, C.size_t(unsafe.Sizeof(req)), unsafe.Pointer(&req))
}

// Event is a received event notification.
type Event struct {
	Type       uint32
	PayloadLen uint64
	Payload    []byte
}

// EventRecv receives an event notification (blocking).
func (c *Client) EventRecv() (*Event, error) {
	var cev *C.struct_gr_api_event
	ret, _ := C.gr_api_client_event_recv(c.c, &cev)
	if ret < 0 {
		return nil, apiError("event_recv", int(ret))
	}
	if cev == nil {
		return nil, nil
	}
	defer C.free(unsafe.Pointer(cev))

	ev := &Event{
		Type:       uint32(cev.ev_type),
		PayloadLen: uint64(cev.payload_len),
	}
	if cev.payload_len > 0 {
		ev.Payload = C.GoBytes(unsafe.Pointer(uintptr(unsafe.Pointer(cev))+unsafe.Sizeof(*cev)), C.int(cev.payload_len))
	}
	return ev, nil
}
