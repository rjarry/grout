// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Robin Jarry

#include "cli.h"
#include "display.h"

#include <gr_api.h>
#include <gr_tgen.h>

#include <ecoli.h>

#include <stdlib.h>

#define TGEN_CTX(root) CLI_CONTEXT(root, CTX_ARG("tgen", "Stateless traffic generator."))

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
	return CLI_COMMAND(
		TGEN_CTX(root), "status", tgen_status, "Show traffic generator status."
	);
}

static struct cli_context ctx = {
	.name = "tgen",
	.init = ctx_init,
};

static void __attribute__((constructor, used)) init(void) {
	cli_context_register(&ctx);
}
