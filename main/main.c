// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2023 Robin Jarry

#include "control.h"
#include "dpdk.h"
#include "gr.h"
#include "sd_notify.h"
#include "signals.h"

#include <gr_api.h>
#include <gr_control.h>
#include <gr_log.h>
#include <gr_macro.h>

#include <event2/event.h>
#include <event2/thread.h>
#include <rte_eal.h>
#include <rte_log.h>
#include <rte_mempool.h>
#include <rte_version.h>

#include <fcntl.h>
#include <getopt.h>
#include <locale.h>
#include <sched.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

// Please keep options/flags in alphabetical order.

static void usage(const char *prog) {
	printf("Usage: %s [-h] [-p] [-s PATH] [-t] [-v] [-v] [-x]\n", prog);
	puts("");
	printf("  Graph router version %s.\n", GROUT_VERSION);
	puts("");
	puts("options:");
	puts("  -h, --help                 Display this help message and exit.");
	puts("  -p, --poll-mode            Disable automatic micro-sleep.");
	puts("  -s PATH, --socket PATH     Path the control plane API socket.");
	puts("                             Default: GROUT_SOCK_PATH from env or");
	printf("                             %s).\n", GR_DEFAULT_SOCK_PATH);
	puts("  -t, --test-mode            Run in test mode (no hugepages).");
	puts("  -V, --version              Print version and exit.");
	puts("  -v, --verbose              Increase verbosity.");
	puts("  -x, --trace-packets        Print all ingress/egress packets.");
}

static struct gr_args args;
bool packet_trace_enabled;

const struct gr_args *gr_args(void) {
	return &args;
}

static int parse_args(int argc, char **argv) {
	int c;

#define FLAGS ":hps:tVvx"
	static struct option long_options[] = {
		{"help", no_argument, NULL, 'h'},
		{"poll-mode", no_argument, NULL, 'p'},
		{"socket", required_argument, NULL, 's'},
		{"test-mode", no_argument, NULL, 't'},
		{"version", no_argument, NULL, 'V'},
		{"verbose", no_argument, NULL, 'v'},
		{"trace-packets", no_argument, NULL, 'x'},
		{0},
	};

	opterr = 0; // disable getopt default error reporting

	args.api_sock_path = getenv("GROUT_SOCK_PATH");
	args.log_level = RTE_LOG_NOTICE;

	while ((c = getopt_long(argc, argv, FLAGS, long_options, NULL)) != -1) {
		switch (c) {
		case 'h':
			usage(argv[0]);
			return -1;
		case 'p':
			args.poll_mode = true;
			break;
		case 's':
			args.api_sock_path = optarg;
			break;
		case 't':
			args.test_mode = true;
			break;
		case 'V':
			printf("grout %s (%s)\n", GROUT_VERSION, rte_version());
			exit(EXIT_SUCCESS);
			break;
		case 'v':
			args.log_level++;
			break;
		case 'x':
			packet_trace_enabled = true;
			break;
		case ':':
			usage(argv[0]);
			fprintf(stderr, "error: -%c requires a value", optopt);
			return errno_set(EINVAL);
		case '?':
			usage(argv[0]);
			fprintf(stderr, "error: -%c unknown option", optopt);
			return errno_set(EINVAL);
		default:
			goto end;
		}
	}
end:
	if (optind < argc) {
		fputs("error: invalid arguments", stderr);
		return errno_set(EINVAL);
	}

	if (args.api_sock_path == NULL)
		args.api_sock_path = GR_DEFAULT_SOCK_PATH;

	return 0;
}

static void finalize_close_fd(struct event *ev, void * /*priv*/) {
	close(event_get_fd(ev));
}

static ssize_t send_response(evutil_socket_t sock, struct gr_api_response *resp) {
	if (resp == NULL)
		return errno_set(ENOMEM);

	LOG(DEBUG,
	    "for_id=%u len=%u status=%u %s",
	    resp->for_id,
	    resp->payload_len,
	    resp->status,
	    strerror(resp->status));

	size_t len = sizeof(*resp) + resp->payload_len;
	return send(sock, resp, len, MSG_DONTWAIT | MSG_NOSIGNAL);
}

static struct event_base *ev_base;

static void api_write_cb(evutil_socket_t sock, short /*what*/, void *priv) {
	struct event *ev = event_base_get_running_event(ev_base);
	struct gr_api_response *resp = priv;

	if (send_response(sock, resp) < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			goto retry;
		LOG(ERR, "send_response: %s", strerror(errno));
	}
	goto free;

retry:
	if (ev == NULL || event_add(ev, NULL) < 0) {
		LOG(ERR, "failed to add event to loop");
		goto free;
	}
	return;

free:
	free(resp);
	if (ev != NULL)
		event_free(ev);
}

static void api_read_cb(evutil_socket_t sock, short what, void * /*ctx*/) {
	struct event *ev = event_base_get_running_event(ev_base);
	void *req_payload = NULL, *resp_payload = NULL;
	struct gr_api_response *resp = NULL;
	struct gr_api_request req;
	struct event *write_ev;
	struct api_out out;
	ssize_t len;

	if (what & EV_CLOSED)
		goto close;

	if ((len = recv(sock, &req, sizeof(req), MSG_DONTWAIT)) < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
			return;
		}
		LOG(ERR, "recv: %s", strerror(errno));
		goto close;
	} else if (len == 0) {
		LOG(DEBUG, "client disconnected");
		goto close;
	}

	if (req.payload_len > 0) {
		req_payload = malloc(req.payload_len);
		if (req_payload == NULL) {
			LOG(ERR, "cannot allocate %u bytes for request payload", req.payload_len);
			goto close;
		}
		if ((len = recv(sock, req_payload, req.payload_len, MSG_DONTWAIT)) < 0) {
			LOG(ERR, "recv: %s", strerror(errno));
			goto close;
		} else if (len == 0) {
			LOG(DEBUG, "client disconnected");
			goto close;
		}
	}

	const struct gr_api_handler *handler = lookup_api_handler(&req);
	if (handler == NULL) {
		out.status = ENOTSUP;
		out.len = 0;
		goto send;
	}

	LOG(DEBUG,
	    "request: id=%u type=0x%08x '%s' len=%u",
	    req.id,
	    req.type,
	    handler->name,
	    req.payload_len);

	out = handler->callback(req_payload, &resp_payload);

send:
	resp = malloc(sizeof(*resp) + out.len);
	if (resp == NULL) {
		LOG(ERR, "cannot allocate %zu bytes for response payload", sizeof(*resp) + out.len);
		goto close;
	}
	resp->for_id = req.id;
	resp->status = out.status;
	resp->payload_len = out.len;
	if (resp_payload != NULL && out.len > 0) {
		memcpy(PAYLOAD(resp), resp_payload, out.len);
		free(resp_payload);
		resp_payload = NULL;
	}
	if (send_response(sock, resp) < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			goto retry_send;
		LOG(ERR, "send: %s", strerror(errno));
		goto close;
	}
	free(req_payload);
	free(resp);
	return;

retry_send:
	write_ev = event_new(ev_base, sock, EV_WRITE | EV_FINALIZE, api_write_cb, resp);
	if (write_ev == NULL || event_add(write_ev, NULL) < 0) {
		LOG(ERR, "failed to add event to loop");
		if (write_ev != NULL)
			event_free(write_ev);
		goto close;
	}
	free(req_payload);
	return;

close:
	free(req_payload);
	free(resp);
	if (ev != NULL)
		event_free_finalize(0, ev, finalize_close_fd);
}

static void listen_cb(evutil_socket_t sock, short what, void * /*ctx*/) {
	struct event *ev;
	int fd;

	if (what & EV_CLOSED) {
		ev = event_base_get_running_event(ev_base);
		event_free_finalize(0, ev, finalize_close_fd);
		return;
	}

	if ((fd = accept4(sock, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC)) < 0) {
		if (errno != EAGAIN && errno != EWOULDBLOCK) {
			LOG(ERR, "accept: %s", strerror(errno));
		}
		return;
	}

	LOG(DEBUG, "new connection");

	ev = event_new(
		ev_base, fd, EV_READ | EV_CLOSED | EV_PERSIST | EV_FINALIZE, api_read_cb, NULL
	);
	if (ev == NULL || event_add(ev, NULL) < 0) {
		LOG(ERR, "failed to add event to loop");
		if (ev != NULL)
			event_free(ev);
		close(fd);
	}
}

#define BACKLOG 16
static struct event *ev_listen;

static int listen_api_socket(void) {
	union {
		struct sockaddr_un un;
		struct sockaddr a;
	} addr;
	int fd;

	fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
	if (fd == -1) {
		LOG(ERR, "socket: %s", strerror(errno));
		return -errno;
	}

	addr.un.sun_family = AF_UNIX;
	strncpy(addr.un.sun_path, args.api_sock_path, sizeof addr.un.sun_path - 1);

	if (bind(fd, &addr.a, sizeof(addr.un)) < 0) {
		LOG(ERR, "bind: %s: %s", args.api_sock_path, strerror(errno));
		close(fd);
		return -errno;
	}

	if (listen(fd, BACKLOG) < 0) {
		LOG(ERR, "listen: %s: %s", args.api_sock_path, strerror(errno));
		close(fd);
		return -errno;
	}

	ev_listen = event_new(
		ev_base,
		fd,
		EV_READ | EV_WRITE | EV_CLOSED | EV_PERSIST | EV_FINALIZE,
		listen_cb,
		NULL
	);
	if (ev_listen == NULL || event_add(ev_listen, NULL) < 0) {
		close(fd);
		LOG(ERR, "event_new: %s: %s", args.api_sock_path, strerror(errno));
		return -errno;
	}

	LOG(INFO, "listening on API socket %s", args.api_sock_path);

	return 0;
}

static int ev_close(const struct event_base *, const struct event *ev, void * /*priv*/) {
	event_callback_fn cb = event_get_callback(ev);
	if (cb == api_read_cb || cb == api_write_cb)
		event_free_finalize(0, (struct event *)ev, finalize_close_fd);
	return 0;
}

int main(int argc, char **argv) {
	int ret = EXIT_FAILURE;
	int err = 0;

	if (setlocale(LC_CTYPE, "C.UTF-8") == NULL) {
		perror("setlocale(LC_CTYPE, C.UTF-8)");
		goto end;
	}
	if (evthread_use_pthreads() < 0) {
		errno = ENOSYS;
		perror("evthread_use_pthreads");
		goto end;
	}
	if (parse_args(argc, argv) < 0)
		goto end;

	if (dpdk_log_init(&args) < 0) {
		err = errno;
		goto end;
	}

	LOG(NOTICE, "starting grout version %s", GROUT_VERSION);

	if (dpdk_init(&args) < 0) {
		err = errno;
		goto dpdk_stop;
	}

	if ((ev_base = event_base_new()) == NULL) {
		LOG(ERR, "event_base_new: %s", strerror(errno));
		err = errno;
		goto shutdown;
	}

	modules_init(ev_base);

	if (listen_api_socket() < 0) {
		err = errno;
		goto shutdown;
	}

	if (register_signals(ev_base) < 0) {
		err = errno;
		goto shutdown;
	}

	if (sd_notifyf(0, "READY=1\nSTATUS=grout version %s started", GROUT_VERSION) < 0)
		LOG(ERR, "sd_notifyf: %s", strerror(errno));

	// run until signal or fatal error
	if (event_base_dispatch(ev_base) == 0) {
		ret = EXIT_SUCCESS;
		if (sd_notifyf(0, "STOPPING=1\nSTATUS=shutting down...") < 0)
			LOG(ERR, "sd_notifyf: %s", strerror(errno));
	} else {
		err = errno;
	}

shutdown:
	unregister_signals();
	if (ev_listen)
		event_free_finalize(0, ev_listen, finalize_close_fd);

	if (ev_base) {
		modules_fini(ev_base);
		event_base_foreach_event(ev_base, ev_close, NULL);
		event_base_free(ev_base);
	}
	unlink(args.api_sock_path);
	libevent_global_shutdown();
dpdk_stop:
	dpdk_fini();
	if (err != 0)
		sd_notifyf(0, "ERRNO=%i", err);
end:
	return ret;
}
