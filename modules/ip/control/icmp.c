// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2024 Christophe Fontaine

#include <gr_api.h>
#include <gr_control.h>
#include <gr_control_input.h>
#include <gr_control_output.h>
#include <gr_ip4.h>
#include <gr_ip4_control.h>
#include <gr_ip4_datapath.h>

#include <rte_icmp.h>
#include <rte_ip.h>

#include <stdatomic.h>
#include <time.h>

static struct rte_mbuf *last_reply = NULL;

static struct rte_mbuf *ip4_icmp_get_reply() {
	struct rte_mbuf *null_mbuf = NULL;
	return atomic_exchange(&last_reply, null_mbuf);
}

static void icmp_response(struct rte_mbuf *m) {
	struct rte_mbuf *previous = atomic_exchange(&last_reply, m);
	if (previous) {
		rte_pktmbuf_free(previous);
		previous = NULL;
	}
}

static void dissect_icmp_response(struct rte_mbuf *icmp_reply, struct gr_ip4_icmp_response *resp) {
	struct rte_ipv4_hdr *inner_ip;
	struct rte_icmp_hdr *icmp;
	struct rte_ipv4_hdr *ip;
	clock_t delay, *sent;

	if (icmp_reply) {
		ip = rte_pktmbuf_mtod_offset(
			icmp_reply, struct rte_ipv4_hdr *, -sizeof(struct rte_ipv4_hdr)
		);
		icmp = rte_pktmbuf_mtod(icmp_reply, struct rte_icmp_hdr *);
		resp->answered = true;
		resp->type = icmp->icmp_type;
		resp->code = icmp->icmp_code;
		resp->sequence_number = ntohs(icmp->icmp_seq_nb);
		resp->ttl = ip->time_to_live;
		if (icmp->icmp_type == GR_IP_ICMP_TTL_EXCEEDED) {
			uint16_t sz_copy = rte_be_to_cpu_16(ip->total_length) - sizeof(*ip);
			sz_copy = (sz_copy > 64 ? 64 : sz_copy);
			inner_ip = rte_pktmbuf_mtod_offset(icmp_reply, void *, sizeof(*icmp));
			memcpy(resp->data, inner_ip, sz_copy);
			sent = rte_pktmbuf_mtod_offset(
				icmp_reply, void *, sizeof(*icmp) + sizeof(*ip) + sizeof(*icmp)
			);
		} else {
			sent = rte_pktmbuf_mtod_offset(
				icmp_reply, clock_t *, sizeof(struct rte_icmp_hdr)
			);
		}

		delay = control_output_mbuf_data(icmp_reply)->timestamp - *sent;
		resp->response_time = delay;
	}
}

static struct api_out icmp_echo_request(const void *request, void **) {
	const struct gr_ip4_icmp_request *req = request;
	struct nexthop *nh = NULL;
	int ret;

	rte_pktmbuf_free(ip4_icmp_get_reply());

	nh = ip4_route_lookup(req->vrf, req->addr.ip);
	if (!nh) {
		return api_out(-1, 0);
	}
	ret = ip4_icmp_output_request(req->vrf, req->addr.ip, nh, req->sequence_number, req->ttl);
	if (ret) {
		return api_out(-ret, 0);
	}

	return api_out(0, 0);
}

static struct api_out icmp_echo_reply(const void *, void **response) {
	struct gr_ip4_icmp_response *resp = NULL;
	struct rte_mbuf *icmp_reply = NULL;

	icmp_reply = ip4_icmp_get_reply();
	if (icmp_reply == NULL)
		return api_out(0, 0);

	resp = calloc(sizeof(*resp), 1);

	dissect_icmp_response(icmp_reply, resp);

	rte_pktmbuf_free(icmp_reply);
	*response = resp;
	return api_out(0, sizeof(*resp));
}

static struct gr_api_handler ip4_icmp_request_handler = {
	.name = "icmp_echo",
	.request_type = GR_IP4_ICMP_REQUEST,
	.callback = icmp_echo_request,
};

static struct gr_api_handler ip4_icmp_get_reply_handler = {
	.name = "icmp_echo_reply",
	.request_type = GR_IP4_ICMP_GET_REPLY,
	.callback = icmp_echo_reply,
};

RTE_INIT(icmp_echo_reply_constructor) {
	gr_register_api_handler(&ip4_icmp_request_handler);
	gr_register_api_handler(&ip4_icmp_get_reply_handler);
	icmp_register_callback(GR_IP_ICMP_DEST_UNREACHABLE, icmp_response);
	icmp_register_callback(GR_IP_ICMP_TTL_EXCEEDED, icmp_response);
	icmp_register_callback(RTE_IP_ICMP_ECHO_REPLY, icmp_response);
}
