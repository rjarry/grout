// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2024 Christophe Fontaine

#include "gr_control_output.h"

#include <gr_control.h>
#include <gr_graph.h>
#include <gr_log.h>
#include <gr_mbuf.h>

#include <rte_ether.h>
#include <rte_graph_worker.h>

#include <stdlib.h>

enum {
	CONTROL_OUTPUT_ERROR,
	EDGE_COUNT,
};

struct rte_ring *control_output_ring;

static uint16_t control_output_process(
	struct rte_graph *graph,
	struct rte_node *node,
	void **objs,
	uint16_t n_objs
) {
	struct gr_control_output_msg msg;
	struct rte_mbuf *mbuf;

	for (unsigned i = 0; i < n_objs; i++) {
		mbuf = objs[i];
		msg.callback = control_output_mbuf_data(mbuf)->callback;
		msg.mbuf = mbuf;

		if (msg.callback == NULL
		    || rte_ring_enqueue_elem(
			       control_output_ring, &msg, sizeof(struct gr_control_output_msg)
		       ) != 0) {
			rte_node_enqueue(graph, node, CONTROL_OUTPUT_ERROR, (void *)mbuf, 1);
		}
	}

	if (n_objs)
		signal_control_ouput_message();

	return n_objs;
}

static void control_output_register(void) {
	control_output_ring = rte_ring_create_elem(
		"control_output",
		sizeof(struct gr_control_output_msg),
		RTE_GRAPH_BURST_SIZE * 4,
		SOCKET_ID_ANY,
		RING_F_MP_RTS_ENQ | RING_F_MC_RTS_DEQ
	);
	if (control_output_ring == NULL)
		ABORT("rte_ring_create(arp_output_request): %s", rte_strerror(rte_errno));
}

static void control_output_unregister(void) {
	rte_ring_free(control_output_ring);
}

static struct rte_node_register control_output_node = {
	.name = "control_output",
	.process = control_output_process,
	.nb_edges = EDGE_COUNT,
	.next_nodes = {[CONTROL_OUTPUT_ERROR] = "control_output_unknown_type"},
};

static struct gr_node_info info = {
	.node = &control_output_node,
	.register_callback = control_output_register,
	.unregister_callback = control_output_unregister,
};

GR_NODE_REGISTER(info);

GR_DROP_REGISTER(control_output_unknown_type);
