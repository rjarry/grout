// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2024 Robin Jarry

#pragma once

#include <gr_trace.h>

#include <rte_common.h>
#include <rte_graph.h>

#include <assert.h>
#include <sys/queue.h>

#ifdef __GROUT_UNIT_TEST__
#include <gr_cmocka.h>

// These functions are defined as static inline in the original code, so they cannot be wrapped
// directly using CMocka's function wrapping mechanism.
#define rte_node_enqueue rte_node_enqueue_real
#define rte_node_next_stream_move rte_node_next_stream_move_real
#include <rte_graph_worker.h>
#undef rte_node_enqueue
#undef rte_node_next_stream_move

static inline void
rte_node_enqueue(struct rte_graph *, struct rte_node *, rte_edge_t next, void **, uint16_t) {
	check_expected(next);
}

static inline void
rte_node_next_stream_move(struct rte_graph *, struct rte_node *, rte_edge_t next) {
	check_expected(next);
}
#else
#include <rte_graph_worker.h>
#endif

#define GR_NODE_CTX_TYPE(type_name, fields)                                                        \
	struct type_name fields;                                                                   \
	static inline struct type_name *type_name(struct rte_node *node) {                         \
		static_assert(sizeof(struct type_name) <= RTE_NODE_CTX_SZ);                        \
		return (struct type_name *)node->ctx;                                              \
	}

rte_edge_t gr_node_attach_parent(const char *parent, const char *node);

uint16_t drop_packets(struct rte_graph *, struct rte_node *, void **, uint16_t);
int drop_format(char *buf, size_t buf_len, const void *data, size_t data_len);

typedef void (*gr_node_register_cb_t)(void);

typedef enum : uint16_t {
	GR_NODE_T_CONTROL = GR_BIT64(0),
	GR_NODE_T_L1 = GR_BIT64(1),
	GR_NODE_T_L2 = GR_BIT64(2),
	GR_NODE_T_L3 = GR_BIT64(3),
	GR_NODE_T_L4 = GR_BIT64(4),
} gr_node_type_t;

struct gr_node_info {
	struct rte_node_register *node;
	gr_node_type_t type;
	gr_node_register_cb_t register_callback;
	gr_node_register_cb_t unregister_callback;
	gr_trace_format_cb_t trace_format;
	STAILQ_ENTRY(gr_node_info) next;
};

const struct gr_node_info *gr_node_info_get(rte_node_t node_id);

STAILQ_HEAD(node_infos, gr_node_info);
extern struct node_infos node_infos;

#define GR_NODE_REGISTER(info)                                                                     \
	RTE_INIT(gr_node_register_##info) {                                                        \
		assert(info.node != NULL);                                                         \
		assert(info.type != 0);                                                            \
		STAILQ_INSERT_TAIL(&node_infos, &info, next);                                      \
	}

#define GR_DROP_REGISTER(node_name)                                                                \
	static struct rte_node_register drop_node_##node_name = {                                  \
		.name = #node_name,                                                                \
		.process = drop_packets,                                                           \
	};                                                                                         \
	static struct gr_node_info drop_info_##node_name = {                                       \
		.node = &drop_node_##node_name,                                                    \
		.trace_format = drop_format,                                                       \
	};                                                                                         \
	RTE_INIT(gr_drop_register_##node_name) {                                                   \
		STAILQ_INSERT_TAIL(&node_infos, &drop_info_##node_name, next);                     \
	}

#define NODE_ENQUEUE_VARS                                                                          \
	uint16_t run_start = 0;                                                                    \
	rte_edge_t last_edge = RTE_EDGE_ID_INVALID;

#define NODE_ENQUEUE_NEXT(graph, node, objs, i, edge)                                              \
	if (last_edge == RTE_EDGE_ID_INVALID) {                                                    \
		/* first packet */                                                                 \
		last_edge = edge;                                                                  \
	} else if (edge != last_edge) {                                                            \
		/* edge changed, flush previous run */                                             \
		rte_node_enqueue(graph, node, last_edge, &objs[run_start], i - run_start);         \
		run_start = i;                                                                     \
		last_edge = edge;                                                                  \
	}

#define NODE_ENQUEUE_FLUSH(graph, node, objs, count)                                               \
	if (run_start == 0 && count != 0) {                                                        \
		/* all packets went to the same edge */                                            \
		rte_node_next_stream_move(graph, node, last_edge);                                 \
	} else if (run_start < count) {                                                            \
		/* flush final run */                                                              \
		rte_node_enqueue(graph, node, last_edge, &objs[run_start], count - run_start);     \
	}
