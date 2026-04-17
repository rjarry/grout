// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Robin Jarry

#pragma once

#include <grout.h>

#include <stdlib.h>
#include <string.h>

// gr_api_client_recv wrapper: avoids void** which CGO cannot handle.
static inline void *grout_recv(struct gr_api_client *c, uint32_t type, uint32_t id, int *ret) {
	void *data = NULL;
	*ret = gr_api_client_recv(c, type, id, &data);
	return data;
}

// gr_iface accessors (BASE() anonymous union fields) ////////////////////////

static inline uint16_t grout_iface_get_id(const struct gr_iface *i) {
	return i->id;
}
static inline uint8_t grout_iface_get_type(const struct gr_iface *i) {
	return i->type;
}
static inline uint8_t grout_iface_get_mode(const struct gr_iface *i) {
	return i->mode;
}
static inline uint16_t grout_iface_get_flags(const struct gr_iface *i) {
	return i->flags;
}
static inline uint16_t grout_iface_get_state(const struct gr_iface *i) {
	return i->state;
}
static inline uint16_t grout_iface_get_mtu(const struct gr_iface *i) {
	return i->mtu;
}
static inline uint16_t grout_iface_get_vrf_id(const struct gr_iface *i) {
	return i->vrf_id;
}
static inline uint16_t grout_iface_get_domain_id(const struct gr_iface *i) {
	return i->domain_id;
}
static inline uint32_t grout_iface_get_speed(const struct gr_iface *i) {
	return i->speed;
}

static inline void grout_iface_set_id(struct gr_iface *i, uint16_t v) {
	i->id = v;
}
static inline void grout_iface_set_type(struct gr_iface *i, uint8_t v) {
	i->type = v;
}
static inline void grout_iface_set_mode(struct gr_iface *i, uint8_t v) {
	i->mode = v;
}
static inline void grout_iface_set_flags(struct gr_iface *i, uint16_t v) {
	i->flags = v;
}
static inline void grout_iface_set_mtu(struct gr_iface *i, uint16_t v) {
	i->mtu = v;
}
static inline void grout_iface_set_vrf_id(struct gr_iface *i, uint16_t v) {
	i->vrf_id = v;
}
static inline void grout_iface_set_domain_id(struct gr_iface *i, uint16_t v) {
	i->domain_id = v;
}
static inline void grout_iface_set_speed(struct gr_iface *i, uint32_t v) {
	i->speed = v;
}

// gr_iface_info_port accessors (BASE() anonymous union fields) //////////////

static inline uint16_t grout_port_get_n_rxq(const struct gr_iface_info_port *p) {
	return p->n_rxq;
}
static inline uint16_t grout_port_get_n_txq(const struct gr_iface_info_port *p) {
	return p->n_txq;
}
static inline uint16_t grout_port_get_rxq_size(const struct gr_iface_info_port *p) {
	return p->rxq_size;
}
static inline uint16_t grout_port_get_txq_size(const struct gr_iface_info_port *p) {
	return p->txq_size;
}
static inline struct rte_ether_addr grout_port_get_mac(const struct gr_iface_info_port *p) {
	return p->mac;
}

static inline void grout_port_set_n_rxq(struct gr_iface_info_port *p, uint16_t v) {
	p->n_rxq = v;
}
static inline void grout_port_set_n_txq(struct gr_iface_info_port *p, uint16_t v) {
	p->n_txq = v;
}
static inline void grout_port_set_rxq_size(struct gr_iface_info_port *p, uint16_t v) {
	p->rxq_size = v;
}
static inline void grout_port_set_txq_size(struct gr_iface_info_port *p, uint16_t v) {
	p->txq_size = v;
}
static inline void grout_port_set_mac(struct gr_iface_info_port *p, struct rte_ether_addr v) {
	p->mac = v;
}

// gr_iface_info_bridge accessors (BASE() anonymous union fields) ////////////

static inline uint16_t grout_bridge_get_ageing_time(const struct gr_iface_info_bridge *b) {
	return b->ageing_time;
}
static inline uint16_t grout_bridge_get_flags(const struct gr_iface_info_bridge *b) {
	return b->flags;
}
static inline struct rte_ether_addr grout_bridge_get_mac(const struct gr_iface_info_bridge *b) {
	return b->mac;
}
static inline uint16_t grout_bridge_get_n_members(const struct gr_iface_info_bridge *b) {
	return b->n_members;
}

static inline void grout_bridge_set_ageing_time(struct gr_iface_info_bridge *b, uint16_t v) {
	b->ageing_time = v;
}
static inline void grout_bridge_set_flags(struct gr_iface_info_bridge *b, uint16_t v) {
	b->flags = v;
}
static inline void grout_bridge_set_mac(struct gr_iface_info_bridge *b, struct rte_ether_addr v) {
	b->mac = v;
}

// gr_nexthop accessors (BASE() anonymous union fields) //////////////////////

static inline uint8_t grout_nh_get_type(const struct gr_nexthop *n) {
	return n->type;
}
static inline uint8_t grout_nh_get_origin(const struct gr_nexthop *n) {
	return n->origin;
}
static inline uint16_t grout_nh_get_iface_id(const struct gr_nexthop *n) {
	return n->iface_id;
}
static inline uint16_t grout_nh_get_vrf_id(const struct gr_nexthop *n) {
	return n->vrf_id;
}
static inline uint32_t grout_nh_get_nh_id(const struct gr_nexthop *n) {
	return n->nh_id;
}

static inline void grout_nh_set_type(struct gr_nexthop *n, uint8_t v) {
	n->type = v;
}
static inline void grout_nh_set_origin(struct gr_nexthop *n, uint8_t v) {
	n->origin = v;
}
static inline void grout_nh_set_iface_id(struct gr_nexthop *n, uint16_t v) {
	n->iface_id = v;
}
static inline void grout_nh_set_vrf_id(struct gr_nexthop *n, uint16_t v) {
	n->vrf_id = v;
}
static inline void grout_nh_set_nh_id(struct gr_nexthop *n, uint32_t v) {
	n->nh_id = v;
}

// gr_nh_config_get_resp accessors (BASE(gr_nexthop_config)) /////////////////

static inline uint32_t grout_nh_conf_get_max_count(const struct gr_nh_config_get_resp *r) {
	return r->max_count;
}
static inline uint32_t grout_nh_conf_get_lifetime_reachable(const struct gr_nh_config_get_resp *r) {
	return r->lifetime_reachable_sec;
}
static inline uint32_t
grout_nh_conf_get_lifetime_unreachable(const struct gr_nh_config_get_resp *r) {
	return r->lifetime_unreachable_sec;
}
static inline uint16_t grout_nh_conf_get_max_held_pkts(const struct gr_nh_config_get_resp *r) {
	return r->max_held_pkts;
}
static inline uint8_t grout_nh_conf_get_max_ucast_probes(const struct gr_nh_config_get_resp *r) {
	return r->max_ucast_probes;
}
static inline uint8_t grout_nh_conf_get_max_bcast_probes(const struct gr_nh_config_get_resp *r) {
	return r->max_bcast_probes;
}

static inline void grout_nh_conf_set_max_count(struct gr_nh_config_set_req *r, uint32_t v) {
	r->max_count = v;
}
static inline void
grout_nh_conf_set_lifetime_reachable(struct gr_nh_config_set_req *r, uint32_t v) {
	r->lifetime_reachable_sec = v;
}
static inline void
grout_nh_conf_set_lifetime_unreachable(struct gr_nh_config_set_req *r, uint32_t v) {
	r->lifetime_unreachable_sec = v;
}
static inline void grout_nh_conf_set_max_held_pkts(struct gr_nh_config_set_req *r, uint16_t v) {
	r->max_held_pkts = v;
}
static inline void grout_nh_conf_set_max_ucast_probes(struct gr_nh_config_set_req *r, uint8_t v) {
	r->max_ucast_probes = v;
}
static inline void grout_nh_conf_set_max_bcast_probes(struct gr_nh_config_set_req *r, uint8_t v) {
	r->max_bcast_probes = v;
}

// gr_conntrack_conf accessors (BASE(gr_conntrack_config)) ///////////////////

static inline uint32_t grout_ct_conf_get_max_count(const struct gr_conntrack_conf_get_resp *r) {
	return r->max_count;
}
static inline uint32_t
grout_ct_conf_get_timeout_closed(const struct gr_conntrack_conf_get_resp *r) {
	return r->timeout_closed_sec;
}
static inline uint32_t grout_ct_conf_get_timeout_new(const struct gr_conntrack_conf_get_resp *r) {
	return r->timeout_new_sec;
}
static inline uint32_t grout_ct_conf_get_timeout_udp(const struct gr_conntrack_conf_get_resp *r) {
	return r->timeout_udp_established_sec;
}
static inline uint32_t grout_ct_conf_get_timeout_tcp(const struct gr_conntrack_conf_get_resp *r) {
	return r->timeout_tcp_established_sec;
}
static inline uint32_t
grout_ct_conf_get_timeout_half_close(const struct gr_conntrack_conf_get_resp *r) {
	return r->timeout_half_close_sec;
}
static inline uint32_t
grout_ct_conf_get_timeout_time_wait(const struct gr_conntrack_conf_get_resp *r) {
	return r->timeout_time_wait_sec;
}

static inline void grout_ct_conf_set_max_count(struct gr_conntrack_conf_set_req *r, uint32_t v) {
	r->max_count = v;
}
static inline void
grout_ct_conf_set_timeout_closed(struct gr_conntrack_conf_set_req *r, uint32_t v) {
	r->timeout_closed_sec = v;
}
static inline void grout_ct_conf_set_timeout_new(struct gr_conntrack_conf_set_req *r, uint32_t v) {
	r->timeout_new_sec = v;
}
static inline void grout_ct_conf_set_timeout_udp(struct gr_conntrack_conf_set_req *r, uint32_t v) {
	r->timeout_udp_established_sec = v;
}
static inline void grout_ct_conf_set_timeout_tcp(struct gr_conntrack_conf_set_req *r, uint32_t v) {
	r->timeout_tcp_established_sec = v;
}
static inline void
grout_ct_conf_set_timeout_half_close(struct gr_conntrack_conf_set_req *r, uint32_t v) {
	r->timeout_half_close_sec = v;
}
static inline void
grout_ct_conf_set_timeout_time_wait(struct gr_conntrack_conf_set_req *r, uint32_t v) {
	r->timeout_time_wait_sec = v;
}

// Flexible array info extractors ////////////////////////////////////////////

static inline const struct gr_iface_info_port *grout_iface_port_info(const struct gr_iface *i) {
	return (const struct gr_iface_info_port *)i->info;
}

static inline struct gr_iface_info_port *grout_iface_port_info_mut(struct gr_iface *i) {
	return (struct gr_iface_info_port *)i->info;
}

static inline const struct gr_iface_info_vrf *grout_iface_vrf_info(const struct gr_iface *i) {
	return (const struct gr_iface_info_vrf *)i->info;
}

static inline struct gr_iface_info_vrf *grout_iface_vrf_info_mut(struct gr_iface *i) {
	return (struct gr_iface_info_vrf *)i->info;
}

static inline const struct gr_iface_info_vlan *grout_iface_vlan_info(const struct gr_iface *i) {
	return (const struct gr_iface_info_vlan *)i->info;
}

static inline struct gr_iface_info_vlan *grout_iface_vlan_info_mut(struct gr_iface *i) {
	return (struct gr_iface_info_vlan *)i->info;
}

static inline const struct gr_iface_info_bond *grout_iface_bond_info(const struct gr_iface *i) {
	return (const struct gr_iface_info_bond *)i->info;
}

static inline struct gr_iface_info_bond *grout_iface_bond_info_mut(struct gr_iface *i) {
	return (struct gr_iface_info_bond *)i->info;
}

static inline const struct gr_iface_info_bridge *grout_iface_bridge_info(const struct gr_iface *i) {
	return (const struct gr_iface_info_bridge *)i->info;
}

static inline struct gr_iface_info_bridge *grout_iface_bridge_info_mut(struct gr_iface *i) {
	return (struct gr_iface_info_bridge *)i->info;
}

static inline const struct gr_iface_info_ipip *grout_iface_ipip_info(const struct gr_iface *i) {
	return (const struct gr_iface_info_ipip *)i->info;
}

static inline struct gr_iface_info_ipip *grout_iface_ipip_info_mut(struct gr_iface *i) {
	return (struct gr_iface_info_ipip *)i->info;
}

static inline const struct gr_iface_info_vxlan *grout_iface_vxlan_info(const struct gr_iface *i) {
	return (const struct gr_iface_info_vxlan *)i->info;
}

static inline struct gr_iface_info_vxlan *grout_iface_vxlan_info_mut(struct gr_iface *i) {
	return (struct gr_iface_info_vxlan *)i->info;
}

// gr_nexthop info extractors ////////////////////////////////////////////////

static inline const struct gr_nexthop_info_l3 *grout_nh_l3_info(const struct gr_nexthop *n) {
	return (const struct gr_nexthop_info_l3 *)n->info;
}

static inline struct gr_nexthop_info_l3 *grout_nh_l3_info_mut(struct gr_nexthop *n) {
	return (struct gr_nexthop_info_l3 *)n->info;
}

static inline const struct gr_nexthop_info_group *grout_nh_group_info(const struct gr_nexthop *n) {
	return (const struct gr_nexthop_info_group *)n->info;
}

static inline const struct gr_nexthop_info_dnat *grout_nh_dnat_info(const struct gr_nexthop *n) {
	return (const struct gr_nexthop_info_dnat *)n->info;
}

static inline const struct gr_nexthop_info_srv6 *grout_nh_srv6_info(const struct gr_nexthop *n) {
	return (const struct gr_nexthop_info_srv6 *)n->info;
}

static inline const struct gr_nexthop_info_srv6_local *
grout_nh_srv6_local_info(const struct gr_nexthop *n) {
	return (const struct gr_nexthop_info_srv6_local *)n->info;
}

// gr_ip6_ra_set_req bitfield accessors (CGO cannot access bitfields) ////////

static inline void grout_ra_set_interval(struct gr_ip6_ra_set_req *r, uint16_t v) {
	r->set_interval = 1;
	r->interval = v;
}
static inline void grout_ra_set_lifetime(struct gr_ip6_ra_set_req *r, uint16_t v) {
	r->set_lifetime = 1;
	r->lifetime = v;
}

// gr_flood_entry union accessors (CGO cannot access anonymous unions) ///////

static inline uint32_t grout_flood_get_vni(const struct gr_flood_entry *e) {
	return e->vtep.vni;
}
static inline ip4_addr_t grout_flood_get_addr(const struct gr_flood_entry *e) {
	return e->vtep.addr;
}
static inline void grout_flood_set_vni(struct gr_flood_entry *e, uint32_t v) {
	e->vtep.vni = v;
}
static inline void grout_flood_set_addr(struct gr_flood_entry *e, ip4_addr_t v) {
	e->vtep.addr = v;
}

// gr_nexthop_info_l3 union accessors (CGO cannot access anonymous unions) ///

static inline ip4_addr_t grout_nh_l3_get_ipv4(const struct gr_nexthop_info_l3 *l) {
	return l->ipv4;
}
static inline struct rte_ipv6_addr grout_nh_l3_get_ipv6(const struct gr_nexthop_info_l3 *l) {
	return l->ipv6;
}
static inline void grout_nh_l3_set_ipv4(struct gr_nexthop_info_l3 *l, ip4_addr_t v) {
	l->ipv4 = v;
}
static inline void grout_nh_l3_set_ipv6(struct gr_nexthop_info_l3 *l, struct rte_ipv6_addr v) {
	l->ipv6 = v;
}

// Flexible array member accessors (CGO cannot access FAMs) /////////////////

static inline const struct gr_nexthop_group_member *
grout_nh_group_members(const struct gr_nexthop_info_group *g) {
	return g->members;
}

static inline const char *grout_packet_trace_buf(const struct gr_packet_trace_dump_resp *r) {
	return r->trace;
}

static inline const struct gr_iface_stats *
grout_iface_stats_at(const struct gr_iface_stats_get_resp *r, int i) {
	return &r->stats[i];
}

static inline const struct gr_stat *grout_stat_at(const struct gr_stats_get_resp *r, int i) {
	return &r->stats[i];
}

// Request allocators for variable-size structs //////////////////////////////

static inline size_t grout_iface_info_size(uint8_t type) {
	switch (type) {
	case GR_IFACE_TYPE_PORT:
		return sizeof(struct gr_iface_info_port);
	case GR_IFACE_TYPE_VRF:
		return sizeof(struct gr_iface_info_vrf);
	case GR_IFACE_TYPE_VLAN:
		return sizeof(struct gr_iface_info_vlan);
	case GR_IFACE_TYPE_BOND:
		return sizeof(struct gr_iface_info_bond);
	case GR_IFACE_TYPE_BRIDGE:
		return sizeof(struct gr_iface_info_bridge);
	case GR_IFACE_TYPE_IPIP:
		return sizeof(struct gr_iface_info_ipip);
	case GR_IFACE_TYPE_VXLAN:
		return sizeof(struct gr_iface_info_vxlan);
	default:
		return 0;
	}
}

static inline struct gr_iface_add_req *grout_iface_add_req_alloc(uint8_t type, size_t *out_len) {
	size_t info_sz = grout_iface_info_size(type);
	size_t total = sizeof(struct gr_iface_add_req) + info_sz;
	struct gr_iface_add_req *req = calloc(1, total);
	if (out_len)
		*out_len = total;
	return req;
}

static inline struct gr_iface_set_req *grout_iface_set_req_alloc(uint8_t type, size_t *out_len) {
	size_t info_sz = grout_iface_info_size(type);
	size_t total = sizeof(struct gr_iface_set_req) + info_sz;
	struct gr_iface_set_req *req = calloc(1, total);
	if (out_len)
		*out_len = total;
	return req;
}

// Nexthop request allocators ////////////////////////////////////////////////

static inline size_t grout_nh_info_size(uint8_t type) {
	switch (type) {
	case GR_NH_T_L3:
		return sizeof(struct gr_nexthop_info_l3);
	case GR_NH_T_DNAT:
		return sizeof(struct gr_nexthop_info_dnat);
	case GR_NH_T_SR6_OUTPUT:
		return sizeof(struct gr_nexthop_info_srv6);
	case GR_NH_T_SR6_LOCAL:
		return sizeof(struct gr_nexthop_info_srv6_local);
	default:
		return 0;
	}
}

static inline struct gr_nh_add_req *grout_nh_add_req_alloc(uint8_t type, size_t *out_len) {
	size_t info_sz = grout_nh_info_size(type);
	size_t total = sizeof(struct gr_nh_add_req) + info_sz;
	struct gr_nh_add_req *req = calloc(1, total);
	if (out_len)
		*out_len = total;
	return req;
}
