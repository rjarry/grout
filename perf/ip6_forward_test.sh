#!/bin/bash
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2025 Robin Jarry

here=$(dirname $0)

. $here/_init.sh

# Build the topology on the fresh grout that perf_check starts for each worker
# count. net_pcap does not support RSS, so one ingress port is added per worker
# and its single queue is polled by a dedicated worker.
setup() {
	local workers="$1" n
	grcli interface add port blackhole devargs net_null,no-rx=1
	grcli address add fd01::1/64 iface blackhole
	grcli nexthop add l3 iface blackhole id 42 address fd02::2 mac 02:00:00:00:00:03
	grcli route add fd02::/64 via id 42
	cp ${0%.sh}.pcap $tmp/p
	pcaps=
	for n in $(seq 0 $((workers - 1))); do
		pcaps="$pcaps,rx_pcap=$tmp/p"
	done
	grcli interface add port ingress devargs net_pcap$pcaps,infinite_rx=1 rxqs $workers
	grcli affinity qmap
}

perf_check ${0%.sh} setup
