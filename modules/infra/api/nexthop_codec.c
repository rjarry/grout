// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Robin Jarry

#include <gr_infra.h>
#include <gr_nexthop_codec.h>

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

struct nh_info_codec {
	const struct gr_field_desc *fields;
	size_t size;
};

static struct nh_info_codec nh_info_codecs[UINT_NUM_VALUES(gr_nh_type_t)];

void gr_nh_info_codec_register(gr_nh_type_t type, const struct gr_field_desc *fields, size_t size) {
	nh_info_codecs[type] = (struct nh_info_codec) {
		.fields = fields,
		.size = size,
	};
}

static const struct gr_field_desc nh_base_fields[] = {
	GR_FIELD_U8(struct gr_nexthop, type),
	GR_FIELD_U8(struct gr_nexthop, origin),
	GR_FIELD_U16(struct gr_nexthop, iface_id),
	GR_FIELD_U16(struct gr_nexthop, vrf_id),
	GR_FIELD_U32(struct gr_nexthop, nh_id),
	GR_FIELD_END,
};

// Encode: base map + info map concatenated.
ssize_t gr_nexthop_encode(void *buf, size_t buf_len, const void *data, size_t data_len) {
	const struct gr_nexthop *nh = data;
	const struct nh_info_codec *ic = &nh_info_codecs[nh->type];
	size_t info_len = data_len > sizeof(*nh) ? data_len - sizeof(*nh) : 0;

	ssize_t n = gr_mpack_encode(buf, buf_len, nh_base_fields, data);
	if (n < 0 || !ic->fields || info_len == 0)
		return n;

	ssize_t n2 = gr_mpack_encode((char *)buf + n, buf_len - n, ic->fields, nh->info);
	return n2 < 0 ? n2 : n + n2;
}

void *gr_nexthop_decode(const void *buf, size_t buf_len, size_t *out_alloc_size) {
	size_t consumed = 0;
	struct gr_nexthop *nh = gr_mpack_decode(
		buf, buf_len, nh_base_fields, sizeof(struct gr_nexthop), &consumed
	);
	if (nh == NULL)
		return NULL;

	const struct nh_info_codec *ic = &nh_info_codecs[nh->type];
	if (!ic->fields || consumed >= buf_len) {
		if (out_alloc_size)
			*out_alloc_size = sizeof(struct gr_nexthop);
		return nh;
	}

	// Decode info into the nh->info FAM region.
	// Realloc nh to fit ic->size, then decode directly into nh->info.
	// gr_mpack_decode will further realloc if GR_F_ARRAY needs more space,
	// but since nh->info is embedded (not a separate allocation), we need
	// a different approach: decode into a temporary, then copy.
	void *info = gr_mpack_decode(
		(const char *)buf + consumed, buf_len - consumed, ic->fields, ic->size, NULL
	);
	if (info == NULL) {
		free(nh);
		return NULL;
	}

	// The decoded info may have been realloc'd for arrays.
	// We don't know the exact final size. Use ic->size as minimum.
	// For array types, the elements are written past ic->size.
	// TODO: track exact alloc size in gr_mpack_decode.
	size_t info_alloc = ic->size;
	size_t total = sizeof(struct gr_nexthop) + info_alloc;
	nh = realloc(nh, total);
	memcpy(nh->info, info, info_alloc);
	free(info);

	if (out_alloc_size)
		*out_alloc_size = total;
	return nh;
}

static void *nexthop_decode_cb(const void *buf, size_t buf_len) {
	return gr_nexthop_decode(buf, buf_len, NULL);
}

static const struct gr_field_desc nh_group_member_fields[] = {
	GR_FIELD_U32(struct gr_nexthop_group_member, nh_id),
	GR_FIELD_U32(struct gr_nexthop_group_member, weight),
	GR_FIELD_END,
};

static const struct gr_field_desc nh_info_l3_fields[] = {
	GR_FIELD_U8(struct gr_nexthop_info_l3, state),
	GR_FIELD_U8(struct gr_nexthop_info_l3, flags),
	GR_FIELD_U8(struct gr_nexthop_info_l3, af),
	GR_FIELD_U8(struct gr_nexthop_info_l3, prefixlen),
	GR_FIELD_IP4(struct gr_nexthop_info_l3, ipv4),
	GR_FIELD_IP6(struct gr_nexthop_info_l3, ipv6),
	GR_FIELD_MAC(struct gr_nexthop_info_l3, mac),
	GR_FIELD_END,
};

static const struct gr_field_desc nh_info_group_fields[] = {
	GR_FIELD_U32(struct gr_nexthop_info_group, n_members),
	GR_FIELD_ARRAY(struct gr_nexthop_info_group, members, n_members, nh_group_member_fields),
	GR_FIELD_END,
};

static const struct gr_api_codec nh_config_get_codec = {
	.resp_fields = GR_FIELD_DESCS(
		GR_FIELD_U32(struct gr_nh_config_get_resp, max_count),
		GR_FIELD_U32(struct gr_nh_config_get_resp, lifetime_reachable_sec),
		GR_FIELD_U32(struct gr_nh_config_get_resp, lifetime_unreachable_sec),
		GR_FIELD_U16(struct gr_nh_config_get_resp, max_held_pkts),
		GR_FIELD_U8(struct gr_nh_config_get_resp, max_ucast_probes),
		GR_FIELD_U8(struct gr_nh_config_get_resp, max_bcast_probes),
		GR_FIELD_U32(struct gr_nh_config_get_resp, used_count),
		GR_FIELD_END,
	),
	.resp_size = sizeof(struct gr_nh_config_get_resp),
};

static const struct gr_api_codec nh_config_set_codec = {
	.req_fields = GR_FIELD_DESCS(
		GR_FIELD_U32(struct gr_nh_config_set_req, max_count),
		GR_FIELD_U32(struct gr_nh_config_set_req, lifetime_reachable_sec),
		GR_FIELD_U32(struct gr_nh_config_set_req, lifetime_unreachable_sec),
		GR_FIELD_U16(struct gr_nh_config_set_req, max_held_pkts),
		GR_FIELD_U8(struct gr_nh_config_set_req, max_ucast_probes),
		GR_FIELD_U8(struct gr_nh_config_set_req, max_bcast_probes),
		GR_FIELD_END,
	),
	.req_size = sizeof(struct gr_nh_config_set_req),
};

static const struct gr_api_codec nh_del_codec = {
	.req_fields = GR_FIELD_DESCS(
		GR_FIELD_U32(struct gr_nh_del_req, nh_id),
		GR_FIELD_BOOL(struct gr_nh_del_req, missing_ok),
		GR_FIELD_END,
	),
	.req_size = sizeof(struct gr_nh_del_req),
};

// GR_NH_ADD request: {bool exist_ok; struct gr_nexthop nh;} (FAM).
static const struct gr_field_desc nh_add_base_fields[] = {
	GR_FIELD_BOOL(struct gr_nh_add_req, exist_ok),
	GR_FIELD_END,
};

static ssize_t nh_add_encode(void *buf, size_t buf_len, const void *data, size_t data_len) {
	const struct gr_nh_add_req *req = data;
	size_t nh_len = data_len - offsetof(struct gr_nh_add_req, nh);

	ssize_t n = gr_mpack_encode(buf, buf_len, nh_add_base_fields, data);
	if (n < 0)
		return n;

	ssize_t n2 = gr_nexthop_encode((char *)buf + n, buf_len - n, &req->nh, nh_len);
	return n2 < 0 ? n2 : n + n2;
}

static void *nh_add_decode(const void *buf, size_t buf_len) {
	size_t consumed = 0;
	struct gr_nh_add_req *tmp = gr_mpack_decode(
		buf, buf_len, nh_add_base_fields, sizeof(struct gr_nh_add_req), &consumed
	);
	if (tmp == NULL)
		return NULL;

	size_t nh_alloc = 0;
	void *nh = gr_nexthop_decode((const char *)buf + consumed, buf_len - consumed, &nh_alloc);
	if (nh == NULL) {
		free(tmp);
		return NULL;
	}

	struct gr_nh_add_req *req = realloc(tmp, offsetof(struct gr_nh_add_req, nh) + nh_alloc);
	if (req == NULL) {
		free(tmp);
		free(nh);
		return NULL;
	}
	memcpy(&req->nh, nh, nh_alloc);
	free(nh);

	return req;
}

static const struct gr_api_codec nh_add_codec = {
	.encode_req = nh_add_encode,
	.decode_req = nh_add_decode,
};

static const struct gr_api_codec nh_list_codec = {
	.req_fields = GR_FIELD_DESCS(
		GR_FIELD_U16(struct gr_nh_list_req, vrf_id),
		GR_FIELD_U8(struct gr_nh_list_req, type),
		GR_FIELD_U16(struct gr_nh_list_req, max_count),
		GR_FIELD_BOOL(struct gr_nh_list_req, include_internal),
		GR_FIELD_END,
	),
	.req_size = sizeof(struct gr_nh_list_req),
	.encode_resp = gr_nexthop_encode,
	.decode_resp = nexthop_decode_cb,
};

static const struct gr_api_codec nh_get_codec = {
	.req_fields = GR_FIELD_DESCS(GR_FIELD_U32(struct gr_nh_get_req, nh_id), GR_FIELD_END),
	.req_size = sizeof(struct gr_nh_get_req),
	.encode_resp = gr_nexthop_encode,
	.decode_resp = nexthop_decode_cb,
};

static void __attribute__((constructor)) nexthop_codecs_init(void) {
	gr_nh_info_codec_register(GR_NH_T_L3, nh_info_l3_fields, sizeof(struct gr_nexthop_info_l3));
	gr_nh_info_codec_register(
		GR_NH_T_GROUP, nh_info_group_fields, sizeof(struct gr_nexthop_info_group)
	);

	gr_api_codec_register(GR_NH_CONFIG_GET, &nh_config_get_codec);
	gr_api_codec_register(GR_NH_CONFIG_SET, &nh_config_set_codec);
	gr_api_codec_register(GR_NH_DEL, &nh_del_codec);
	gr_api_codec_register(GR_NH_ADD, &nh_add_codec);
	gr_api_codec_register(GR_NH_LIST, &nh_list_codec);
	gr_api_codec_register(GR_NH_GET, &nh_get_codec);

	gr_event_codec_register(GR_EVENT_NEXTHOP_NEW, &nh_list_codec);
	gr_event_codec_register(GR_EVENT_NEXTHOP_DELETE, &nh_list_codec);
	gr_event_codec_register(GR_EVENT_NEXTHOP_UPDATE, &nh_list_codec);
}
