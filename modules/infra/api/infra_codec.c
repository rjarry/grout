// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Robin Jarry

#include <gr_codec.h>
#include <gr_iface_codec.h>
#include <gr_infra.h>

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

// iface info codec registry ///////////////////////////////////////////////////

struct iface_info_codec {
	const struct gr_field_desc *fields;
	size_t size;
};

static struct iface_info_codec iface_info_codecs[GR_IFACE_TYPE_COUNT];

void gr_iface_info_codec_register(
	gr_iface_type_t type,
	const struct gr_field_desc *fields,
	size_t size
) {
	iface_info_codecs[type] = (struct iface_info_codec) {
		.fields = fields,
		.size = size,
	};
}

static const struct gr_field_desc iface_base_fields[] = {
	GR_FIELD_U16(struct gr_iface, id),
	GR_FIELD_U8(struct gr_iface, type),
	GR_FIELD_U8(struct gr_iface, mode),
	GR_FIELD_U16(struct gr_iface, flags),
	GR_FIELD_U16(struct gr_iface, state),
	GR_FIELD_U16(struct gr_iface, mtu),
	GR_FIELD_U16(struct gr_iface, vrf_id),
	GR_FIELD_U16(struct gr_iface, domain_id),
	GR_FIELD_U32(struct gr_iface, speed),
	GR_FIELD_CSTR(struct gr_iface, name),
	GR_FIELD_CSTR(struct gr_iface, description),
	GR_FIELD_END,
};

// Encode: base map + info map concatenated.
ssize_t gr_iface_encode(void *buf, size_t buf_len, const void *data, size_t data_len) {
	const struct gr_iface *iface = data;
	const struct iface_info_codec *ic = &iface_info_codecs[iface->type];
	size_t info_len = data_len > sizeof(*iface) ? data_len - sizeof(*iface) : 0;

	ssize_t n = gr_mpack_encode(buf, buf_len, iface_base_fields, data);
	if (n < 0 || !ic->fields || info_len == 0)
		return n;

	ssize_t n2 = gr_mpack_encode((char *)buf + n, buf_len - n, ic->fields, iface->info);
	return n2 < 0 ? n2 : n + n2;
}

void *gr_iface_decode(const void *buf, size_t buf_len, size_t *alloc_size) {
	size_t consumed = 0;
	struct gr_iface *iface = gr_mpack_decode(
		buf, buf_len, iface_base_fields, sizeof(struct gr_iface), &consumed
	);
	if (iface == NULL)
		return NULL;

	const struct iface_info_codec *ic = &iface_info_codecs[iface->type];
	if (!ic->fields || consumed >= buf_len) {
		if (alloc_size)
			*alloc_size = sizeof(struct gr_iface);
		return iface;
	}

	void *info = gr_mpack_decode(
		(const char *)buf + consumed, buf_len - consumed, ic->fields, ic->size, NULL
	);
	if (info == NULL) {
		free(iface);
		return NULL;
	}

	size_t total = sizeof(struct gr_iface) + ic->size;
	iface = realloc(iface, total);
	memcpy(iface->info, info, ic->size);
	free(info);

	if (alloc_size)
		*alloc_size = total;
	return iface;
}

static ssize_t iface_encode_cb(void *buf, size_t buf_len, const void *data, size_t data_len) {
	return gr_iface_encode(buf, buf_len, data, data_len);
}

static void *iface_decode_cb(const void *buf, size_t buf_len) {
	return gr_iface_decode(buf, buf_len, NULL);
}

static const struct gr_field_desc iface_set_base_fields[] = {
	GR_FIELD_U64(struct gr_iface_set_req, set_attrs),
	GR_FIELD_END,
};

static ssize_t iface_set_encode(void *buf, size_t buf_len, const void *data, size_t data_len) {
	const struct gr_iface_set_req *req = data;
	size_t iface_len = data_len - offsetof(struct gr_iface_set_req, iface);

	ssize_t n = gr_mpack_encode(buf, buf_len, iface_set_base_fields, data);
	if (n < 0)
		return n;

	ssize_t n2 = gr_iface_encode((char *)buf + n, buf_len - n, &req->iface, iface_len);
	return n2 < 0 ? n2 : n + n2;
}

static void *iface_set_decode(const void *buf, size_t buf_len) {
	size_t consumed = 0;
	struct gr_iface_set_req *tmp = gr_mpack_decode(
		buf, buf_len, iface_set_base_fields, sizeof(struct gr_iface_set_req), &consumed
	);
	if (tmp == NULL)
		return NULL;

	size_t iface_alloc = 0;
	void *iface = gr_iface_decode(
		(const char *)buf + consumed, buf_len - consumed, &iface_alloc
	);
	if (iface == NULL)
		return NULL;

	struct gr_iface_set_req *req = realloc(
		tmp, offsetof(struct gr_iface_set_req, iface) + iface_alloc
	);
	if (req == NULL) {
		free(tmp);
		free(iface);
		return NULL;
	}
	memcpy(&req->iface, iface, iface_alloc);
	free(iface);

	return req;
}

// Infra iface info types (port, vlan, vrf, bond) registered here.

static const struct gr_field_desc port_info_fields[] = {
	GR_FIELD_U16(struct gr_iface_info_port, n_rxq),
	GR_FIELD_U16(struct gr_iface_info_port, n_txq),
	GR_FIELD_U16(struct gr_iface_info_port, rxq_size),
	GR_FIELD_U16(struct gr_iface_info_port, txq_size),
	GR_FIELD_MAC(struct gr_iface_info_port, mac),
	GR_FIELD_CSTR(struct gr_iface_info_port, devargs),
	GR_FIELD_CSTR(struct gr_iface_info_port, driver_name),
	GR_FIELD_END,
};

static const struct gr_field_desc vrf_fib_fields[] = {
	GR_FIELD_U32(struct gr_iface_info_vrf_fib, max_routes),
	GR_FIELD_U32(struct gr_iface_info_vrf_fib, num_tbl8),
	GR_FIELD_END,
};

static const struct gr_field_desc vrf_info_fields[] = {
	GR_FIELD_STRUCT(struct gr_iface_info_vrf, ipv4, vrf_fib_fields),
	GR_FIELD_STRUCT(struct gr_iface_info_vrf, ipv6, vrf_fib_fields),
	GR_FIELD_END,
};

static const struct gr_field_desc vlan_info_fields[] = {
	GR_FIELD_U16(struct gr_iface_info_vlan, parent_id),
	GR_FIELD_U16(struct gr_iface_info_vlan, vlan_id),
	GR_FIELD_MAC(struct gr_iface_info_vlan, mac),
	GR_FIELD_END,
};

static const struct gr_field_desc bond_member_fields[] = {
	GR_FIELD_U16(struct gr_bond_member, iface_id),
	GR_FIELD_BOOL(struct gr_bond_member, active),
	GR_FIELD_END,
};

static const struct gr_field_desc bond_info_fields[] = {
	GR_FIELD_U8(struct gr_iface_info_bond, mode),
	GR_FIELD_U8(struct gr_iface_info_bond, algo),
	GR_FIELD_MAC(struct gr_iface_info_bond, mac),
	GR_FIELD_U8(struct gr_iface_info_bond, primary_member),
	GR_FIELD_U8(struct gr_iface_info_bond, n_members),
	GR_FIELD_ARRAY(struct gr_iface_info_bond, members, n_members, bond_member_fields),
	GR_FIELD_END,
};

// interface management ////////////////////////////////////////////////////////

static const struct gr_api_codec iface_add_codec = {
	.encode_req = iface_encode_cb,
	.decode_req = iface_decode_cb,
	.resp_fields = (const struct gr_field_desc []) {
		GR_FIELD_U16(struct gr_iface_add_resp, iface_id),
		GR_FIELD_END,
	},
	.resp_size = sizeof(struct gr_iface_add_resp),
};

static const struct gr_api_codec iface_del_codec = {
	.req_fields = (const struct gr_field_desc []) {
		GR_FIELD_U16(struct gr_iface_del_req, iface_id),
		GR_FIELD_END,
	},
	.req_size = sizeof(struct gr_iface_del_req),
};

static const struct gr_api_codec iface_set_codec = {
	.encode_req = iface_set_encode,
	.decode_req = iface_set_decode,
};

static const struct gr_api_codec iface_get_codec = {
	.req_fields = (const struct gr_field_desc []) {
		GR_FIELD_U16(struct gr_iface_get_req, iface_id),
		GR_FIELD_CSTR(struct gr_iface_get_req, name),
		GR_FIELD_END,
	},
	.req_size = sizeof(struct gr_iface_get_req),
	.encode_resp = iface_encode_cb,
	.decode_resp = iface_decode_cb,
};

static const struct gr_api_codec iface_list_codec = {
	.req_fields = (const struct gr_field_desc []) {
		GR_FIELD_U8(struct gr_iface_list_req, type),
		GR_FIELD_END,
	},
	.req_size = sizeof(struct gr_iface_list_req),
	.encode_resp = iface_encode_cb,
	.decode_resp = iface_decode_cb,
};

// port rxqs ///////////////////////////////////////////////////////////////////

static const struct gr_api_codec affinity_rxq_list_codec = {
	.resp_fields = (const struct gr_field_desc []) {
		GR_FIELD_U16(struct gr_port_rxq_map, iface_id),
		GR_FIELD_U16(struct gr_port_rxq_map, rxq_id),
		GR_FIELD_U16(struct gr_port_rxq_map, cpu_id),
		GR_FIELD_U16(struct gr_port_rxq_map, enabled),
		GR_FIELD_END,
	},
	.resp_size = sizeof(struct gr_port_rxq_map),
};

static const struct gr_api_codec affinity_rxq_set_codec = {
	.req_fields = (const struct gr_field_desc []) {
		GR_FIELD_U16(struct gr_affinity_rxq_set_req, iface_id),
		GR_FIELD_U16(struct gr_affinity_rxq_set_req, rxq_id),
		GR_FIELD_U16(struct gr_affinity_rxq_set_req, cpu_id),
		GR_FIELD_END,
	},
	.req_size = sizeof(struct gr_affinity_rxq_set_req),
};

// iface stats /////////////////////////////////////////////////////////////////

static const struct gr_field_desc iface_stat_fields[] = {
	GR_FIELD_U16(struct gr_iface_stats, iface_id),
	GR_FIELD_U64(struct gr_iface_stats, rx_packets),
	GR_FIELD_U64(struct gr_iface_stats, rx_bytes),
	GR_FIELD_U64(struct gr_iface_stats, rx_drops),
	GR_FIELD_U64(struct gr_iface_stats, tx_packets),
	GR_FIELD_U64(struct gr_iface_stats, tx_bytes),
	GR_FIELD_U64(struct gr_iface_stats, tx_errors),
	GR_FIELD_U64(struct gr_iface_stats, cp_rx_packets),
	GR_FIELD_U64(struct gr_iface_stats, cp_rx_bytes),
	GR_FIELD_U64(struct gr_iface_stats, cp_tx_packets),
	GR_FIELD_U64(struct gr_iface_stats, cp_tx_bytes),
	GR_FIELD_END,
};

static const struct gr_api_codec iface_stats_get_codec = {
	.resp_fields = (const struct gr_field_desc []) {
		GR_FIELD_U16(struct gr_iface_stats_get_resp, n_stats),
		GR_FIELD_ARRAY(struct gr_iface_stats_get_resp, stats, n_stats, iface_stat_fields),
		GR_FIELD_END,
	},
	.resp_size = sizeof(struct gr_iface_stats_get_resp),
};

// graph stats /////////////////////////////////////////////////////////////////

static const struct gr_field_desc stat_fields[] = {
	GR_FIELD_CSTR(struct gr_stat, name),
	GR_FIELD_U64(struct gr_stat, topo_order),
	GR_FIELD_U64(struct gr_stat, packets),
	GR_FIELD_U64(struct gr_stat, batches),
	GR_FIELD_U64(struct gr_stat, cycles),
	GR_FIELD_END,
};

static const struct gr_api_codec stats_get_codec = {
	.req_fields = (const struct gr_field_desc []) {
		GR_FIELD_U16(struct gr_stats_get_req, flags),
		GR_FIELD_U16(struct gr_stats_get_req, cpu_id),
		GR_FIELD_CSTR(struct gr_stats_get_req, pattern),
		GR_FIELD_END,
	},
	.req_size = sizeof(struct gr_stats_get_req),
	.resp_fields = (const struct gr_field_desc []) {
		GR_FIELD_U16(struct gr_stats_get_resp, n_stats),
		GR_FIELD_ARRAY(struct gr_stats_get_resp, stats, n_stats, stat_fields),
		GR_FIELD_END,
	},
	.resp_size = sizeof(struct gr_stats_get_resp),
};

// graph ///////////////////////////////////////////////////////////////////////

static const struct gr_api_codec graph_dump_codec = {
	.req_fields = (const struct gr_field_desc []) {
		GR_FIELD_BOOL(struct gr_graph_dump_req, full),
		GR_FIELD_BOOL(struct gr_graph_dump_req, by_layer),
		GR_FIELD_BOOL(struct gr_graph_dump_req, compact),
		GR_FIELD_END,
	},
	.req_size = sizeof(struct gr_graph_dump_req),
	// Response is raw DOT string - raw fallback.
};

static const struct gr_field_desc graph_conf_fields[] = {
	GR_FIELD_U16(struct gr_graph_conf, rx_burst_max),
	GR_FIELD_U16(struct gr_graph_conf, vector_max),
	GR_FIELD_END,
};

static const struct gr_api_codec graph_conf_get_codec = {
	.resp_fields = graph_conf_fields,
	.resp_size = sizeof(struct gr_graph_conf),
};

static const struct gr_api_codec graph_conf_set_codec = {
	.req_fields = graph_conf_fields,
	.req_size = sizeof(struct gr_graph_conf),
};

// packet tracing //////////////////////////////////////////////////////////////

static const struct gr_field_desc trace_dump_resp_fields[] = {
	GR_FIELD_U16(struct gr_packet_trace_dump_resp, n_packets),
	GR_FIELD_U32(struct gr_packet_trace_dump_resp, len),
	GR_FIELD_ARRAY(struct gr_packet_trace_dump_resp, trace, len, NULL),
	GR_FIELD_END,
};

static void *trace_dump_decode(const void *buf, size_t buf_len) {
	return gr_mpack_decode(
		buf, buf_len, trace_dump_resp_fields, sizeof(struct gr_packet_trace_dump_resp), NULL
	);
}

static const struct gr_api_codec trace_dump_codec = {
	.req_fields = (const struct gr_field_desc[]) {
		GR_FIELD_U16(struct gr_packet_trace_dump_req, max_packets),
		GR_FIELD_END,
	},
	.req_size = sizeof(struct gr_packet_trace_dump_req),
	.resp_fields = trace_dump_resp_fields,
	.resp_size = sizeof(struct gr_packet_trace_dump_resp),
	.decode_resp = trace_dump_decode,
};

static const struct gr_api_codec trace_set_codec = {
	.req_fields = (const struct gr_field_desc[]) {
		GR_FIELD_BOOL(struct gr_packet_trace_set_req, enabled),
		GR_FIELD_BOOL(struct gr_packet_trace_set_req, all),
		GR_FIELD_U16(struct gr_packet_trace_set_req, iface_id),
		GR_FIELD_END,
	},
	.req_size = sizeof(struct gr_packet_trace_set_req),
};

// cpu affinities //////////////////////////////////////////////////////////////

static const struct gr_api_codec affinity_cpu_get_codec = {
	.resp_fields = (const struct gr_field_desc []) {
		GR_FIELD_BIN(struct gr_affinity_cpu_get_resp, control_cpus, sizeof(cpu_set_t)),
		GR_FIELD_BIN(struct gr_affinity_cpu_get_resp, datapath_cpus, sizeof(cpu_set_t)),
		GR_FIELD_END,
	},
	.resp_size = sizeof(struct gr_affinity_cpu_get_resp),
};

static const struct gr_api_codec affinity_cpu_set_codec = {
	.req_fields = (const struct gr_field_desc[]) {
		GR_FIELD_BIN(struct gr_affinity_cpu_set_req, control_cpus, sizeof(cpu_set_t)),
		GR_FIELD_BIN(struct gr_affinity_cpu_set_req, datapath_cpus, sizeof(cpu_set_t)),
		GR_FIELD_END,
	},
	.req_size = sizeof(struct gr_affinity_cpu_set_req),
};

// registration ////////////////////////////////////////////////////////////////

static void __attribute__((constructor)) infra_codecs_init(void) {
	// Iface info type registrations.
	gr_iface_info_codec_register(
		GR_IFACE_TYPE_PORT, port_info_fields, sizeof(struct gr_iface_info_port)
	);
	gr_iface_info_codec_register(
		GR_IFACE_TYPE_VRF, vrf_info_fields, sizeof(struct gr_iface_info_vrf)
	);
	gr_iface_info_codec_register(
		GR_IFACE_TYPE_VLAN, vlan_info_fields, sizeof(struct gr_iface_info_vlan)
	);
	gr_iface_info_codec_register(
		GR_IFACE_TYPE_BOND, bond_info_fields, sizeof(struct gr_iface_info_bond)
	);

	// Interface management.
	gr_api_codec_register(GR_IFACE_ADD, &iface_add_codec);
	gr_api_codec_register(GR_IFACE_DEL, &iface_del_codec);
	gr_api_codec_register(GR_IFACE_GET, &iface_get_codec);
	gr_api_codec_register(GR_IFACE_LIST, &iface_list_codec);
	gr_api_codec_register(GR_IFACE_SET, &iface_set_codec);
	gr_api_codec_register(GR_IFACE_STATS_GET, &iface_stats_get_codec);

	// Port RX queue affinity.
	gr_api_codec_register(GR_AFFINITY_RXQ_LIST, &affinity_rxq_list_codec);
	gr_api_codec_register(GR_AFFINITY_RXQ_SET, &affinity_rxq_set_codec);

	// Stats.
	gr_api_codec_register(GR_STATS_GET, &stats_get_codec);

	// Graph.
	gr_api_codec_register(GR_GRAPH_DUMP, &graph_dump_codec);

	gr_api_codec_register(GR_GRAPH_CONF_GET, &graph_conf_get_codec);
	gr_api_codec_register(GR_GRAPH_CONF_SET, &graph_conf_set_codec);

	// Packet tracing.
	gr_api_codec_register(GR_PACKET_TRACE_DUMP, &trace_dump_codec);
	gr_api_codec_register(GR_PACKET_TRACE_SET, &trace_set_codec);

	// CPU affinity.
	gr_api_codec_register(GR_AFFINITY_CPU_GET, &affinity_cpu_get_codec);
	gr_api_codec_register(GR_AFFINITY_CPU_SET, &affinity_cpu_set_codec);

	// Iface event codecs reuse the list response codec.
	gr_event_codec_register(GR_EVENT_IFACE_ADD, &iface_list_codec);
	gr_event_codec_register(GR_EVENT_IFACE_POST_ADD, &iface_list_codec);
	gr_event_codec_register(GR_EVENT_IFACE_PRE_REMOVE, &iface_list_codec);
	gr_event_codec_register(GR_EVENT_IFACE_REMOVE, &iface_list_codec);
	gr_event_codec_register(GR_EVENT_IFACE_POST_RECONFIG, &iface_list_codec);
	gr_event_codec_register(GR_EVENT_IFACE_STATUS_UP, &iface_list_codec);
	gr_event_codec_register(GR_EVENT_IFACE_STATUS_DOWN, &iface_list_codec);
	gr_event_codec_register(GR_EVENT_IFACE_MAC_CHANGE, &iface_list_codec);
}
