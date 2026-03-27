// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Robin Jarry

#include <gr_api.h>
#include <gr_codec.h>

static const struct gr_api_codec log_packets_set_codec = {
	.req_fields = GR_FIELD_DESCS(
		GR_FIELD_BOOL(struct gr_log_packets_set_req, enabled),
		GR_FIELD_END,
	),
	.req_size = sizeof(struct gr_log_packets_set_req),
};
static const struct gr_api_codec log_level_list_codec = {
	.req_fields = GR_FIELD_DESCS(
		GR_FIELD_BOOL(struct gr_log_level_list_req, show_all),
		GR_FIELD_END,
	),
	.req_size = sizeof(struct gr_log_level_list_req),
	.resp_fields = GR_FIELD_DESCS(
		GR_FIELD_CSTR(struct gr_log_entry, name),
		GR_FIELD_U32(struct gr_log_entry, level),
		GR_FIELD_END,
	),
	.resp_size = sizeof(struct gr_log_entry),
};

static const struct gr_api_codec log_level_set_codec = {
	.req_fields = GR_FIELD_DESCS(
		GR_FIELD_CSTR(struct gr_log_level_set_req, pattern),
		GR_FIELD_U32(struct gr_log_level_set_req, level),
		GR_FIELD_END,
	),
	.req_size = sizeof(struct gr_log_level_set_req),
};

static const struct gr_api_codec event_subscribe_codec = {
	.req_fields = GR_FIELD_DESCS(
		GR_FIELD_BOOL(struct gr_event_subscribe_req, suppress_self_events),
		GR_FIELD_U32(struct gr_event_subscribe_req, ev_type),
		GR_FIELD_END,
	),
	.req_size = sizeof(struct gr_event_subscribe_req),
};

static const struct gr_api_codec event_unsubscribe_codec = {0};

static void __attribute__((constructor)) main_codecs_init(void) {
	gr_api_codec_register(GR_LOG_PACKETS_SET, &log_packets_set_codec);
	gr_api_codec_register(GR_LOG_LEVEL_LIST, &log_level_list_codec);
	gr_api_codec_register(GR_LOG_LEVEL_SET, &log_level_set_codec);
	gr_api_codec_register(GR_EVENT_SUBSCRIBE, &event_subscribe_codec);
	gr_api_codec_register(GR_EVENT_UNSUBSCRIBE, &event_unsubscribe_codec);
}
