// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Robin Jarry

#include <gr_codec.h>
#include <gr_errno.h>

#include <mpack.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Sparse 2D lookup table indexed by module (high 16 bits) and request id (low 16 bits).
// Same pattern as mod_handlers in main/module.c.

struct module_codecs {
	const struct gr_api_codec *codecs[UINT16_MAX + 1];
};

static struct module_codecs *mod_codecs[UINT16_MAX + 1];

void gr_api_codec_register(uint32_t req_type, const struct gr_api_codec *codec) {
	uint16_t mod = (req_type >> 16) & 0xffff;
	uint16_t id = req_type & 0xffff;

	if (mod_codecs[mod] == NULL) {
		mod_codecs[mod] = calloc(1, sizeof(*mod_codecs[mod]));
		if (mod_codecs[mod] == NULL)
			abort();
	}
	mod_codecs[mod]->codecs[id] = codec;
}

const struct gr_api_codec *gr_api_codec_lookup(uint32_t req_type) {
	uint16_t mod = (req_type >> 16) & 0xffff;
	uint16_t id = req_type & 0xffff;

	if (mod_codecs[mod] == NULL)
		return NULL;
	return mod_codecs[mod]->codecs[id];
}

// Event codec registry (separate namespace from request codecs).
static struct module_codecs *ev_codecs[UINT16_MAX + 1];

void gr_event_codec_register(uint32_t ev_type, const struct gr_api_codec *codec) {
	uint16_t mod = (ev_type >> 16) & 0xffff;
	uint16_t id = ev_type & 0xffff;

	if (ev_codecs[mod] == NULL) {
		ev_codecs[mod] = calloc(1, sizeof(*ev_codecs[mod]));
		if (ev_codecs[mod] == NULL)
			abort();
	}
	ev_codecs[mod]->codecs[id] = codec;
}

const struct gr_api_codec *gr_event_codec_lookup(uint32_t ev_type) {
	uint16_t mod = (ev_type >> 16) & 0xffff;
	uint16_t id = ev_type & 0xffff;

	if (ev_codecs[mod] == NULL)
		return NULL;
	return ev_codecs[mod]->codecs[id];
}

// Event serializer registry.
struct event_serializer {
	const struct gr_field_desc *fields;
	gr_event_serialize_fn callback;
};

struct module_serializers {
	struct event_serializer serializers[UINT16_MAX + 1];
};

static struct module_serializers *mod_serializers[UINT16_MAX + 1];

void gr_event_serializer(
	uint32_t ev_type,
	const struct gr_field_desc *fields,
	gr_event_serialize_fn callback
) {
	uint16_t mod = (ev_type >> 16) & 0xffff;
	uint16_t id = ev_type & 0xffff;

	if (callback == NULL && fields == NULL)
		abort();

	if (mod_serializers[mod] == NULL) {
		mod_serializers[mod] = calloc(1, sizeof(*mod_serializers[mod]));
		if (mod_serializers[mod] == NULL)
			abort();
	}
	mod_serializers[mod]->serializers[id] = (struct event_serializer) {
		.fields = fields,
		.callback = callback,
	};
}

ssize_t gr_event_serialize(uint32_t ev_type, const void *obj, void *buf, size_t buf_len) {
	uint16_t mod = (ev_type >> 16) & 0xffff;
	uint16_t id = ev_type & 0xffff;
	struct module_serializers *sers = mod_serializers[mod];

	if (sers == NULL || obj == NULL)
		return -EINVAL;

	struct event_serializer *s = &sers->serializers[id];
	if (s->callback != NULL)
		return s->callback(obj, buf, buf_len);
	if (s->fields != NULL)
		return gr_mpack_encode(buf, buf_len, s->fields, obj);

	return -ENOTSUP;
}

// Shared sub-field tables for network types.
const struct gr_field_desc gr_ip4_net_fields[] = {
	GR_FIELD_IP4(struct ip4_net, ip),
	GR_FIELD_U8(struct ip4_net, prefixlen),
	GR_FIELD_END,
};

const struct gr_field_desc gr_ip6_net_fields[] = {
	GR_FIELD_IP6(struct ip6_net, ip),
	GR_FIELD_U8(struct ip6_net, prefixlen),
	GR_FIELD_END,
};

// Count the number of fields in a descriptor table (excluding the sentinel).
static int field_count(const struct gr_field_desc *fields) {
	int n = 0;
	while (fields[n].key != NULL)
		n++;
	return n;
}

static uint32_t read_count(const void *data, uint16_t offset, uint8_t size) {
	const void *p = (const char *)data + offset;
	switch (size) {
	case 1:
		return *(const uint8_t *)p;
	case 2:
		return *(const uint16_t *)p;
	case 4:
		return *(const uint32_t *)p;
	}
	return 0;
}

static void write_count(void *data, uint16_t offset, uint8_t size, uint32_t val) {
	void *p = (char *)data + offset;
	switch (size) {
	case 1:
		*(uint8_t *)p = val;
		break;
	case 2:
		*(uint16_t *)p = val;
		break;
	case 4:
		*(uint32_t *)p = val;
		break;
	}
}

static void encode_fields(mpack_writer_t *w, const struct gr_field_desc *fields, const void *data) {
	int n = field_count(fields);

	mpack_start_map(w, n);
	for (int i = 0; i < n; i++) {
		const struct gr_field_desc *f = &fields[i];
		const void *ptr = (const char *)data + f->offset;

		mpack_write_cstr(w, f->key);
		switch (f->type) {
		case GR_F_U8:
			mpack_write_u8(w, *(const uint8_t *)ptr);
			break;
		case GR_F_U16:
			mpack_write_u16(w, *(const uint16_t *)ptr);
			break;
		case GR_F_U32:
			mpack_write_u32(w, *(const uint32_t *)ptr);
			break;
		case GR_F_U64:
			mpack_write_u64(w, *(const uint64_t *)ptr);
			break;
		case GR_F_I8:
			mpack_write_i8(w, *(const int8_t *)ptr);
			break;
		case GR_F_I16:
			mpack_write_i16(w, *(const int16_t *)ptr);
			break;
		case GR_F_I32:
			mpack_write_i32(w, *(const int32_t *)ptr);
			break;
		case GR_F_I64:
			mpack_write_i64(w, *(const int64_t *)ptr);
			break;
		case GR_F_BOOL:
			mpack_write_bool(w, *(const bool *)ptr);
			break;
		case GR_F_CSTR:
			mpack_write_cstr(w, (const char *)ptr);
			break;
		case GR_F_BIN:
			mpack_write_bin(w, (const char *)ptr, f->size);
			break;
		case GR_F_STRUCT:
			encode_fields(w, f->sub_fields, ptr);
			break;
		case GR_F_ARRAY: {
			uint32_t count = read_count(data, f->count_offset, f->count_size);
			mpack_start_array(w, count);
			for (uint32_t j = 0; j < count; j++) {
				const void *elem = (const char *)ptr + j * f->size;
				if (f->sub_fields)
					encode_fields(w, f->sub_fields, elem);
				else
					mpack_write_bin(w, elem, f->size);
			}
			mpack_finish_array(w);
			break;
		}
		case GR_F_END:
			break;
		}
	}
	mpack_finish_map(w);
}

static int mpack_errno(mpack_error_t err) {
	switch (err) {
	case mpack_ok:
		return 0;
	case mpack_error_io:
		return EIO;
	case mpack_error_invalid:
		return EILSEQ;
	case mpack_error_unsupported:
		return EOPNOTSUPP;
	case mpack_error_type:
		return EBADR;
	case mpack_error_too_big:
		return EMSGSIZE;
	case mpack_error_memory:
		return ENOMEM;
	case mpack_error_bug:
		return ENOMEM;
	case mpack_error_data:
		return EBADMSG;
	case mpack_error_eof:
		return EPIPE;
	}
	return EFAULT;
}

ssize_t
gr_mpack_encode(void *buf, size_t buf_len, const struct gr_field_desc *fields, const void *data) {
	mpack_writer_t writer;

	mpack_writer_init(&writer, buf, buf_len);
	encode_fields(&writer, fields, data);
	size_t len = mpack_writer_buffer_used(&writer);

	if (mpack_writer_destroy(&writer) != mpack_ok)
		return errno_set(mpack_errno(mpack_error_too_big));

	return len;
}

// Decode fields from an mpack map into *data_p.
// data_p/alloc_size: root allocation that can be grown via realloc for GR_F_ARRAY.
// base_offset: offset of the current struct within the root allocation.
static void decode_fields(
	mpack_reader_t *r,
	const struct gr_field_desc *fields,
	void **data_p,
	size_t *alloc_size,
	size_t base_offset
) {
	uint32_t count = mpack_expect_map(r);

	for (uint32_t i = 0; i < count; i++) {
		char key[64];
		mpack_expect_cstr(r, key, sizeof(key));

		if (mpack_reader_error(r) != mpack_ok)
			return;

		bool found = false;
		for (const struct gr_field_desc *f = fields; f->key != NULL; f++) {
			if (strcmp(key, f->key) != 0)
				continue;

			void *ptr = (char *)*data_p + base_offset + f->offset;
			found = true;

			switch (f->type) {
			case GR_F_U8:
				*(uint8_t *)ptr = mpack_expect_u8(r);
				break;
			case GR_F_U16:
				*(uint16_t *)ptr = mpack_expect_u16(r);
				break;
			case GR_F_U32:
				*(uint32_t *)ptr = mpack_expect_u32(r);
				break;
			case GR_F_U64:
				*(uint64_t *)ptr = mpack_expect_u64(r);
				break;
			case GR_F_I8:
				*(int8_t *)ptr = mpack_expect_i8(r);
				break;
			case GR_F_I16:
				*(int16_t *)ptr = mpack_expect_i16(r);
				break;
			case GR_F_I32:
				*(int32_t *)ptr = mpack_expect_i32(r);
				break;
			case GR_F_I64:
				*(int64_t *)ptr = mpack_expect_i64(r);
				break;
			case GR_F_BOOL:
				*(bool *)ptr = mpack_expect_bool(r);
				break;
			case GR_F_CSTR:
				mpack_expect_cstr(r, (char *)ptr, f->size);
				break;
			case GR_F_BIN:
				mpack_expect_bin_buf(r, (char *)ptr, f->size);
				break;
			case GR_F_STRUCT:
				decode_fields(
					r,
					f->sub_fields,
					data_p,
					alloc_size,
					base_offset + f->offset
				);
				break;
			case GR_F_ARRAY: {
				uint32_t ac = mpack_expect_array(r);
				write_count(
					*data_p, base_offset + f->count_offset, f->count_size, ac
				);
				// Grow allocation to fit array elements.
				size_t array_end = base_offset + f->offset + ac * f->size;
				if (array_end > *alloc_size) {
					void *grown = realloc(*data_p, array_end);
					if (grown == NULL) {
						mpack_reader_flag_error(r, mpack_error_memory);
						return;
					}
					memset((char *)grown + *alloc_size,
					       0,
					       array_end - *alloc_size);
					*data_p = grown;
					*alloc_size = array_end;
				}
				for (uint32_t j = 0; j < ac; j++) {
					size_t elem_off = base_offset + f->offset + j * f->size;
					if (f->sub_fields) {
						decode_fields(
							r,
							f->sub_fields,
							data_p,
							alloc_size,
							elem_off
						);
					} else {
						void *elem = (char *)*data_p + elem_off;
						mpack_expect_bin_buf(r, elem, f->size);
					}
				}
				mpack_done_array(r);
				break;
			}
			case GR_F_END:
				break;
			}
			break;
		}
		if (!found)
			mpack_discard(r);
	}
	mpack_done_map(r);
}

void *gr_mpack_decode(
	const void *buf,
	size_t buf_len,
	const struct gr_field_desc *fields,
	size_t struct_size,
	size_t *consumed
) {
	size_t alloc = struct_size;
	void *data = calloc(1, alloc);
	if (data == NULL)
		return NULL;

	mpack_reader_t reader;
	mpack_reader_init_data(&reader, buf, buf_len);
	decode_fields(&reader, fields, &data, &alloc, 0);
	size_t remaining = mpack_reader_remaining(&reader, NULL);

	mpack_error_t err = mpack_reader_destroy(&reader);
	if (err != mpack_ok) {
		free(data);
		errno = mpack_errno(err);
		return NULL;
	}

	if (consumed)
		*consumed = buf_len - remaining;
	return data;
}
