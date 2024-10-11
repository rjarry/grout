// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2024 Christophe Fontaine
// Copyright (c) 2024 Robin Jarry

#ifndef _GR_INFRA_PACKET_TRACE
#define _GR_INFRA_PACKET_TRACE

#include <rte_arp.h>
#include <rte_graph.h>
#include <rte_mbuf.h>

#include <sys/queue.h>

// Write a log message with detailed packet information.
void trace_log_packet(const struct rte_mbuf *m, const char *node, const char *iface);

// Callback associated with each node that will be invoked by gr_trace_dump
// to format each individual trace items.
typedef int (*gr_trace_format_cb_t)(char *buf, size_t buf_len, const void *data, uint16_t data_len);

// Format the buffered trace items and empty the buffer.
// Return the number of bytes written to buffer or a negative value on error.
int gr_trace_dump(char *buf, size_t buf_len);

// Empty the trace buffer.
void gr_trace_clear(void);

// Return true if trace is enabled for all interfaces.
bool gr_trace_all_enabled(void);

int eth_type_format(char *buf, size_t len, rte_be16_t type);

int trace_arp_format(char *buf, size_t len, const struct rte_arp_hdr *, uint16_t data_len);

#endif
