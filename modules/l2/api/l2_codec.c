// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Robin Jarry

#include <gr_codec.h>
#include <gr_iface_codec.h>
#include <gr_l2.h>

#include <stddef.h>

// Bridge iface info.
static const struct gr_field_desc bridge_info_fields[] = {
	GR_FIELD_U16(struct gr_iface_info_bridge, ageing_time),
	GR_FIELD_U16(struct gr_iface_info_bridge, flags),
	GR_FIELD_MAC(struct gr_iface_info_bridge, mac),
	GR_FIELD_U16(struct gr_iface_info_bridge, n_members),
	GR_FIELD_ARRAY(struct gr_iface_info_bridge, members, n_members, NULL),
	GR_FIELD_END,
};

// VXLAN iface info.
static const struct gr_field_desc vxlan_info_fields[] = {
	GR_FIELD_U32(struct gr_iface_info_vxlan, vni),
	GR_FIELD_U16(struct gr_iface_info_vxlan, encap_vrf_id),
	GR_FIELD_U16(struct gr_iface_info_vxlan, dst_port),
	GR_FIELD_IP4(struct gr_iface_info_vxlan, local),
	GR_FIELD_MAC(struct gr_iface_info_vxlan, mac),
	GR_FIELD_END,
};

static const struct gr_field_desc fdb_entry_fields[] = {
	GR_FIELD_U16(struct gr_fdb_entry, bridge_id),
	GR_FIELD_MAC(struct gr_fdb_entry, mac),
	GR_FIELD_U16(struct gr_fdb_entry, vlan_id),
	GR_FIELD_U16(struct gr_fdb_entry, iface_id),
	GR_FIELD_IP4(struct gr_fdb_entry, vtep),
	GR_FIELD_U8(struct gr_fdb_entry, flags),
	GR_FIELD_U64(struct gr_fdb_entry, last_seen),
	GR_FIELD_END,
};

static const struct gr_field_desc flood_vtep_fields[] = {
	GR_FIELD_U32(struct gr_flood_vtep, vni),
	GR_FIELD_IP4(struct gr_flood_vtep, addr),
	GR_FIELD_END,
};

static const struct gr_field_desc flood_entry_fields[] = {
	GR_FIELD_U8(struct gr_flood_entry, type),
	GR_FIELD_U16(struct gr_flood_entry, vrf_id),
	GR_FIELD_STRUCT(struct gr_flood_entry, vtep, flood_vtep_fields),
	GR_FIELD_END,
};

static const struct gr_api_codec fdb_add_codec = {
	.req_fields = GR_FIELD_DESCS(
		GR_FIELD_STRUCT(struct gr_fdb_add_req, fdb, fdb_entry_fields),
		GR_FIELD_BOOL(struct gr_fdb_add_req, exist_ok),
		GR_FIELD_END,
	),
	.req_size = sizeof(struct gr_fdb_add_req),
};

static const struct gr_api_codec fdb_del_codec = {
	.req_fields = GR_FIELD_DESCS(
		GR_FIELD_U16(struct gr_fdb_del_req, bridge_id),
		GR_FIELD_MAC(struct gr_fdb_del_req, mac),
		GR_FIELD_U16(struct gr_fdb_del_req, vlan_id),
		GR_FIELD_BOOL(struct gr_fdb_del_req, missing_ok),
		GR_FIELD_END,
	),
	.req_size = sizeof(struct gr_fdb_del_req),
};

static const struct gr_api_codec fdb_flush_codec = {
	.req_fields = GR_FIELD_DESCS(
		GR_FIELD_U16(struct gr_fdb_flush_req, bridge_id),
		GR_FIELD_MAC(struct gr_fdb_flush_req, mac),
		GR_FIELD_U16(struct gr_fdb_flush_req, iface_id),
		GR_FIELD_U8(struct gr_fdb_flush_req, flags),
		GR_FIELD_END,
	),
	.req_size = sizeof(struct gr_fdb_flush_req),
};

static const struct gr_api_codec fdb_list_codec = {
	.req_fields = GR_FIELD_DESCS(
		GR_FIELD_U16(struct gr_fdb_list_req, bridge_id),
		GR_FIELD_U16(struct gr_fdb_list_req, iface_id),
		GR_FIELD_U8(struct gr_fdb_list_req, flags),
		GR_FIELD_END,
	),
	.req_size = sizeof(struct gr_fdb_list_req),
	.resp_fields = fdb_entry_fields,
	.resp_size = sizeof(struct gr_fdb_entry),
};

static const struct gr_api_codec fdb_config_get_codec = {
	.resp_fields = GR_FIELD_DESCS(
		GR_FIELD_U32(struct gr_fdb_config_get_resp, max_entries),
		GR_FIELD_U32(struct gr_fdb_config_get_resp, used_entries),
		GR_FIELD_END,
	),
	.resp_size = sizeof(struct gr_fdb_config_get_resp),
};

static const struct gr_api_codec fdb_config_set_codec = {
	.req_fields = GR_FIELD_DESCS(
		GR_FIELD_U32(struct gr_fdb_config_set_req, max_entries),
		GR_FIELD_END,
	),
	.req_size = sizeof(struct gr_fdb_config_set_req),
};

static const struct gr_api_codec flood_add_codec = {
	.req_fields = GR_FIELD_DESCS(
		GR_FIELD_STRUCT(struct gr_flood_add_req, entry, flood_entry_fields),
		GR_FIELD_BOOL(struct gr_flood_add_req, exist_ok),
		GR_FIELD_END,
	),
	.req_size = sizeof(struct gr_flood_add_req),
};

static const struct gr_api_codec flood_del_codec = {
	.req_fields = GR_FIELD_DESCS(
		GR_FIELD_STRUCT(struct gr_flood_del_req, entry, flood_entry_fields),
		GR_FIELD_BOOL(struct gr_flood_del_req, missing_ok),
		GR_FIELD_END,
	),
	.req_size = sizeof(struct gr_flood_del_req),
};

static const struct gr_api_codec flood_list_codec = {
	.req_fields = GR_FIELD_DESCS(
		GR_FIELD_U8(struct gr_flood_list_req, type),
		GR_FIELD_U16(struct gr_flood_list_req, vrf_id),
		GR_FIELD_END,
	),
	.req_size = sizeof(struct gr_flood_list_req),
	.resp_fields = flood_entry_fields,
	.resp_size = sizeof(struct gr_flood_entry),
};

static void __attribute__((constructor)) l2_codecs_init(void) {
	gr_iface_info_codec_register(
		GR_IFACE_TYPE_BRIDGE, bridge_info_fields, sizeof(struct gr_iface_info_bridge)
	);
	gr_iface_info_codec_register(
		GR_IFACE_TYPE_VXLAN, vxlan_info_fields, sizeof(struct gr_iface_info_vxlan)
	);

	gr_api_codec_register(GR_FDB_ADD, &fdb_add_codec);
	gr_api_codec_register(GR_FDB_DEL, &fdb_del_codec);
	gr_api_codec_register(GR_FDB_FLUSH, &fdb_flush_codec);
	gr_api_codec_register(GR_FDB_LIST, &fdb_list_codec);
	gr_api_codec_register(GR_FDB_CONFIG_GET, &fdb_config_get_codec);
	gr_api_codec_register(GR_FDB_CONFIG_SET, &fdb_config_set_codec);
	gr_api_codec_register(GR_FLOOD_ADD, &flood_add_codec);
	gr_api_codec_register(GR_FLOOD_DEL, &flood_del_codec);
	gr_api_codec_register(GR_FLOOD_LIST, &flood_list_codec);

	// Event codecs reuse the list response codecs.
	gr_event_serializer(GR_EVENT_FDB_ADD, fdb_entry_fields, NULL);
	gr_event_serializer(GR_EVENT_FDB_DEL, fdb_entry_fields, NULL);
	gr_event_serializer(GR_EVENT_FDB_UPDATE, fdb_entry_fields, NULL);
	gr_event_codec_register(GR_EVENT_FDB_ADD, &fdb_list_codec);
	gr_event_codec_register(GR_EVENT_FDB_DEL, &fdb_list_codec);
	gr_event_codec_register(GR_EVENT_FDB_UPDATE, &fdb_list_codec);

	gr_event_serializer(GR_EVENT_FLOOD_ADD, flood_entry_fields, NULL);
	gr_event_serializer(GR_EVENT_FLOOD_DEL, flood_entry_fields, NULL);
	gr_event_codec_register(GR_EVENT_FLOOD_ADD, &flood_list_codec);
	gr_event_codec_register(GR_EVENT_FLOOD_DEL, &flood_list_codec);
}
