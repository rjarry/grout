// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2024 Robin Jarry

#ifndef _GR_INFRA_ETH_INPUT
#define _GR_INFRA_ETH_INPUT

#include "gr_mbuf.h"

#include <gr_iface.h>

#include <rte_byteorder.h>
#include <rte_graph.h>

typedef enum {
	ETH_DST_UNKNOWN = 0,
	ETH_DST_LOCAL, // destination is the input interface mac
	ETH_DST_BROADCAST, // destination is ff:ff:ff:ff:ff:ff
	ETH_DST_MULTICAST, // destination is a multicast ethernet address
	ETH_DST_OTHER, // destination is *not* the input interface mac
} eth_dst_type_t;

GR_MBUF_PRIV_DATA_TYPE(eth_input_mbuf_data, {
	const struct iface *iface;
	eth_dst_type_t eth_dst;
})

void gr_eth_input_add_type(rte_be16_t eth_type, const char *node_name);

#endif
