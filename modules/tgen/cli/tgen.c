// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Robin Jarry

#include "cli.h"
#include "cli_iface.h"
#include "display.h"

#include <gr_api.h>
#include <gr_infra.h>
#include <gr_tgen.h>

#include <ecoli.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TGEN_CTX(root) CLI_CONTEXT(root, CTX_ARG("tgen", "Stateless traffic generator."))
#define FLOW_CTX(root) CLI_CONTEXT(TGEN_CTX(root), CTX_ARG("flow", "Traffic flows."))
#define SWEEP_CTX(root) CLI_CONTEXT(TGEN_CTX(root), CTX_ARG("sweep", "Packet field sweeps."))

// Read the first ethernet frame from a pcap file into a freshly allocated
// buffer. Supports both microsecond and nanosecond pcap variants in either
// byte order.
static int pcap_read_first(const char *path, uint8_t **frame, uint16_t *frame_len) {
	uint8_t gh[24], rh[16];
	uint32_t magic, network, incl;
	uint8_t *buf;
	bool swap;
	FILE *f;

	if ((f = fopen(path, "rb")) == NULL)
		return -errno;

	if (fread(gh, 1, sizeof(gh), f) != sizeof(gh)) {
		fclose(f);
		return -EINVAL;
	}
	memcpy(&magic, gh, 4);
	if (magic == 0xa1b2c3d4 || magic == 0xa1b23c4d)
		swap = false;
	else if (magic == 0xd4c3b2a1 || magic == 0x4d3cb2a1)
		swap = true;
	else {
		fclose(f);
		return -EINVAL;
	}
	memcpy(&network, gh + 20, 4);
	if (swap)
		network = __builtin_bswap32(network);
	if (network != 1) { // LINKTYPE_ETHERNET
		fclose(f);
		return -EPROTONOSUPPORT;
	}

	if (fread(rh, 1, sizeof(rh), f) != sizeof(rh)) {
		fclose(f); // no packet in the capture
		return -ENODATA;
	}
	memcpy(&incl, rh + 8, 4);
	if (swap)
		incl = __builtin_bswap32(incl);
	if (incl == 0 || incl > GR_TGEN_MAX_PKT_LEN) {
		fclose(f);
		return -EMSGSIZE;
	}

	if ((buf = malloc(incl)) == NULL) {
		fclose(f);
		return -ENOMEM;
	}
	if (fread(buf, 1, incl, f) != incl) {
		free(buf);
		fclose(f);
		return -EINVAL;
	}
	fclose(f);

	*frame = buf;
	*frame_len = incl;
	return 0;
}

static cmd_status_t tgen_flow_add(struct gr_api_client *c, const struct ec_pnode *p) {
	struct gr_tgen_flow_add_req *req = NULL;
	const struct gr_tgen_flow_add_resp *resp;
	uint16_t tx_id, rx_id, frame_len = 0;
	void *resp_ptr = NULL;
	uint8_t *frame = NULL;
	const char *path;
	size_t len;
	int ret;

	if (arg_iface(c, p, "TX", GR_IFACE_TYPE_PORT, &tx_id) < 0)
		return CMD_ERROR;
	if (arg_iface(c, p, "RX", GR_IFACE_TYPE_PORT, &rx_id) < 0)
		return CMD_ERROR;

	path = arg_str(p, "FILE");
	if ((ret = pcap_read_first(path, &frame, &frame_len)) < 0) {
		errorf("%s: %s", path, strerror(-ret));
		return CMD_ERROR;
	}

	len = sizeof(*req) + frame_len;
	if ((req = calloc(1, len)) == NULL) {
		free(frame);
		return CMD_ERROR;
	}
	req->tx_iface_id = tx_id;
	req->rx_iface_id = rx_id;
	req->pkt_len = frame_len;
	memcpy(req->pkt, frame, frame_len);
	free(frame);

	if (gr_api_client_send_recv(c, GR_TGEN_FLOW_ADD, len, req, &resp_ptr) < 0) {
		free(req);
		return CMD_ERROR;
	}
	free(req);

	resp = resp_ptr;
	printf("Created flow %u\n", resp->flow_id);
	free(resp_ptr);

	return CMD_SUCCESS;
}

static cmd_status_t tgen_flow_del(struct gr_api_client *c, const struct ec_pnode *p) {
	struct gr_tgen_flow_del_req req = {0};

	if (arg_u32(p, "ID", &req.flow_id) < 0)
		return CMD_ERROR;

	if (gr_api_client_send_recv(c, GR_TGEN_FLOW_DEL, sizeof(req), &req, NULL) < 0)
		return CMD_ERROR;

	return CMD_SUCCESS;
}

static cmd_status_t tgen_flow_clear(struct gr_api_client *c, const struct ec_pnode *) {
	if (gr_api_client_send_recv(c, GR_TGEN_FLOW_CLEAR, 0, NULL, NULL) < 0)
		return CMD_ERROR;
	return CMD_SUCCESS;
}

static cmd_status_t tgen_flow_show(struct gr_api_client *c, const struct ec_pnode *) {
	const struct gr_tgen_flow *flow;
	struct gr_table *t;
	int ret;

	t = gr_table_new();
	gr_table_column(t, "ID", GR_DISP_RIGHT | GR_DISP_INT);
	gr_table_column(t, "TX", GR_DISP_LEFT);
	gr_table_column(t, "RX", GR_DISP_LEFT);
	gr_table_column(t, "PKT_LEN", GR_DISP_RIGHT | GR_DISP_INT);

	gr_api_client_stream_foreach (flow, ret, c, GR_TGEN_FLOW_LIST, 0, NULL) {
		gr_table_cell(t, 0, "%u", flow->id);
		gr_table_cell(t, 1, "%s", iface_name_from_id(c, flow->tx_iface_id));
		gr_table_cell(t, 2, "%s", iface_name_from_id(c, flow->rx_iface_id));
		gr_table_cell(t, 3, "%u", flow->pkt_len);
		gr_table_print_row(t);
	}
	gr_table_free(t);

	if (ret < 0)
		return CMD_ERROR;

	return CMD_SUCCESS;
}

static cmd_status_t tgen_sweep_add(struct gr_api_client *c, const struct ec_pnode *p) {
	struct gr_tgen_sweep_add_req req = {.step = 1};
	const struct gr_tgen_sweep_add_resp *resp;
	void *resp_ptr = NULL;
	uint64_t step;

	if (arg_u32(p, "FLOW", &req.flow_id) < 0)
		return CMD_ERROR;
	if (arg_u16(p, "OFF", &req.offset) < 0)
		return CMD_ERROR;
	if (arg_u16(p, "SIZE", &req.size) < 0)
		return CMD_ERROR;
	if (arg_u64(p, "START", &req.start) < 0)
		return CMD_ERROR;
	if (arg_u64(p, "END", &req.end) < 0)
		return CMD_ERROR;
	if (arg_u64(p, "STEP", &step) < 0) {
		if (errno != ENOENT)
			return CMD_ERROR;
	} else {
		req.step = step;
	}

	if (gr_api_client_send_recv(c, GR_TGEN_SWEEP_ADD, sizeof(req), &req, &resp_ptr) < 0)
		return CMD_ERROR;

	resp = resp_ptr;
	printf("Created sweep %u\n", resp->sweep_id);
	free(resp_ptr);

	return CMD_SUCCESS;
}

static cmd_status_t tgen_sweep_del(struct gr_api_client *c, const struct ec_pnode *p) {
	struct gr_tgen_sweep_del_req req = {0};

	if (arg_u32(p, "ID", &req.sweep_id) < 0)
		return CMD_ERROR;
	if (gr_api_client_send_recv(c, GR_TGEN_SWEEP_DEL, sizeof(req), &req, NULL) < 0)
		return CMD_ERROR;
	return CMD_SUCCESS;
}

static cmd_status_t tgen_sweep_clear(struct gr_api_client *c, const struct ec_pnode *) {
	if (gr_api_client_send_recv(c, GR_TGEN_SWEEP_CLEAR, 0, NULL, NULL) < 0)
		return CMD_ERROR;
	return CMD_SUCCESS;
}

static cmd_status_t tgen_sweep_show(struct gr_api_client *c, const struct ec_pnode *) {
	const struct gr_tgen_sweep *sw;
	struct gr_table *t;
	int ret;

	t = gr_table_new();
	gr_table_column(t, "ID", GR_DISP_RIGHT | GR_DISP_INT);
	gr_table_column(t, "FLOW", GR_DISP_RIGHT | GR_DISP_INT);
	gr_table_column(t, "OFFSET", GR_DISP_RIGHT | GR_DISP_INT);
	gr_table_column(t, "SIZE", GR_DISP_RIGHT | GR_DISP_INT);
	gr_table_column(t, "START", GR_DISP_RIGHT | GR_DISP_INT);
	gr_table_column(t, "END", GR_DISP_RIGHT | GR_DISP_INT);
	gr_table_column(t, "STEP", GR_DISP_RIGHT | GR_DISP_INT);

	gr_api_client_stream_foreach (sw, ret, c, GR_TGEN_SWEEP_LIST, 0, NULL) {
		gr_table_cell(t, 0, "%u", sw->id);
		gr_table_cell(t, 1, "%u", sw->flow_id);
		gr_table_cell(t, 2, "%u", sw->offset);
		gr_table_cell(t, 3, "%u", sw->size);
		gr_table_cell(t, 4, "%lu", sw->start);
		gr_table_cell(t, 5, "%lu", sw->end);
		gr_table_cell(t, 6, "%lu", sw->step);
		gr_table_print_row(t);
	}
	gr_table_free(t);

	if (ret < 0)
		return CMD_ERROR;

	return CMD_SUCCESS;
}

static cmd_status_t tgen_start(struct gr_api_client *c, const struct ec_pnode *p) {
	struct gr_tgen_start_req req = {0};
	const char *rate;
	uint16_t pid;
	double v;
	char *end;

	rate = arg_str(p, "RATE");
	v = strtod(rate, &end);
	if (end == rate || v <= 0) {
		errorf("invalid rate '%s'", rate);
		return CMD_ERROR;
	}
	if (strcmp(end, "%") == 0) {
		req.rate_mode = GR_TGEN_RATE_PCT;
	} else if (strcmp(end, "pps") == 0) {
		req.rate_mode = GR_TGEN_RATE_PPS;
	} else {
		errorf("rate must end with %% or pps");
		return CMD_ERROR;
	}
	req.rate_value = v;

	if (arg_iface(c, p, "PORT", GR_IFACE_TYPE_PORT, &pid) < 0) {
		if (errno != ENOENT)
			return CMD_ERROR;
	} else {
		req.only_iface_id = pid;
		req.has_port = true;
	}

	if (gr_api_client_send_recv(c, GR_TGEN_START, sizeof(req), &req, NULL) < 0)
		return CMD_ERROR;

	return CMD_SUCCESS;
}

static cmd_status_t tgen_stop(struct gr_api_client *c, const struct ec_pnode *) {
	if (gr_api_client_send_recv(c, GR_TGEN_STOP, 0, NULL, NULL) < 0)
		return CMD_ERROR;
	return CMD_SUCCESS;
}

static cmd_status_t tgen_status(struct gr_api_client *c, const struct ec_pnode *) {
	const struct gr_tgen_status_resp *resp;
	void *resp_ptr = NULL;
	double drop_pct = 0;

	if (gr_api_client_send_recv(c, GR_TGEN_STATUS, 0, NULL, &resp_ptr) < 0)
		return CMD_ERROR;

	resp = resp_ptr;
	if (resp->tx_packets > 0)
		drop_pct = 100.0 * (double)resp->drop_packets / (double)resp->tx_packets;

	struct gr_object *o = gr_object_new(NULL);
	gr_object_field(o, "running", GR_DISP_BOOL, "%s", resp->running ? "true" : "false");
	if (resp->rate_mode == GR_TGEN_RATE_PCT)
		gr_object_field(o, "rate", GR_DISP_LEFT, "%g%%", resp->rate_value);
	else if (resp->rate_mode == GR_TGEN_RATE_PPS)
		gr_object_field(o, "rate", GR_DISP_LEFT, "%g pps", resp->rate_value);
	gr_object_field(o, "pps_per_clone", GR_DISP_FLOAT, "%.0f", resp->pps_per_clone);
	gr_object_field(o, "tx_packets", GR_DISP_INT, "%lu", resp->tx_packets);
	gr_object_field(o, "tx_bytes", GR_DISP_INT, "%lu", resp->tx_bytes);
	gr_object_field(o, "rx_packets", GR_DISP_INT, "%lu", resp->rx_packets);
	gr_object_field(o, "rx_bytes", GR_DISP_INT, "%lu", resp->rx_bytes);
	gr_object_field(o, "rx_missed", GR_DISP_INT, "%lu", resp->rx_missed);
	gr_object_field(o, "drop_packets", GR_DISP_INT, "%lu", resp->drop_packets);
	gr_object_field(o, "drop_pct", GR_DISP_FLOAT, "%.6f", drop_pct);
	gr_object_free(o);

	free(resp_ptr);

	return CMD_SUCCESS;
}

static int ctx_init(struct ec_node *root) {
	int ret;

	ret = CLI_COMMAND(
		FLOW_CTX(root),
		"add tx TX rx RX pcap FILE",
		tgen_flow_add,
		"Add a traffic flow from a pcap template.",
		with_help(
			"Transmit interface.",
			ec_node_dyn("TX", complete_iface_names, INT2PTR(GR_IFACE_TYPE_PORT))
		),
		with_help(
			"Receive interface.",
			ec_node_dyn("RX", complete_iface_names, INT2PTR(GR_IFACE_TYPE_PORT))
		),
		with_help("Path to a pcap file.", ec_node("any", "FILE"))
	);
	if (ret < 0)
		return ret;

	ret = CLI_COMMAND(
		FLOW_CTX(root),
		"del ID",
		tgen_flow_del,
		"Delete a traffic flow.",
		with_help("Flow ID.", ec_node_uint("ID", 1, UINT32_MAX, 10))
	);
	if (ret < 0)
		return ret;

	ret = CLI_COMMAND(FLOW_CTX(root), "clear", tgen_flow_clear, "Delete all traffic flows.");
	if (ret < 0)
		return ret;

	ret = CLI_COMMAND(FLOW_CTX(root), "[show]", tgen_flow_show, "List traffic flows.");
	if (ret < 0)
		return ret;

	ret = CLI_COMMAND(
		SWEEP_CTX(root),
		"add flow FLOW offset OFF start START end END size SIZE [step STEP]",
		tgen_sweep_add,
		"Add a field sweep to a flow.",
		with_help("Flow ID.", ec_node_uint("FLOW", 1, UINT32_MAX, 10)),
		with_help("Field offset in the frame.", ec_node_uint("OFF", 0, UINT16_MAX, 10)),
		with_help("First value (inclusive).", ec_node_uint("START", 0, UINT32_MAX, 10)),
		with_help("Last value (inclusive).", ec_node_uint("END", 0, UINT32_MAX, 10)),
		with_help("Field width in bytes (1-8).", ec_node_uint("SIZE", 1, 8, 10)),
		with_help("Increment (default 1).", ec_node_uint("STEP", 1, UINT32_MAX, 10))
	);
	if (ret < 0)
		return ret;

	ret = CLI_COMMAND(
		SWEEP_CTX(root),
		"del ID",
		tgen_sweep_del,
		"Delete a field sweep.",
		with_help("Sweep ID.", ec_node_uint("ID", 1, UINT32_MAX, 10))
	);
	if (ret < 0)
		return ret;

	ret = CLI_COMMAND(SWEEP_CTX(root), "clear", tgen_sweep_clear, "Delete all field sweeps.");
	if (ret < 0)
		return ret;

	ret = CLI_COMMAND(SWEEP_CTX(root), "[show]", tgen_sweep_show, "List field sweeps.");
	if (ret < 0)
		return ret;

	ret = CLI_COMMAND(
		TGEN_CTX(root),
		"start rate RATE [port PORT]",
		tgen_start,
		"Start generating traffic at the given rate.",
		with_help(
			"Rate as a line-rate percentage (e.g. 100%) or packets per "
			"second (e.g. 1000pps).",
			ec_node_re("RATE", "[0-9]+(\\.[0-9]+)?(%|pps)")
		),
		with_help(
			"Only transmit out of this port.",
			ec_node_dyn("PORT", complete_iface_names, INT2PTR(GR_IFACE_TYPE_PORT))
		)
	);
	if (ret < 0)
		return ret;

	ret = CLI_COMMAND(TGEN_CTX(root), "stop", tgen_stop, "Stop generating traffic.");
	if (ret < 0)
		return ret;

	ret = CLI_COMMAND(TGEN_CTX(root), "status", tgen_status, "Show traffic generator status.");
	if (ret < 0)
		return ret;

	return 0;
}

static struct cli_context ctx = {
	.name = "tgen",
	.init = ctx_init,
};

static void __attribute__((constructor, used)) init(void) {
	cli_context_register(&ctx);
}
