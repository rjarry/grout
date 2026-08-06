#!/bin/bash
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Robin Jarry

# This test simulates an OpenPERouter baremetal deployment:
# - Top-of-rack (ToR) switch: pure ISIS Level-1 transit
# - Bastion gateway: ISIS + SRv6 L3VPN termination (Linux stack)
# - Three PE nodes: EVPN L2VPN over VXLAN + SRv6 L3VPN (north-south)
#
# Grout acts as node-a (EVPN route reflector + SRv6 L3VPN PE).
# Nodes b and c are FRR-on-Linux EVPN clients + SRv6 L3VPN PEs.
#
# East-west L2 : host-{a,b,c} talk via EVPN/VXLAN VNI 210
# North-south L3: host-{a,b,c} reach host-ext via SRv6 L3VPN through bastion
#
#                            .----------------.
#                            |   "host-ext"   |
#                            |                |
#   ^                        |     .100.2     |
#   |                        |                |
#                            |      x-ext     |
#   |                        '--------|-------'
#                                     |
#   |                        .--------|-------.
#                            |      p-ext     |
#   |                        |                |
#                            |    VRF red     |
#   |                        |                |
#                            |   "bastion"    |
#   |                        |                |
#                            |   SRv6 L3VPN   |
#   |                        |  gw 10.0.0.20  |
#                            |                |
#   | north                  |       bas0     |
#     south                  '--------|-------'
#   |  SRv6                           |
#     L3VPN    .----------------------|----------------------.
#   |          |                     tor0                    |
#              |                                             |
#   |          |          "tor" ISIS level-1 transit         |
#              |              10.0.0.1  fc00:1::1            |
#   |          |                                             |
#              |     x-p0            tor1            tor2    |
#   |          '------|---------------|---------------|------'
#                     |               |               |
#   |          .------|------. .------|------. .------|------.
#              |      p0     | |     nb0     | |     nc0     |
#   |          |  10.0.0.2   | |  10.0.0.3   | |  10.0.0.4   |
#              |             | |             | |             |
#   |          |   (grout)   | |   "node-b"  | |   "node-c"  |
#              |  EVPN RR    | |   EVPN      | |   EVPN      |
#   |          |   SRv6      | |     SRv6    | |    SRv6     |
#              |             | |             | |             |
#   |          |  br-pe-210  | |  br-pe-210  | |  br-pe-210  |
#              |             | |             | |             |
#   |          |      p1     | |    p-host   | |    p-host   |
#              '------|------' '------|------' '------|------'
#   |                 |               |               |
#              .------|------. .------|------. .------|------.
#   |          |     x-p1    | |    x-host   | |    x-host   |
#   v          |             | |             | |             |
#              |   .110.2    | |   .110.3    | |   .110.4    |
#              |             | |             | |             |
#              |  "host-a"   | |  "host-b"   | |  "host-c"   |
#              '-------------' '-------------' '-------------'
#
#                < = = = = = = = = = = = = = = = = = = = = >
#                          east-west VXLAN L2VPN

set -e

zebra=$(PATH="$1/frr_install/sbin:$1/frr_install/bin:$PATH" command -v zebra)
frr_version=$($zebra --version | sed -En 's/zebra version //p')
min_version=$(printf '%s\n%s\n' "$frr_version" "10.6.0" | sort -V | head -n1)
if ! [ "$min_version" = "10.6.0" ]; then
	echo "$0: FRR $frr_version < 10.6.0, skipping"
	exit 125
fi

. $(dirname $0)/_init_frr.sh

# -- node-a (grout): VRF and underlay port -------------------------------------
create_vrf red
create_interface p0
set_ip_address p0 10.4.0.3/31

# -- host namespaces (no FRR) --------------------------------------------------
netns_add host-ext
netns_add host-a
netns_add host-b
netns_add host-c

# -- FRR namespaces ------------------------------------------------------------
start_frr tor 0
start_frr bastion 0
start_frr node-b 0
start_frr node-c 0

for ns in tor bastion node-b node-c; do
	ip netns exec $ns sysctl -qw net.ipv4.conf.all.forwarding=1
	ip netns exec $ns sysctl -qw net.ipv6.conf.all.forwarding=1
done
for ns in bastion node-b node-c; do
	ip netns exec $ns sysctl -qw net.ipv4.conf.all.rp_filter=0
	ip netns exec $ns sysctl -qw net.ipv4.conf.default.rp_filter=0
	ip netns exec $ns sysctl -qw net.ipv6.conf.all.seg6_enabled=1
	ip netns exec $ns sysctl -qw net.vrf.strict_mode=1
	ip -n $ns link add sr0 type dummy
	ip -n $ns link set sr0 up
done

# -- links ---------------------------------------------------------------------

# tor <-> node-a (grout tap)
move_to_netns x-p0 tor

# tor <-> bastion (veth)
ip link add tor0 type veth peer name bas0
ip link set tor0 netns tor
ip link set bas0 netns bastion
ip -n tor link set tor0 up
ip -n bastion link set bas0 up

# tor <-> node-b (veth)
ip link add tor1 type veth peer name nb0
ip link set tor1 netns tor
ip link set nb0 netns node-b
ip -n tor link set tor1 up
ip -n node-b link set nb0 up

# tor <-> node-c (veth)
ip link add tor2 type veth peer name nc0
ip link set tor2 netns tor
ip link set nc0 netns node-c
ip -n tor link set tor2 up
ip -n node-c link set nc0 up

# -- bastion: kernel VRF + host-ext link ---------------------------------------
ip -n bastion link add red type vrf table 10
ip -n bastion link set red up
ip -n bastion link add p-ext type veth peer name x-ext
ip -n bastion link set p-ext master red
ip -n bastion link set p-ext up
ip -n bastion addr add 10.100.0.1/24 dev p-ext
ip -n bastion link set x-ext netns host-ext

ip -n host-ext link set x-ext up
ip -n host-ext addr add 10.100.0.2/24 dev x-ext
ip -n host-ext route add default via 10.100.0.1

# -- node-b: kernel VRF, bridge, VXLAN, host link -----------------------------
ip -n node-b link add red type vrf table 10
ip -n node-b link set red up
ip -n node-b link add br-pe-210 type bridge
ip -n node-b link set br-pe-210 master red
ip -n node-b link set br-pe-210 up
ip -n node-b addr add 192.168.110.1/24 dev br-pe-210
ip -n node-b link add vni210 type vxlan id 210 local 10.0.0.3 dstport 4789 nolearning
ip -n node-b link set vni210 master br-pe-210
ip -n node-b link set vni210 up
ip -n node-b link add p-host type veth peer name x-host
ip -n node-b link set p-host master br-pe-210
ip -n node-b link set p-host up
ip -n node-b link set x-host netns host-b

ip -n host-b link set x-host up
ip -n host-b addr add 192.168.110.3/24 dev x-host
ip -n host-b route add default via 192.168.110.1

# -- node-c: kernel VRF, bridge, VXLAN, host link -----------------------------
ip -n node-c link add red type vrf table 10
ip -n node-c link set red up
ip -n node-c link add br-pe-210 type bridge
ip -n node-c link set br-pe-210 master red
ip -n node-c link set br-pe-210 up
ip -n node-c addr add 192.168.110.1/24 dev br-pe-210
ip -n node-c link add vni210 type vxlan id 210 local 10.0.0.4 dstport 4789 nolearning
ip -n node-c link set vni210 master br-pe-210
ip -n node-c link set vni210 up
ip -n node-c link add p-host type veth peer name x-host
ip -n node-c link set p-host master br-pe-210
ip -n node-c link set p-host up
ip -n node-c link set x-host netns host-c

ip -n host-c link set x-host up
ip -n host-c addr add 192.168.110.4/24 dev x-host
ip -n host-c route add default via 192.168.110.1

# -- ToR FRR config (ISIS transit only) ----------------------------------------
vtysh -N tor <<-EOF
configure terminal

interface lo
 ip address 10.0.0.1/32
 ipv6 address fc00:0:1::1/128
 ip router isis PE
 ipv6 router isis PE
 isis passive
exit

interface x-p0
 ip address 10.4.0.2/31
 ipv6 address fc00:400::2/127
 ip router isis PE
 ipv6 router isis PE
 isis network point-to-point
exit

interface tor0
 ip address 10.4.0.0/31
 ipv6 address fc00:400::/127
 ip router isis PE
 ipv6 router isis PE
 isis network point-to-point
exit

interface tor1
 ip address 10.4.0.4/31
 ipv6 address fc00:400::4/127
 ip router isis PE
 ipv6 router isis PE
 isis network point-to-point
exit

interface tor2
 ip address 10.4.0.6/31
 ipv6 address fc00:400::6/127
 ip router isis PE
 ipv6 router isis PE
 isis network point-to-point
exit

router isis PE
 net 49.0001.0000.0000.0001.00
 is-type level-1
 metric-style wide
exit
EOF

# -- Bastion FRR config (ISIS + SRv6 L3VPN) ------------------------------------
vtysh -N bastion <<-EOF
configure terminal

interface lo
 ip address 10.0.0.20/32
 ipv6 address fc00:0:14::1/128
 ipv6 address fd00:14::1/128
 ip router isis PE
 ipv6 router isis PE
 isis passive
exit

interface bas0
 ip address 10.4.0.1/31
 ipv6 address fc00:400::1/127
 ip router isis PE
 ipv6 router isis PE
 isis network point-to-point
exit

segment-routing
 srv6
  locators
   locator MAIN
    prefix fd00:14::/48 func-bits 16
   exit
  exit
 exit

router isis PE
 net 49.0001.0000.0000.0020.00
 is-type level-1
 metric-style wide
 segment-routing srv6
  locator MAIN
 exit
exit

router bgp 65500
 bgp router-id 10.0.0.20
 no bgp default ipv4-unicast

 neighbor PE peer-group
 neighbor PE remote-as 65500
 neighbor PE update-source fc00:0:14::1
 neighbor PE capability extended-nexthop

 neighbor fc00:0:2::1 peer-group PE
 neighbor fc00:0:3::1 peer-group PE
 neighbor fc00:0:4::1 peer-group PE

 address-family ipv4 vpn
  neighbor PE activate
 exit-address-family

 segment-routing srv6
  locator MAIN
 exit
exit

router bgp 65500 vrf red
 bgp router-id 10.0.0.20

 address-family ipv4 unicast
  rd vpn export 10.0.0.20:2
  rt vpn both 65500:2
  export vpn
  import vpn
  redistribute connected
  sid vpn per-vrf export auto
 exit-address-family
exit
EOF

# -- Node-b FRR config (ISIS + EVPN client + SRv6 L3VPN) ----------------------
vtysh -N node-b <<-EOF
configure terminal

interface lo
 ip address 10.0.0.3/32
 ipv6 address fc00:0:3::1/128
 ipv6 address fd00:3::1/128
 ip router isis PE
 ipv6 router isis PE
 isis passive
exit

interface nb0
 ip address 10.4.0.5/31
 ipv6 address fc00:400::5/127
 ip router isis PE
 ipv6 router isis PE
 isis network point-to-point
exit

segment-routing
 srv6
  locators
   locator MAIN
    prefix fd00:3::/48 func-bits 16
   exit
  exit
 exit

router isis PE
 net 49.0001.0000.0000.0003.00
 is-type level-1
 metric-style wide
 segment-routing srv6
  locator MAIN
 exit
exit

router bgp 65500
 bgp router-id 10.0.0.3
 no bgp default ipv4-unicast

 neighbor fc00:0:14::1 remote-as 65500
 neighbor fc00:0:14::1 update-source fc00:0:3::1
 neighbor fc00:0:14::1 capability extended-nexthop

 neighbor fc00:0:2::1 remote-as 65500
 neighbor fc00:0:2::1 update-source fc00:0:3::1

 address-family ipv4 vpn
  neighbor fc00:0:14::1 activate
 exit-address-family

 address-family l2vpn evpn
  neighbor fc00:0:2::1 activate
  advertise-all-vni
 exit-address-family

 segment-routing srv6
  locator MAIN
 exit
exit

router bgp 65500 vrf red
 bgp router-id 10.0.0.3

 address-family ipv4 unicast
  rd vpn export 10.0.0.3:2
  rt vpn both 65500:2
  export vpn
  import vpn
  redistribute connected
  sid vpn per-vrf export auto
 exit-address-family
exit
EOF

# -- Node-c FRR config (ISIS + EVPN client + SRv6 L3VPN) ----------------------
vtysh -N node-c <<-EOF
configure terminal

interface lo
 ip address 10.0.0.4/32
 ipv6 address fc00:0:4::1/128
 ipv6 address fd00:4::1/128
 ip router isis PE
 ipv6 router isis PE
 isis passive
exit

interface nc0
 ip address 10.4.0.7/31
 ipv6 address fc00:400::7/127
 ip router isis PE
 ipv6 router isis PE
 isis network point-to-point
exit

segment-routing
 srv6
  locators
   locator MAIN
    prefix fd00:4::/48 func-bits 16
   exit
  exit
 exit

router isis PE
 net 49.0001.0000.0000.0004.00
 is-type level-1
 metric-style wide
 segment-routing srv6
  locator MAIN
 exit
exit

router bgp 65500
 bgp router-id 10.0.0.4
 no bgp default ipv4-unicast

 neighbor fc00:0:14::1 remote-as 65500
 neighbor fc00:0:14::1 update-source fc00:0:4::1
 neighbor fc00:0:14::1 capability extended-nexthop

 neighbor fc00:0:2::1 remote-as 65500
 neighbor fc00:0:2::1 update-source fc00:0:4::1

 address-family ipv4 vpn
  neighbor fc00:0:14::1 activate
 exit-address-family

 address-family l2vpn evpn
  neighbor fc00:0:2::1 activate
  advertise-all-vni
 exit-address-family

 segment-routing srv6
  locator MAIN
 exit
exit

router bgp 65500 vrf red
 bgp router-id 10.0.0.4

 address-family ipv4 unicast
  rd vpn export 10.0.0.4:2
  rt vpn both 65500:2
  export vpn
  import vpn
  redistribute connected
  sid vpn per-vrf export auto
 exit-address-family
exit
EOF

# -- Node-a (grout) FRR config -------------------------------------------------
mark_events

vtysh <<-EOF
configure terminal

interface p0
 ip address 10.0.0.2/32
 ipv6 address fc00:400::3/127
 ipv6 address fc00:0:2::1/128
 ipv6 address fd00:2::1/128
 ip router isis PE
 ipv6 router isis PE
 isis network point-to-point
exit

segment-routing
 srv6
  locators
   locator MAIN
    prefix fd00:2::/48 func-bits 16
   exit
  exit
 exit

router isis PE
 net 49.0001.0000.0000.0002.00
 is-type level-1
 metric-style wide
 segment-routing srv6
  locator MAIN
 exit
exit

router bgp 65500
 bgp router-id 10.0.0.2
 no bgp default ipv4-unicast

 neighbor fc00:0:14::1 remote-as 65500
 neighbor fc00:0:14::1 update-source fc00:0:2::1
 neighbor fc00:0:14::1 capability extended-nexthop

 neighbor EVPN peer-group
 neighbor EVPN remote-as 65500
 neighbor EVPN update-source fc00:0:2::1

 neighbor fc00:0:3::1 peer-group EVPN
 neighbor fc00:0:4::1 peer-group EVPN

 address-family ipv4 vpn
  neighbor fc00:0:14::1 activate
 exit-address-family

 address-family l2vpn evpn
  neighbor EVPN activate
  neighbor EVPN route-reflector-client
  advertise-all-vni
 exit-address-family

 segment-routing srv6
  locator MAIN
 exit
exit

router bgp 65500 vrf red
 bgp router-id 10.0.0.2

 address-family ipv4 unicast
  rd vpn export 10.0.0.2:2
  rt vpn both 65500:2
  export vpn
  import vpn
  redistribute connected
  sid vpn per-vrf export auto
 exit-address-family
exit
EOF

# -- wait for ISIS convergence -------------------------------------------------

# ToR should see 4 ISIS neighbors (node-a, bastion, node-b, node-c)
SECONDS=0
while [ "$(vtysh -N tor -c 'show isis neighbor' | grep -c Up)" -lt 4 ]; do
	if [ "$SECONDS" -ge 60 ]; then
		vtysh -N tor -c 'show isis neighbor'
		fail "ISIS did not form all 4 adjacencies on ToR"
	fi
	sleep 1
done

# Wait for bastion loopback reachable from grout via ISIS
SECONDS=0
while ! vtysh -c 'show ip route isis' | grep -qF "10.0.0.20/32"; do
	if [ "$SECONDS" -ge 60 ]; then
		vtysh -c 'show ip route isis'
		fail "ISIS route 10.0.0.20/32 not learned on grout"
	fi
	sleep 1
done

# Wait for SRv6 locator distributed by ISIS
SECONDS=0
while ! vtysh -c 'show ipv6 route isis' | grep -qF "fd00:14::/48"; do
	if [ "$SECONDS" -ge 30 ]; then
		vtysh -c 'show ipv6 route isis'
		fail "SRv6 locator fd00:14::/48 not learned via ISIS"
	fi
	sleep 1
done

# -- node-a (grout): create EVPN/VXLAN overlay ---------------------------------

# Wait for EVPN to be enabled in zebra (from advertise-all-vni)
SECONDS=0
while ! vtysh -c "show evpn" | grep -q "L2 VNIs"; do
	if [ "$SECONDS" -ge 10 ]; then
		vtysh -c "show evpn"
		fail "EVPN not enabled in zebra"
	fi
	sleep 1
done

mark_events

grcli interface add bridge br-pe-210 vrf red
grcli interface add vxlan vni210 vni 210 local 10.0.0.2 domain br-pe-210

# Wait for zebra to learn VNI 210
SECONDS=0
while ! vtysh -c "show evpn vni 210" | grep -q "VNI: 210"; do
	if [ "$SECONDS" -ge 10 ]; then
		vtysh -c "show evpn vni"
		fail "zebra did not learn VNI 210"
	fi
	sleep 1
done

# -- wait for EVPN type-3 flood VTEP exchange ----------------------------------
# Must wait before set_ip_address/create_interface which advance the event mark.
wait_event -t 30 "flood add: vtep vrf=main 10.0.0.3 vni=210"
wait_event -t 10 "flood add: vtep vrf=main 10.0.0.4 vni=210"

set_ip_address br-pe-210 192.168.110.1/24
create_interface p1 domain br-pe-210

move_to_netns x-p1 host-a
ip -n host-a addr add 192.168.110.2/24 dev x-p1
ip -n host-a route add default via 192.168.110.1

# Verify node-b and node-c learned grout's VTEP
SECONDS=0
while ! bridge -n node-b fdb show dev vni210 | grep -qF "10.0.0.2"; do
	[ "$SECONDS" -ge 10 ] && fail "node-b did not learn remote VTEP 10.0.0.2"
	sleep 1
done
SECONDS=0
while ! bridge -n node-c fdb show dev vni210 | grep -qF "10.0.0.2"; do
	[ "$SECONDS" -ge 10 ] && fail "node-c did not learn remote VTEP 10.0.0.2"
	sleep 1
done

# -- wait for SRv6 L3VPN route exchange ----------------------------------------
# Poll instead of wait_event to avoid event mark timing issues: the VPN route
# may have arrived before set_ip_address advanced the mark.
SECONDS=0
while ! grcli -j route show vrf red | jq -e '.[] | select(.destination == "10.100.0.0/24")' >/dev/null 2>&1; do
	[ "$SECONDS" -ge 30 ] && fail "SRv6 L3VPN route 10.100.0.0/24 not learned in VRF red"
	sleep 1
done

nh_id=$(route_nh_id 10.100.0.0/24 red SRv6)
assert_nexthop "$nh_id" '.vrf == "main"'

# Check bastion learned VRF red subnets from all PE nodes
SECONDS=0
while ! ip -n bastion route show vrf red proto bgp | grep -qF "192.168.110.0/24"; do
	[ "$SECONDS" -ge 10 ] && fail "Bastion did not learn 192.168.110.0/24 via SRv6 L3VPN"
	sleep 1
done

# Check node-b and node-c learned bastion's route via SRv6 L3VPN
for ns in node-b node-c; do
	SECONDS=0
	while ! ip -n $ns route show vrf red proto bgp | grep -qF "10.100.0.0/24"; do
		[ "$SECONDS" -ge 10 ] && fail "$ns did not learn 10.100.0.0/24 via SRv6 L3VPN"
		sleep 1
	done
done

# -- verify EVPN L2 connectivity (east-west) -----------------------------------
ip netns exec host-a ping -i0.1 -c3 -W2 192.168.110.3
ip netns exec host-a ping -i0.1 -c3 -W2 192.168.110.4
ip netns exec host-b ping -i0.1 -c3 -W2 192.168.110.2
ip netns exec host-b ping -i0.1 -c3 -W2 192.168.110.4
ip netns exec host-c ping -i0.1 -c3 -W2 192.168.110.2
ip netns exec host-c ping -i0.1 -c3 -W2 192.168.110.3

# -- verify SRv6 L3VPN connectivity (north-south) -----------------------------
ip netns exec host-a ping -i0.1 -c3 -W2 10.100.0.2
ip netns exec host-b ping -i0.1 -c3 -W2 10.100.0.2
ip netns exec host-c ping -i0.1 -c3 -W2 10.100.0.2
ip netns exec host-ext ping -i0.1 -c3 -W2 192.168.110.2
