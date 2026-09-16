// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Robin Jarry

#include "pktbuild.h"

#include <gr_tgen.h>

#include <rte_byteorder.h>
#include <rte_common.h>
#include <rte_ether.h>
#include <rte_icmp.h>
#include <rte_ip.h>
#include <rte_ip6.h>
#include <rte_tcp.h>
#include <rte_udp.h>
#include <rte_vxlan.h>

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LAYERS 16
// minimum ethernet frame excluding the 4 byte CRC the hardware appends
#define ETH_MIN_FRAME_LEN 60

enum ltype {
	L_ETHER,
	L_DOT1Q,
	L_IP,
	L_IP6,
	L_UDP,
	L_TCP,
	L_ICMP,
	L_VXLAN,
	L_SRH,
	L_RAW,
};

// IPv6 Segment Routing Header (RFC 8754), followed by the segment list.
struct srh {
	uint8_t next_header;
	uint8_t hdr_ext_len;
	uint8_t routing_type;
	uint8_t segments_left;
	uint8_t last_entry;
	uint8_t flags;
	rte_be16_t tag;
};

struct layer {
	enum ltype type;
	size_t off;
};

#define FAIL(...)                                                                                  \
	do {                                                                                       \
		if (err != NULL)                                                                   \
			snprintf(err, errlen, __VA_ARGS__);                                        \
		return -EINVAL;                                                                    \
	} while (0)

// value parsers, return 0 on success

static int pv_u64(const char *s, uint64_t max, uint64_t *out) {
	char *end;
	errno = 0;
	unsigned long long v = strtoull(s, &end, 0);
	if (end == s || *end != '\0' || errno != 0 || v > max)
		return -1;
	*out = v;
	return 0;
}

static int pv_mac(const char *s, struct rte_ether_addr *a) {
	return rte_ether_unformat_addr(s, a);
}

static int pv_ip4(const char *s, rte_be32_t *a) {
	struct in_addr in;
	if (inet_pton(AF_INET, s, &in) != 1)
		return -1;
	*a = in.s_addr;
	return 0;
}

static int pv_ip6(const char *s, void *a) {
	return inet_pton(AF_INET6, s, a) == 1 ? 0 : -1;
}

static int pv_hex(const char *s, uint8_t *out, size_t max, size_t *n) {
	size_t len = 0;
	while (*s != '\0') {
		if (*s == ' ') {
			s++;
			continue;
		}
		if (!isxdigit((unsigned char)s[0]) || !isxdigit((unsigned char)s[1]))
			return -1;
		if (len >= max)
			return -1;
		char byte[3] = {s[0], s[1], 0};
		out[len++] = (uint8_t)strtoul(byte, NULL, 16);
		s += 2;
	}
	*n = len;
	return 0;
}

// iterate "k=v,k=v" fields over a mutable string
static bool next_field(char **s, char **key, char **val) {
	char *tok;
	while ((tok = strsep(s, ",")) != NULL) {
		while (*tok == ' ')
			tok++;
		if (*tok == '\0')
			continue;
		char *eq = strchr(tok, '=');
		if (eq != NULL) {
			*eq = '\0';
			*val = eq + 1;
		} else {
			*val = (char *)"";
		}
		*key = tok;
		return true;
	}
	return false;
}

static void *grow(uint8_t *buf, size_t *len, size_t sz) {
	void *p = buf + *len;
	memset(p, 0, sz);
	*len += sz;
	return p;
}

static int emit_ether(char *args, uint8_t *buf, size_t *len, char *err, size_t errlen) {
	struct rte_ether_hdr *e = grow(buf, len, sizeof(*e));
	char *k, *v;
	uint64_t n;
	while (next_field(&args, &k, &v)) {
		if (strcmp(k, "dst") == 0) {
			if (pv_mac(v, &e->dst_addr))
				FAIL("Ether: bad dst mac '%s'", v);
		} else if (strcmp(k, "src") == 0) {
			if (pv_mac(v, &e->src_addr))
				FAIL("Ether: bad src mac '%s'", v);
		} else if (strcmp(k, "type") == 0) {
			if (pv_u64(v, UINT16_MAX, &n))
				FAIL("Ether: bad type '%s'", v);
			e->ether_type = rte_cpu_to_be_16(n);
		} else {
			FAIL("Ether: unknown field '%s' (available: dst, src, type)", k);
		}
	}
	return 0;
}

static int emit_dot1q(char *args, uint8_t *buf, size_t *len, char *err, size_t errlen) {
	struct rte_vlan_hdr *q = grow(buf, len, sizeof(*q));
	uint16_t vid = 0, pcp = 0;
	char *k, *v;
	uint64_t n;
	while (next_field(&args, &k, &v)) {
		if (strcmp(k, "vlan") == 0) {
			if (pv_u64(v, 0xfff, &n))
				FAIL("Dot1Q: bad vlan '%s'", v);
			vid = n;
		} else if (strcmp(k, "prio") == 0) {
			if (pv_u64(v, 7, &n))
				FAIL("Dot1Q: bad prio '%s'", v);
			pcp = n;
		} else if (strcmp(k, "type") == 0) {
			if (pv_u64(v, UINT16_MAX, &n))
				FAIL("Dot1Q: bad type '%s'", v);
			q->eth_proto = rte_cpu_to_be_16(n);
		} else {
			FAIL("Dot1Q: unknown field '%s' (available: vlan, prio, type)", k);
		}
	}
	q->vlan_tci = rte_cpu_to_be_16((pcp << 13) | vid);
	return 0;
}

static int emit_ip(char *args, uint8_t *buf, size_t *len, char *err, size_t errlen) {
	struct rte_ipv4_hdr *ip = grow(buf, len, sizeof(*ip));
	char *k, *v;
	uint64_t n;
	ip->version_ihl = 0x45;
	ip->time_to_live = 64;
	while (next_field(&args, &k, &v)) {
		if (strcmp(k, "src") == 0) {
			rte_be32_t a;
			if (pv_ip4(v, &a))
				FAIL("IP: bad src '%s'", v);
			ip->src_addr = a;
		} else if (strcmp(k, "dst") == 0) {
			rte_be32_t a;
			if (pv_ip4(v, &a))
				FAIL("IP: bad dst '%s'", v);
			ip->dst_addr = a;
		} else if (strcmp(k, "ttl") == 0) {
			if (pv_u64(v, UINT8_MAX, &n))
				FAIL("IP: bad ttl '%s'", v);
			ip->time_to_live = n;
		} else if (strcmp(k, "tos") == 0) {
			if (pv_u64(v, UINT8_MAX, &n))
				FAIL("IP: bad tos '%s'", v);
			ip->type_of_service = n;
		} else if (strcmp(k, "id") == 0) {
			if (pv_u64(v, UINT16_MAX, &n))
				FAIL("IP: bad id '%s'", v);
			ip->packet_id = rte_cpu_to_be_16(n);
		} else if (strcmp(k, "proto") == 0) {
			if (pv_u64(v, UINT8_MAX, &n))
				FAIL("IP: bad proto '%s'", v);
			ip->next_proto_id = n;
		} else {
			FAIL("IP: unknown field '%s' (available: src, dst, ttl, tos, id, proto)",
			     k);
		}
	}
	return 0;
}

static int emit_ip6(char *args, uint8_t *buf, size_t *len, char *err, size_t errlen) {
	struct rte_ipv6_hdr *ip = grow(buf, len, sizeof(*ip));
	char *k, *v;
	uint64_t n;
	ip->vtc_flow = rte_cpu_to_be_32(6u << 28);
	ip->hop_limits = 64;
	while (next_field(&args, &k, &v)) {
		if (strcmp(k, "src") == 0) {
			if (pv_ip6(v, &ip->src_addr))
				FAIL("IPv6: bad src '%s'", v);
		} else if (strcmp(k, "dst") == 0) {
			if (pv_ip6(v, &ip->dst_addr))
				FAIL("IPv6: bad dst '%s'", v);
		} else if (strcmp(k, "hlim") == 0) {
			if (pv_u64(v, UINT8_MAX, &n))
				FAIL("IPv6: bad hlim '%s'", v);
			ip->hop_limits = n;
		} else if (strcmp(k, "nh") == 0) {
			if (pv_u64(v, UINT8_MAX, &n))
				FAIL("IPv6: bad nh '%s'", v);
			ip->proto = n;
		} else {
			FAIL("IPv6: unknown field '%s' (available: src, dst, hlim, nh)", k);
		}
	}
	return 0;
}

static int emit_udp(char *args, uint8_t *buf, size_t *len, char *err, size_t errlen) {
	struct rte_udp_hdr *udp = grow(buf, len, sizeof(*udp));
	char *k, *v;
	uint64_t n;
	while (next_field(&args, &k, &v)) {
		if (strcmp(k, "sport") == 0) {
			if (pv_u64(v, UINT16_MAX, &n))
				FAIL("UDP: bad sport '%s'", v);
			udp->src_port = rte_cpu_to_be_16(n);
		} else if (strcmp(k, "dport") == 0) {
			if (pv_u64(v, UINT16_MAX, &n))
				FAIL("UDP: bad dport '%s'", v);
			udp->dst_port = rte_cpu_to_be_16(n);
		} else {
			FAIL("UDP: unknown field '%s' (available: sport, dport)", k);
		}
	}
	return 0;
}

static int emit_tcp(char *args, uint8_t *buf, size_t *len, char *err, size_t errlen) {
	struct rte_tcp_hdr *tcp = grow(buf, len, sizeof(*tcp));
	char *k, *v;
	uint64_t n;
	tcp->data_off = 0x50; // 5 words, no options
	tcp->tcp_flags = 0x02; // SYN
	while (next_field(&args, &k, &v)) {
		if (strcmp(k, "sport") == 0) {
			if (pv_u64(v, UINT16_MAX, &n))
				FAIL("TCP: bad sport '%s'", v);
			tcp->src_port = rte_cpu_to_be_16(n);
		} else if (strcmp(k, "dport") == 0) {
			if (pv_u64(v, UINT16_MAX, &n))
				FAIL("TCP: bad dport '%s'", v);
			tcp->dst_port = rte_cpu_to_be_16(n);
		} else if (strcmp(k, "seq") == 0) {
			if (pv_u64(v, UINT32_MAX, &n))
				FAIL("TCP: bad seq '%s'", v);
			tcp->sent_seq = rte_cpu_to_be_32(n);
		} else if (strcmp(k, "flags") == 0) {
			if (pv_u64(v, UINT8_MAX, &n))
				FAIL("TCP: bad flags '%s'", v);
			tcp->tcp_flags = n;
		} else {
			FAIL("TCP: unknown field '%s' (available: sport, dport, seq, flags)", k);
		}
	}
	return 0;
}

static int emit_icmp(char *args, uint8_t *buf, size_t *len, char *err, size_t errlen) {
	struct rte_icmp_hdr *icmp = grow(buf, len, sizeof(*icmp));
	char *k, *v;
	uint64_t n;
	icmp->icmp_type = 8; // echo request
	while (next_field(&args, &k, &v)) {
		if (strcmp(k, "type") == 0) {
			if (pv_u64(v, UINT8_MAX, &n))
				FAIL("ICMP: bad type '%s'", v);
			icmp->icmp_type = n;
		} else if (strcmp(k, "code") == 0) {
			if (pv_u64(v, UINT8_MAX, &n))
				FAIL("ICMP: bad code '%s'", v);
			icmp->icmp_code = n;
		} else if (strcmp(k, "id") == 0) {
			if (pv_u64(v, UINT16_MAX, &n))
				FAIL("ICMP: bad id '%s'", v);
			icmp->icmp_ident = rte_cpu_to_be_16(n);
		} else if (strcmp(k, "seq") == 0) {
			if (pv_u64(v, UINT16_MAX, &n))
				FAIL("ICMP: bad seq '%s'", v);
			icmp->icmp_seq_nb = rte_cpu_to_be_16(n);
		} else {
			FAIL("ICMP: unknown field '%s' (available: type, code, id, seq)", k);
		}
	}
	return 0;
}

static int emit_vxlan(char *args, uint8_t *buf, size_t *len, char *err, size_t errlen) {
	struct rte_vxlan_hdr *vx = grow(buf, len, sizeof(*vx));
	char *k, *v;
	uint64_t n;
	vx->vx_flags = RTE_BE32(0x08000000); // I flag: valid VNI
	while (next_field(&args, &k, &v)) {
		if (strcmp(k, "vni") == 0) {
			if (pv_u64(v, 0xffffff, &n))
				FAIL("VXLAN: bad vni '%s'", v);
			vx->vx_vni = rte_cpu_to_be_32((uint32_t)n << 8);
		} else {
			FAIL("VXLAN: unknown field '%s' (available: vni)", k);
		}
	}
	return 0;
}

static int emit_srh(char *args, uint8_t *buf, size_t *len, char *err, size_t errlen) {
	struct srh *srh = grow(buf, len, sizeof(*srh));
	unsigned nseg = 0;
	bool sl_set = false;
	uint64_t sl = 0;
	char *k, *v;
	uint64_t n;

	srh->routing_type = 4; // SRv6

	while (next_field(&args, &k, &v)) {
		if (strcmp(k, "segments") == 0) {
			// ';' separated to avoid clashing with the ',' field separator
			char *seg, *save = v;
			while ((seg = strsep(&save, ";")) != NULL) {
				if (*seg == '\0')
					continue;
				void *dst = grow(buf, len, 16);
				if (pv_ip6(seg, dst))
					FAIL("SRH: bad segment '%s'", seg);
				nseg++;
			}
		} else if (strcmp(k, "sl") == 0) {
			if (pv_u64(v, UINT8_MAX, &n))
				FAIL("SRH: bad sl '%s'", v);
			sl = n;
			sl_set = true;
		} else {
			FAIL("SRH: unknown field '%s' (available: segments, sl)", k);
		}
	}

	if (nseg == 0)
		FAIL("SRH: at least one segment is required");

	srh->hdr_ext_len = 2 * nseg; // (8 + 16*nseg) / 8 - 1
	srh->last_entry = nseg - 1;
	srh->segments_left = sl_set ? sl : nseg - 1;
	return 0;
}

static int emit_raw(char *args, uint8_t *buf, size_t *len, char *err, size_t errlen) {
	char *k, *v;
	while (next_field(&args, &k, &v)) {
		if (strcmp(k, "load") == 0) {
			size_t n;
			if (pv_hex(v, buf + *len, GR_TGEN_MAX_PKT_LEN - *len, &n))
				FAIL("Raw: bad load '%s'", v);
			*len += n;
		} else {
			FAIL("Raw: unknown field '%s' (available: load)", k);
		}
	}
	return 0;
}

static rte_be16_t ethertype(enum ltype t) {
	switch (t) {
	case L_IP:
		return RTE_BE16(RTE_ETHER_TYPE_IPV4);
	case L_IP6:
		return RTE_BE16(RTE_ETHER_TYPE_IPV6);
	case L_DOT1Q:
		return RTE_BE16(RTE_ETHER_TYPE_VLAN);
	default:
		return 0;
	}
}

static uint8_t ipproto(enum ltype t) {
	switch (t) {
	case L_UDP:
		return IPPROTO_UDP;
	case L_TCP:
		return IPPROTO_TCP;
	case L_ICMP:
		return IPPROTO_ICMP;
	case L_SRH:
		return IPPROTO_ROUTING;
	case L_IP:
		return IPPROTO_IPIP;
	case L_IP6:
		return IPPROTO_IPV6;
	default:
		return 0;
	}
}

// random ephemeral source port (32768 - 65535)
static rte_be16_t rand_sport(void) {
	return rte_cpu_to_be_16(32768 + (rand() & 0x7fff));
}

// fill in ethertypes, next-protocols, addresses, ports, lengths and checksums
// left unset
static void
fixup(uint8_t *buf, size_t len, struct layer *layers, int nl, const struct tgen_pkt_defaults *def) {
	// chaining and lengths, outer to inner
	for (int i = 0; i < nl; i++) {
		enum ltype next = i + 1 < nl ? layers[i + 1].type : L_RAW;
		void *h = buf + layers[i].off;
		switch (layers[i].type) {
		case L_ETHER: {
			struct rte_ether_hdr *e = h;
			if (e->ether_type == 0)
				e->ether_type = ethertype(next);
			if (def != NULL) {
				if (rte_is_zero_ether_addr(&e->src_addr)
				    && !rte_is_zero_ether_addr(&def->src_mac))
					e->src_addr = def->src_mac;
				if (rte_is_zero_ether_addr(&e->dst_addr)
				    && !rte_is_zero_ether_addr(&def->dst_mac))
					e->dst_addr = def->dst_mac;
			}
			break;
		}
		case L_DOT1Q: {
			struct rte_vlan_hdr *q = h;
			if (q->eth_proto == 0)
				q->eth_proto = ethertype(next);
			break;
		}
		case L_IP: {
			struct rte_ipv4_hdr *ip = h;
			if (ip->next_proto_id == 0)
				ip->next_proto_id = ipproto(next);
			if (def != NULL) {
				if (ip->src_addr == 0)
					ip->src_addr = def->src_ip4;
				if (ip->dst_addr == 0)
					ip->dst_addr = def->dst_ip4;
			}
			ip->total_length = rte_cpu_to_be_16(len - layers[i].off);
			break;
		}
		case L_IP6: {
			struct rte_ipv6_hdr *ip = h;
			if (ip->proto == 0)
				ip->proto = ipproto(next);
			if (def != NULL) {
				if (rte_ipv6_addr_is_unspec(&ip->src_addr))
					ip->src_addr = def->src_ip6;
				if (rte_ipv6_addr_is_unspec(&ip->dst_addr))
					ip->dst_addr = def->dst_ip6;
			}
			ip->payload_len = rte_cpu_to_be_16(len - layers[i].off - sizeof(*ip));
			break;
		}
		case L_UDP: {
			struct rte_udp_hdr *udp = h;
			if (udp->dst_port == 0)
				udp->dst_port = next == L_VXLAN ?
					RTE_BE16(RTE_VXLAN_DEFAULT_PORT) :
					RTE_BE16(53);
			if (udp->src_port == 0)
				udp->src_port = rand_sport();
			udp->dgram_len = rte_cpu_to_be_16(len - layers[i].off);
			break;
		}
		case L_TCP: {
			struct rte_tcp_hdr *tcp = h;
			if (tcp->dst_port == 0)
				tcp->dst_port = RTE_BE16(80);
			if (tcp->src_port == 0)
				tcp->src_port = rand_sport();
			break;
		}
		case L_SRH: {
			struct srh *srh = h;
			if (srh->next_header == 0)
				srh->next_header = ipproto(next);
			break;
		}
		default:
			break;
		}
	}

	// checksums, inner to outer so an outer L4 covers finalized inner bytes
	for (int i = nl - 1; i >= 0; i--) {
		void *h = buf + layers[i].off;
		// nearest enclosing IP header
		void *ip = NULL;
		enum ltype ipt = L_RAW;
		for (int j = i - 1; j >= 0; j--) {
			if (layers[j].type == L_IP || layers[j].type == L_IP6) {
				ip = buf + layers[j].off;
				ipt = layers[j].type;
				break;
			}
		}
		switch (layers[i].type) {
		case L_UDP: {
			struct rte_udp_hdr *udp = h;
			udp->dgram_cksum = 0;
			if (ipt == L_IP)
				udp->dgram_cksum = rte_ipv4_udptcp_cksum(ip, udp);
			else if (ipt == L_IP6)
				udp->dgram_cksum = rte_ipv6_udptcp_cksum(ip, udp);
			if (udp->dgram_cksum == 0)
				udp->dgram_cksum = 0xffff;
			break;
		}
		case L_TCP: {
			struct rte_tcp_hdr *tcp = h;
			tcp->cksum = 0;
			if (ipt == L_IP)
				tcp->cksum = rte_ipv4_udptcp_cksum(ip, tcp);
			else if (ipt == L_IP6)
				tcp->cksum = rte_ipv6_udptcp_cksum(ip, tcp);
			break;
		}
		case L_ICMP: {
			struct rte_icmp_hdr *icmp = h;
			icmp->icmp_cksum = 0;
			icmp->icmp_cksum = (uint16_t)~rte_raw_cksum(icmp, len - layers[i].off);
			break;
		}
		case L_IP: {
			struct rte_ipv4_hdr *ip4 = h;
			ip4->hdr_checksum = 0;
			ip4->hdr_checksum = rte_ipv4_cksum(ip4);
			break;
		}
		default:
			break;
		}
	}
}

int tgen_pkt_build(
	const char *text,
	const struct tgen_pkt_defaults *def,
	uint8_t *buf_out,
	size_t bufsz,
	uint16_t *len_out,
	char *err,
	size_t errlen
) {
	static const struct {
		const char *name;
		enum ltype type;
		int (*emit)(char *, uint8_t *, size_t *, char *, size_t);
	} defs[] = {
		{"Ether", L_ETHER, emit_ether},
		{"Dot1Q", L_DOT1Q, emit_dot1q},
		{"IP", L_IP, emit_ip},
		{"IPv6", L_IP6, emit_ip6},
		{"UDP", L_UDP, emit_udp},
		{"TCP", L_TCP, emit_tcp},
		{"ICMP", L_ICMP, emit_icmp},
		{"VXLAN", L_VXLAN, emit_vxlan},
		{"SRH", L_SRH, emit_srh},
		{"Raw", L_RAW, emit_raw},
	};
	struct layer layers[MAX_LAYERS];
	uint8_t buf[GR_TGEN_MAX_PKT_LEN];
	char *copy, *save, *tok;
	size_t len = 0;
	int nl = 0;
	int ret;

	if ((copy = strdup(text)) == NULL)
		return -ENOMEM;

	for (tok = strtok_r(copy, "/", &save); tok != NULL; tok = strtok_r(NULL, "/", &save)) {
		while (*tok == ' ')
			tok++;
		// split "Name(args)"
		char *open = strchr(tok, '(');
		char *args = (char *)"";
		if (open != NULL) {
			char *close = strrchr(open, ')');
			if (close == NULL) {
				if (err != NULL)
					snprintf(err, errlen, "missing ')' in '%s'", tok);
				ret = -EINVAL;
				goto out;
			}
			*open = '\0';
			*close = '\0';
			args = open + 1;
		}
		// trim trailing spaces off the name
		char *end = tok + strlen(tok);
		while (end > tok && end[-1] == ' ')
			*--end = '\0';

		if (nl >= MAX_LAYERS) {
			if (err != NULL)
				snprintf(err, errlen, "too many layers");
			ret = -EINVAL;
			goto out;
		}

		bool found = false;
		for (unsigned d = 0; d < RTE_DIM(defs); d++) {
			if (strcmp(tok, defs[d].name) != 0)
				continue;
			layers[nl].type = defs[d].type;
			layers[nl].off = len;
			if ((ret = defs[d].emit(args, buf, &len, err, errlen)) < 0)
				goto out;
			nl++;
			found = true;
			break;
		}
		if (!found) {
			if (err != NULL) {
				char avail[128];
				size_t o = 0;
				for (unsigned d = 0; d < RTE_DIM(defs); d++)
					o += snprintf(
						avail + o,
						o < sizeof(avail) ? sizeof(avail) - o : 0,
						"%s%s",
						d != 0 ? ", " : "",
						defs[d].name
					);
				snprintf(
					err,
					errlen,
					"unknown layer '%s' (available: %s)",
					tok,
					avail
				);
			}
			ret = -EINVAL;
			goto out;
		}
	}

	if (nl == 0 || len < sizeof(struct rte_ether_hdr)) {
		if (err != NULL)
			snprintf(err, errlen, "empty or truncated packet");
		ret = -EINVAL;
		goto out;
	}

	// pad as payload up to the minimum ethernet frame size so the packet stays
	// well formed (lengths and checksums cover the padding); the hardware
	// appends the 4 byte CRC afterwards, making it 64 bytes on the wire
	if (len < ETH_MIN_FRAME_LEN) {
		memset(buf + len, 0, ETH_MIN_FRAME_LEN - len);
		len = ETH_MIN_FRAME_LEN;
	}

	fixup(buf, len, layers, nl, def);

	if (len > bufsz) {
		if (err != NULL)
			snprintf(err, errlen, "packet too large (%zu bytes)", len);
		ret = -ENOBUFS;
		goto out;
	}
	memcpy(buf_out, buf, len);
	*len_out = len;
	ret = 0;
out:
	free(copy);
	return ret;
}
