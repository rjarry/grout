// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Robin Jarry

// Private header for iface codec internals (not installed).

#pragma once

#include <gr_codec.h>
#include <gr_infra.h>

// Register an iface info codec for a given type. Each module that defines
// a gr_iface_info_* struct calls this from a constructor.
void gr_iface_info_codec_register(
	gr_iface_type_t type,
	const struct gr_field_desc *fields,
	size_t size
);

// Custom MPack encode/decode for struct gr_iface (concatenated base + info maps).
ssize_t gr_iface_encode(void *buf, size_t buf_len, const void *data, size_t data_len);
void *gr_iface_decode(const void *buf, size_t buf_len, size_t *alloc_size);
