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
};

struct gr_tgen_status_resp {
	bool running;
};

GR_REQ(GR_TGEN_STATUS, struct gr_empty, struct gr_tgen_status_resp);

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
