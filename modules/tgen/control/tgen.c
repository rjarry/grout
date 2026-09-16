// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Robin Jarry

#include "module.h"

#include <gr_tgen.h>

#include <rte_common.h>

#include <errno.h>
#include <stdlib.h>

static struct api_out tgen_status(const void * /*request*/, struct api_ctx *) {
	struct gr_tgen_status_resp *resp = calloc(1, sizeof(*resp));
	if (resp == NULL)
		return api_out(ENOMEM, 0, NULL);

	resp->running = false;

	return api_out(0, sizeof(*resp), resp);
}

static struct module tgen_module = {
	.name = "tgen",
	.depends_on = "infra",
};

RTE_INIT(tgen_control_init) {
	api_handler(GR_TGEN_STATUS, tgen_status);
	module_register(&tgen_module);
}
