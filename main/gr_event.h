// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025 Robin Jarry

#pragma once

#include <stddef.h>
#include <stdint.h>

typedef void (*gr_event_sub_cb_t)(uint32_t ev_type, const void *obj);

// Register a callback for a specific event type
void gr_event_subscribe(uint32_t ev_type, gr_event_sub_cb_t callback);

// Notify all subscribers (see gr_event_subscribe)
void gr_event_push(uint32_t ev_type, const void *obj);
