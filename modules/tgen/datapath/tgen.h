// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Robin Jarry

#pragma once

#include <rte_mbuf.h>
#include <rte_mempool.h>

#include <stdatomic.h>
#include <stdint.h>

// Per-queue clones of the generator source nodes. They form a graph that is
// completely disjoint from the forwarding graph: tgen_tx transmits directly with
// rte_eth_tx_burst() and tgen_rx frees received packets in place.
#define GR_TGEN_TX_NODE_BASE "tgen_tx"
#define GR_TGEN_TX_NODE_FMT GR_TGEN_TX_NODE_BASE "-p%uq%u"
#define GR_TGEN_RX_NODE_BASE "tgen_rx"
#define GR_TGEN_RX_NODE_FMT GR_TGEN_RX_NODE_BASE "-p%uq%u"
// Dummy sink: source nodes must have at least one edge, but tgen nodes never
// forward packets downstream.
#define GR_TGEN_SINK_NODE "tgen_sink"

// A packet field swept over [start, end] by step on every transmitted packet,
// written big-endian at offset (size bytes wide).
struct tgen_sweep {
	uint16_t offset;
	uint16_t size;
	uint64_t start;
	uint64_t end;
	uint64_t step;
};

// Immutable per-flow data shared between control (writer) and datapath (reader).
// The template is a const mbuf that tgen_tx clones by reference (zero copy) when
// there is no sweep, or deep-copies to apply the sweeps.
struct tgen_flow_priv {
	struct rte_mbuf *template;
	uint16_t pkt_len;
	struct tgen_sweep *sweeps;
	unsigned n_sweeps;
};

// Per-clone view of a flow: the shared immutable data plus the mutable sweep
// cursors (one per sweep) so each clone advances independently.
struct tgen_tx_flow {
	struct tgen_flow_priv *priv;
	uint64_t *cursors;
};

// Global generator run state. Control writes, datapath reads every graph walk.
struct tgen_run {
	atomic_bool running;
	// Packets per second each tgen_tx clone must emit (total rate / clone count).
	_Atomic double pps_per_clone;
	// If >= 0, only this port transmits; otherwise all generator ports do.
	atomic_int only_port;
};

extern struct tgen_run tgen_run;

// tgen_tx per-clone context, stored in node->ctx_ptr. Built by the tgen graph
// builder on reload, only accessed by the owning worker at run time.
struct tgen_tx_ctx {
	uint16_t port_id;
	uint16_t queue_id;
	struct rte_mempool *pool; // pool for the cloned (indirect) mbufs
	struct tgen_tx_flow *flows; // flows generated out of this port
	unsigned n_flows;
	// weighted schedule: sched[i] indexes flows[]; each flow appears weight
	// times so packets follow the configured ratio (e.g. IMIX). rr walks it.
	uint16_t *sched;
	unsigned sched_len;
	unsigned rr;
	// token-bucket pacing state
	uint64_t last_tsc;
	double tokens;
};

// tgen_rx per-clone context, stored in node->ctx_ptr.
struct tgen_rx_ctx {
	uint16_t port_id;
	uint16_t queue_id;
	uint16_t burst_size;
};
