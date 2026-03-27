// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Robin Jarry

#pragma once

#include <gr_macro.h>
#include <gr_net_types.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

enum gr_field_type {
	GR_F_END = 0,
	GR_F_U8,
	GR_F_U16,
	GR_F_U32,
	GR_F_U64,
	GR_F_I8,
	GR_F_I16,
	GR_F_I32,
	GR_F_I64,
	GR_F_BOOL,
	GR_F_CSTR, // NUL-terminated fixed-size char array
	GR_F_BIN, // fixed-size byte blob (MAC, IPv6 addr, etc.)
	GR_F_STRUCT, // nested sub-struct with its own field table
	GR_F_ARRAY, // array of sub-structs, count from another field
};

struct gr_field_desc {
	const char *key;
	enum gr_field_type type;
	uint16_t offset; // offsetof() into the struct
	uint16_t size; // element/buffer size (GR_F_CSTR, GR_F_BIN, GR_F_STRUCT, GR_F_ARRAY)
	const struct gr_field_desc *sub_fields; // for GR_F_STRUCT and GR_F_ARRAY
	uint16_t count_offset; // GR_F_ARRAY: offset of the count field
	uint8_t count_size; // GR_F_ARRAY: sizeof the count field (1, 2, or 4)
};

// Custom encode callback for composite types (FAM structs like gr_iface, gr_nexthop).
// Encodes data (data_len bytes) into buf (buf_len capacity).
// Returns the number of bytes written or negative errno.
typedef ssize_t (*gr_codec_encode_fn)(void *buf, size_t buf_len, const void *data, size_t data_len);

// Custom decode callback for composite types (FAM structs like gr_iface, gr_nexthop).
// Decodes buf (buf_len bytes of MPack data) into a malloc'd struct.
// Returns the allocated struct or NULL on error (sets errno). Caller frees.
typedef void *(*gr_codec_decode_fn)(const void *buf, size_t buf_len);

struct gr_api_codec {
	const struct gr_field_desc *req_fields;
	size_t req_size;
	const struct gr_field_desc *resp_fields;
	size_t resp_size;
	// Custom callbacks override field tables when non-NULL.
	gr_codec_encode_fn encode_req;
	gr_codec_decode_fn decode_req;
	gr_codec_encode_fn encode_resp;
	gr_codec_decode_fn decode_resp;
};

// Register a codec for a given request type. Called from __attribute__((constructor)).
void gr_api_codec_register(uint32_t req_type, const struct gr_api_codec *);

// Look up the codec for a given request type. Returns NULL if none registered.
const struct gr_api_codec *gr_api_codec_lookup(uint32_t req_type);

// Helper macros for declaring field descriptor table entries.
// s: struct type, f: field name.
#define GR_FIELD(s, f, t)                                                                          \
	{                                                                                          \
		.key = #f,                                                                         \
		.type = (t),                                                                       \
		.offset = offsetof(s, f),                                                          \
	}
#define GR_FIELD_BOOL(s, f) GR_FIELD(s, f, GR_F_BOOL)
#define GR_FIELD_U8(s, f) GR_FIELD(s, f, GR_F_U8)
#define GR_FIELD_U16(s, f) GR_FIELD(s, f, GR_F_U16)
#define GR_FIELD_U32(s, f) GR_FIELD(s, f, GR_F_U32)
#define GR_FIELD_U64(s, f) GR_FIELD(s, f, GR_F_U64)
#define GR_FIELD_I8(s, f) GR_FIELD(s, f, GR_F_I8)
#define GR_FIELD_I16(s, f) GR_FIELD(s, f, GR_F_I16)
#define GR_FIELD_I32(s, f) GR_FIELD(s, f, GR_F_I32)
#define GR_FIELD_I64(s, f) GR_FIELD(s, f, GR_F_I64)
#define GR_FIELD_CSTR(s, f)                                                                        \
	{                                                                                          \
		.key = #f,                                                                         \
		.type = GR_F_CSTR,                                                                 \
		.offset = offsetof(s, f),                                                          \
		.size = MEMBER_SIZE(s, f),                                                         \
	}
#define GR_FIELD_BIN(s, f, sz)                                                                     \
	{                                                                                          \
		.key = #f,                                                                         \
		.type = GR_F_BIN,                                                                  \
		.offset = offsetof(s, f),                                                          \
		.size = (sz),                                                                      \
	}
#define GR_FIELD_STRUCT(s, f, sub)                                                                 \
	{                                                                                          \
		.key = #f,                                                                         \
		.type = GR_F_STRUCT,                                                               \
		.offset = offsetof(s, f),                                                          \
		.size = MEMBER_SIZE(s, f),                                                         \
		.sub_fields = (sub),                                                               \
	}
// Array of sub-structs. count_f: field name holding the element count.
// sub: field descriptor table for each element.
#define GR_FIELD_ARRAY(s, f, count_f, sub)                                                         \
	{                                                                                          \
		.key = #f,                                                                         \
		.type = GR_F_ARRAY,                                                                \
		.offset = offsetof(s, f),                                                          \
		.size = MEMBER_SIZE(s, f[0]),                                                      \
		.sub_fields = (sub),                                                               \
		.count_offset = offsetof(s, count_f),                                              \
		.count_size = MEMBER_SIZE(s, count_f),                                             \
	}
#define GR_FIELD_END {.key = NULL, .type = GR_F_END}

// Network type convenience macros. These use GR_F_BIN to preserve raw
// byte order (no host-endian conversion), which is correct for fields
// stored in network byte order.
#define GR_FIELD_BE16(s, f) GR_FIELD_BIN(s, f, sizeof(rte_be16_t))
#define GR_FIELD_IP4(s, f) GR_FIELD_BIN(s, f, sizeof(ip4_addr_t))
#define GR_FIELD_IP6(s, f) GR_FIELD_BIN(s, f, sizeof(struct rte_ipv6_addr))
#define GR_FIELD_MAC(s, f) GR_FIELD_BIN(s, f, sizeof(struct rte_ether_addr))

// Shared sub-field tables for ip4_net and ip6_net (defined in codec.c).
extern const struct gr_field_desc gr_ip4_net_fields[];
extern const struct gr_field_desc gr_ip6_net_fields[];

#define GR_FIELD_IP4_NET(s, f) GR_FIELD_STRUCT(s, f, gr_ip4_net_fields)
#define GR_FIELD_IP6_NET(s, f) GR_FIELD_STRUCT(s, f, gr_ip6_net_fields)

// Convenience macro to define an inline list of field descriptions.
// This is a workaround to avoid clang-format ignoring these compound statements.
#define GR_FIELD_DESCS(...)                                                                        \
	(const struct gr_field_desc[]) {                                                           \
		__VA_ARGS__                                                                        \
	}

// Encode a struct into an MPack map using a field descriptor table.
// Returns the number of bytes written or negative errno.
ssize_t
gr_mpack_encode(void *buf, size_t buf_len, const struct gr_field_desc *fields, const void *data);

// Decode an MPack map into a calloc'd struct using a field descriptor table.
// struct_size: minimum allocation size (sizeof the target struct).
// For GR_F_ARRAY fields, the allocation is automatically grown with realloc.
// Returns the allocated struct or NULL on error (sets errno). Caller frees.
// If consumed is non-NULL, set to the number of MPack bytes consumed.
void *gr_mpack_decode(
	const void *buf,
	size_t buf_len,
	const struct gr_field_desc *fields,
	size_t struct_size,
	size_t *consumed
);
