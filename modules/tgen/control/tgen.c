// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Robin Jarry

#include "iface.h"
#include "log.h"
#include "mempool.h"
#include "module.h"
#include "port.h"
#include "tgen.h"
#include "vec.h"
#include "worker.h"

#include <gr_infra.h>
#include <gr_string.h>
#include <gr_tgen.h>

#include <rte_common.h>
#include <rte_cycles.h>
#include <rte_errno.h>
#include <rte_ethdev.h>
#include <rte_graph.h>
#include <rte_graph_worker.h>
#include <rte_lcore.h>
#include <rte_malloc.h>
#include <rte_mbuf.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>

LOG_TYPE("tgen");

// Number of mbufs reserved for the generator: template frames plus the clones
// in flight on all tx queues and NIC rings.
#define TGEN_POOL_SIZE 8192
// Receive burst size for tgen_rx clones.
#define TGEN_RX_BURST 64

struct tgen_flow {
	uint32_t id;
	uint16_t tx_iface_id;
	uint16_t rx_iface_id;
	uint16_t tx_port_id;
	uint16_t rx_port_id;
	struct tgen_flow_priv priv;
};

static vec struct tgen_flow **flows;
static uint32_t next_flow_id = 1;
static struct rte_mempool *tgen_pool;

// current rate request, replayed on reloads while running
static gr_tgen_rate_mode_t rate_mode;
static double rate_value;
static int rate_only_port = -1;

// hardware counter snapshot taken when generation starts
struct port_stat_base {
	uint64_t opackets;
	uint64_t obytes;
	uint64_t ipackets;
	uint64_t ibytes;
	uint64_t imissed;
};
static struct port_stat_base baseline[RTE_MAX_ETHPORTS];

static bool port_is_tgen(uint16_t port_id) {
	vec_foreach (struct tgen_flow *f, flows)
		if (f->tx_port_id == port_id || f->rx_port_id == port_id)
			return true;
	return false;
}

static bool port_generates(uint16_t port_id) {
	vec_foreach (struct tgen_flow *f, flows)
		if (f->tx_port_id == port_id)
			return true;
	return false;
}

// Count the tgen_tx clones that actually generate traffic, i.e. the enabled tx
// queues of a flow transmit port (optionally restricted to a single port).
static unsigned count_gen_clones(int only_port) {
	const struct queue_map *q;
	struct worker *w;
	unsigned n = 0;

	STAILQ_FOREACH (w, &workers, next) {
		vec_foreach_ref (q, w->txqs) {
			if (!q->enabled || !port_generates(q->port_id))
				continue;
			if (only_port >= 0 && (int)q->port_id != only_port)
				continue;
			n++;
		}
	}
	return n;
}

// Compute the per-clone packet rate from the current rate request and publish it
// to the datapath.
static int rate_apply(void) {
	double pps_per_clone;
	unsigned n;

	n = count_gen_clones(rate_only_port);
	if (n == 0)
		return errno_set(ENODEV);

	if (rate_mode == GR_TGEN_RATE_PPS) {
		pps_per_clone = rate_value / n;
	} else {
		int port = rate_only_port;
		uint16_t pkt = 64;

		if (port < 0 && vec_len(flows) > 0)
			port = flows[0]->tx_port_id;
		vec_foreach (struct tgen_flow *f, flows) {
			if ((int)f->tx_port_id == port) {
				pkt = f->priv.pkt_len;
				break;
			}
		}
		const struct iface *iface = port_get_iface(port);
		uint32_t speed = iface != NULL ? iface->speed : 0;
		if (speed == 0)
			return errno_set(ENOTCONN);
		// megabit/s divided by frame bits (incl. 20B IFG + preamble)
		double line_pps = (double)speed * 1e6 / (((double)pkt + 20.0) * 8.0);
		pps_per_clone = (rate_value / 100.0) * line_pps / count_gen_clones(port);
	}

	atomic_store(&tgen_run.only_port, rate_only_port);
	atomic_store(&tgen_run.pps_per_clone, pps_per_clone);
	return 0;
}

static void baseline_reset(void) {
	struct rte_eth_stats s;

	vec_foreach (struct tgen_flow *f, flows) {
		uint16_t ports[] = {f->tx_port_id, f->rx_port_id};
		for (unsigned i = 0; i < 2; i++) {
			if (rte_eth_stats_get(ports[i], &s) < 0)
				continue;
			baseline[ports[i]] = (struct port_stat_base) {
				.opackets = s.opackets,
				.obytes = s.obytes,
				.ipackets = s.ipackets,
				.ibytes = s.ibytes,
				.imissed = s.imissed,
			};
		}
	}
}

static int resolve_port(uint16_t iface_id, uint16_t *port_id) {
	struct iface *iface = iface_from_id(iface_id);
	if (iface == NULL)
		return errno_set(ENODEV);
	if (iface->type != GR_IFACE_TYPE_PORT)
		return errno_set(EINVAL);
	*port_id = iface_info_port(iface)->port_id;
	return 0;
}

static vec struct iface_info_port **get_all_ports(void) {
	vec struct iface_info_port **ports = NULL;
	struct iface *iface = NULL;
	while ((iface = iface_next(GR_IFACE_TYPE_PORT, iface)) != NULL)
		vec_add(ports, iface_info_port(iface));
	return ports;
}

static int tgen_reload(void) {
	vec struct iface_info_port **ports = get_all_ports();
	int ret = worker_graph_reload_all(ports);
	vec_free(ports);
	// the number of generating clones may have changed, refresh the rate
	if (ret == 0 && atomic_load(&tgen_run.running))
		rate_apply();
	return ret;
}

// worker graph builder: build a graph made only of tgen nodes

static bool tgen_worker_owns(const struct worker *w) {
	const struct queue_map *q;

	vec_foreach_ref (q, w->rxqs)
		if (q->enabled && port_is_tgen(q->port_id))
			return true;
	vec_foreach_ref (q, w->txqs)
		if (q->enabled && port_is_tgen(q->port_id))
			return true;
	return false;
}

static char *ensure_clone(const char *base, const char *fmt, uint16_t port, uint16_t queue) {
	char *name = astrcat(NULL, fmt, port, queue);
	assert(name != NULL);
	if (rte_node_from_name(name) == RTE_NODE_ID_INVALID) {
		rte_node_t b = rte_node_from_name(base);
		assert(b != RTE_NODE_ID_INVALID);
		rte_node_clone(b, strstr(name, "-") + 1);
	}
	return name;
}

static void tgen_rx_ctx_set(const char *graph, uint16_t port_id, uint16_t queue_id) {
	char name[RTE_NODE_NAMESIZE];
	snprintf(name, sizeof(name), GR_TGEN_RX_NODE_FMT, port_id, queue_id);
	struct rte_node *node = rte_graph_node_get_by_name(graph, name);
	struct tgen_rx_ctx *ctx = rte_zmalloc(__func__, sizeof(*ctx), RTE_CACHE_LINE_SIZE);
	if (ctx == NULL) {
		LOG(ERR, "rte_zmalloc(tgen_rx_ctx) failed");
		return;
	}
	ctx->port_id = port_id;
	ctx->queue_id = queue_id;
	ctx->burst_size = TGEN_RX_BURST;
	node->ctx_ptr = ctx;
}

static void tgen_tx_ctx_set(const char *graph, uint16_t port_id, uint16_t queue_id) {
	char name[RTE_NODE_NAMESIZE];
	snprintf(name, sizeof(name), GR_TGEN_TX_NODE_FMT, port_id, queue_id);
	struct rte_node *node = rte_graph_node_get_by_name(graph, name);
	struct tgen_tx_ctx *ctx = rte_zmalloc(__func__, sizeof(*ctx), RTE_CACHE_LINE_SIZE);
	if (ctx == NULL) {
		LOG(ERR, "rte_zmalloc(tgen_tx_ctx) failed");
		return;
	}
	ctx->port_id = port_id;
	ctx->queue_id = queue_id;
	ctx->pool = tgen_pool;
	ctx->last_tsc = rte_get_tsc_cycles();

	unsigned n = 0;
	vec_foreach (struct tgen_flow *f, flows)
		if (f->tx_port_id == port_id)
			n++;
	if (n > 0) {
		ctx->flows = rte_malloc(__func__, n * sizeof(*ctx->flows), RTE_CACHE_LINE_SIZE);
		if (ctx->flows == NULL) {
			LOG(ERR, "rte_malloc(tgen flows) failed");
			rte_free(ctx);
			return;
		}
		vec_foreach (struct tgen_flow *f, flows)
			if (f->tx_port_id == port_id)
				ctx->flows[ctx->n_flows++] = &f->priv;
	}
	node->ctx_ptr = ctx;
}

static int tgen_graph_build(struct worker *w, uint8_t index, vec struct iface_info_port **ports) {
	char graph_name[RTE_GRAPH_NAMESIZE];
	vec const char **nodes = NULL;
	vec char **owned = NULL;
	struct queue_map *q;
	int ret = 0;

	(void)ports;

	vec_foreach_ref (q, w->rxqs) {
		if (!q->enabled || !port_is_tgen(q->port_id))
			continue;
		char *name = ensure_clone(
			GR_TGEN_RX_NODE_BASE, GR_TGEN_RX_NODE_FMT, q->port_id, q->queue_id
		);
		vec_add(owned, name);
		vec_add(nodes, name);
	}
	vec_foreach_ref (q, w->txqs) {
		if (!q->enabled || !port_is_tgen(q->port_id))
			continue;
		char *name = ensure_clone(
			GR_TGEN_TX_NODE_BASE, GR_TGEN_TX_NODE_FMT, q->port_id, q->queue_id
		);
		vec_add(owned, name);
		vec_add(nodes, name);
	}

	if (vec_len(nodes) == 0) {
		w->graph[index] = NULL;
		goto out;
	}
	vec_add(nodes, GR_TGEN_SINK_NODE);

	snprintf(graph_name, sizeof(graph_name), "tg-%04x", (w->cpu_id << 1) | (index & 0x1));
	struct rte_graph_param params = {
		.socket_id = rte_lcore_to_socket_id(w->lcore_id),
		.nb_node_patterns = vec_len(nodes),
		.node_patterns = nodes,
	};
	if (rte_graph_create(graph_name, &params) == RTE_GRAPH_ID_INVALID) {
		ret = rte_errno != 0 ? -rte_errno : -EINVAL;
		goto out;
	}
	w->graph[index] = rte_graph_lookup(graph_name);

	vec_foreach_ref (q, w->rxqs) {
		if (!q->enabled || !port_is_tgen(q->port_id))
			continue;
		tgen_rx_ctx_set(graph_name, q->port_id, q->queue_id);
	}
	vec_foreach_ref (q, w->txqs) {
		if (!q->enabled || !port_is_tgen(q->port_id))
			continue;
		tgen_tx_ctx_set(graph_name, q->port_id, q->queue_id);
	}

	LOG(DEBUG,
	    "[CPU %d] built tgen graph %s with %u nodes",
	    w->cpu_id,
	    graph_name,
	    vec_len(nodes));

out:
	vec_free(nodes);
	strvec_free(owned);
	return errno_set(-ret);
}

static const struct worker_graph_builder tgen_builder = {
	.owns = tgen_worker_owns,
	.build = tgen_graph_build,
};

// API handlers

static struct api_out tgen_status(const void * /*request*/, struct api_ctx *) {
	bool tx_seen[RTE_MAX_ETHPORTS] = {0};
	bool rx_seen[RTE_MAX_ETHPORTS] = {0};
	struct gr_tgen_status_resp *resp;
	struct rte_eth_stats s;

	resp = calloc(1, sizeof(*resp));
	if (resp == NULL)
		return api_out(ENOMEM, 0, NULL);

	resp->running = atomic_load(&tgen_run.running);
	resp->rate_mode = rate_mode;
	resp->rate_value = rate_value;
	resp->pps_per_clone = atomic_load(&tgen_run.pps_per_clone);

	vec_foreach (struct tgen_flow *f, flows) {
		if (!tx_seen[f->tx_port_id] && rte_eth_stats_get(f->tx_port_id, &s) == 0) {
			tx_seen[f->tx_port_id] = true;
			resp->tx_packets += s.opackets - baseline[f->tx_port_id].opackets;
			resp->tx_bytes += s.obytes - baseline[f->tx_port_id].obytes;
		}
		if (!rx_seen[f->rx_port_id] && rte_eth_stats_get(f->rx_port_id, &s) == 0) {
			rx_seen[f->rx_port_id] = true;
			resp->rx_packets += s.ipackets - baseline[f->rx_port_id].ipackets;
			resp->rx_bytes += s.ibytes - baseline[f->rx_port_id].ibytes;
			resp->rx_missed += s.imissed - baseline[f->rx_port_id].imissed;
		}
	}

	if (resp->tx_packets > resp->rx_packets + resp->rx_missed)
		resp->drop_packets = resp->tx_packets - (resp->rx_packets + resp->rx_missed);

	return api_out(0, sizeof(*resp), resp);
}

static struct api_out tgen_start(const void *request, struct api_ctx *) {
	const struct gr_tgen_start_req *req = request;
	int only_port = -1;
	int ret;

	if (vec_len(flows) == 0)
		return api_out(ENOENT, 0, NULL);
	if (req->rate_mode != GR_TGEN_RATE_PCT && req->rate_mode != GR_TGEN_RATE_PPS)
		return api_out(EINVAL, 0, NULL);
	if (req->rate_value <= 0)
		return api_out(EINVAL, 0, NULL);

	if (req->has_port) {
		uint16_t p;
		if ((ret = resolve_port(req->only_iface_id, &p)) < 0)
			return api_out(-ret, 0, NULL);
		if (!port_generates(p))
			return api_out(EINVAL, 0, NULL);
		only_port = p;
	}

	rate_mode = req->rate_mode;
	rate_value = req->rate_value;
	rate_only_port = only_port;

	if ((ret = rate_apply()) < 0)
		return api_out(-ret, 0, NULL);

	baseline_reset();
	atomic_store(&tgen_run.running, true);

	return api_out(0, 0, NULL);
}

static struct api_out tgen_stop(const void * /*request*/, struct api_ctx *) {
	atomic_store(&tgen_run.running, false);
	return api_out(0, 0, NULL);
}

static struct api_out tgen_flow_add(const void *request, struct api_ctx *ctx) {
	const struct gr_tgen_flow_add_req *req = request;
	struct gr_tgen_flow_add_resp *resp;
	uint16_t tx_port, rx_port;
	struct tgen_flow *flow;
	struct rte_mbuf *m;
	void *data;
	int ret;

	if (req->pkt_len == 0 || req->pkt_len > GR_TGEN_MAX_PKT_LEN)
		return api_out(EINVAL, 0, NULL);
	if (ctx->header.payload_len < sizeof(*req) + req->pkt_len)
		return api_out(EMSGSIZE, 0, NULL);
	if ((ret = resolve_port(req->tx_iface_id, &tx_port)) < 0)
		return api_out(-ret, 0, NULL);
	if ((ret = resolve_port(req->rx_iface_id, &rx_port)) < 0)
		return api_out(-ret, 0, NULL);

	if (tgen_pool == NULL) {
		tgen_pool = gr_pktmbuf_pool_get(SOCKET_ID_ANY, TGEN_POOL_SIZE);
		if (tgen_pool == NULL)
			return api_out(ENOMEM, 0, NULL);
	}

	m = rte_pktmbuf_alloc(tgen_pool);
	if (m == NULL)
		return api_out(ENOMEM, 0, NULL);
	data = rte_pktmbuf_append(m, req->pkt_len);
	if (data == NULL) {
		rte_pktmbuf_free(m);
		return api_out(EMSGSIZE, 0, NULL);
	}
	memcpy(data, req->pkt, req->pkt_len);

	flow = calloc(1, sizeof(*flow));
	if (flow == NULL) {
		rte_pktmbuf_free(m);
		return api_out(ENOMEM, 0, NULL);
	}
	flow->id = next_flow_id++;
	flow->tx_iface_id = req->tx_iface_id;
	flow->rx_iface_id = req->rx_iface_id;
	flow->tx_port_id = tx_port;
	flow->rx_port_id = rx_port;
	flow->priv.template = m;
	flow->priv.pkt_len = req->pkt_len;
	vec_add(flows, flow);

	if ((ret = tgen_reload()) < 0) {
		vec_pop(flows);
		rte_pktmbuf_free(m);
		free(flow);
		return api_out(-ret, 0, NULL);
	}

	resp = calloc(1, sizeof(*resp));
	if (resp == NULL)
		return api_out(ENOMEM, 0, NULL);
	resp->flow_id = flow->id;

	return api_out(0, sizeof(*resp), resp);
}

static void flow_free(struct tgen_flow *flow) {
	rte_pktmbuf_free(flow->priv.template);
	free(flow);
}

static struct api_out tgen_flow_del(const void *request, struct api_ctx *) {
	const struct gr_tgen_flow_del_req *req = request;
	struct tgen_flow *flow = NULL;
	int ret;

	for (unsigned i = 0; i < vec_len(flows); i++) {
		if (flows[i]->id == req->flow_id) {
			flow = flows[i];
			vec_del(flows, i);
			break;
		}
	}
	if (flow == NULL)
		return api_out(ENOENT, 0, NULL);

	// rebuild all graphs so no worker references the flow before freeing it
	if ((ret = tgen_reload()) < 0)
		LOG(ERR, "tgen_reload after flow del: %s", strerror(-ret));

	flow_free(flow);

	return api_out(0, 0, NULL);
}

static struct api_out tgen_flow_clear(const void * /*request*/, struct api_ctx *) {
	vec struct tgen_flow **old = flows;
	int ret;

	flows = NULL;
	atomic_store(&tgen_run.running, false);

	if ((ret = tgen_reload()) < 0)
		LOG(ERR, "tgen_reload after flow clear: %s", strerror(-ret));

	vec_foreach (struct tgen_flow *f, old)
		flow_free(f);
	vec_free(old);

	return api_out(0, 0, NULL);
}

static struct api_out tgen_flow_list(const void * /*request*/, struct api_ctx *ctx) {
	vec_foreach (struct tgen_flow *f, flows) {
		struct gr_tgen_flow g = {
			.id = f->id,
			.tx_iface_id = f->tx_iface_id,
			.rx_iface_id = f->rx_iface_id,
			.pkt_len = f->priv.pkt_len,
		};
		api_send(ctx, sizeof(g), &g);
	}
	return api_out(0, 0, NULL);
}

static struct module tgen_module = {
	.name = "tgen",
	.depends_on = "infra",
};

RTE_INIT(tgen_control_init) {
	atomic_store(&tgen_run.only_port, -1);
	api_handler(GR_TGEN_STATUS, tgen_status);
	api_handler(GR_TGEN_FLOW_ADD, tgen_flow_add);
	api_handler(GR_TGEN_FLOW_DEL, tgen_flow_del);
	api_handler(GR_TGEN_FLOW_CLEAR, tgen_flow_clear);
	api_handler(GR_TGEN_FLOW_LIST, tgen_flow_list);
	api_handler(GR_TGEN_START, tgen_start);
	api_handler(GR_TGEN_STOP, tgen_stop);
	worker_graph_builder_register(&tgen_builder);
	module_register(&tgen_module);
}
