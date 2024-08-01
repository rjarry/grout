// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2024 Christophe Fontaine

#include <gr_control.h>
#include <gr_control_output.h>
#include <gr_graph.h>
#include <gr_log.h>
#include <gr_macro.h>
#include <gr_mbuf.h>

#include <event2/event.h>
#include <rte_ether.h>
#include <rte_graph_worker.h>

#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>

extern struct rte_ring *control_output_ring;
static struct event *control_output_evt;

static int tshutdown;
pthread_t thread_id;
pthread_cond_t cv;
pthread_mutex_t mp;

static void poll_control_output_ring(evutil_socket_t, short, void *) {
	struct gr_control_output_msg msg[RTE_GRAPH_BURST_SIZE];
	int n = 0;

	while ((n = rte_ring_dequeue_burst_elem(
			control_output_ring, msg, sizeof(msg[0]), ARRAY_DIM(msg), NULL
		))
	       > 0) {
		for (int i = 0; i < n; i++) {
			msg[i].callback(msg[i].mbuf);
		}
	}
}

void signal_control_ouput_message() {
	pthread_cond_signal(&cv);
}

static void *cond_wait_to_event(void *) {
	struct timespec ts;
	pthread_setname_np(pthread_self(), "gr:cond_wait_to_event");
	clock_gettime(CLOCK_REALTIME, &ts);

	while (atomic_load_explicit(&tshutdown, memory_order_acquire) == 0) {
		ts.tv_sec += 1;
		pthread_mutex_lock(&mp);
		if (pthread_cond_timedwait(&cv, &mp, &ts) == 0) {
			evuser_trigger(control_output_evt);
		}
		pthread_mutex_unlock(&mp);
	}
	return NULL;
}

pthread_attr_t attr;
static void control_output_init(struct event_base *ev_base) {
	atomic_init(&tshutdown, 0);

	if (pthread_attr_init(&attr)) {
		ABORT("pthread_attr_init");
	}

	if (pthread_mutex_init(&mp, NULL)) {
		ABORT("pthread_mutex_init failed");
	}

	if (pthread_cond_init(&cv, NULL)) {
		ABORT("pthread_cond_init failed");
	}

	control_output_evt = event_new(
		ev_base, -1, EV_PERSIST | EV_FINALIZE, poll_control_output_ring, NULL
	);
	if (control_output_evt == NULL)
		ABORT("event_new() failed");

	pthread_create(&thread_id, &attr, cond_wait_to_event, 0);
}

static void control_output_fini(struct event_base *) {
	atomic_store_explicit(&tshutdown, 1, memory_order_release);
	signal_control_ouput_message();
	pthread_attr_destroy(&attr);
	pthread_join(thread_id, NULL);
	pthread_cond_destroy(&cv);
	event_free(control_output_evt);
	pthread_mutex_destroy(&mp);
}

static struct gr_module control_output_module = {
	.name = "control_output",
	.init = control_output_init,
	.fini = control_output_fini,
};

RTE_INIT(control_output_module_init) {
	gr_register_module(&control_output_module);
}
