// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2024 Robin Jarry

#include <gr_graph.h>
#include <gr_mbuf.h>
#include <gr_trace.h>

#include <rte_fib6.h>
#include <rte_ip.h>
#include <rte_mbuf.h>

enum edges {
	OUTPUT = 0,
	TTL_EXCEEDED,
	EDGE_COUNT,
};

static uint16_t
ip6_forward_process(struct rte_graph *graph, struct rte_node *node, void **objs, uint16_t nb_objs) {
	struct rte_ipv6_hdr *ip;
	struct rte_mbuf *mbuf;
	rte_edge_t edge;
	uint16_t i;

	NODE_ENQUEUE_VARS;

	for (i = 0; i < nb_objs; i++) {
		mbuf = objs[i];
		ip = rte_pktmbuf_mtod(mbuf, struct rte_ipv6_hdr *);
		if (gr_mbuf_is_traced(mbuf))
			gr_mbuf_trace_add(mbuf, node, 0);

		if (ip->hop_limits <= 1) {
			edge = TTL_EXCEEDED;
		} else {
			ip->hop_limits -= 1;
			edge = OUTPUT;
		}
		NODE_ENQUEUE_NEXT(graph, node, objs, i, edge);
	}

	NODE_ENQUEUE_FLUSH(graph, node, objs, nb_objs);

	return nb_objs;
}

static struct rte_node_register node = {
	.name = "ip6_forward",

	.process = ip6_forward_process,

	.nb_edges = EDGE_COUNT,
	.next_nodes = {
		[OUTPUT] = "ip6_output",
		[TTL_EXCEEDED] = "ip6_error_ttl_exceeded",
	},
};

static struct gr_node_info info = {
	.node = &node,
	.type = GR_NODE_T_L3,
};

GR_NODE_REGISTER(info);
