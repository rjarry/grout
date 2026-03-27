// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Robin Jarry

#include <gr_codec.h>
#include <gr_conntrack.h>

#include <stddef.h>

static const struct gr_field_desc conntrack_flow_fields[] = {
	GR_FIELD_IP4(struct gr_conntrack_flow, src),
	GR_FIELD_IP4(struct gr_conntrack_flow, dst),
	GR_FIELD_BE16(struct gr_conntrack_flow, src_id),
	GR_FIELD_BE16(struct gr_conntrack_flow, dst_id),
	GR_FIELD_END,
};

static const struct gr_api_codec conntrack_list_codec = {
	.resp_fields = GR_FIELD_DESCS(
		GR_FIELD_U16(struct gr_conntrack, iface_id),
		GR_FIELD_U8(struct gr_conntrack, af),
		GR_FIELD_U8(struct gr_conntrack, proto),
		GR_FIELD_STRUCT(struct gr_conntrack, fwd_flow, conntrack_flow_fields),
		GR_FIELD_STRUCT(struct gr_conntrack, rev_flow, conntrack_flow_fields),
		GR_FIELD_U64(struct gr_conntrack, last_update),
		GR_FIELD_U32(struct gr_conntrack, id),
		GR_FIELD_I32(struct gr_conntrack, state),
		GR_FIELD_END,
	),
	.resp_size = sizeof(struct gr_conntrack),
};

static const struct gr_api_codec conntrack_flush_codec = {0};

static const struct gr_api_codec conntrack_conf_get_codec = {
	.resp_fields = GR_FIELD_DESCS(
		GR_FIELD_U32(struct gr_conntrack_conf_get_resp, max_count),
		GR_FIELD_U32(struct gr_conntrack_conf_get_resp, timeout_closed_sec),
		GR_FIELD_U32(struct gr_conntrack_conf_get_resp, timeout_new_sec),
		GR_FIELD_U32(struct gr_conntrack_conf_get_resp, timeout_udp_established_sec),
		GR_FIELD_U32(struct gr_conntrack_conf_get_resp, timeout_tcp_established_sec),
		GR_FIELD_U32(struct gr_conntrack_conf_get_resp, timeout_half_close_sec),
		GR_FIELD_U32(struct gr_conntrack_conf_get_resp, timeout_time_wait_sec),
		GR_FIELD_U32(struct gr_conntrack_conf_get_resp, used_count),
		GR_FIELD_END,
	),
	.resp_size = sizeof(struct gr_conntrack_conf_get_resp),
};

static const struct gr_api_codec conntrack_conf_set_codec = {
	.req_fields = GR_FIELD_DESCS(
		GR_FIELD_U32(struct gr_conntrack_conf_set_req, max_count),
		GR_FIELD_U32(struct gr_conntrack_conf_set_req, timeout_closed_sec),
		GR_FIELD_U32(struct gr_conntrack_conf_set_req, timeout_new_sec),
		GR_FIELD_U32(struct gr_conntrack_conf_set_req, timeout_udp_established_sec),
		GR_FIELD_U32(struct gr_conntrack_conf_set_req, timeout_tcp_established_sec),
		GR_FIELD_U32(struct gr_conntrack_conf_set_req, timeout_half_close_sec),
		GR_FIELD_U32(struct gr_conntrack_conf_set_req, timeout_time_wait_sec),
		GR_FIELD_END,
	),
	.req_size = sizeof(struct gr_conntrack_conf_set_req),
};

static void __attribute__((constructor)) conntrack_codecs_init(void) {
	gr_api_codec_register(GR_CONNTRACK_LIST, &conntrack_list_codec);
	gr_api_codec_register(GR_CONNTRACK_FLUSH, &conntrack_flush_codec);
	gr_api_codec_register(GR_CONNTRACK_CONF_GET, &conntrack_conf_get_codec);
	gr_api_codec_register(GR_CONNTRACK_CONF_SET, &conntrack_conf_set_codec);
}
