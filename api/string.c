// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2024 Robin Jarry

#include <gr_errno.h>
#include <gr_string.h>

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

char *astrcat(char *buf, const char *fmt, ...) {
	char *ret = NULL;
	va_list ap;
	int n;

	if (fmt == NULL)
		return errno_set_null(EINVAL);

	va_start(ap, fmt);
	if ((n = vasprintf(&ret, fmt, ap)) < 0)
		return errno_set_null(errno);
	va_end(ap);

	if (buf != NULL) {
		int buf_len = strlen(buf);
		char *tmp = malloc(buf_len + n + 1);
		if (tmp == NULL) {
			free(ret);
			return errno_set_null(ENOMEM);
		}
		memcpy(tmp, buf, buf_len);
		memcpy(tmp + buf_len, ret, n + 1);
		free(buf);
		free(ret);
		ret = tmp;
	}

	return ret;
}

int utf8_check(const char *buf, size_t maxlen) {
	mbstate_t mb;
	size_t len;

	if (strlen(buf) >= maxlen)
		return errno_set(ENAMETOOLONG);

	memset(&mb, 0, sizeof(mb));
	len = mbsrtowcs(NULL, &buf, 0, &mb);
	if (len == (size_t)-1)
		return errno_set(EILSEQ);

	return 0;
}
