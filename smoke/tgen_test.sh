#!/bin/bash
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Robin Jarry

. $(dirname $0)/_init.sh

# The traffic generator transmits pcap flows out of a port at a target rate,
# optionally sweeping a packet field, and measures drops from hardware counters.
# The two port tap backends are bridged in the kernel so p0 and p1 loop into
# each other.

grcli trace disable all

port_add p0 mac ba:d0:ca:ca:00:01 vrf main
port_add p1 mac ba:d0:ca:ca:00:02 vrf main

ip link add br0 type bridge
ip link set x-p0 master br0
ip link set x-p1 master br0
ip link set x-p0 up
ip link set x-p1 up
ip link set br0 up

# a single flow with a swept field: verify the field varies on the wire
grcli tgen flow add tx p0 rx p1 packet 'Ether()/IP()' | grep -q "Created flow 1" || fail "flow add p0"
grcli tgen flow show | grep -qE "1\s+p0\s+p1\s+60" || fail "flow 1 not listed"

grcli tgen sweep add flow 1 offset 40 start 1024 end 4000 size 2 |
	grep -q "Created sweep 1" || fail "sweep add"
grcli tgen sweep show | grep -qE "1\s+1\s+40\s+2\s+1024\s+4000\s+1" || fail "sweep not listed"

timeout 5 tcpdump -i x-p1 -w $tmp/cap.pcap -c 100 ether src ba:d0:ca:ca:00:01 2>/dev/null &
tcpid=$!
grcli tgen start rate 5000pps
sleep 2
grcli tgen stop
wait $tcpid 2>/dev/null || true

python3 - "$tmp/cap.pcap" <<'PY' || fail "swept field did not vary"
import struct, sys
vals = set()
with open(sys.argv[1], "rb") as f:
    f.read(24)
    while True:
        rh = f.read(16)
        if len(rh) < 16:
            break
        _, _, incl, _ = struct.unpack("<IIII", rh)
        data = f.read(incl)
        if len(data) >= 42:
            vals.add(data[40:42])
sys.exit(0 if len(vals) > 1 else 1)
PY

# add the reverse flow and check the counters at a low rate
grcli tgen flow add tx p1 rx p0 packet 'Ether()/IP()' | grep -q "Created flow 2" || fail "flow add p1"

grcli tgen start rate 1000pps
sleep 2
grcli tgen status | tee $tmp/status
grcli tgen stop

grep -qE "running:\s+true" $tmp/status || fail "generator not running"
grep -qE "rate:\s+1000 pps" $tmp/status || fail "rate not reported"
tx=$(grep -oP 'tx_packets:\s*\K[0-9]+' $tmp/status)
drop=$(grep -oP 'drop_packets:\s*\K[0-9]+' $tmp/status)
[ "${tx:-0}" -gt 0 ] || fail "no packets transmitted"
# imissed is added back to the received count, so a looped frame is never a drop
[ "${drop:-1}" -eq 0 ] || fail "unexpected drops: $drop"

# the RFC2544 search must run and return a verdict
grcli tgen rfc2544 max_iterations 4 max_drop 1% duration 1 | tee $tmp/rfc
grep -qE "no-drop rate|no no-drop rate found" $tmp/rfc || fail "rfc2544 did not complete"

# cleanup
grcli tgen sweep clear
grcli tgen flow clear
test -z "$(grcli tgen flow show | tail -n +2)" || fail "flows not cleared"
grcli tgen status | grep -qE "running:\s+false" || fail "generator still running"
