#!/bin/bash
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2025 Maxime Leroy, Free Mobile
#
# Demo script for BGP/SRv6 with FRR and grout.
#
# Launched automatically by bgp6_srv6_frr_test.sh when DEMO=true.
# Creates a tiled tmux layout with 4 panes and sends commands to each.
#
# Layout:
#   +---------------------+---------------------+
#   |       grout         |   vtysh (bgp-peer)  |
#   +---------------------+---------------------+
#   |      Host-A         |       Host-B        |
#   +---------------------+---------------------+
#
# Keys:
#   F12  - advance to next command
#   F5   - cleanup and exit
#
# Prerequisites (host):
#   sudo apt install obs-studio
#
# OBS setup:
#   1. Sources > + > Screen Capture (or Window Capture for terminal only)
#   2. Sources > + > Video Capture Device (webcam, resize to a corner)
#   3. Sources > + > Audio Input Capture (microphone)
#   4. Click "Start Recording" before running the demo
#
# Run (on the host):
#   sudo apt install wmctrl        # if not already installed
#   wmctrl -r :ACTIVE: -e 0,0,0,1920,1080  # resize terminal to 1080p
#   sudo INTERACTIVE=true DEMO=true smoke/bgp6_srv6_frr_test.sh build

TYPING_DELAY=${TYPING_DELAY:-0.03}
CMD_PAUSE=${CMD_PAUSE:-1}

# Prompt colors (ANSI escape codes)
C_RED='\[\033[1;31m\]'
C_MAGENTA='\[\033[1;35m\]'
C_GREEN='\[\033[1;32m\]'
C_CYAN='\[\033[1;36m\]'
C_RESET='\[\033[0m\]'

PS1_GROUT="${C_RED}grout#${C_RESET} "
PS1_PEER="${C_MAGENTA}bgp-peer#${C_RESET} "
PS1_HOSTA="${C_GREEN}host-a#${C_RESET} "
PS1_HOSTB="${C_CYAN}host-b#${C_RESET} "

DEMO_SIGNAL="demo-$$"
STEP=0
TOTAL=10
cleanup() {
	rm -f "$TOPO_FILE"
	tmux unbind -n F12
	tmux unbind -n F5
	tmux set status-right ""
	tmux set -g status 1
	tmux set -g status-position bottom
	tmux set -gu status-format[0]
	tmux set -gu status-format[1]
}
trap cleanup EXIT

# Bind F12 to advance, F5 to quit
tmux bind -n F12 run-shell -b "tmux set -g @demo_action next && tmux wait-for -S $DEMO_SIGNAL"
tmux bind -n F5 run-shell -b "tmux set -g @demo_action quit && tmux wait-for -S $DEMO_SIGNAL"

step_advance() {
	STEP=$((STEP + 1))
	tmux set -g status-right "#[fg=white] [$STEP/$TOTAL]"
}

wait_key() {
	tmux wait-for "$DEMO_SIGNAL"
	local msg
	msg=$(tmux show -gv @demo_action)
	if [ "$msg" = "quit" ]; then
		# Send Enter to the test window to trigger cleanup
		tmux send-keys -t test Enter
		# Kill all demo windows and exit
		tmux kill-window -t demo 2>/dev/null
		tmux kill-window -t bgp-demo 2>/dev/null
		tmux kill-window -t ping-demo 2>/dev/null
		tmux kill-window -t end-screen 2>/dev/null
		exit 0
	fi
}

# Wait for any key (F12 or F5) at the end of the demo, then exit cleanly.
wait_end_key() {
	tmux wait-for "$DEMO_SIGNAL"
	tmux send-keys -t test Enter
	tmux kill-window -t demo 2>/dev/null
	tmux kill-window -t bgp-demo 2>/dev/null
	tmux kill-window -t ping-demo 2>/dev/null
	tmux kill-window -t end-screen 2>/dev/null
	exit 0
}

# Title bar on top, comment + keys on bottom
tmux set -g status on
tmux set -g status 2
tmux set -g status-style "bg=colour238,fg=white"
tmux set -g status-position top
tmux set -g status-format[0] "#[align=left,fg=colour160,bold] DPDK Summit 2026 -- Integrating FRR with a DPDK Data Plane"
tmux set -g status-format[1] "#[align=left]#{status-left}#[align=right]#{status-right}"
tmux set -g window-status-format ""
tmux set -g window-status-current-format ""
tmux set -g window-status-separator ""
tmux set -g status-right ""
tmux set -g status-left-length 80

# Create a 2x2 tiled window
# +---------------------+---------------------+
# |       grout         |   vtysh (bgp-peer)  |
# +---------------------+---------------------+
# |      Host-A         |       Host-B        |
# +---------------------+---------------------+
tmux new-window -n demo
PANE_GROUT=$(tmux display-message -p -t demo '#{pane_id}')
tmux send-keys -t "$PANE_GROUT" "export PS1='$PS1_GROUT'" Enter
sleep 0.5
PANE_PEER=$(tmux split-window -h -t "$PANE_GROUT" -P -F '#{pane_id}')
tmux send-keys -t "$PANE_PEER" "export PS1='$PS1_PEER'" Enter
sleep 0.5
PANE_HOSTA=$(tmux split-window -v -t "$PANE_GROUT" -P -F '#{pane_id}')
tmux send-keys -t "$PANE_HOSTA" "ip netns exec ns-a bash --norc" Enter
tmux send-keys -t "$PANE_HOSTA" "export PS1='$PS1_HOSTA'" Enter
sleep 0.5
PANE_HOSTB=$(tmux split-window -v -t "$PANE_PEER" -P -F '#{pane_id}')
tmux send-keys -t "$PANE_HOSTB" "ip netns exec ns-b bash --norc" Enter
tmux send-keys -t "$PANE_HOSTB" "export PS1='$PS1_HOSTB'" Enter

# Set pane titles with matching colors (embedded in title)
tmux select-pane -t "$PANE_GROUT" -T "#[fg=red,bold] grout #[default]"
tmux select-pane -t "$PANE_PEER" -T "#[fg=magenta,bold] bgp-peer #[default]"
tmux select-pane -t "$PANE_HOSTA" -T "#[fg=green,bold] Host-A #[default]"
tmux select-pane -t "$PANE_HOSTB" -T "#[fg=cyan,bold] Host-B #[default]"

# Show pane titles in borders
tmux set -g pane-border-status top
tmux set -g pane-border-format " #{pane_title} "
tmux set -g pane-active-border-style "fg=colour245"
tmux set -g pane-border-style "fg=colour245"
sleep 1

# Clear all panes after setup
for p in $PANE_GROUT $PANE_HOSTA $PANE_PEER $PANE_HOSTB; do
	tmux send-keys -t "$p" C-l
done
sleep 0.3

comment() {
	step_advance
	tmux set status-left "#[fg=colour160,bold] >> $* #[default]"
}

ZOOM=true

unzoom() {
	if tmux list-panes -F '#{window_zoomed_flag}' | grep -q '1'; then
		tmux resize-pane -Z
	fi
}

CLEAR=true

run() {
	local pane="$1"
	shift
	local cmd="$*"

	wait_key
	[ -n "$COMMENT" ] && comment "$COMMENT" && COMMENT=""

	if [ "$ZOOM" = true ]; then
		unzoom
		tmux select-pane -t "$pane"
		[ "$CLEAR" = true ] && tmux send-keys -t "$pane" C-l
		tmux resize-pane -Z -t "$pane"
	else
		tmux select-pane -t "$pane"
		[ "$CLEAR" = true ] && tmux send-keys -t "$pane" C-l
	fi
	sleep 0.5

	for (( i = 0; i < ${#cmd}; i++ )); do
		tmux send-keys -t "$pane" -l -- "${cmd:$i:1}"
		sleep "$TYPING_DELAY"
	done

	sleep 0.3
	tmux send-keys -t "$pane" Enter
	sleep "$CMD_PAUSE"
}

clear_pane() {
	tmux send-keys -t "$1" C-l
	sleep 0.3
}

# ---- Pre-create all extra windows for smooth transitions ----

# BGP VPN routes window (2-pane: grout | bgp-peer)
tmux new-window -n bgp-demo
PANE_BGP_GROUT=$(tmux display-message -p -t bgp-demo '#{pane_id}')
tmux send-keys -t "$PANE_BGP_GROUT" "export PS1='$PS1_GROUT'" Enter
sleep 0.3
PANE_BGP_PEER=$(tmux split-window -v -t "$PANE_BGP_GROUT" -P -F '#{pane_id}')
tmux send-keys -t "$PANE_BGP_PEER" "export PS1='$PS1_PEER'" Enter
sleep 0.3
tmux select-pane -t "$PANE_BGP_GROUT" -T "#[fg=red,bold] FRR + Grout #[default]"
tmux select-pane -t "$PANE_BGP_PEER" -T "#[fg=magenta,bold] bgp-peer #[default]"
clear_pane "$PANE_BGP_GROUT"
clear_pane "$PANE_BGP_PEER"

# tcpdump + ping window (2-pane: tcpdump | host-a)
tmux new-window -n ping-demo
PANE_TCPDUMP=$(tmux display-message -p -t ping-demo '#{pane_id}')
tmux send-keys -t "$PANE_TCPDUMP" "export PS1='$PS1_PEER'" Enter
sleep 0.3
PANE_PING=$(tmux split-window -v -t "$PANE_TCPDUMP" -P -F '#{pane_id}')
tmux send-keys -t "$PANE_PING" "ip netns exec ns-a bash --norc" Enter
tmux send-keys -t "$PANE_PING" "export PS1='$PS1_HOSTA'" Enter
sleep 0.3
tmux select-pane -t "$PANE_TCPDUMP" -T "#[fg=magenta,bold] tcpdump (bgp-peer) #[default]"
tmux select-pane -t "$PANE_PING" -T "#[fg=green,bold] Host-A #[default]"
clear_pane "$PANE_TCPDUMP"
clear_pane "$PANE_PING"

# End screen window (single full-screen pane)
tmux new-window -n end-screen
tmux send-keys -t end-screen "export PS1=''" Enter
sleep 0.3
clear_pane end-screen

# Switch back to demo window for the presentation
tmux select-window -t demo

# ---- show network topology ----
TOPO_FILE=/tmp/network-topology.txt
# Topology is 95 chars wide, 17 lines tall
TOPO_W=95
TOPO_H=17
COLS=$(tmux display-message -p '#{window_width}')
ROWS=$(tmux display-message -p '#{window_height}')
TPAD_LEFT=$(( (COLS - TOPO_W) / 2 ))
TPAD_TOP=$(( (ROWS - TOPO_H) / 2 ))
[ "$TPAD_LEFT" -lt 0 ] && TPAD_LEFT=0
[ "$TPAD_TOP" -lt 0 ] && TPAD_TOP=0
TS=$(printf "%${TPAD_LEFT}s" "")
TN=$(printf "%${TPAD_TOP}s" "" | tr ' ' '\n')
# ANSI colors for topology (raw codes, not PS1 \[ \] wrappers)
T_RED=$'\033[1;31m'
T_MAG=$'\033[1;35m'
T_GRN=$'\033[1;32m'
T_CYN=$'\033[1;36m'
T_RST=$'\033[0m'
cat > "$TOPO_FILE" <<EOF
${TN}
${TS}+--------------------------------------------+          +---------------------------------------------+
${TS}|              ${T_MAG}frr-bgp-peer${T_RST}                  |          |               ${T_RED}FRR + Grout${T_RST}                   |
${TS}|                                            |          |                                             |
${TS}|        vrf-vpn4                            |          |                                  vrf-vpn4   |
${TS}|                                            |          |                                             |
${TS}|   +--------------+    +-----------------+  |          |  +-----------------+    +-----------------+ |
${TS}|   |  to-host-a   |    |     x-p0        |  |          |  |       p0        |    |       p1        | |
${TS}+---+              +----+                 +--+          +--+                 +----+                 +-+
${TS}    |   16.0.0.1   |    |  fd00:102::1    |                |  fd00:102::2    |    |    16.1.0.1     |
${TS}    +-------+------+    +--------+--------+                +--------+--------+    +-----------------+
${TS}            |                    |                                  |                      |
${TS}            |                    |          srv6/vpn4               |                      |
${TS}            |                    +----------------------------------+                      |
${TS}            |                                                                              |
${TS}    +-------+------+                                                              +-------+------+
${TS}    |     eth0     |                                                              |     x-p1     |
${TS} +--|              |--+                                                        +--|              |--+
${TS} |  |   16.0.0.2   |  |                                                        |  |   16.1.0.2   |  |
${TS} |  +--------------+  |              <-----      PING    ----->                |  +--------------+  |
${TS} |      ${T_GRN}Host-A${T_RST}        |                                                        |      ${T_CYN}Host-B${T_RST}        |
${TS} +--------------------+                                                        +--------------------+
EOF
tmux set status-left "#[fg=colour160,bold] >> Press F12 to start #[default]"
wait_key
comment "Network topology"
unzoom
tmux select-pane -t "$PANE_GROUT"
tmux send-keys -t "$PANE_GROUT" C-l
tmux resize-pane -Z -t "$PANE_GROUT"
sleep 0.3
tmux send-keys -t "$PANE_GROUT" "cat $TOPO_FILE" Enter
sleep 0.5

# ---- FRR BGP/SRv6 configuration ----
COMMENT="FRR BGP/SRv6 configuration"
run "$PANE_GROUT" "vtysh -c 'show running-config bgpd'"

# ---- grout/FRR sync ----
ZOOM=false

COMMENT="Grout interfaces (grcli) vs FRR (vtysh)"
run "$PANE_GROUT" "grcli interface show"
CLEAR=false
run "$PANE_GROUT" "vtysh -c 'show interface brief'"
CLEAR=true

COMMENT="Grout routes (grcli) vs FRR (vtysh)"
run "$PANE_GROUT" "grcli route show vrf vrf-vpn4"
CLEAR=false
run "$PANE_GROUT" "vtysh -c 'show ip route vrf vrf-vpn4'"
CLEAR=true

# ---- SRv6 nexthops ----
ZOOM=true
COMMENT="SRv6 nexthops (grcli vs vtysh)"
run "$PANE_GROUT" "grcli nexthop show vrf vrf-vpn4"
CLEAR=false
run "$PANE_GROUT" "vtysh -c 'show nexthop-group rib' | grep -B8 End.DT4"
CLEAR=true

# ---- Control plane exception path ----
ZOOM=false
COMMENT="Control plane path"
run "$PANE_GROUT" "grcli interface show main"
CLEAR=false
run "$PANE_GROUT" "ethtool -i main | grep driv"
CLEAR=false
run "$PANE_GROUT" "ip -6 route show default"
CLEAR=false
run "$PANE_GROUT" "ss -tlnp '( sport = :bgp or dport = :bgp )'"
CLEAR=true

# ---- BGP VPN routes (2-pane layout) ----
ZOOM=false
comment "BGP VPN routes -- Grout side vs peer side"
tmux select-window -t bgp-demo
clear_pane "$PANE_BGP_GROUT"
clear_pane "$PANE_BGP_PEER"

run "$PANE_BGP_GROUT" "vtysh -c 'show bgp ipv4 vpn'"
run "$PANE_BGP_PEER" "vtysh -N bgp-peer -c 'show bgp ipv4 vpn'"

# Switch back to demo window
wait_key
tmux select-window -t demo
ZOOM=true

# ---- tcpdump + ping (2-pane layout) ----
ZOOM=false
comment "Ping through SRv6 tunnel"
tmux select-window -t ping-demo
clear_pane "$PANE_TCPDUMP"
clear_pane "$PANE_PING"

# Start tcpdump immediately -- no F12 needed, it just waits for packets
tmux send-keys -t "$PANE_TCPDUMP" "ip netns exec bgp-peer tcpdump -lni x-p0 -c2 ip6 and net 2001:db8::/32" Enter
sleep 1

# Single F12 triggers the ping -- audience sees packets appear in tcpdump live
COMMENT="Ping Host-A -> Host-B (watch tcpdump)"
run "$PANE_PING" "ping -c1 16.1.0.2"
# Wait for tcpdump to finish capturing
sleep 3

# Wait for presenter before showing end screen
wait_key

# End screen -- show THANKS banner
comment "Demo complete"
END_FILE=/tmp/demo-end.txt
COLS=$(tmux display-message -p '#{window_width}')
ROWS=$(tmux display-message -p '#{window_height}')
ART_W=42
BLOCK_H=14
PAD_LEFT=$(( (COLS - ART_W) / 2 ))
PAD_TOP=$(( (ROWS - BLOCK_H) / 2 - 2 ))
[ "$PAD_LEFT" -lt 0 ] && PAD_LEFT=0
[ "$PAD_TOP" -lt 0 ] && PAD_TOP=0
SP=$(printf "%${PAD_LEFT}s" "")
NL=$(printf "%${PAD_TOP}s" "" | tr ' ' '\n')
cat > "$END_FILE" <<END
${NL}
${SP}${T_RED} _____ _   _    _    _   _ _  ______  _${T_RST}
${SP}${T_RED}|_   _| | | |  / \\  | \\ | | |/ / ___|| |${T_RST}
${SP}${T_RED}  | | | |_| | / _ \\ |  \\| | ' /\\___ \\| |${T_RST}
${SP}${T_RED}  | | |  _  |/ ___ \\| |\\  | . \\ ___) |_|${T_RST}
${SP}${T_RED}  |_| |_| |_/_/   \\_\\_| \\_|_|\\_\\____/(_)${T_RST}
${SP}
${SP}
${SP}       Maxime Leroy -- Free Mobile
${SP}
${SP}      https://github.com/DPDK/grout
END
# Switch to pre-created end screen and show banner
tmux select-window -t end-screen
clear_pane end-screen
tmux send-keys -t end-screen "cat $END_FILE" Enter
wait_end_key
