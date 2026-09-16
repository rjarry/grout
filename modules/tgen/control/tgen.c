// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Robin Jarry

#include "iface.h"
#include "ip4.h"
#include "ip6.h"
#include "log.h"
#include "mempool.h"
#include "module.h"
#include "nexthop.h"
#include "pktbuild.h"
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
	uint16_t weight;
	struct tgen_flow_priv priv;
};

static vec struct tgen_flow **flows;
static uint32_t next_flow_id = 1;
static struct rte_mempool *tgen_pool;

struct tgen_sweep_entry {
	uint32_t id;
	uint32_t flow_id;
	struct tgen_sweep cfg;
};

static vec struct tgen_sweep_entry **sweeps;
static uint32_t next_sweep_id = 1;

static struct tgen_flow *flow_find(uint32_t id) {
	vec_foreach (struct tgen_flow *f, flows)
		if (f->id == id)
			return f;
	return NULL;
}

// Rebuild the flat sweep array the datapath reads for a flow from the control
// plane registry.
static void flow_sweeps_rebuild(struct tgen_flow *f) {
	unsigned n = 0;

	rte_free(f->priv.sweeps);
	f->priv.sweeps = NULL;
	f->priv.n_sweeps = 0;

	vec_foreach (struct tgen_sweep_entry *e, sweeps)
		if (e->flow_id == f->id)
			n++;
	if (n == 0)
		return;

	f->priv.sweeps = rte_malloc(__func__, n * sizeof(*f->priv.sweeps), 0);
	if (f->priv.sweeps == NULL) {
		LOG(ERR, "rte_malloc(tgen sweeps) failed");
		return;
	}
	vec_foreach (struct tgen_sweep_entry *e, sweeps)
		if (e->flow_id == f->id)
			f->priv.sweeps[f->priv.n_sweeps++] = e->cfg;
}

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

struct run_counters {
	uint64_t tx_packets;
	uint64_t tx_bytes;
	uint64_t rx_packets;
	uint64_t rx_bytes;
	uint64_t rx_missed;
	uint64_t drop_packets;
};

// Aggregate the per-run hardware counters over the distinct transmit and receive
// ports of all flows. imissed is added back to the received count so a saturated
// receive queue is not mistaken for a drop caused by the device under test.
static void run_counters_get(struct run_counters *c) {
	bool tx_seen[RTE_MAX_ETHPORTS] = {0};
	bool rx_seen[RTE_MAX_ETHPORTS] = {0};
	struct rte_eth_stats s;

	memset(c, 0, sizeof(*c));

	vec_foreach (struct tgen_flow *f, flows) {
		if (!tx_seen[f->tx_port_id] && rte_eth_stats_get(f->tx_port_id, &s) == 0) {
			tx_seen[f->tx_port_id] = true;
			c->tx_packets += s.opackets - baseline[f->tx_port_id].opackets;
			c->tx_bytes += s.obytes - baseline[f->tx_port_id].obytes;
		}
		if (!rx_seen[f->rx_port_id] && rte_eth_stats_get(f->rx_port_id, &s) == 0) {
			rx_seen[f->rx_port_id] = true;
			c->rx_packets += s.ipackets - baseline[f->rx_port_id].ipackets;
			c->rx_bytes += s.ibytes - baseline[f->rx_port_id].ibytes;
			c->rx_missed += s.imissed - baseline[f->rx_port_id].imissed;
		}
	}

	if (c->tx_packets > c->rx_packets + c->rx_missed)
		c->drop_packets = c->tx_packets - (c->rx_packets + c->rx_missed);
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

	unsigned n = 0, total_weight = 0;
	vec_foreach (struct tgen_flow *f, flows) {
		if (f->tx_port_id == port_id) {
			n++;
			total_weight += f->weight;
		}
	}
	if (n == 0) {
		node->ctx_ptr = ctx;
		return;
	}

	ctx->flows = rte_zmalloc(__func__, n * sizeof(*ctx->flows), RTE_CACHE_LINE_SIZE);
	ctx->sched = rte_malloc(__func__, total_weight * sizeof(*ctx->sched), RTE_CACHE_LINE_SIZE);
	if (ctx->flows == NULL || ctx->sched == NULL) {
		LOG(ERR, "rte_malloc(tgen flows/sched) failed");
		rte_free(ctx->flows);
		rte_free(ctx->sched);
		rte_free(ctx);
		return;
	}

	vec_foreach (struct tgen_flow *f, flows) {
		if (f->tx_port_id != port_id)
			continue;
		unsigned idx = ctx->n_flows++;
		struct tgen_tx_flow *tf = &ctx->flows[idx];
		tf->priv = &f->priv;
		if (f->priv.n_sweeps > 0) {
			tf->cursors = rte_malloc(
				__func__,
				f->priv.n_sweeps * sizeof(*tf->cursors),
				RTE_CACHE_LINE_SIZE
			);
			if (tf->cursors != NULL) {
				for (unsigned s = 0; s < f->priv.n_sweeps; s++)
					tf->cursors[s] = f->priv.sweeps[s].start;
			} else {
				LOG(ERR, "rte_malloc(tgen cursors) failed");
			}
		}
		// append this flow to the weighted schedule
		for (unsigned w = 0; w < f->weight; w++)
			ctx->sched[ctx->sched_len++] = idx;
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

// RFC2544 no-drop-rate binary search, driven by a libevent timer

#define RFC2544_DEFAULT_ITER 10
#define RFC2544_DEFAULT_DURATION 2.0
#define RFC2544_RATE_EPSILON 0.1 // stop once the rate interval is this narrow (%)

static struct event_base *tgen_base;

static struct rfc2544 {
	bool active;
	unsigned iter;
	unsigned max_iter;
	double low; // highest known passing rate (percent)
	double high; // lowest known failing rate (percent)
	double cur; // rate under test
	double best; // best passing rate found, -1 if none
	double max_drop_pct;
	struct timeval duration;
	struct event *timer;
} rfc;

static void rfc_finish(void) {
	atomic_store(&tgen_run.running, false);
	rfc.active = false;
	LOG(NOTICE, "rfc2544: done after %u iterations, NDR=%.3f%%", rfc.iter, rfc.best);
}

static void rfc_start_iteration(void) {
	rfc.cur = (rfc.low + rfc.high) / 2.0;
	rate_mode = GR_TGEN_RATE_PCT;
	rate_value = rfc.cur;
	rate_only_port = -1;

	if (rate_apply() < 0) {
		LOG(ERR, "rfc2544: rate_apply failed, aborting");
		rfc_finish();
		return;
	}
	baseline_reset();
	atomic_store(&tgen_run.running, true);
	if (event_add(rfc.timer, &rfc.duration) < 0) {
		LOG(ERR, "rfc2544: event_add failed, aborting");
		rfc_finish();
	}
}

static void rfc_iteration_done(evutil_socket_t, short, void *) {
	struct run_counters c;
	double drop_pct;

	atomic_store(&tgen_run.running, false);
	run_counters_get(&c);
	drop_pct = c.tx_packets > 0 ? 100.0 * (double)c.drop_packets / (double)c.tx_packets : 100.0;

	if (drop_pct <= rfc.max_drop_pct) {
		rfc.best = rfc.cur;
		rfc.low = rfc.cur; // passed: aim higher
	} else {
		rfc.high = rfc.cur; // failed: aim lower
	}
	rfc.iter++;

	LOG(INFO,
	    "rfc2544: iteration %u rate=%.3f%% drop=%.6f%% best=%.3f%%",
	    rfc.iter,
	    rfc.cur,
	    drop_pct,
	    rfc.best);

	if (rfc.iter >= rfc.max_iter || (rfc.high - rfc.low) < RFC2544_RATE_EPSILON) {
		rfc_finish();
		return;
	}
	rfc_start_iteration();
}

static struct api_out tgen_rfc2544(const void *request, struct api_ctx *) {
	const struct gr_tgen_rfc2544_req *req = request;
	double dur;

	if (vec_len(flows) == 0)
		return api_out(ENOENT, 0, NULL);
	if (atomic_load(&tgen_run.running) || rfc.active)
		return api_out(EBUSY, 0, NULL);
	if (req->max_drop < 0)
		return api_out(EINVAL, 0, NULL);

	rfc.max_iter = req->max_iterations != 0 ? req->max_iterations : RFC2544_DEFAULT_ITER;
	rfc.max_drop_pct = req->max_drop;
	dur = req->duration > 0 ? req->duration : RFC2544_DEFAULT_DURATION;
	rfc.duration.tv_sec = (time_t)dur;
	rfc.duration.tv_usec = (suseconds_t)((dur - (double)rfc.duration.tv_sec) * 1e6);
	rfc.low = 0;
	rfc.high = 100;
	rfc.best = -1;
	rfc.iter = 0;
	rfc.active = true;

	if (rfc.timer == NULL) {
		rfc.timer = evtimer_new(tgen_base, rfc_iteration_done, NULL);
		if (rfc.timer == NULL) {
			rfc.active = false;
			return api_out(ENOMEM, 0, NULL);
		}
	}

	rfc_start_iteration();
	if (!rfc.active)
		return api_out(EIO, 0, NULL);

	return api_out(0, 0, NULL);
}

// API handlers

static struct api_out tgen_status(const void * /*request*/, struct api_ctx *) {
	struct gr_tgen_status_resp *resp;
	struct run_counters c;

	resp = calloc(1, sizeof(*resp));
	if (resp == NULL)
		return api_out(ENOMEM, 0, NULL);

	resp->running = atomic_load(&tgen_run.running);
	resp->rate_mode = rate_mode;
	resp->rate_value = rate_value;
	resp->pps_per_clone = atomic_load(&tgen_run.pps_per_clone);

	run_counters_get(&c);
	resp->tx_packets = c.tx_packets;
	resp->tx_bytes = c.tx_bytes;
	resp->rx_packets = c.rx_packets;
	resp->rx_bytes = c.rx_bytes;
	resp->rx_missed = c.rx_missed;
	resp->drop_packets = c.drop_packets;

	resp->rfc2544_active = rfc.active;
	resp->rfc2544_iteration = rfc.iter;
	resp->rfc2544_ndr = rfc.best;

	return api_out(0, sizeof(*resp), resp);
}

static struct api_out tgen_start(const void *request, struct api_ctx *) {
	const struct gr_tgen_start_req *req = request;
	int only_port = -1;
	int ret;

	if (vec_len(flows) == 0)
		return api_out(ENOENT, 0, NULL);
	if (rfc.active)
		return api_out(EBUSY, 0, NULL);
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

// Resolve the MAC and IP addresses used to fill unset packet fields: source
// from the transmit interface, destination from the receive interface.
static void
tgen_resolve_defaults(uint16_t tx_iface_id, uint16_t rx_iface_id, struct tgen_pkt_defaults *def) {
	struct rte_ipv6_addr any6 = {0};
	struct iface *txi = iface_from_id(tx_iface_id);
	struct iface *rxi = iface_from_id(rx_iface_id);
	struct nexthop *nh;

	if (txi != NULL)
		iface_get_eth_addr(txi, &def->src_mac);
	if (rxi != NULL)
		iface_get_eth_addr(rxi, &def->dst_mac);
	if (txi != NULL && (nh = addr4_get_preferred(txi->id, 0)) != NULL)
		def->src_ip4 = nexthop_info_l3(nh)->ipv4;
	if (rxi != NULL && (nh = addr4_get_preferred(rxi->id, 0)) != NULL)
		def->dst_ip4 = nexthop_info_l3(nh)->ipv4;
	if (txi != NULL && (nh = addr6_get_preferred(txi->id, &any6)) != NULL)
		def->src_ip6 = nexthop_info_l3(nh)->ipv6;
	if (rxi != NULL && (nh = addr6_get_preferred(rxi->id, &any6)) != NULL)
		def->dst_ip6 = nexthop_info_l3(nh)->ipv6;
}

static struct api_out tgen_flow_add(const void *request, struct api_ctx *ctx) {
	const struct gr_tgen_flow_add_req *req = request;
	uint8_t framebuf[GR_TGEN_MAX_PKT_LEN];
	struct gr_tgen_flow_add_resp *resp;
	uint16_t tx_port, rx_port;
	const uint8_t *frame;
	uint16_t frame_len;
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

	if (req->format == GR_TGEN_PKT_TEXT) {
		struct tgen_pkt_defaults def = {0};
		char errbuf[128] = {0};
		if (req->pkt[req->pkt_len - 1] != '\0')
			return api_out(EINVAL, 0, NULL);
		tgen_resolve_defaults(req->tx_iface_id, req->rx_iface_id, &def);
		ret = tgen_pkt_build(
			(const char *)req->pkt,
			&def,
			framebuf,
			sizeof(framebuf),
			&frame_len,
			errbuf,
			sizeof(errbuf)
		);
		if (ret < 0) {
			LOG(ERR, "tgen flow: %s", errbuf);
			return api_out(-ret, 0, NULL);
		}
		frame = framebuf;
	} else {
		frame = req->pkt;
		frame_len = req->pkt_len;
	}

	if (tgen_pool == NULL) {
		tgen_pool = gr_pktmbuf_pool_get(SOCKET_ID_ANY, TGEN_POOL_SIZE);
		if (tgen_pool == NULL)
			return api_out(ENOMEM, 0, NULL);
	}

	m = rte_pktmbuf_alloc(tgen_pool);
	if (m == NULL)
		return api_out(ENOMEM, 0, NULL);
	data = rte_pktmbuf_append(m, frame_len);
	if (data == NULL) {
		rte_pktmbuf_free(m);
		return api_out(EMSGSIZE, 0, NULL);
	}
	memcpy(data, frame, frame_len);

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
	flow->weight = req->weight != 0 ? req->weight : 1;
	flow->priv.template = m;
	flow->priv.pkt_len = frame_len;
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

// Remove all sweep entries attached to a flow.
static void flow_sweeps_drop(uint32_t flow_id) {
	for (unsigned i = 0; i < vec_len(sweeps);) {
		if (sweeps[i]->flow_id == flow_id) {
			free(sweeps[i]);
			vec_del(sweeps, i);
		} else {
			i++;
		}
	}
}

static void flow_free(struct tgen_flow *flow) {
	rte_pktmbuf_free(flow->priv.template);
	rte_free(flow->priv.sweeps);
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

	flow_sweeps_drop(flow->id);

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

	vec_foreach (struct tgen_sweep_entry *e, sweeps)
		free(e);
	vec_free(sweeps);

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
			.weight = f->weight,
		};
		api_send(ctx, sizeof(g), &g);
	}
	return api_out(0, 0, NULL);
}

static struct api_out tgen_sweep_add(const void *request, struct api_ctx *) {
	const struct gr_tgen_sweep_add_req *req = request;
	struct gr_tgen_sweep_add_resp *resp;
	struct tgen_sweep_entry *entry;
	struct tgen_flow *flow;
	int ret;

	flow = flow_find(req->flow_id);
	if (flow == NULL)
		return api_out(ENOENT, 0, NULL);
	if (req->size < 1 || req->size > 8)
		return api_out(EINVAL, 0, NULL);
	if ((uint32_t)req->offset + req->size > flow->priv.pkt_len)
		return api_out(ERANGE, 0, NULL);
	if (req->end < req->start || req->step == 0)
		return api_out(EINVAL, 0, NULL);
	// a size < 8 field cannot hold values above its width
	if (req->size < 8 && req->end > (UINT64_C(1) << (8 * req->size)) - 1)
		return api_out(ERANGE, 0, NULL);

	entry = calloc(1, sizeof(*entry));
	if (entry == NULL)
		return api_out(ENOMEM, 0, NULL);
	entry->id = next_sweep_id++;
	entry->flow_id = flow->id;
	entry->cfg = (struct tgen_sweep) {
		.offset = req->offset,
		.size = req->size,
		.start = req->start,
		.end = req->end,
		.step = req->step,
	};
	vec_add(sweeps, entry);

	flow_sweeps_rebuild(flow);
	if ((ret = tgen_reload()) < 0) {
		vec_pop(sweeps);
		free(entry);
		flow_sweeps_rebuild(flow);
		return api_out(-ret, 0, NULL);
	}

	resp = calloc(1, sizeof(*resp));
	if (resp == NULL)
		return api_out(ENOMEM, 0, NULL);
	resp->sweep_id = entry->id;

	return api_out(0, sizeof(*resp), resp);
}

static struct api_out tgen_sweep_del(const void *request, struct api_ctx *) {
	const struct gr_tgen_sweep_del_req *req = request;
	struct tgen_sweep_entry *entry = NULL;
	struct tgen_flow *flow;
	int ret;

	for (unsigned i = 0; i < vec_len(sweeps); i++) {
		if (sweeps[i]->id == req->sweep_id) {
			entry = sweeps[i];
			vec_del(sweeps, i);
			break;
		}
	}
	if (entry == NULL)
		return api_out(ENOENT, 0, NULL);

	flow = flow_find(entry->flow_id);
	if (flow != NULL)
		flow_sweeps_rebuild(flow);
	free(entry);

	if ((ret = tgen_reload()) < 0)
		LOG(ERR, "tgen_reload after sweep del: %s", strerror(-ret));

	return api_out(0, 0, NULL);
}

static struct api_out tgen_sweep_clear(const void * /*request*/, struct api_ctx *) {
	int ret;

	vec_foreach (struct tgen_sweep_entry *e, sweeps)
		free(e);
	vec_free(sweeps);

	vec_foreach (struct tgen_flow *f, flows)
		flow_sweeps_rebuild(f);

	if ((ret = tgen_reload()) < 0)
		LOG(ERR, "tgen_reload after sweep clear: %s", strerror(-ret));

	return api_out(0, 0, NULL);
}

static struct api_out tgen_sweep_list(const void * /*request*/, struct api_ctx *ctx) {
	vec_foreach (struct tgen_sweep_entry *e, sweeps) {
		struct gr_tgen_sweep g = {
			.id = e->id,
			.flow_id = e->flow_id,
			.offset = e->cfg.offset,
			.size = e->cfg.size,
			.start = e->cfg.start,
			.end = e->cfg.end,
			.step = e->cfg.step,
		};
		api_send(ctx, sizeof(g), &g);
	}
	return api_out(0, 0, NULL);
}

static void tgen_control_start(struct event_base *base) {
	tgen_base = base;
	rfc.best = -1;
	atomic_store(&tgen_run.only_port, -1);
}

static struct module tgen_module = {
	.name = "tgen",
	.depends_on = "infra,ip,ip6",
	.init = tgen_control_start,
};

RTE_INIT(tgen_control_init) {
	api_handler(GR_TGEN_STATUS, tgen_status);
	api_handler(GR_TGEN_FLOW_ADD, tgen_flow_add);
	api_handler(GR_TGEN_FLOW_DEL, tgen_flow_del);
	api_handler(GR_TGEN_FLOW_CLEAR, tgen_flow_clear);
	api_handler(GR_TGEN_FLOW_LIST, tgen_flow_list);
	api_handler(GR_TGEN_START, tgen_start);
	api_handler(GR_TGEN_STOP, tgen_stop);
	api_handler(GR_TGEN_SWEEP_ADD, tgen_sweep_add);
	api_handler(GR_TGEN_SWEEP_DEL, tgen_sweep_del);
	api_handler(GR_TGEN_SWEEP_CLEAR, tgen_sweep_clear);
	api_handler(GR_TGEN_SWEEP_LIST, tgen_sweep_list);
	api_handler(GR_TGEN_RFC2544, tgen_rfc2544);
	worker_graph_builder_register(&tgen_builder);
	module_register(&tgen_module);
}
