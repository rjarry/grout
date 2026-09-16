// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Robin Jarry

#include "tgen.h"

#include <rte_cycles.h>
#include <rte_ethdev.h>
#include <rte_graph_worker.h>
#include <rte_malloc.h>
#include <rte_mbuf.h>

#include <graph.h>
#include <stdatomic.h>

struct tgen_run tgen_run;

enum {
	SINK = 0,
	NB_EDGES,
};

// Apply every sweep of a flow to a writable packet copy and advance the
// per-clone cursors. The field is written big-endian and cycles over
// [start, end] by step.
static inline void
tgen_apply_sweeps(struct rte_mbuf *m, const struct tgen_flow_priv *flow, uint64_t *cursors) {
	for (unsigned s = 0; s < flow->n_sweeps; s++) {
		const struct tgen_sweep *sw = &flow->sweeps[s];
		uint64_t val = cursors[s];

		if (likely((uint32_t)sw->offset + sw->size <= rte_pktmbuf_data_len(m))) {
			uint8_t *p = rte_pktmbuf_mtod_offset(m, uint8_t *, sw->offset);
			for (unsigned b = 0; b < sw->size; b++)
				p[b] = (val >> (8 * (sw->size - 1 - b))) & 0xff;
		}

		uint64_t range = sw->end - sw->start + 1;
		cursors[s] = sw->start + (val - sw->start + sw->step) % range;
	}
}

// tgen_tx generates packets from its flow templates, paces them to the target
// rate and transmits them straight out of the port with rte_eth_tx_burst().
static uint16_t tgen_tx_process(
	struct rte_graph * /*graph*/,
	struct rte_node *node,
	void ** /*objs*/,
	uint16_t /*nb_objs*/
) {
	struct rte_mbuf **mbufs = (struct rte_mbuf **)node->objs;
	struct tgen_tx_ctx *ctx = node->ctx_ptr;
	uint16_t n, gen, tx;
	uint64_t now, hz;
	double pps;

	if (ctx == NULL || ctx->n_flows == 0)
		return 0;
	if (!atomic_load_explicit(&tgen_run.running, memory_order_relaxed))
		return 0;

	int only_port = atomic_load_explicit(&tgen_run.only_port, memory_order_relaxed);
	if (only_port >= 0 && only_port != ctx->port_id)
		return 0;

	pps = atomic_load_explicit(&tgen_run.pps_per_clone, memory_order_relaxed);
	if (pps <= 0)
		return 0;

	now = rte_get_tsc_cycles();
	hz = rte_get_tsc_hz();
	ctx->tokens += (double)(now - ctx->last_tsc) * pps / (double)hz;
	ctx->last_tsc = now;

	if (ctx->tokens > RTE_GRAPH_BURST_SIZE)
		ctx->tokens = RTE_GRAPH_BURST_SIZE;
	n = (uint16_t)ctx->tokens;
	if (n == 0)
		return 0;

	gen = 0;
	for (uint16_t i = 0; i < n; i++) {
		struct tgen_tx_flow *tf = &ctx->flows[ctx->rr];
		struct tgen_flow_priv *flow = tf->priv;
		struct rte_mbuf *m;

		if (flow->n_sweeps == 0) {
			// no mutation: share the template data by reference
			m = rte_pktmbuf_clone(flow->template, ctx->pool);
		} else {
			// mutation needs a writable packet
			m = rte_pktmbuf_copy(flow->template, ctx->pool, 0, UINT32_MAX);
			if (likely(m != NULL))
				tgen_apply_sweeps(m, flow, tf->cursors);
		}
		if (unlikely(m == NULL))
			break;
		if (++ctx->rr >= ctx->n_flows)
			ctx->rr = 0;
		mbufs[gen++] = m;
	}
	ctx->tokens -= gen;
	if (gen == 0)
		return 0;

	tx = rte_eth_tx_burst(ctx->port_id, ctx->queue_id, mbufs, gen);
	if (unlikely(tx < gen))
		rte_pktmbuf_free_bulk(&mbufs[tx], gen - tx);

	return gen;
}

static void tgen_tx_fini(const struct rte_graph *, struct rte_node *node) {
	struct tgen_tx_ctx *ctx = node->ctx_ptr;
	if (ctx != NULL) {
		for (unsigned i = 0; i < ctx->n_flows; i++)
			rte_free(ctx->flows[i].cursors);
		rte_free(ctx->flows);
		rte_free(ctx);
		node->ctx_ptr = NULL;
	}
}

static struct rte_node_register tgen_tx_node = {
	.name = GR_TGEN_TX_NODE_BASE,
	.flags = RTE_NODE_SOURCE_F,
	.process = tgen_tx_process,
	.fini = tgen_tx_fini,
	.nb_edges = NB_EDGES,
	.next_nodes = {
		[SINK] = GR_TGEN_SINK_NODE,
	},
};

static struct gr_node_info tgen_tx_info = {
	.node = &tgen_tx_node,
	.type = GR_NODE_T_L1,
	.clone_per_queue = true,
};

GR_NODE_REGISTER(tgen_tx_info);
