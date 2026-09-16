// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Robin Jarry

#pragma once

#include <gr_api.h>

#include <stdbool.h>
#include <stdint.h>

#define GR_TGEN_MODULE 0x7e57 // "TEST"

enum gr_tgen_requests : uint32_t {
	GR_TGEN_STATUS = GR_MSG_TYPE(GR_TGEN_MODULE, 0x0001),
};

struct gr_tgen_status_resp {
	bool running;
};

GR_REQ(GR_TGEN_STATUS, struct gr_empty, struct gr_tgen_status_resp);
