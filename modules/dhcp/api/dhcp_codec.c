// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Robin Jarry

#include <gr_codec.h>
#include <gr_dhcp.h>

#include <stddef.h>

static const struct gr_api_codec dhcp_list_codec = {
	.resp_fields = GR_FIELD_DESCS(
		GR_FIELD_U16(struct gr_dhcp_status, iface_id),
		GR_FIELD_U8(struct gr_dhcp_status, state),
		GR_FIELD_IP4(struct gr_dhcp_status, server_ip),
		GR_FIELD_IP4(struct gr_dhcp_status, assigned_ip),
		GR_FIELD_U32(struct gr_dhcp_status, lease_time),
		GR_FIELD_U32(struct gr_dhcp_status, renewal_time),
		GR_FIELD_U32(struct gr_dhcp_status, rebind_time),
		GR_FIELD_END,
	),
	.resp_size = sizeof(struct gr_dhcp_status),
};

static const struct gr_api_codec dhcp_start_codec = {
	.req_fields = GR_FIELD_DESCS(
		GR_FIELD_U16(struct gr_dhcp_start_req, iface_id),
		GR_FIELD_END,
	),
	.req_size = sizeof(struct gr_dhcp_start_req),
};

static const struct gr_api_codec dhcp_stop_codec = {
	.req_fields = GR_FIELD_DESCS(
		GR_FIELD_U16(struct gr_dhcp_stop_req, iface_id),
		GR_FIELD_END,
	),
	.req_size = sizeof(struct gr_dhcp_stop_req),
};

static void __attribute__((constructor)) dhcp_codecs_init(void) {
	gr_api_codec_register(GR_DHCP_LIST, &dhcp_list_codec);
	gr_api_codec_register(GR_DHCP_START, &dhcp_start_codec);
	gr_api_codec_register(GR_DHCP_STOP, &dhcp_stop_codec);
}
