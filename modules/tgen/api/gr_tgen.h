// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Robin Jarry

#pragma once

#include <gr_api.h>

#include <stdbool.h>
#include <stdint.h>

#define GR_TGEN_MODULE 0x7e57 // "TEST"

// Maximum template frame size accepted for a flow.
#define GR_TGEN_MAX_PKT_LEN 9000

enum gr_tgen_requests : uint32_t {
	GR_TGEN_STATUS = GR_MSG_TYPE(GR_TGEN_MODULE, 0x0001),
	GR_TGEN_FLOW_ADD,
	GR_TGEN_FLOW_DEL,
	GR_TGEN_FLOW_CLEAR,
	GR_TGEN_FLOW_LIST,
	GR_TGEN_START,
	GR_TGEN_STOP,
	GR_TGEN_SWEEP_ADD,
	GR_TGEN_SWEEP_DEL,
	GR_TGEN_SWEEP_CLEAR,
	GR_TGEN_SWEEP_LIST,
	GR_TGEN_RFC2544,
};

// Rate specification modes.
typedef enum : uint8_t {
	GR_TGEN_RATE_PCT = 1, // percentage of port line rate
	GR_TGEN_RATE_PPS = 2, // total packets per second
} gr_tgen_rate_mode_t;

struct gr_tgen_status_resp {
	bool running;
	gr_tgen_rate_mode_t rate_mode;
	double rate_value;
	double pps_per_clone;
	uint64_t tx_packets;
	uint64_t tx_bytes;
	uint64_t rx_packets;
	uint64_t rx_bytes;
	uint64_t rx_missed;
	uint64_t drop_packets; // tx_packets - (rx_packets + rx_missed), floored at 0
	// aggregate line rate of the tx ports: the 100% reference
	double line_rate_bps;
	double line_rate_pps;
	// RFC2544 binary search progress
	bool rfc2544_active;
	uint32_t rfc2544_iteration;
	double rfc2544_ndr; // best no-drop rate as a percentage of line rate, -1 if none
	// highest rate actually received during the search, regardless of drops
	double best_effort_pps;
	double best_effort_bps;
};

GR_REQ(GR_TGEN_STATUS, struct gr_empty, struct gr_tgen_status_resp);

// Start transmitting all configured flows at the given rate. If has_port is set,
// only the given transmit port generates traffic.
struct gr_tgen_start_req {
	gr_tgen_rate_mode_t rate_mode;
	double rate_value;
	uint16_t only_iface_id;
	bool has_port;
};

GR_REQ(GR_TGEN_START, struct gr_tgen_start_req, struct gr_empty);

GR_REQ(GR_TGEN_STOP, struct gr_empty, struct gr_empty);

// Template frame representation.
typedef enum : uint8_t {
	GR_TGEN_PKT_RAW = 0, // pkt[] holds the frame bytes verbatim (e.g. from pcap)
	GR_TGEN_PKT_TEXT = 1, // pkt[] holds a NUL-terminated scapy-like expression
} gr_tgen_pkt_format_t;

// Create a flow transmitting a template frame out of tx_iface and expecting it
// back on rx_iface. Both must be port interfaces. A text template is forged into
// frame bytes by the daemon, so the API is usable without grcli.
struct gr_tgen_flow_add_req {
	uint16_t tx_iface_id;
	uint16_t rx_iface_id;
	gr_tgen_pkt_format_t format;
	uint16_t weight; // relative share among flows on the same tx port (>=1)
	uint16_t pkt_len;
	uint8_t pkt[/* pkt_len */];
};

struct gr_tgen_flow_add_resp {
	uint32_t flow_id;
};

GR_REQ(GR_TGEN_FLOW_ADD, struct gr_tgen_flow_add_req, struct gr_tgen_flow_add_resp);

struct gr_tgen_flow_del_req {
	uint32_t flow_id;
};

GR_REQ(GR_TGEN_FLOW_DEL, struct gr_tgen_flow_del_req, struct gr_empty);

GR_REQ(GR_TGEN_FLOW_CLEAR, struct gr_empty, struct gr_empty);

struct gr_tgen_flow {
	uint32_t id;
	uint16_t tx_iface_id;
	uint16_t rx_iface_id;
	uint16_t pkt_len;
	uint16_t weight;
};

GR_REQ_STREAM(GR_TGEN_FLOW_LIST, struct gr_empty, struct gr_tgen_flow);

// Incrementally mutate a packet field to spread traffic across RX queues (RSS).
// The field is `size` bytes at `offset` in the frame, written big-endian, cycling
// over [start, end] by `step` on every transmitted packet.
struct gr_tgen_sweep_add_req {
	uint32_t flow_id;
	uint16_t offset;
	uint16_t size; // field width in bytes, 1 to 8
	uint64_t start;
	uint64_t end;
	uint64_t step;
};

struct gr_tgen_sweep_add_resp {
	uint32_t sweep_id;
};

GR_REQ(GR_TGEN_SWEEP_ADD, struct gr_tgen_sweep_add_req, struct gr_tgen_sweep_add_resp);

struct gr_tgen_sweep_del_req {
	uint32_t sweep_id;
};

GR_REQ(GR_TGEN_SWEEP_DEL, struct gr_tgen_sweep_del_req, struct gr_empty);

GR_REQ(GR_TGEN_SWEEP_CLEAR, struct gr_empty, struct gr_empty);

struct gr_tgen_sweep {
	uint32_t id;
	uint32_t flow_id;
	uint16_t offset;
	uint16_t size;
	uint64_t start;
	uint64_t end;
	uint64_t step;
};

GR_REQ_STREAM(GR_TGEN_SWEEP_LIST, struct gr_empty, struct gr_tgen_sweep);

// Kick off a standalone RFC2544 no-drop-rate binary search. The daemon runs the
// search asynchronously; poll GR_TGEN_STATUS for progress and the result.
struct gr_tgen_rfc2544_req {
	uint32_t max_iterations; // 0 for the default
	double max_drop; // acceptable drop percentage (0 for strict no-drop)
	double duration; // seconds per iteration (0 for the default)
};

GR_REQ(GR_TGEN_RFC2544, struct gr_tgen_rfc2544_req, struct gr_empty);

// Broadcast when the generator starts and stops transmitting. The RFC2544
// fields are only meaningful on the stop event of a search.
enum gr_tgen_events : uint32_t {
	GR_EVENT_TGEN_START = GR_MSG_TYPE(GR_TGEN_MODULE, 0x1001),
	GR_EVENT_TGEN_STOP,
};

struct gr_tgen_event {
	bool rfc2544; // set when the run was an RFC2544 search
	uint32_t rfc2544_iteration;
	double rfc2544_ndr; // percentage of line rate
	// 100% reference, to turn the NDR percentage into an absolute throughput
	double line_rate_bps;
	double line_rate_pps;
	// highest rate actually received during the search, regardless of drops
	double best_effort_pps;
	double best_effort_bps;
};

GR_EVENT(GR_EVENT_TGEN_START, struct gr_tgen_event);
GR_EVENT(GR_EVENT_TGEN_STOP, struct gr_tgen_event);
