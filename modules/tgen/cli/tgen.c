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

static cmd_status_t tgen_status(struct gr_api_client *c, const struct ec_pnode *) {
	const struct gr_tgen_status_resp *resp;
	void *resp_ptr = NULL;

	if (gr_api_client_send_recv(c, GR_TGEN_STATUS, 0, NULL, &resp_ptr) < 0)
		return CMD_ERROR;

	resp = resp_ptr;

	struct gr_object *o = gr_object_new(NULL);
	gr_object_field(o, "running", GR_DISP_BOOL, "%s", resp->running ? "true" : "false");
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
