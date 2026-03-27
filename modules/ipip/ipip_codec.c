// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Robin Jarry

#include <gr_codec.h>
#include <gr_iface_codec.h>
#include <gr_ipip.h>

#include <stddef.h>

static const struct gr_field_desc ipip_info_fields[] = {
	GR_FIELD_IP4(struct gr_iface_info_ipip, local),
	GR_FIELD_IP4(struct gr_iface_info_ipip, remote),
	GR_FIELD_END,
};

static void __attribute__((constructor)) ipip_codecs_init(void) {
	gr_iface_info_codec_register(
		GR_IFACE_TYPE_IPIP, ipip_info_fields, sizeof(struct gr_iface_info_ipip)
	);
}
