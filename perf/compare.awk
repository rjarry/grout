#!/usr/bin/awk -f
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2025 Robin Jarry
#
# Usage: compare.awk baseline stats

/^# fingerprint:/ {
	fp = substr($0, index($0, ":") + 2)
	if (NR == FNR) {
		base_fp = fp
	} else {
		cur_fp = fp
	}
	next
}

/^#/ { next }
/^NODE/ { next }

NR == FNR {
	# Reading baseline file
	if ($4 > 10 && $6 > 0) {
		baseline[$1] = $6
		base_sum += $6
	}
	next
}

{
	# Reading current stats file
	if ($4 > 10 && $6 > 0) {
		current[$1] = $6
		cur_sum += $6
	}
}

END {
	if (base_fp != "" && cur_fp != "" && base_fp != cur_fp &&
	    ENVIRON["PERF_IGNORE_FINGERPRINT"] != "1") {
		printf "error: environment differs from baseline\n" > "/dev/stderr"
		printf "  baseline: %s\n", base_fp > "/dev/stderr"
		printf "  current:  %s\n", cur_fp > "/dev/stderr"
		printf "  recapture the baseline (PERF_UPDATE_BASELINE=true) or set" \
		       " PERF_IGNORE_FINGERPRINT=1\n" > "/dev/stderr"
		exit 1
	}

	printf "%-15s  %8s  %8s  %8s  %s\n", \
	       "NODE", "BASELINE", "CURRENT", "DIFF", "CHANGE"

	for (node in current) {
		base = baseline[node]
		cur = current[node]
		if (base > 0) {
			diff = 100 * (cur - base) / base
			if (diff < -10) {
				change = "improved"
			} else if (diff > 10 && cur > 10) {
				change = "regression"
			} else {
				change = "stable"
			}
			printf "%-15s  %8.1f  %8.1f  %+7.1f%%  %s\n", \
				node, base, cur, diff, change
		}
	}

	total_diff = base_sum > 0 ? 100 * (cur_sum - base_sum) / base_sum : 0
	if (total_diff < -2) {
		change = "IMPROVED"
	} else if (total_diff > 2) {
		change = "REGRESSION"
	} else {
		change = "STABLE"
	}
	printf "%-15s  %8.1f  %8.1f  %+7.1f%%  %s\n", \
	       "TOTAL", base_sum, cur_sum, total_diff, change

	if (change == "REGRESSION") {
		printf "error: %+.1f%% regression overall\n", total_diff
		exit 1
	}
}
