// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Robin Jarry

#include "tgen.h"

#include <graph.h>

#include <rte_ethdev.h>
#include <rte_graph_worker.h>
#include <rte_malloc.h>
#include <rte_mbuf.h>

enum {
	SINK = 0,
	NB_EDGES,
};

// tgen_rx is a sink: it drains the receive queue and frees everything. Drop
// measurement relies on hardware port counters (opackets vs ipackets+imissed),
// so no per-packet software accounting is needed here.
static uint16_t tgen_rx_process(
	struct rte_graph * /*graph*/,
	struct rte_node *node,
	void ** /*objs*/,
	uint16_t /*nb_objs*/
) {
	struct rte_mbuf **mbufs = (struct rte_mbuf **)node->objs;
	struct tgen_rx_ctx *ctx = node->ctx_ptr;
	uint16_t rx;

	if (ctx == NULL)
		return 0;

	rx = rte_eth_rx_burst(ctx->port_id, ctx->queue_id, mbufs, ctx->burst_size);
	if (rx == 0)
		return 0;

	rte_pktmbuf_free_bulk(mbufs, rx);

	return rx;
}

static void tgen_rx_fini(const struct rte_graph *, struct rte_node *node) {
	rte_free(node->ctx_ptr);
	node->ctx_ptr = NULL;
}

static struct rte_node_register tgen_rx_node = {
	.name = GR_TGEN_RX_NODE_BASE,
	.flags = RTE_NODE_SOURCE_F,
	.process = tgen_rx_process,
	.fini = tgen_rx_fini,
	.nb_edges = NB_EDGES,
	.next_nodes = {
		[SINK] = GR_TGEN_SINK_NODE,
	},
};

static struct gr_node_info tgen_rx_info = {
	.node = &tgen_rx_node,
	.type = GR_NODE_T_L1,
	.clone_per_queue = true,
};

GR_NODE_REGISTER(tgen_rx_info);

// Shared dummy sink for tgen_tx and tgen_rx. Kept out of the base node set
// (clone_per_queue) so it never appears in a forwarding graph.
static struct rte_node_register tgen_sink_node = {
	.name = GR_TGEN_SINK_NODE,
	.process = drop_packets,
};

static struct gr_node_info tgen_sink_info = {
	.node = &tgen_sink_node,
	.type = GR_NODE_T_L1,
	.trace_format = drop_format,
	.clone_per_queue = true,
};

GR_NODE_REGISTER(tgen_sink_info);
