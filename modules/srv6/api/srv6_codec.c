// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Robin Jarry

#include <gr_codec.h>
#include <gr_nexthop_codec.h>
#include <gr_srv6.h>

#include <stddef.h>

static const struct gr_api_codec srv6_tunsrc_set_codec = {
	.req_fields = GR_FIELD_DESCS(
		GR_FIELD_IP6(struct gr_srv6_tunsrc_set_req, addr),
		GR_FIELD_END,
	),
	.req_size = sizeof(struct gr_srv6_tunsrc_set_req),
};

static const struct gr_api_codec srv6_tunsrc_clear_codec = {0};

static const struct gr_api_codec srv6_tunsrc_show_codec = {
	.resp_fields = GR_FIELD_DESCS(
		GR_FIELD_IP6(struct gr_srv6_tunsrc_show_resp, addr),
		GR_FIELD_END,
	),
	.resp_size = sizeof(struct gr_srv6_tunsrc_show_resp),
};

// SRv6 local nexthop info (fixed size).
static const struct gr_field_desc nh_info_srv6_local_fields[] = {
	GR_FIELD_U16(struct gr_nexthop_info_srv6_local, out_vrf_id),
	GR_FIELD_U16(struct gr_nexthop_info_srv6_local, behavior),
	GR_FIELD_U8(struct gr_nexthop_info_srv6_local, flags),
	GR_FIELD_U8(struct gr_nexthop_info_srv6_local, block_bits),
	GR_FIELD_U8(struct gr_nexthop_info_srv6_local, csid_bits),
	GR_FIELD_END,
};

// SRv6 output nexthop info (FAM: seglist[]).
static const struct gr_field_desc nh_info_srv6_fields[] = {
	GR_FIELD_U8(struct gr_nexthop_info_srv6, encap_behavior),
	GR_FIELD_U8(struct gr_nexthop_info_srv6, n_seglist),
	GR_FIELD_ARRAY(struct gr_nexthop_info_srv6, seglist, n_seglist, NULL),
	GR_FIELD_END,
};

static void __attribute__((constructor)) srv6_codecs_init(void) {
	gr_nh_info_codec_register(
		GR_NH_T_SR6_LOCAL,
		nh_info_srv6_local_fields,
		sizeof(struct gr_nexthop_info_srv6_local)
	);
	gr_nh_info_codec_register(
		GR_NH_T_SR6_OUTPUT, nh_info_srv6_fields, sizeof(struct gr_nexthop_info_srv6)
	);

	gr_api_codec_register(GR_SRV6_TUNSRC_SET, &srv6_tunsrc_set_codec);
	gr_api_codec_register(GR_SRV6_TUNSRC_CLEAR, &srv6_tunsrc_clear_codec);
	gr_api_codec_register(GR_SRV6_TUNSRC_SHOW, &srv6_tunsrc_show_codec);
}
