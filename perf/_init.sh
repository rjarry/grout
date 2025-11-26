# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2025 Robin Jarry

set -e -o pipefail
trap '' PIPE
ulimit -c unlimited

# Re-exec in a private network namespace for isolation and automatic cleanup
# (no stale named namespace is left behind on the host).
if [ "${_PERF_UNSHARED:-}" != 1 ]; then
	export _PERF_UNSHARED=1
	exec unshare --net -- "$0" "$@"
fi

perf_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

if [ -S "$GROUT_SOCK_PATH" ]; then
	run_grout=false
else
	run_grout=true
fi

expand_cpulist() {
	# "4-7,10" -> "4 5 6 7 10"
	local part
	echo "$1" | tr ',' '\n' | while read -r part; do
		[ -n "$part" ] || continue
		case "$part" in
		*-*) seq "${part%-*}" "${part#*-}" ;;
		*)   echo "$part" ;;
		esac
	done | sort -un | paste -sd' '
}

in_list() { # in_list CPU "SPACE SEP LIST"
	case " $2 " in *" $1 "*) return 0 ;; *) return 1 ;; esac
}

cmdline_get() { # value of key= in /proc/cmdline
	local key="$1" tok
	for tok in $(cat /proc/cmdline); do
		case "$tok" in "$key="*) echo "${tok#"$key"=}"; return ;; esac
	done
}

# Datapath cores. The pool of isolated CPUs available for worker threads (up
# to 4, one per worker) is auto-discovered from the kernel isolcpus set. The
# control CPU is a housekeeping core outside that pool. Override with
# PERF_DP_CPUS=... / PERF_CTRL_CPU=... only if needed.
if [ -z "${PERF_DP_CPUS:-}" ]; then
	_iso=$(expand_cpulist "$(cat /sys/devices/system/cpu/isolated 2>/dev/null)")
	if [ -n "$_iso" ]; then
		PERF_DP_CPUS=$(echo $_iso | tr ' ' '\n' | head -4 | paste -sd' ')
	fi
fi
PERF_DP_CPUS=$(expand_cpulist "$PERF_DP_CPUS") # normalize to space separated
if [ -z "${PERF_CTRL_CPU:-}" ]; then
	# first online CPU not reserved for the datapath
	for _c in $(seq 0 $(($(nproc) - 1))); do
		in_list "$_c" "$PERF_DP_CPUS" || { PERF_CTRL_CPU=$_c; break; }
	done
	PERF_CTRL_CPU=${PERF_CTRL_CPU:-0}
fi

# Worker counts to benchmark, capped to the number of datapath cores available.
: "${PERF_WORKERS:=1 2 4}"
_ndp=$(echo $PERF_DP_CPUS | wc -w)
_w=""
for _n in $PERF_WORKERS; do
	[ "$_n" -le "$_ndp" ] && _w="$_w $_n"
done
PERF_WORKERS=${_w# }

# Measurement parameters. The metric (cycles/packet, read from the TSC inside
# the graph) is normalized per packet, so a longer window mostly re-measures
# the same steady state. Variability is reduced instead by taking several
# short windows and keeping the minimum: noise can only add cycles, so the
# minimum is the least-perturbed measurement.
: "${PERF_WARMUP:=1}"
: "${PERF_WINDOW:=1}"
: "${PERF_RUNS:=5}"

# Reliable cycle accounting requires the datapath cores to be fully shielded
# from the kernel scheduler, RCU callbacks, IRQs and frequency scaling. These
# are boot-time (isolcpus/nohz_full/rcu_nocbs) or system-wide (turbo, governor,
# hugepages) settings that this script only verifies, never changes. When the
# machine is not up to spec, abort (or warn if PERF_IGNORE_ISOLATION=true).
check_isolation() {
	local problems=() cpu g total iso nohz rcu

	iso=$(expand_cpulist "$(cat /sys/devices/system/cpu/isolated 2>/dev/null)")
	nohz=$(expand_cpulist "$(cat /sys/devices/system/cpu/nohz_full 2>/dev/null)")
	rcu=$(expand_cpulist "$(cmdline_get rcu_nocbs)")

	for cpu in $PERF_DP_CPUS; do
		in_list "$cpu" "$iso"  || problems+=("cpu$cpu is not in isolcpus")
		in_list "$cpu" "$nohz" || problems+=("cpu$cpu is not in nohz_full")
		in_list "$cpu" "$rcu"  || problems+=("cpu$cpu is not in rcu_nocbs")
		g=$(cat /sys/devices/system/cpu/cpu$cpu/cpufreq/scaling_governor 2>/dev/null)
		[ "$g" = performance ] || problems+=("cpu$cpu governor is '${g:-unknown}', expected 'performance'")
	done

	if [ -r /sys/devices/system/cpu/intel_pstate/no_turbo ]; then
		[ "$(cat /sys/devices/system/cpu/intel_pstate/no_turbo)" = 1 ] ||
			problems+=("turbo boost is enabled (intel_pstate/no_turbo=0)")
	elif [ -r /sys/devices/system/cpu/cpufreq/boost ]; then
		[ "$(cat /sys/devices/system/cpu/cpufreq/boost)" = 0 ] ||
			problems+=("cpu boost is enabled (cpufreq/boost=1)")
	fi

	total=$(sed -nE 's/^HugePages_Total:[[:space:]]*//p' /proc/meminfo)
	[ "${total:-0}" -gt 0 ] || problems+=("no hugepages allocated (HugePages_Total=0)")

	[ ${#problems[@]} -eq 0 ] && return 0

	{
		echo
		echo "############################################################"
		echo "#  benchmark environment is NOT properly isolated          #"
		echo "############################################################"
		for cpu in "${problems[@]}"; do
			echo "  - $cpu"
		done
		echo
		echo "  results will be unreliable. expected setup:"
		echo "    - kernel cmdline: isolcpus=C nohz_full=C rcu_nocbs=C"
		echo "    - turbo/boost disabled, scaling_governor=performance"
		echo "    - hugepages allocated (see 'grcli' / dpdk docs)"
		echo "  (C = the datapath cores, e.g. an E-core cluster)"
		echo
	} >&2

	if [ "${PERF_IGNORE_ISOLATION:-false}" = true ]; then
		echo "PERF_IGNORE_ISOLATION=true: continuing anyway" >&2
		return 0
	fi
	fail "aborting: fix the isolation or set PERF_IGNORE_ISOLATION=true"
}

# Absolute cycles/packet is only comparable on the same CPU at the same clock.
# This fingerprint is stamped in the results and checked against the baseline
# before comparing.
perf_fingerprint() {
	local model cpu0 fmax
	model=$(sed -nE 's/^model name[[:space:]]*:[[:space:]]*//p' /proc/cpuinfo | head -1)
	cpu0=${PERF_DP_CPUS%% *}
	fmax=$(cat /sys/devices/system/cpu/cpu$cpu0/cpufreq/scaling_max_freq 2>/dev/null || echo unknown)
	echo "cpu='$model' workers=${_perf_workers:-?} khz=$fmax"
}

# Start grout pinned to the control CPU plus the given datapath cores (comma
# separated). grout uses the lowest CPU of its affinity as the control core and
# the remaining ones as datapath workers, so the number of datapath cores given
# here decides the worker count. Real hugepages (no -t) give stable TLB
# behaviour and setarch -R disables ASLR for a reproducible memory layout. Wait
# until the API socket is ready before returning. When an external grout is
# used (GROUT_SOCK_PATH already up), only wait for the socket.
grout_start() {
	local dp="$1" cmd
	if [ "$run_grout" = true ]; then
		cmd="setarch $(uname -m) -R nice -n-20 ionice -c1 -n0"
		cmd="$cmd taskset -c $PERF_CTRL_CPU,$dp grout ${GROUT_OPTS:-}"
		if [ -t 1 ]; then
			# print grout logs in blue (stderr in bold red)
			$cmd \
				> >(awk '{print "\033[34m" $0 "\033[0m"}') \
				2> >(awk '{print "\033[1;31m" $0 "\033[0m"}' >&2) &
		else
			$cmd &
		fi
		grout_pid=$!
	fi
	SECONDS=0
	while ! socat FILE:/dev/null UNIX-CONNECT:$GROUT_SOCK_PATH 2>/dev/null; do
		[ "$SECONDS" -gt 30 ] && fail "grout took more than 30s to start"
		[ "$run_grout" = true ] && kill -0 "$grout_pid"
		sleep 0.2
	done
}

# Terminate the grout started by grout_start and report any crash. No-op when
# an external grout is used.
grout_stop() {
	[ "$run_grout" = true ] && [ -n "${grout_pid:-}" ] || return 0
	local pid="$grout_pid" ret=0
	grout_pid=
	set +x
	kill_wait "$pid" 30 || ret=$?
	if [ "$ret" -ne 0 ]; then
		if [ "$ret" -gt 128 ]; then
			echo "fail: grout terminated by signal SIG$(kill -l $((ret - 128)))"
		else
			echo "fail: grout exited with an error status $ret"
		fi >&2
		coredumpctl debug --no-pager -q "$pid" \
			--debugger-arguments="-batch -ex 'thread apply all bt'" \
			2>/dev/null || true
	fi
	set -x
	return $ret
}

# Run PERF_RUNS measurement windows and write, per graph node, the row with
# the minimum cycles/packet across all windows. The output keeps the same
# columns as "grcli stats software" so compare.awk can consume it directly.
perf_run() {
	local out="$1" i
	sleep "$PERF_WARMUP" # warmup caches and branch predictors
	: > $tmp/samples
	for i in $(seq "$PERF_RUNS"); do
		grcli stats reset
		sleep "$PERF_WINDOW"
		grcli stats software >> $tmp/samples
	done
	{
		echo "# fingerprint: $(perf_fingerprint)"
		awk '
			$1 == "NODE" { next }
			$6 ~ /^[0-9.]+$/ && $6 + 0 > 0 {
				if (!($1 in best) || $6 + 0 < best[$1]) {
					best[$1] = $6 + 0
					line[$1] = $0
				}
			}
			END { for (n in line) print line[n] }
		' $tmp/samples
	} > "$out"
}

# perf_check <prefix> <setup>
#
# For each worker count in PERF_WORKERS: start a fresh grout pinned to that many
# datapath cores, call the test-provided <setup> function to build the topology
# (it receives the worker count as $1 and is expected to add one ingress port
# per worker), measure, then stop grout. Restarting grout is the simplest way to
# change the worker count: net_pcap ports cannot be reconfigured at runtime.
# Results are compared against the matching per-worker baseline
# "<prefix>.w<N>.baseline"; with PERF_UPDATE_BASELINE=true they are written as
# the new baselines instead. Returns non-zero if any comparison reports a
# regression.
perf_check() {
	local prefix="$1" setup="$2" w dp baseline rc=0
	for w in $PERF_WORKERS; do
		dp=$(echo $PERF_DP_CPUS | tr ' ' '\n' | head -n "$w" | paste -sd,)
		_perf_workers="$w"
		echo "==================== $w worker(s) ===================="
		grout_start "$dp"
		"$setup" "$w"
		baseline="${prefix}.w${w}.baseline"
		perf_run $tmp/stats.txt
		if [ "${PERF_UPDATE_BASELINE:-false}" = true ]; then
			cp $tmp/stats.txt "$baseline"
			echo "baseline updated: $baseline"
		else
			"$perf_dir/compare.awk" "$baseline" $tmp/stats.txt || rc=1
		fi
		grout_stop
	done
	return $rc
}

kill_wait() {
	local pid="$1" seconds="$2" cmd ret gc
	cmd=$(ps -o comm= "$pid" 2>/dev/null || echo grout)
	kill -TERM "$pid"
	{
		sleep "$seconds" &&
		echo "'$cmd' (PID $pid) didn't terminate after ${seconds}s, killing it" &&
		kill -KILL "$pid"
	} 2>/dev/null &
	gc=$!
	wait "$pid"
	ret=$?
	kill -KILL "$gc" 2>/dev/null && wait "$gc" 2>/dev/null || true
	return $ret
}

cleanup() {
	status="$?"
	set +e
	# stop grout if a measurement was interrupted before grout_stop ran
	grout_stop
	rm -rf -- "$tmp"
	exit $status
}

fail() {
	echo "fail: $*" >&2
	return 1
}

tmp=$(mktemp -d)
trap cleanup EXIT
builddir=${1-}

if [ "$run_grout" = true ]; then
	export GROUT_SOCK_PATH=$tmp/grout.sock
fi
if [ -n "${builddir}" ]; then
	export PATH=$builddir:$PATH
fi

if [ "$run_grout" = true ]; then
	check_isolation
fi

if [ -t 1 ]; then
	# print bash xtrace in cyan
	exec 9> >(awk '{print "\033[36m" $0 "\033[0m"}')
	export BASH_XTRACEFD=9
	export PS4='+ '
fi

set -x

# grout is started and stopped per worker count by perf_check via grout_start /
# grout_stop; nothing is launched at source time.
