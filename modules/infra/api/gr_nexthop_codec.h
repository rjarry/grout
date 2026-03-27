// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Robin Jarry

// Private header for nexthop codec internals (not installed).

#pragma once

#include <gr_codec.h>
#include <gr_nexthop.h>

// Register a nexthop info codec for a given type.
void gr_nh_info_codec_register(gr_nh_type_t type, const struct gr_field_desc *fields, size_t size);

// Encode: returns malloc'd buffer. Sets *out_len. Caller frees.
ssize_t gr_nexthop_encode(void *buf, size_t buf_len, const void *data, size_t data_len);

// Decode a struct gr_nexthop. Returns malloc'd allocation.
// If alloc_size is non-NULL, set to total allocation size.
void *gr_nexthop_decode(const void *buf, size_t buf_len, size_t *alloc_size);
