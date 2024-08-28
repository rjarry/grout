// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2024 Robin Jarry

#ifndef _GR_IP6_DATAPATH_H
#define _GR_IP6_DATAPATH_H

#include <gr_icmp6.h>
#include <gr_iface.h>
#include <gr_ip6_control.h>
#include <gr_mbuf.h>
#include <gr_net_types.h>

#include <rte_byteorder.h>
#include <rte_ether.h>
#include <rte_graph_worker.h>
#include <rte_ip6.h>

#include <stdint.h>

GR_MBUF_PRIV_DATA_TYPE(ip6_output_mbuf_data, {
	const struct iface *input_iface;
	struct nexthop6 *nh;
});

GR_MBUF_PRIV_DATA_TYPE(ip6_local_mbuf_data, {
	struct rte_ipv6_addr src;
	struct rte_ipv6_addr dst;
	uint16_t len;
	uint8_t hop_limit;
	uint8_t proto;
	const struct iface *input_iface;
});

void ip6_input_local_add_proto(uint8_t proto, const char *next_node);

#define IP6_DEFAULT_HOP_LIMIT 255

static inline void ip6_set_fields(
	struct rte_ipv6_hdr *ip,
	uint16_t len,
	uint8_t proto,
	struct rte_ipv6_addr *src,
	struct rte_ipv6_addr *dst
) {
	ip->vtc_flow = RTE_IPV6_VTC_FLOW_VERSION;
	ip->payload_len = rte_cpu_to_be_16(len);
	ip->proto = proto;
	ip->hop_limits = IP6_DEFAULT_HOP_LIMIT;
	rte_ipv6_addr_cpy(&ip->src_addr, src);
	rte_ipv6_addr_cpy(&ip->dst_addr, dst);
}

#endif