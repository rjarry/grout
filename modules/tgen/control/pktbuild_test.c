// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Robin Jarry

#include "_cmocka.h"
#include "pktbuild.h"

#include <gr_tgen.h>

#include <rte_common.h>
#include <rte_eal.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_udp.h>
#include <rte_vxlan.h>

#include <errno.h>
#include <string.h>

static void ether_ip_udp(void **) {
	uint8_t buf[GR_TGEN_MAX_PKT_LEN];
	char err[128] = {0};
	uint16_t len = 0;

	int ret = tgen_pkt_build(
		"Ether()/IP(src=1.1.1.1,dst=2.2.2.2)/UDP(sport=1234,dport=53)/Raw(load=deadbeef)",
		NULL,
		buf,
		sizeof(buf),
		&len,
		err,
		sizeof(err)
	);
	assert_int_equal(ret, 0);
	// 14 + 20 + 8 + 4 = 46 bytes, padded as payload to the 60 byte minimum frame
	assert_int_equal(len, 60);

	const struct rte_ether_hdr *eth = (const void *)buf;
	assert_int_equal(eth->ether_type, RTE_BE16(RTE_ETHER_TYPE_IPV4));

	const struct rte_ipv4_hdr *ip = (const void *)(buf + 14);
	assert_int_equal(ip->version_ihl, 0x45);
	assert_int_equal(ip->next_proto_id, IPPROTO_UDP);
	assert_int_equal(ip->time_to_live, 64);
	// lengths include the padding, which is part of the payload
	assert_int_equal(rte_be_to_cpu_16(ip->total_length), 60 - 14);
	// a correct header checksum makes the recomputed value zero
	assert_int_equal(rte_ipv4_cksum(ip), 0);

	const struct rte_udp_hdr *udp = (const void *)(buf + 34);
	assert_int_equal(rte_be_to_cpu_16(udp->src_port), 1234);
	assert_int_equal(rte_be_to_cpu_16(udp->dst_port), 53);
	assert_int_equal(rte_be_to_cpu_16(udp->dgram_len), 60 - 34);

	// recompute the L4 checksum in place (it spans the payload) and compare
	struct rte_udp_hdr *u = (void *)(buf + 34);
	uint16_t stored = u->dgram_cksum;
	u->dgram_cksum = 0;
	uint16_t expected = rte_ipv4_udptcp_cksum(ip, u);
	if (expected == 0)
		expected = 0xffff;
	u->dgram_cksum = stored;
	assert_int_equal(stored, expected);

	assert_memory_equal(buf + 42, "\xde\xad\xbe\xef", 4);
}

static void defaults_and_vlan(void **) {
	uint8_t buf[GR_TGEN_MAX_PKT_LEN];
	char err[128] = {0};
	uint16_t len = 0;

	int ret = tgen_pkt_build(
		"Ether()/Dot1Q(vlan=42)/IPv6(dst=fd00::1)/UDP(dport=4789)",
		NULL,
		buf,
		sizeof(buf),
		&len,
		err,
		sizeof(err)
	);
	assert_int_equal(ret, 0);

	const struct rte_ether_hdr *eth = (const void *)buf;
	assert_int_equal(eth->ether_type, RTE_BE16(RTE_ETHER_TYPE_VLAN));

	const struct rte_vlan_hdr *q = (const void *)(buf + 14);
	assert_int_equal(rte_be_to_cpu_16(q->vlan_tci) & 0xfff, 42);
	assert_int_equal(q->eth_proto, RTE_BE16(RTE_ETHER_TYPE_IPV6));

	const struct rte_ipv6_hdr *ip6 = (const void *)(buf + 18);
	assert_int_equal(ip6->proto, IPPROTO_UDP);
	assert_int_equal(ip6->hop_limits, 64);
	assert_int_equal(rte_be_to_cpu_16(ip6->payload_len), 8);
}

static void vxlan(void **) {
	uint8_t buf[GR_TGEN_MAX_PKT_LEN];
	char err[128] = {0};
	uint16_t len = 0;

	int ret = tgen_pkt_build(
		"Ether()/IP()/UDP()/VXLAN(vni=100)/Ether()/IP()/UDP()",
		NULL,
		buf,
		sizeof(buf),
		&len,
		err,
		sizeof(err)
	);
	assert_int_equal(ret, 0);

	const struct rte_udp_hdr *outer = (const void *)(buf + 34);
	assert_int_equal(rte_be_to_cpu_16(outer->dst_port), 4789);

	const struct rte_vxlan_hdr *vx = (const void *)(buf + 42);
	assert_int_equal(rte_be_to_cpu_32(vx->vx_vni) >> 8, 100);

	const struct rte_ether_hdr *inner = (const void *)(buf + 50);
	assert_int_equal(inner->ether_type, RTE_BE16(RTE_ETHER_TYPE_IPV4));
}

static void srh(void **) {
	uint8_t buf[GR_TGEN_MAX_PKT_LEN];
	char err[128] = {0};
	uint16_t len = 0;

	int ret = tgen_pkt_build(
		"Ether()/IPv6()/SRH(segments=fd00::1;fd00::2)/IPv6()/UDP()",
		NULL,
		buf,
		sizeof(buf),
		&len,
		err,
		sizeof(err)
	);
	assert_int_equal(ret, 0);

	const struct rte_ipv6_hdr *ip6 = (const void *)(buf + 14);
	assert_int_equal(ip6->proto, 43); // IPPROTO_ROUTING

	const uint8_t *s = buf + 54; // after ethernet + ipv6
	assert_int_equal(s[0], 41); // next_header = IPv6 (encap)
	assert_int_equal(s[1], 4); // hdr_ext_len for 2 segments
	assert_int_equal(s[2], 4); // routing_type = SRv6
	assert_int_equal(s[3], 1); // segments_left = nseg - 1
	assert_int_equal(s[4], 1); // last_entry = nseg - 1
}

static void padding(void **) {
	uint8_t buf[GR_TGEN_MAX_PKT_LEN];
	char err[128] = {0};
	uint16_t len = 0;

	// 14 + 20 + 8 = 42 bytes, must be zero-padded to 60
	int ret = tgen_pkt_build(
		"Ether()/IP()/UDP()", NULL, buf, sizeof(buf), &len, err, sizeof(err)
	);
	assert_int_equal(ret, 0);
	assert_int_equal(len, 60);
	for (unsigned i = 42; i < 60; i++)
		assert_int_equal(buf[i], 0);
	// the padding is payload, so the IP/UDP lengths include it
	const struct rte_ipv4_hdr *ip = (const void *)(buf + 14);
	assert_int_equal(rte_be_to_cpu_16(ip->total_length), 60 - 14);
	const struct rte_udp_hdr *udp = (const void *)(buf + 34);
	assert_int_equal(rte_be_to_cpu_16(udp->dgram_len), 60 - 34);
}

static void defaults(void **) {
	struct tgen_pkt_defaults def = {
		.src_mac = {{0x02, 0, 0, 0, 0, 0x01}},
		.dst_mac = {{0x02, 0, 0, 0, 0, 0x02}},
		.src_ip4 = RTE_BE32(0x0a000001), // 10.0.0.1
		.dst_ip4 = RTE_BE32(0x0a000102), // 10.0.1.2
	};
	uint8_t buf[GR_TGEN_MAX_PKT_LEN];
	char err[128] = {0};
	uint16_t len = 0;

	// everything unset: filled from def, ports defaulted
	int ret = tgen_pkt_build(
		"Ether()/IP()/UDP()", &def, buf, sizeof(buf), &len, err, sizeof(err)
	);
	assert_int_equal(ret, 0);

	const struct rte_ether_hdr *eth = (const void *)buf;
	assert_memory_equal(&eth->src_addr, &def.src_mac, 6);
	assert_memory_equal(&eth->dst_addr, &def.dst_mac, 6);

	const struct rte_ipv4_hdr *ip = (const void *)(buf + 14);
	assert_int_equal(ip->src_addr, def.src_ip4);
	assert_int_equal(ip->dst_addr, def.dst_ip4);

	const struct rte_udp_hdr *udp = (const void *)(buf + 34);
	assert_int_equal(rte_be_to_cpu_16(udp->dst_port), 53);
	assert_true(rte_be_to_cpu_16(udp->src_port) >= 32768);

	// explicit fields win over the defaults
	ret = tgen_pkt_build(
		"Ether(src=0a:0b:0c:0d:0e:0f)/IP(dst=8.8.8.8)/UDP(dport=123)",
		&def,
		buf,
		sizeof(buf),
		&len,
		err,
		sizeof(err)
	);
	assert_int_equal(ret, 0);
	const struct rte_ether_addr expect = {{0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f}};
	assert_memory_equal(&eth->src_addr, &expect, 6);
	assert_int_equal(rte_be_to_cpu_32(ip->dst_addr), 0x08080808);
	assert_int_equal(rte_be_to_cpu_16(udp->dst_port), 123);
}

static void errors(void **) {
	uint8_t buf[256];
	char err[128] = {0};
	uint16_t len = 0;

	assert_int_equal(
		tgen_pkt_build("Ether()/Foo()", NULL, buf, sizeof(buf), &len, err, sizeof(err)),
		-EINVAL
	);
	// unknown layer error lists the available layer names
	assert_non_null(strstr(err, "available:"));
	assert_non_null(strstr(err, "Ether"));
	assert_int_equal(
		tgen_pkt_build(
			"Ether()/IP(dst=nope)", NULL, buf, sizeof(buf), &len, err, sizeof(err)
		),
		-EINVAL
	);
	assert_int_equal(
		tgen_pkt_build("Ether(bogus=1)", NULL, buf, sizeof(buf), &len, err, sizeof(err)),
		-EINVAL
	);
	// unknown field error lists the available field names
	assert_non_null(strstr(err, "available:"));
	assert_non_null(strstr(err, "type"));
}

int main(void) {
	char arg0[] = "pktbuild_test";
	char arg1[] = "--no-huge";
	char arg2[] = "--in-memory";
	char arg3[] = "--lcores=0";
	char arg4[] = "--no-pci";
	char arg5[] = "--log-level=*:error";
	char *argv[] = {arg0, arg1, arg2, arg3, arg4, arg5};

	if (rte_eal_init(RTE_DIM(argv), argv) < 0)
		return 1;

	const struct CMUnitTest tests[] = {
		cmocka_unit_test(ether_ip_udp),
		cmocka_unit_test(defaults_and_vlan),
		cmocka_unit_test(vxlan),
		cmocka_unit_test(srh),
		cmocka_unit_test(padding),
		cmocka_unit_test(defaults),
		cmocka_unit_test(errors),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
