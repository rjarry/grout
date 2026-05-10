#!/bin/bash
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2025 Robin Jarry
#
# Standalone demo script for VXLAN L2VPN with native tcpdump.
#
# Starts its own tmux session, allocates SR-IOV VFs on a ConnectX PF,
# launches grout in an isolated netns, and walks through configuration
# step by step.
#
# The demo intentionally leaves br42 down on the remote host so that
# ping fails. tcpdump is used to diagnose the issue.
#
# Prerequisites:
#   - ConnectX NIC with SR-IOV support
#   - Hugepages configured
#   - grout, grcli, pcap-grout.so built in ./build/
#   - SSH access to the remote host (passwordless)
#
# Usage:
#   sudo ./demo-vxlan.sh
#
# Keys:
#   F5  - advance to next step
#   F9  - cleanup and exit

trap '' PIPE

if [ "${_UNSHARED:-}" != 1 ]; then
	export _UNSHARED=1
	export BUILDDIR=${BUILDDIR:-$PWD/build}
	export PATH="$BUILDDIR:$PATH"
	export GROUT_PAGER=""
	export LD_LIBRARY_PATH="$BUILDDIR/subprojects/libpcap"
	export PCAP_PLUGIN_DIR="$BUILDDIR/pcap"
	export PAGER='less'
	export LESS='-RS'
	export LESSSECURE=1
	export CLICOLOR=1
	export LESS_TERMCAP_md=$'\e[1;36m'
	export LESS_TERMCAP_me=$'\e[0m'
	export LESS_TERMCAP_us=$'\e[1;32m'
	export LESS_TERMCAP_ue=$'\e[0m'
	export GROFF_NO_SGR=1
	export HISTTIMEFORMAT="%a %F %T -- "
	export HISTSIZE=10000
	export HISTIGNORE=ls:ps:history:ll:l:jobs
	export HISTCONTROL=ignoreboth
	exec unshare --mount --net -- "$0" "$@"
fi

if [ "$_MOUNTS" != 1 ]; then
	# prevent any mount events from leaking to the host
	mount --make-rprivate /
	# start with a clean /run/netns, free of any stale host entries
	mkdir -p /run/netns
	mount -t tmpfs tmpfs /run/netns
	export _MOUNTS=1
fi

if [ -z "$_TMUX_SOCK" ]; then
	export _TMUX_SOCK="demo-$$"
	exec tmux -L "$_TMUX_SOCK" new-session -n demo "$0" "$@"
fi

cd ~
ip link set lo up

cat > /tmp/bashrc <<'EOF'
if shopt -q progcomp && [ -z "$BASH_COMPLETION_COMPAT_DIR" ]; then
	if [ -r /usr/share/bash-completion/bash_completion ]; then
		. /usr/share/bash-completion/bash_completion
	elif [ -r /etc/bash_completion ]; then
		. /etc/bash_completion
	fi
	. $BUILDDIR/../cli/grcli.bash-completion
fi
fancy_prompt() {
	local Z="\[\e[0m\]"		# unset all colors
	local H='\$'
	local index host
	local netns=$(ip netns identify 2>/dev/null)
	if [ -n "$netns" ]; then
		host="$netns"
	else
		host="grout"
	fi
	index=$(echo "$host" | md5sum | head -c1)
	local h U color p
	declare -a palette
	declare -A colors
	palette=(33 37 39 40 51 73 118 121 159 162 174 202 206 208 214 216 219 226 229)
	local i=0 j=0
	for i in {0..9} {a..f}; do
		colors[$i]=${palette[$j]}
		j=$(((j + 1) % ${#palette[@]}))
	done
	if [ -n "$BASH_PROMPT_COLOR" ]; then
		color="$BASH_PROMPT_COLOR"
	else
		color=${colors[$index]}
	fi
	PS1="\[\e[38;5;${color}m\][$host ~]\$$Z "
	export PS1
}
fancy_prompt
unset -f fancy_prompt
if [ -f ~/.bash_aliases ]; then
	. ~/.bash_aliases
fi
EOF

# ---- tmux session configuration ----
tmux set -g status-left-length 100
tmux set -g status-right-length 70
tmux set -g status-left ""
tmux set -g status-right "#[fg=colour208]DPDK Summit 2026 -- Grout: two years in#[default]"
tmux set -g window-status-format ""
tmux set -g window-status-current-format ""
tmux set -g window-status-separator ""

declare -a NETNS_LIST

cleanup() {
	set +e
	local ns pids
	killall grout
	for ns in "${NETNS_LIST[@]}"; do
		pids=$(ip netns pids "$ns")
		if [ -n "$pids" ]; then
			kill -TERM $pids 2>/dev/null
			timeout 0.5 wait $pids 2>/dev/null
			kill -KILL $pids 2>/dev/null
		fi
		ip netns del "$ns"
	done
	tmux kill-server
} 2>/dev/null

trap cleanup EXIT

netns_add() {
	local ns="$1"
	ip netns add "$ns"
	ip netns exec "$ns" sysctl -wq net.ipv6.conf.all.disable_ipv6=1
	ip netns exec "$ns" sysctl -wq net.ipv6.conf.default.disable_ipv6=1
	ip -n "$ns" link set lo up
	NETNS_LIST+=("$ns")
}

# ---- Step machinery ----
DEMO_SIGNAL="demo-$$"

tmux bind -n F5 run-shell -b \
	"tmux set -g @demo_action next && tmux wait-for -S $DEMO_SIGNAL"
tmux bind -n F9 run-shell -b \
	"tmux set -g @demo_action quit && tmux wait-for -S $DEMO_SIGNAL"

wait_key() {
	tmux wait-for "$DEMO_SIGNAL"
	local action
	action=$(tmux show -gv @demo_action)
	if [ "$action" = "quit" ]; then
		exit 0
	fi
}

step() {
	tmux set -g status-left "#[fg=colour208,bold]>> $*#[default]"
}

run() {
	local pane="$1"
	shift
	local cmd="$*"

	wait_key
	tmux send-keys -t "$pane" -l -- "$cmd"
	tmux send-keys -t "$pane" Enter
}

clear_pane() {
	tmux send-keys -t "$1" C-l
}

TAP_COUNT=0
add_port() {
	local name=$1 ns=$2
	shift 2
	run "$PANE_PE" grcli interface add port $name devargs net_tap$TAP_COUNT "$@"
	sleep 1
	ip link set dtap0 netns $ns
	ip -n $ns link set dtap0 name eth0
	ip -n $ns link set eth0 up
	TAP_COUNT=$((TAP_COUNT+1))
}

# ======================================================================
# DEMO START
# ======================================================================

netns_add pod
netns_add host

# Window 1 is this script (never shown). Create window 2 for setup.
tmux new-window -n grout "bash --rcfile /tmp/bashrc"
PANE_GROUT=$(tmux display-message -p '#{pane_id}')
tmux select-pane -t "$PANE_GROUT" -T grout

step "Phase 1: Start grout"

run "$PANE_GROUT" grout -t

wait_key

step "Phase 2: Configure grout uplink port"
tmux new-window -n pe "bash --rcfile /tmp/bashrc"
PANE_PE=$(tmux display-message -p '#{pane_id}')
tmux select-pane -t "$PANE_PE" -T pe

tmux split-window -v "ip netns exec pod bash --rcfile /tmp/bashrc"
PANE_POD=$(tmux display-message -p '#{pane_id}')
tmux select-pane -t "$PANE_POD" -T pod

tmux split-window -h "ip netns exec host bash --rcfile /tmp/bashrc"
PANE_HOST=$(tmux display-message -p '#{pane_id}')
tmux select-pane -t "$PANE_HOST" -T host

# Focus the pe pane for grout configuration
tmux select-pane -t "$PANE_PE"

grcli route config set default rib4-routes 128 rib6-routes 128

run "$PANE_PE" grcli interface add vrf underlay

add_port uplink host vrf underlay

run "$PANE_PE" grcli address add 172.16.0.1/24 iface uplink

step "Phase 3: Configure remote host port"
tmux select-pane -t "$PANE_HOST"
run "$PANE_HOST" ip addr add 172.16.0.2/24 dev eth0

step "Phase 4: Ensure uplink can reach remote host"
tmux select-pane -t "$PANE_PE"
run "$PANE_PE" grcli ping 172.16.0.2 vrf underlay count 3 delay 100

step "Phase 5: Configure VXLAN overlay"
run "$PANE_PE" grcli interface add bridge br42
add_port pe pod domain br42

run "$PANE_PE" grcli interface add vxlan vni42 vni 42 local 172.16.0.1 domain br42 encap_vrf underlay

tmux select-pane -t "$PANE_HOST"
run "$PANE_HOST" ip link add br42 type bridge
run "$PANE_HOST" ip link add vni42 type vxlan id 42 local 172.16.0.2 dstport 4789 dev eth0
run "$PANE_HOST" ip link set vni42 master br42
run "$PANE_HOST" ip link set vni42 up

step "Phase 6: Configure VXLAN flood entries"
run "$PANE_HOST" bridge fdb add 00:00:00:00:00:00 dev vni42 self vni 42 dst 172.16.0.1

tmux select-pane -t "$PANE_PE"
run "$PANE_PE" grcli flood vtep add 172.16.0.2 vni 42 vrf underlay

step "Phase 7: Assign private inner address on host bridge"
tmux select-pane -t "$PANE_HOST"
run "$PANE_HOST" ip addr add 10.88.0.2/24 dev br42

step "Phase 8: Assign private inner address in pod"
tmux select-pane -t "$PANE_POD"
run "$PANE_POD" "ip addr add 10.88.0.1/24 dev eth0"

step "Phase 9: Ping remote host"
run "$PANE_POD" "ping 10.88.0.2"

# Ping runs continuously, audience sees timeouts.
# Presenter presses F5 to move on.
wait_key

step "Phase 10: Diagnose the problem"
tmux select-pane -t "$PANE_PE"
clear_pane "$PANE_PE"

run "$PANE_PE" "tcpdump -i grout:uplink -lpnn | sed '/^[0-2][0-9]:/i\\-------'"

# Audience sees ARP requests in VXLAN going out, no reply.
# Presenter presses F5 to move on.

step "Phase 11: Find and fix the bug"
tmux select-pane -t "$PANE_HOST"
clear_pane "$PANE_HOST"
run "$PANE_HOST" "ip -br addr show"
run "$PANE_HOST" "ip link set br42 up"

# Audience sees ping starting to work and tcpdump showing replies.

step "Phase 12: Show learned FDB entries"
tmux select-pane -t "$PANE_PE"
wait_key
tmux send-keys -t "$PANE_PE" C-c
clear_pane "$PANE_PE"
run "$PANE_PE" "grcli fdb"

# Presenter presses F5 to end.
wait_key
exit 0
