// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Robin Jarry

#include <gr_cmocka.h>
#include <gr_codec.h>
#include <gr_macro.h>

#include <string.h>

#include <cmocka.h>

struct nested {
	uint32_t x;
	uint16_t y;
};

static const struct gr_field_desc nested_fields[] = {
	GR_FIELD_U32(struct nested, x),
	GR_FIELD_U16(struct nested, y),
	GR_FIELD_END,
};

struct test_msg {
	uint16_t id;
	uint8_t flags;
	bool enabled;
	char name[16];
	uint8_t mac[6];
	struct nested sub;
};

static const struct gr_field_desc test_msg_fields[] = {
	GR_FIELD_U16(struct test_msg, id),
	GR_FIELD_U8(struct test_msg, flags),
	GR_FIELD_BOOL(struct test_msg, enabled),
	GR_FIELD_CSTR(struct test_msg, name),
	GR_FIELD_BIN(struct test_msg, mac, 6),
	GR_FIELD_STRUCT(struct test_msg, sub, nested_fields),
	GR_FIELD_END,
};

static void test_roundtrip(void **) {
	struct test_msg orig = {
		.id = 42,
		.flags = 0x7f,
		.enabled = true,
		.name = "eth0",
		.mac = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01},
		.sub = {.x = 12345, .y = 99},
	};
	uint8_t buf[512];
	ssize_t written = gr_mpack_encode(buf, sizeof(buf), test_msg_fields, &orig);
	assert_true(written > 0);

	struct test_msg *decoded = gr_mpack_decode(
		buf, written, test_msg_fields, sizeof(*decoded), NULL
	);
	assert_non_null(decoded);

	assert_int_equal(decoded->id, 42);
	assert_int_equal(decoded->flags, 0x7f);
	assert_true(decoded->enabled);
	assert_string_equal(decoded->name, "eth0");
	assert_memory_equal(decoded->mac, orig.mac, 6);
	assert_int_equal(decoded->sub.x, 12345);
	assert_int_equal(decoded->sub.y, 99);
	free(decoded);
}

static void test_unknown_fields_skipped(void **) {
	// Encode a message with all fields, then decode using a smaller
	// field table that only knows about "id". Unknown fields are skipped.
	struct test_msg orig = {
		.id = 7,
		.flags = 0xff,
		.enabled = true,
		.name = "lo",
		.sub = {.x = 1},
	};
	uint8_t buf[512];
	ssize_t written = gr_mpack_encode(buf, sizeof(buf), test_msg_fields, &orig);
	assert_true(written > 0);

	// Decode with a reduced field table (only "id").
	static const struct gr_field_desc id_only_fields[] = {
		{.key = "id", .type = GR_F_U16, .offset = 0},
		GR_FIELD_END,
	};
	uint16_t *id = gr_mpack_decode(buf, written, id_only_fields, sizeof(uint16_t), NULL);
	assert_non_null(id);
	assert_int_equal(*id, 7);
	free(id);
}

static void test_missing_fields_zeroed(void **) {
	// Encode a message with only "id", decode into a full struct.
	// Missing fields should stay zero.
	static const struct gr_field_desc id_only_fields[] = {
		GR_FIELD_U16(struct test_msg, id),
		GR_FIELD_END,
	};
	struct test_msg msg = {.id = 99};
	uint8_t buf[64];
	ssize_t written = gr_mpack_encode(buf, sizeof(buf), id_only_fields, &msg);
	assert_true(written > 0);

	struct test_msg *decoded = gr_mpack_decode(
		buf, written, test_msg_fields, sizeof(*decoded), NULL
	);
	assert_non_null(decoded);

	assert_int_equal(decoded->id, 99);
	assert_int_equal(decoded->flags, 0);
	assert_false(decoded->enabled);
	assert_string_equal(decoded->name, "");
	assert_int_equal(decoded->sub.x, 0);
	assert_int_equal(decoded->sub.y, 0);
	free(decoded);
}

int main(void) {
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_roundtrip),
		cmocka_unit_test(test_unknown_fields_skipped),
		cmocka_unit_test(test_missing_fields_zeroed),
	};
	return cmocka_run_group_tests(tests, NULL, NULL);
}
