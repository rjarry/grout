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

set -e

# ---- Configuration (adapt before running) ----
PF=ens1f0np0
VF_UPLINK_PCI=0000:18:00.2
VF_PE_PCI=0000:18:00.3
VF_UPLINK=ens1f0v0
VF_PE=ens1f0v1
VF_POD=ens1f0v2
REMOTE_HOST=root@dell-r450-nfv2023-02.lab.eng.brq2.redhat.com
REMOTE_IFACE=ens1f0np0

BUILDDIR=${BUILDDIR:-$PWD/build}

# ---- PS1 prompts ----
PS1_SETUP='\[\033[38;5;39m\][setup ~]#\[\033[0m\] '
PS1_GROUT='\[\033[38;5;202m\][grout ~]#\[\033[0m\] '
PS1_HOST='\[\033[38;5;118m\][host ~]#\[\033[0m\] '
PS1_POD='\[\033[38;5;214m\][pod ~]#\[\033[0m\] '

# ---- Bootstrap: re-exec inside a dedicated tmux session ----
if [ "$(id -u)" -ne 0 ]; then
	echo "error: must run as root" >&2
	exit 1
fi

if [ "${_DEMO_INSIDE_TMUX:-}" != 1 ]; then
	export _DEMO_INSIDE_TMUX=1
	exec tmux -f /dev/null -L demo-vxlan new-session \
		-n demo-vxlan \
		-x 130 -y 35 \
		"$0" "$@"
fi

# ---- tmux session configuration ----
tmux set -g default-terminal tmux-256color
tmux set -g history-limit 30000
tmux set -sg escape-time 10
tmux set -g mouse off
tmux set -g allow-passthrough on
tmux set -g base-index 1
tmux set -g renumber-windows on

tmux set -g status-position top
tmux setw -g mode-style 'fg=colour15 bg=colour32 none'
tmux set -g message-command-style 'fg=colour16 bg=colour32 bold'
tmux set -g message-style 'fg=colour15 bg=colour32 bold'
tmux set -g pane-border-status top
tmux set -g pane-border-format ' #{pane_title} '
tmux set -g pane-border-style 'fg=colour244 bg=default none'
tmux set -g pane-active-border-style 'fg=colour32 bg=default bold'
tmux set -g status-style 'fg=colour244 bg=default none'
tmux set -g status-left-length 100
tmux set -g status-right-length 70
tmux set -g status-left ""
tmux set -g status-right "#[fg=colour208]DPDK Summit 2026 -- Grout: two years in#[default]"
tmux set -g window-status-format ""
tmux set -g window-status-current-format ""
tmux set -g window-status-separator ""

tmux setw -g mode-keys vi
tmux setw -g allow-rename off

# switch windows using Ctrl-Alt Left/Right or Alt-Page Up/Down
tmux bind -n M-PPage select-window -p
tmux bind -n M-NPage select-window -n
tmux bind -n C-M-Left select-window -p
tmux bind -n C-M-Right select-window -n
# switch panes using Alt-arrows
tmux bind -n M-Up    select-pane -U
tmux bind -n M-Down  select-pane -D
tmux bind -n M-Left  select-pane -L
tmux bind -n M-Right select-pane -R

# environment variables for grcli and tcpdump
tmux set-environment PATH "$BUILDDIR:$PATH"
export PATH="$BUILDDIR:$PATH"
tmux set-environment GROUT_PAGER ""
export GROUT_PAGER=""
tmux set-environment LD_LIBRARY_PATH "$BUILDDIR/subprojects/libpcap"
export LD_LIBRARY_PATH="$BUILDDIR/subprojects/libpcap"
tmux set-environment PCAP_PLUGIN_DIR "$BUILDDIR/pcap"
export PCAP_PLUGIN_DIR="$BUILDDIR/pcap"

# ---- Step machinery ----
DEMO_SIGNAL="demo-$$"

cleanup() {
	set +e

	ip netns pids pod 2>/dev/null | xargs -r kill 2>/dev/null
	ip netns del pod 2>/dev/null
	ip netns pids hbn 2>/dev/null | xargs -r kill 2>/dev/null
	ip netns del hbn 2>/dev/null
	echo 0 > "/sys/class/net/$PF/device/sriov_numvfs" 2>/dev/null
	ssh "$REMOTE_HOST" "ip netns del host" 2>/dev/null
	tmux kill-server 2>/dev/null
}
trap cleanup EXIT

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
	wait_key

	tmux send-keys -t "$pane" Enter
}

clear_pane() {
	tmux send-keys -t "$1" C-l
	sleep 0.3
}

# ======================================================================
# DEMO START
# ======================================================================

# Window 1 is this script (never shown). Create window 2 for setup.
tmux new-window -n setup "PS1=\"$PS1_SETUP\" bash --norc"
PANE_SETUP=$(tmux display-message -p '#{pane_id}')
tmux select-pane -t "$PANE_SETUP" -T setup

step "Initial setup"

# ---- Phase 1: SR-IOV and netns setup ----
run "$PANE_SETUP" "echo 3 > /sys/class/net/$PF/device/sriov_numvfs"

run "$PANE_SETUP" "ip link set $PF vf 0 vlan 42"

run "$PANE_SETUP" "ip link show $PF"

run "$PANE_SETUP" "ip netns add hbn"

run "$PANE_SETUP" "ip link set $VF_UPLINK netns hbn"

run "$PANE_SETUP" "ip link set $VF_PE netns hbn"

run "$PANE_SETUP" "ip netns add pod"
run "$PANE_SETUP" "ip link set $VF_POD netns pod"

# ---- Phase 2: Start grout (reuse setup window) ----
wait_key
step "Start grout"

# Replace the setup shell with an hbn netns shell, rename window
tmux send-keys -t "$PANE_SETUP" \
	"PS1=\"$PS1_GROUT\" exec ip netns exec hbn bash --norc" Enter
tmux rename-window -t setup grout
tmux select-pane -t "$PANE_SETUP" -T grout
clear_pane "$PANE_SETUP"
run "$PANE_SETUP" "grout"

# Wait for presenter to confirm grout is up
wait_key

# ---- Phase 3: Create main window with 3 panes ----
# Layout:
#   +---------------------+---------------------+
#   |        pod          |        host          |
#   +---------------------+---------------------+
#   |              hbn (grout commands)          |
#   +-------------------------------------------+

step "Configure grout interfaces"

tmux new-window -n main \
	"PS1=\"$PS1_POD\" ip netns exec pod bash --norc"
PANE_POD=$(tmux display-message -p '#{pane_id}')
tmux select-pane -t "$PANE_POD" -T pod

tmux split-window -h -t "$PANE_POD" "ssh -t $REMOTE_HOST"
PANE_HOST=$(tmux display-message -p '#{pane_id}')
tmux select-pane -t "$PANE_HOST" -T host
tmux send-keys -t "$PANE_HOST" "export PS1=\"$PS1_HOST\"" Enter
tmux send-keys -t "$PANE_HOST" "ip netns add host" Enter
tmux send-keys -t "$PANE_HOST" "ip link set $REMOTE_IFACE netns host" Enter
tmux send-keys -t "$PANE_HOST" "ip netns exec host bash --norc" Enter
clear_pane "$PANE_HOST"

tmux split-window -v -f -t "$PANE_POD" "PS1=\"$PS1_GROUT\" ip netns exec hbn bash --norc"
PANE_HBN=$(tmux display-message -p '#{pane_id}')
tmux select-pane -t "$PANE_HBN" -T hbn

# Clear all panes
clear_pane "$PANE_POD"
clear_pane "$PANE_HOST"
clear_pane "$PANE_HBN"

# Focus the hbn pane for grout configuration
tmux select-pane -t "$PANE_HBN"

run "$PANE_HBN" "grcli interface add port pe devargs $VF_PE_PCI"
# ---- Grout configuration (non-interactive grcli) ----
step "Configure VXLAN overlay"
run "$PANE_HBN" "grcli interface add vrf underlay"
run "$PANE_HBN" "grcli interface add port uplink devargs $VF_UPLINK_PCI vrf underlay"
run "$PANE_HBN" "grcli address add 172.16.0.1/24 iface uplink"

step "Configure VXLAN overlay"
run "$PANE_HBN" "grcli interface add bridge br42"
run "$PANE_HBN" "grcli interface add vlan vlan42 parent pe vlan_id 42 domain br42"
run "$PANE_HBN" "grcli interface add vxlan vni42 vni 42 local 172.16.0.1 domain br42 encap_vrf underlay"
run "$PANE_HBN" "grcli flood vtep add 172.16.0.2 vni 42 vrf underlay"

step "Show configured interfaces"
run "$PANE_HBN" "grcli interface show"

# ---- Phase 4: Remote host configuration ----
tmux select-pane -t "$PANE_HOST"
step "Configure remote host"
run "$PANE_HOST" "ip addr add 172.16.0.2/24 dev $REMOTE_IFACE"
run "$PANE_HOST" "ip link set $REMOTE_IFACE up"
run "$PANE_HOST" "ip link add vni42 type vxlan id 42 local 172.16.0.2 dstport 4789 dev $REMOTE_IFACE"
run "$PANE_HOST" "ip link add br42 type bridge"
run "$PANE_HOST" "ip link set vni42 master br42"
run "$PANE_HOST" "ip link set vni42 up"
run "$PANE_HOST" "ip addr add 10.88.0.2/24 dev br42"
run "$PANE_HOST" "bridge fdb add 00:00:00:00:00:00 dev vni42 self vni 42 dst 172.16.0.1"

# NOTE: intentionally skip "ip link set br42 up"

# ---- Phase 5: Pod configuration ----
tmux select-pane -t "$PANE_POD"
step "Configure pod"
run "$PANE_POD" "ip addr add 10.88.0.1/24 dev $VF_POD"
run "$PANE_POD" "ip link set $VF_POD up"

# ---- Phase 6: Ping fails ----
step "Ping remote host from pod"
run "$PANE_POD" "ping 10.88.0.2"

# Ping runs continuously, audience sees timeouts.
# Presenter presses F5 to move on.

# ---- Phase 7: Debug with tcpdump ----
step "Debug with tcpdump"
wait_key
step "$COMMENT"
tmux select-pane -t "$PANE_HBN"

run "$PANE_HBN" "tcpdump -i grout:uplink -pnnve"

# Audience sees ARP requests in VXLAN going out, no reply.
# Presenter presses F5 to move on.

# ---- Phase 8: Find and fix the bug ----
tmux select-pane -t "$PANE_HOST"
step "Check remote host bridge"
run "$PANE_HOST" "ip link show br42"

step "Fix: bring br42 up"
run "$PANE_HOST" "ip link set br42 up"

# Audience sees ping starting to work and tcpdump showing replies.
# Presenter presses F5 to end.
wait_key
exit 0
