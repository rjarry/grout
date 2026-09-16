// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Robin Jarry

#pragma once

#include <gr_net_types.h>

#include <rte_ether.h>

#include <stddef.h>
#include <stdint.h>

// Defaults used to fill unset packet fields, resolved from the transmit and
// receive interfaces. A zero field means "no default" and leaves the packet
// field as parsed. Kept as plain values so the parser stays free of interface
// and IP module dependencies (and unit testable).
struct tgen_pkt_defaults {
	struct rte_ether_addr src_mac; // transmit interface MAC
	struct rte_ether_addr dst_mac; // receive interface MAC
	ip4_addr_t src_ip4;
	ip4_addr_t dst_ip4;
	struct rte_ipv6_addr src_ip6;
	struct rte_ipv6_addr dst_ip6;
};

// Forge frame bytes from a scapy-like text expression, e.g.
//   "Ether(dst=..)/IP(src=1.1.1.1,dst=2.2.2.2)/UDP(sport=1024,dport=53)/Raw(load=00ff)"
//
// Layers are chained with '/'; ethertypes, next-protocols, lengths and checksums
// are filled in automatically. Unset Ether and IP addresses are taken from def
// (may be NULL), and unset L4 ports default to a random source above 32768 and a
// well-known destination (53 for UDP, 80 for TCP).
//
// The forged frame is written into buf (of bufsz bytes) and its length into
// *len. On failure returns -errno and, if err is not NULL, writes a
// human-readable reason into it.
int tgen_pkt_build(
	const char *text,
	const struct tgen_pkt_defaults *def,
	uint8_t *buf,
	size_t bufsz,
	uint16_t *len,
	char *err,
	size_t errlen
);
