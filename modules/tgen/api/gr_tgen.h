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

// Create a flow transmitting a template frame out of tx_iface and expecting it
// back on rx_iface. Both must be port interfaces.
struct gr_tgen_flow_add_req {
	uint16_t tx_iface_id;
	uint16_t rx_iface_id;
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
};

GR_REQ_STREAM(GR_TGEN_FLOW_LIST, struct gr_empty, struct gr_tgen_flow);
