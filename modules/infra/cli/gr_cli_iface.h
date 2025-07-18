// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2024 Robin Jarry

#pragma once

#include <gr_infra.h>

#include <ecoli.h>

#include <stdint.h>
#include <sys/queue.h>

struct cli_iface_type {
	STAILQ_ENTRY(cli_iface_type) next;
	gr_iface_type_t type_id;
	const char *name;
	void (*show)(const struct gr_api_client *c, const struct gr_iface *);
	void (*list_info)(const struct gr_api_client *c, const struct gr_iface *, char *, size_t);
};

void register_iface_type(struct cli_iface_type *);

const struct cli_iface_type *type_from_name(const char *name);
const struct cli_iface_type *type_from_id(gr_iface_type_t type_id);
int iface_from_name(const struct gr_api_client *c, const char *name, struct gr_iface *iface);
int iface_from_id(const struct gr_api_client *c, uint16_t ifid, struct gr_iface *iface);

struct ec_node;
struct ec_comp;

int complete_iface_types(
	const struct gr_api_client *c,
	const struct ec_node *node,
	struct ec_comp *comp,
	const char *arg,
	void *cb_arg
);
int complete_iface_names(
	const struct gr_api_client *c,
	const struct ec_node *node,
	struct ec_comp *comp,
	const char *arg,
	void *cb_arg
);

#define INT2PTR(i) (void *)(uintptr_t)(i)

#define IFACE_ATTRS_CMD "(up|down),(promisc PROMISC),(allmulti ALLMULTI),(mtu MTU),(vrf VRF)"

#define IFACE_ATTRS_ARGS                                                                           \
	with_help("Set the interface UP.", ec_node_str("up", "up")),                               \
		with_help("Enable/disable promiscuous mode.", ec_node_re("PROMISC", "on|off")),    \
		with_help("Enable/disable all-multicast mode.", ec_node_re("ALLMULTI", "on|off")), \
		with_help("Set the interface DOWN.", ec_node_str("down", "down")),                 \
		with_help(                                                                         \
			"Maximum transmission unit size.",                                         \
			ec_node_uint("MTU", 1280, UINT16_MAX - 1, 10)                              \
		),                                                                                 \
		with_help(                                                                         \
			"L3 addressing/routing domain ID.",                                        \
			ec_node_uint("VRF", 0, UINT16_MAX - 1, 10)                                 \
		)

uint64_t parse_iface_args(
	const struct gr_api_client *c,
	const struct ec_pnode *p,
	struct gr_iface *iface,
	bool update
);
