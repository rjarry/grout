// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2024 Christophe Fontaine

#include <gr_datapath.h>
#include <gr_graph.h>
#include <gr_ip4_control.h>
#include <gr_ip4_datapath.h>
#include <gr_log.h>
#include <gr_mbuf.h>

#include <rte_common.h>
#include <rte_graph_worker.h>
#include <rte_icmp.h>
#include <rte_ip.h>

enum edges {
	ICMP_OUTPUT = 0,
	NO_HEADROOM,
	NO_IP,
	EDGE_COUNT,
};

static uint16_t
ip_error_process(struct rte_graph *graph, struct rte_node *node, void **objs, uint16_t nb_objs) {
	struct ip_local_mbuf_data *ip_data;
	const struct iface *iface;
	struct rte_icmp_hdr *icmp;
	struct rte_ipv4_hdr *ip;
	struct rte_mbuf *mbuf;
	struct nexthop *nh;
	uint8_t icmp_type;
	rte_edge_t edge;

	icmp_type = node->ctx[0];

	for (uint16_t i = 0; i < nb_objs; i++) {
		mbuf = objs[i];

		ip = rte_pktmbuf_mtod(mbuf, struct rte_ipv4_hdr *);
		icmp = (struct rte_icmp_hdr *)rte_pktmbuf_prepend(mbuf, sizeof(*icmp));
		if (unlikely(icmp == NULL)) {
			edge = NO_HEADROOM;
			goto next;
		}

		// Get the local router IP address from the input iface
		iface = ip_output_mbuf_data(mbuf)->input_iface;
		if (iface == NULL) {
			edge = NO_IP;
			goto next;
		}
		if ((nh = ip4_addr_get_preferred(iface->id, ip->src_addr)) == NULL) {
			edge = NO_IP;
			goto next;
		}

		ip_data = ip_local_mbuf_data(mbuf);
		ip_data->vrf_id = iface->vrf_id;
		ip_data->src = nh->ip;
		ip_data->dst = ip->src_addr;

		// RFC792 payload size: ip header + 64 bits of original datagram
		ip_data->len = sizeof(*icmp) + rte_ipv4_hdr_len(ip) + 8;
		ip_data->proto = IPPROTO_ICMP;

		icmp->icmp_type = icmp_type;
		icmp->icmp_code = 0; // time to live exceeded in transit
		icmp->icmp_cksum = 0;
		icmp->icmp_ident = 0;
		icmp->icmp_seq_nb = 0;

		edge = ICMP_OUTPUT;
next:
		rte_node_enqueue_x1(graph, node, edge, mbuf);
	}

	return nb_objs;
}

static int ttl_exceeded_init(const struct rte_graph *, struct rte_node *node) {
	node->ctx[0] = GR_IP_ICMP_TTL_EXCEEDED;
	return 0;
}

static int no_route_init(const struct rte_graph *, struct rte_node *node) {
	node->ctx[0] = GR_IP_ICMP_DEST_UNREACHABLE;
	return 0;
}

struct rte_node_register ip_forward_ttl_exceeded_node = {
	.name = "ip_error_ttl_exceeded",
	.process = ip_error_process,
	.nb_edges = EDGE_COUNT,
	.next_nodes = {
		[ICMP_OUTPUT] = "icmp_output",
		[NO_HEADROOM] = "error_no_headroom",
		[NO_IP] = "error_no_local_ip",
	},
	.init = ttl_exceeded_init,
};

static struct rte_node_register no_route_node = {
	.name = "ip_error_dest_unreach",
	.process = ip_error_process,
	.nb_edges = EDGE_COUNT,
	.next_nodes = {
		[ICMP_OUTPUT] = "icmp_output",
		[NO_HEADROOM] = "error_no_headroom",
		[NO_IP] = "error_no_local_ip",
	},
	.init = no_route_init,
};

static struct gr_node_info info_ttl_exceeded = {
	.node = &ip_forward_ttl_exceeded_node,
};

static struct gr_node_info info_no_route = {
	.node = &no_route_node,
};

GR_NODE_REGISTER(info_ttl_exceeded);
GR_NODE_REGISTER(info_no_route);

GR_DROP_REGISTER(error_no_local_ip);