// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Robin Jarry

#include <gr_codec.h>
#include <gr_ip6.h>
#include <gr_nexthop_codec.h>

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static const struct gr_field_desc ip6_ifaddr_fields[] = GR_FIELD_DESCS(
	GR_FIELD_U16(struct gr_ip6_ifaddr, iface_id),
	GR_FIELD_IP6_NET(struct gr_ip6_ifaddr, addr),
	GR_FIELD_END,
);
// gr_ip6_route: fixed fields + struct gr_nexthop (FAM).
static const struct gr_field_desc ip6_route_base_fields[] = GR_FIELD_DESCS(
	GR_FIELD_IP6_NET(struct gr_ip6_route, dest),
	GR_FIELD_U16(struct gr_ip6_route, vrf_id),
	GR_FIELD_U8(struct gr_ip6_route, origin),
	GR_FIELD_END,
);

static ssize_t ip6_route_encode(void *buf, size_t buf_len, const void *data, size_t data_len) {
	const struct gr_ip6_route *route = data;
	size_t nh_len = data_len - offsetof(struct gr_ip6_route, nh);

	ssize_t n = gr_mpack_encode(buf, buf_len, ip6_route_base_fields, data);
	if (n < 0)
		return n;

	ssize_t n2 = gr_nexthop_encode((char *)buf + n, buf_len - n, &route->nh, nh_len);
	return n2 < 0 ? n2 : n + n2;
}

static void *ip6_route_decode(const void *buf, size_t buf_len) {
	size_t consumed = 0;
	struct gr_ip6_route *route = gr_mpack_decode(
		buf, buf_len, ip6_route_base_fields, sizeof(struct gr_ip6_route), &consumed
	);
	if (route == NULL)
		return NULL;

	size_t nh_alloc = 0;
	void *nh = gr_nexthop_decode((const char *)buf + consumed, buf_len - consumed, &nh_alloc);
	if (nh == NULL) {
		free(route);
		return NULL;
	}

	route = realloc(route, offsetof(struct gr_ip6_route, nh) + nh_alloc);
	memcpy(&route->nh, nh, nh_alloc);
	free(nh);

	return route;
}

static void *ip6_nh_decode(const void *buf, size_t buf_len) {
	return gr_nexthop_decode(buf, buf_len, NULL);
}

static const struct gr_api_codec ip6_route_add_codec = {
	.req_fields = GR_FIELD_DESCS(
		GR_FIELD_U16(struct gr_ip6_route_add_req, vrf_id),
		GR_FIELD_IP6_NET(struct gr_ip6_route_add_req, dest),
		GR_FIELD_IP6(struct gr_ip6_route_add_req, nh),
		GR_FIELD_U32(struct gr_ip6_route_add_req, nh_id),
		GR_FIELD_U8(struct gr_ip6_route_add_req, origin),
		GR_FIELD_BOOL(struct gr_ip6_route_add_req, exist_ok),
		GR_FIELD_END,
	),
	.req_size = sizeof(struct gr_ip6_route_add_req),
};

static const struct gr_api_codec ip6_route_del_codec = {
	.req_fields = GR_FIELD_DESCS(
		GR_FIELD_U16(struct gr_ip6_route_del_req, vrf_id),
		GR_FIELD_IP6_NET(struct gr_ip6_route_del_req, dest),
		GR_FIELD_BOOL(struct gr_ip6_route_del_req, missing_ok),
		GR_FIELD_END,
	),
	.req_size = sizeof(struct gr_ip6_route_del_req),
};

static const struct gr_api_codec ip6_route_get_codec = {
	.req_fields = GR_FIELD_DESCS(
		GR_FIELD_U16(struct gr_ip6_route_get_req, vrf_id),
		GR_FIELD_IP6(struct gr_ip6_route_get_req, dest),
		GR_FIELD_END,
	),
	.req_size = sizeof(struct gr_ip6_route_get_req),
	.encode_resp = gr_nexthop_encode,
	.decode_resp = ip6_nh_decode,
};

static const struct gr_api_codec ip6_route_list_codec = {
	.req_fields = GR_FIELD_DESCS(
		GR_FIELD_U16(struct gr_ip6_route_list_req, vrf_id),
		GR_FIELD_U16(struct gr_ip6_route_list_req, max_count),
		GR_FIELD_END,
	),
	.req_size = sizeof(struct gr_ip6_route_list_req),
	.encode_resp = ip6_route_encode,
	.decode_resp = ip6_route_decode,
};

static const struct gr_api_codec ip6_addr_add_codec = {
	.req_fields = GR_FIELD_DESCS(
		GR_FIELD_STRUCT(struct gr_ip6_addr_add_req, addr, ip6_ifaddr_fields),
		GR_FIELD_BOOL(struct gr_ip6_addr_add_req, exist_ok),
		GR_FIELD_END,
	),
	.req_size = sizeof(struct gr_ip6_addr_add_req),
};

static const struct gr_api_codec ip6_addr_del_codec = {
	.req_fields = GR_FIELD_DESCS(
		GR_FIELD_STRUCT(struct gr_ip6_addr_del_req, addr, ip6_ifaddr_fields),
		GR_FIELD_BOOL(struct gr_ip6_addr_del_req, missing_ok),
		GR_FIELD_END,
	),
	.req_size = sizeof(struct gr_ip6_addr_del_req),
};

static const struct gr_api_codec ip6_addr_list_codec = {
	.req_fields = GR_FIELD_DESCS(
		GR_FIELD_U16(struct gr_ip6_addr_list_req, vrf_id),
		GR_FIELD_U16(struct gr_ip6_addr_list_req, iface_id),
		GR_FIELD_END,
	),
	.req_size = sizeof(struct gr_ip6_addr_list_req),
	.resp_fields = ip6_ifaddr_fields,
	.resp_size = sizeof(struct gr_ip6_ifaddr),
};

static const struct gr_api_codec ip6_addr_flush_codec = {
	.req_fields = GR_FIELD_DESCS(
		GR_FIELD_U16(struct gr_ip6_addr_flush_req, iface_id),
		GR_FIELD_END,
	),
	.req_size = sizeof(struct gr_ip6_addr_flush_req),
};

static const struct gr_api_codec ip6_iface_ra_set_codec = {
	.req_fields = GR_FIELD_DESCS(
		GR_FIELD_U16(struct gr_ip6_ra_set_req, iface_id),
		GR_FIELD_U8(struct gr_ip6_ra_set_req, set_attrs),
		GR_FIELD_U16(struct gr_ip6_ra_set_req, interval),
		GR_FIELD_U16(struct gr_ip6_ra_set_req, lifetime),
		GR_FIELD_END,
	),
	.req_size = sizeof(struct gr_ip6_ra_set_req),
};

static const struct gr_api_codec ip6_iface_ra_clear_codec = {
	.req_fields = GR_FIELD_DESCS(
		GR_FIELD_U16(struct gr_ip6_ra_clear_req, iface_id),
		GR_FIELD_END,
	),
	.req_size = sizeof(struct gr_ip6_ra_clear_req),
};

static const struct gr_api_codec ip6_iface_ra_show_codec = {
	.req_fields = GR_FIELD_DESCS(
		GR_FIELD_U16(struct gr_ip6_ra_show_req, iface_id),
		GR_FIELD_END,
	),
	.req_size = sizeof(struct gr_ip6_ra_show_req),
	.resp_fields = GR_FIELD_DESCS(
		GR_FIELD_BOOL(struct gr_ip6_ra_conf, enabled),
		GR_FIELD_U16(struct gr_ip6_ra_conf, iface_id),
		GR_FIELD_U16(struct gr_ip6_ra_conf, interval),
		GR_FIELD_U16(struct gr_ip6_ra_conf, lifetime),
		GR_FIELD_END,
	),
	.resp_size = sizeof(struct gr_ip6_ra_conf),
};

static const struct gr_api_codec ip6_icmp6_send_codec = {
	.req_fields = GR_FIELD_DESCS(
		GR_FIELD_IP6(struct gr_ip6_icmp_send_req, addr),
		GR_FIELD_U16(struct gr_ip6_icmp_send_req, iface),
		GR_FIELD_U16(struct gr_ip6_icmp_send_req, vrf),
		GR_FIELD_U16(struct gr_ip6_icmp_send_req, ident),
		GR_FIELD_U16(struct gr_ip6_icmp_send_req, seq_num),
		GR_FIELD_U8(struct gr_ip6_icmp_send_req, ttl),
		GR_FIELD_END,
	),
	.req_size = sizeof(struct gr_ip6_icmp_send_req),
};

static const struct gr_api_codec ip6_icmp6_recv_codec = {
	.req_fields = GR_FIELD_DESCS(
		GR_FIELD_U16(struct gr_ip6_icmp_recv_req, ident),
		GR_FIELD_U16(struct gr_ip6_icmp_recv_req, seq_num),
		GR_FIELD_END,
	),
	.req_size = sizeof(struct gr_ip6_icmp_recv_req),
	.resp_fields = GR_FIELD_DESCS(
		GR_FIELD_U8(struct gr_ip6_icmp_recv_resp, type),
		GR_FIELD_U8(struct gr_ip6_icmp_recv_resp, code),
		GR_FIELD_U8(struct gr_ip6_icmp_recv_resp, ttl),
		GR_FIELD_U16(struct gr_ip6_icmp_recv_resp, ident),
		GR_FIELD_U16(struct gr_ip6_icmp_recv_resp, seq_num),
		GR_FIELD_IP6(struct gr_ip6_icmp_recv_resp, src_addr),
		GR_FIELD_U64(struct gr_ip6_icmp_recv_resp, response_time),
		GR_FIELD_END,
	),
	.resp_size = sizeof(struct gr_ip6_icmp_recv_resp),
};

static const struct gr_api_codec ip6_fib_default_set_codec = {
	.req_fields = GR_FIELD_DESCS(
		GR_FIELD_U32(struct gr_ip6_fib_default_set_req, max_routes),
		GR_FIELD_END,
	),
	.req_size = sizeof(struct gr_ip6_fib_default_set_req),
};

static const struct gr_api_codec ip6_fib_info_list_codec = {
	.req_fields = GR_FIELD_DESCS(
		GR_FIELD_U16(struct gr_ip6_fib_info_list_req, vrf_id),
		GR_FIELD_END,
	),
	.req_size = sizeof(struct gr_ip6_fib_info_list_req),
	.resp_fields = GR_FIELD_DESCS(
		GR_FIELD_U16(struct gr_fib6_info, vrf_id),
		GR_FIELD_U32(struct gr_fib6_info, max_routes),
		GR_FIELD_U32(struct gr_fib6_info, used_routes),
		GR_FIELD_U32(struct gr_fib6_info, num_tbl8),
		GR_FIELD_U32(struct gr_fib6_info, used_tbl8),
		GR_FIELD_END,
	),
	.resp_size = sizeof(struct gr_fib6_info),
};

static void __attribute__((constructor)) ip6_codecs_init(void) {
	gr_api_codec_register(GR_IP6_ROUTE_ADD, &ip6_route_add_codec);
	gr_api_codec_register(GR_IP6_ROUTE_DEL, &ip6_route_del_codec);
	gr_api_codec_register(GR_IP6_ROUTE_GET, &ip6_route_get_codec);
	gr_api_codec_register(GR_IP6_ROUTE_LIST, &ip6_route_list_codec);
	gr_api_codec_register(GR_IP6_ADDR_ADD, &ip6_addr_add_codec);
	gr_api_codec_register(GR_IP6_ADDR_DEL, &ip6_addr_del_codec);
	gr_api_codec_register(GR_IP6_ADDR_LIST, &ip6_addr_list_codec);
	gr_api_codec_register(GR_IP6_ADDR_FLUSH, &ip6_addr_flush_codec);
	gr_api_codec_register(GR_IP6_IFACE_RA_SET, &ip6_iface_ra_set_codec);
	gr_api_codec_register(GR_IP6_IFACE_RA_CLEAR, &ip6_iface_ra_clear_codec);
	gr_api_codec_register(GR_IP6_IFACE_RA_SHOW, &ip6_iface_ra_show_codec);
	gr_api_codec_register(GR_IP6_ICMP6_SEND, &ip6_icmp6_send_codec);
	gr_api_codec_register(GR_IP6_ICMP6_RECV, &ip6_icmp6_recv_codec);
	gr_api_codec_register(GR_IP6_FIB_DEFAULT_SET, &ip6_fib_default_set_codec);
	gr_api_codec_register(GR_IP6_FIB_INFO_LIST, &ip6_fib_info_list_codec);

	// Event codecs reuse the list response codecs.
	gr_event_serializer(GR_EVENT_IP6_ADDR_ADD, ip6_ifaddr_fields, NULL);
	gr_event_serializer(GR_EVENT_IP6_ADDR_DEL, ip6_ifaddr_fields, NULL);
	gr_event_codec_register(GR_EVENT_IP6_ADDR_ADD, &ip6_addr_list_codec);
	gr_event_codec_register(GR_EVENT_IP6_ADDR_DEL, &ip6_addr_list_codec);

	// Route event serializers are in daemon code (modules/ip6/control/route.c).
	gr_event_codec_register(GR_EVENT_IP6_ROUTE_ADD, &ip6_route_list_codec);
	gr_event_codec_register(GR_EVENT_IP6_ROUTE_DEL, &ip6_route_list_codec);
}
