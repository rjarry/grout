// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2023 Robin Jarry

#ifndef _BROUTER_BRO
#define _BROUTER_BRO

#include <rte_log.h>

#include <stdbool.h>

struct brouter {
	const char *config_file_path;
	const char *api_sock_path;
	bool test_mode;

	// libevent
	struct event_base *base;
	struct event *ev_listen;
	struct event *ev_sock;
	struct event *ev_sigint;
	struct event *ev_sigquit;
	struct event *ev_sigterm;
	struct event *ev_sigchld;
	struct event *ev_sigpipe;

	// dpdk
	struct rte_mempool *api_pool;
};

#define DEFAULT_SOCK_PATH "/run/brouter.sock"
#define BROUTER "BROUTER"

#endif
