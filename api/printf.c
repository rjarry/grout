// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2024 Robin Jarry

#include <gr_macro.h>
#include <gr_net_types.h>

#include <arpa/inet.h>
#include <math.h>
#include <printf.h>
#include <stdlib.h>
#include <sys/socket.h>

static int format_pointer(FILE *f, const struct printf_info *info, const void *const *args) {
	char buf[INET6_ADDRSTRLEN];
	const void *arg = *(const void **)*args;

	if (arg == NULL)
		return fprintf(f, "(nil)");

	switch (info->width) {
	case 2: // struct rte_ether_addr *
		const struct rte_ether_addr *mac = arg;
		return fprintf(
			f,
			"%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
			mac->addr_bytes[0],
			mac->addr_bytes[1],
			mac->addr_bytes[2],
			mac->addr_bytes[3],
			mac->addr_bytes[4],
			mac->addr_bytes[5]
		);
	case 4: // ip4_addr_t *
		inet_ntop(AF_INET, arg, buf, sizeof(buf));
		return fprintf(f, "%s", buf);
	case 6: // struct rte_ipv6_addr *
		inet_ntop(AF_INET6, arg, buf, sizeof(buf));
		return fprintf(f, "%s", buf);
	case 32: // struct ip4_net *
		const struct ip4_net *net4 = arg;
		inet_ntop(AF_INET, &net4->ip, buf, sizeof(buf));
		return fprintf(f, "%s/%hhu", buf, net4->prefixlen);
	case 128: // struct ip6_net *
		const struct ip6_net *net6 = arg;
		inet_ntop(AF_INET6, &net6->ip, buf, sizeof(buf));
		return fprintf(f, "%s/%hhu", buf, net6->prefixlen);
	case 1000: // human readable, SI units (double *)
	case 1024: { // human readable, IEC units (double *)
		static const char *const units[] = {"K", "M", "G", "T", "P"};
		double value = *(const double *)arg;
		double order = info->width;
		const char *unit = "";
		double v = value;

		for (unsigned i = 0; fabs(v) >= order && i < ARRAY_DIM(units); i++) {
			unit = units[i];
			v /= order;
		}
		if (unit[0] == '\0')
			return fprintf(f, "%lld", (long long)value);
		// one decimal below the unit's tenth, none above
		return fprintf(
			f,
			"%.*f%s%s",
			fabs(v) < order / 10.0 ? 1 : 0,
			v,
			unit,
			info->width == 1024 ? "i" : ""
		);
	}
	}

	return fprintf(f, "0x%lx", (uintptr_t)arg);
}

static int check_pointer_arg(const struct printf_info *, size_t n, int *argtypes, int *sizes) {
	if (n == 1) {
		sizes[0] = sizeof(void *);
		argtypes[0] = PA_POINTER;
		return 1;
	}
	return -1;
}

static void __attribute__((constructor, used)) init(void) {
	if (register_printf_specifier('p', format_pointer, check_pointer_arg) < 0)
		abort();
}
