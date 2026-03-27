// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Robin Jarry

#include <gr_codec.h>
#include <gr_nat.h>
#include <gr_nexthop_codec.h>

#include <stddef.h>

static const struct gr_field_desc dnat44_policy_fields[] = {
	GR_FIELD_U16(struct gr_dnat44_policy, iface_id),
	GR_FIELD_IP4(struct gr_dnat44_policy, match),
	GR_FIELD_IP4(struct gr_dnat44_policy, replace),
	GR_FIELD_END,
};

static const struct gr_api_codec dnat44_add_codec = {
	.req_fields = GR_FIELD_DESCS(
		GR_FIELD_STRUCT(struct gr_dnat44_add_req, policy, dnat44_policy_fields),
		GR_FIELD_BOOL(struct gr_dnat44_add_req, exist_ok),
		GR_FIELD_END,
	),
	.req_size = sizeof(struct gr_dnat44_add_req),
};

static const struct gr_api_codec dnat44_del_codec = {
	.req_fields = GR_FIELD_DESCS(
		GR_FIELD_U16(struct gr_dnat44_del_req, iface_id),
		GR_FIELD_IP4(struct gr_dnat44_del_req, match),
		GR_FIELD_BOOL(struct gr_dnat44_del_req, missing_ok),
		GR_FIELD_END,
	),
	.req_size = sizeof(struct gr_dnat44_del_req),
};

static const struct gr_api_codec dnat44_list_codec = {
	.req_fields = GR_FIELD_DESCS(
		GR_FIELD_U16(struct gr_dnat44_list_req, vrf_id),
		GR_FIELD_END,
	),
	.req_size = sizeof(struct gr_dnat44_list_req),
	.resp_fields = dnat44_policy_fields,
	.resp_size = sizeof(struct gr_dnat44_policy),
};

static const struct gr_field_desc snat44_policy_fields[] = {
	GR_FIELD_U16(struct gr_snat44_policy, iface_id),
	GR_FIELD_IP4_NET(struct gr_snat44_policy, net),
	GR_FIELD_IP4(struct gr_snat44_policy, replace),
	GR_FIELD_END,
};

static const struct gr_api_codec snat44_add_codec = {
	.req_fields = GR_FIELD_DESCS(
		GR_FIELD_STRUCT(struct gr_snat44_add_req, policy, snat44_policy_fields),
		GR_FIELD_BOOL(struct gr_snat44_add_req, exist_ok),
		GR_FIELD_END,
	),
	.req_size = sizeof(struct gr_snat44_add_req),
};

static const struct gr_api_codec snat44_del_codec = {
	.req_fields = GR_FIELD_DESCS(
		GR_FIELD_STRUCT(struct gr_snat44_del_req, policy, snat44_policy_fields),
		GR_FIELD_BOOL(struct gr_snat44_del_req, missing_ok),
		GR_FIELD_END,
	),
	.req_size = sizeof(struct gr_snat44_del_req),
};

static const struct gr_api_codec snat44_list_codec = {
	.resp_fields = snat44_policy_fields,
	.resp_size = sizeof(struct gr_snat44_policy),
};

static const struct gr_field_desc nh_info_dnat_fields[] = {
	GR_FIELD_IP4(struct gr_nexthop_info_dnat, match),
	GR_FIELD_IP4(struct gr_nexthop_info_dnat, replace),
	GR_FIELD_END,
};

static void __attribute__((constructor)) nat_codecs_init(void) {
	gr_nh_info_codec_register(
		GR_NH_T_DNAT, nh_info_dnat_fields, sizeof(struct gr_nexthop_info_dnat)
	);

	gr_api_codec_register(GR_DNAT44_ADD, &dnat44_add_codec);
	gr_api_codec_register(GR_DNAT44_DEL, &dnat44_del_codec);
	gr_api_codec_register(GR_DNAT44_LIST, &dnat44_list_codec);
	gr_api_codec_register(GR_SNAT44_ADD, &snat44_add_codec);
	gr_api_codec_register(GR_SNAT44_DEL, &snat44_del_codec);
	gr_api_codec_register(GR_SNAT44_LIST, &snat44_list_codec);
}
