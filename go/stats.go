// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Robin Jarry

package grout

/*
#include "shim.h"
*/
import "C"

import "unsafe"

type StatsFlags uint16

const (
	StatsFlagSW   StatsFlags = C.GR_STATS_F_SW
	StatsFlagHW   StatsFlags = C.GR_STATS_F_HW
	StatsFlagZero StatsFlags = C.GR_STATS_F_ZERO
)

// Stat is a graph node statistic entry.
type Stat struct {
	Name      string
	TopoOrder uint64
	Packets   uint64
	Batches   uint64
	Cycles    uint64
}

// StatsGet returns graph statistics matching the given filters.
func (c *Client) StatsGet(flags StatsFlags, cpuID uint16, pattern string) ([]Stat, error) {
	var req C.struct_gr_stats_get_req
	req.flags = C.gr_stats_flags_t(flags)
	req.cpu_id = C.uint16_t(cpuID)
	if pattern != "" {
		cp := C.CString(pattern)
		defer C.free(unsafe.Pointer(cp))
		C.strncpy(&req.pattern[0], cp, C.size_t(len(req.pattern)-1))
	}

	ptr, err := c.sendRecv(C.GR_STATS_GET, C.size_t(unsafe.Sizeof(req)), unsafe.Pointer(&req))
	if err != nil {
		return nil, err
	}
	defer C.free(ptr)

	resp := (*C.struct_gr_stats_get_resp)(ptr)
	n := int(resp.n_stats)
	result := make([]Stat, n)
	for i := 0; i < n; i++ {
		s := C.grout_stat_at(resp, C.int(i))
		result[i] = Stat{
			Name:      C.GoString(&s.name[0]),
			TopoOrder: uint64(s.topo_order),
			Packets:   uint64(s.packets),
			Batches:   uint64(s.batches),
			Cycles:    uint64(s.cycles),
		}
	}
	return result, nil
}

// StatsReset resets all statistics.
func (c *Client) StatsReset() error {
	var req C.struct_gr_empty
	return c.sendOnly(C.GR_STATS_RESET, C.size_t(unsafe.Sizeof(req)), unsafe.Pointer(&req))
}

// GraphConf contains graph processing configuration.
type GraphConf struct {
	RxBurstMax uint16
	VectorMax  uint16
}

// GraphConfGet returns the graph configuration.
func (c *Client) GraphConfGet() (*GraphConf, error) {
	var req C.struct_gr_empty
	ptr, err := c.sendRecv(C.GR_GRAPH_CONF_GET, C.size_t(unsafe.Sizeof(req)), unsafe.Pointer(&req))
	if err != nil {
		return nil, err
	}
	defer C.free(ptr)
	r := (*C.struct_gr_graph_conf)(ptr)
	return &GraphConf{
		RxBurstMax: uint16(r.rx_burst_max),
		VectorMax:  uint16(r.vector_max),
	}, nil
}

// GraphConfSet updates the graph configuration.
func (c *Client) GraphConfSet(conf GraphConf) error {
	var req C.struct_gr_graph_conf
	req.rx_burst_max = C.uint16_t(conf.RxBurstMax)
	req.vector_max = C.uint16_t(conf.VectorMax)
	return c.sendOnly(C.GR_GRAPH_CONF_SET, C.size_t(unsafe.Sizeof(req)), unsafe.Pointer(&req))
}

// GraphDump returns the packet processing graph in DOT format.
func (c *Client) GraphDump(full, byLayer, compact bool) (string, error) {
	var req C.struct_gr_graph_dump_req
	req.full = C.bool(full)
	req.by_layer = C.bool(byLayer)
	req.compact = C.bool(compact)
	ptr, err := c.sendRecv(C.GR_GRAPH_DUMP, C.size_t(unsafe.Sizeof(req)), unsafe.Pointer(&req))
	if err != nil {
		return "", err
	}
	defer C.free(ptr)
	resp := (*C.struct_gr_graph_dump_resp)(ptr)
	return C.GoString(&resp.buf[0]), nil
}

// LogPacketsSet enables or disables packet ingress/egress logging.
func (c *Client) LogPacketsSet(enabled bool) error {
	var req C.struct_gr_log_packets_set_req
	req.enabled = C.bool(enabled)
	return c.sendOnly(C.GR_LOG_PACKETS_SET, C.size_t(unsafe.Sizeof(req)), unsafe.Pointer(&req))
}

// LogEntry is a log level entry.
type LogEntry struct {
	Name  string
	Level uint32
}

// LogLevelList lists log levels.
func (c *Client) LogLevelList(showAll bool) ([]LogEntry, error) {
	var req C.struct_gr_log_level_list_req
	req.show_all = C.bool(showAll)

	var result []LogEntry
	err := c.streamForEach(
		C.GR_LOG_LEVEL_LIST,
		C.size_t(unsafe.Sizeof(req)),
		unsafe.Pointer(&req),
		func(ptr unsafe.Pointer) error {
			e := (*C.struct_gr_log_entry)(ptr)
			result = append(result, LogEntry{
				Name:  C.GoString(&e.name[0]),
				Level: uint32(e.level),
			})
			return nil
		},
	)
	return result, err
}

// LogLevelSet sets the log level for modules matching the pattern.
func (c *Client) LogLevelSet(pattern string, level uint32) error {
	var req C.struct_gr_log_level_set_req
	cp := C.CString(pattern)
	defer C.free(unsafe.Pointer(cp))
	C.strncpy(&req.pattern[0], cp, C.size_t(len(req.pattern)-1))
	req.level = C.uint32_t(level)
	return c.sendOnly(C.GR_LOG_LEVEL_SET, C.size_t(unsafe.Sizeof(req)), unsafe.Pointer(&req))
}

// PacketTraceClear clears the packet trace buffer.
func (c *Client) PacketTraceClear() error {
	var req C.struct_gr_empty
	return c.sendOnly(C.GR_PACKET_TRACE_CLEAR, C.size_t(unsafe.Sizeof(req)), unsafe.Pointer(&req))
}

// PacketTraceDump returns the packet trace buffer contents.
func (c *Client) PacketTraceDump(maxPackets uint16) (string, uint16, error) {
	var req C.struct_gr_packet_trace_dump_req
	req.max_packets = C.uint16_t(maxPackets)
	ptr, err := c.sendRecv(C.GR_PACKET_TRACE_DUMP, C.size_t(unsafe.Sizeof(req)), unsafe.Pointer(&req))
	if err != nil {
		return "", 0, err
	}
	defer C.free(ptr)
	resp := (*C.struct_gr_packet_trace_dump_resp)(ptr)
	trace := C.GoStringN(C.grout_packet_trace_buf(resp), C.int(resp.len))
	return trace, uint16(resp.n_packets), nil
}

// PacketTraceSet enables or disables packet tracing.
func (c *Client) PacketTraceSet(enabled, all bool, ifaceID uint16) error {
	var req C.struct_gr_packet_trace_set_req
	req.enabled = C.bool(enabled)
	req.all = C.bool(all)
	req.iface_id = C.uint16_t(ifaceID)
	return c.sendOnly(C.GR_PACKET_TRACE_SET, C.size_t(unsafe.Sizeof(req)), unsafe.Pointer(&req))
}
