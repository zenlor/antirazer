/* SPDX-License-Identifier: GPL-2.0-or-later OR BSD-2-Clause */
/*
 * test_keys.c — unit tests for the pure parts of the driver (no device).
 */
#include <stdio.h>
#include <string.h>

#include "naga.h"

static int failures;

#define CHECK(cond)                                                                    \
	do {                                                                           \
		if (!(cond)) {                                                         \
			fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__,         \
				__LINE__, #cond);                                      \
			failures++;                                                    \
		}                                                                      \
	} while (0)

static void test_named_keys(void)
{
	static const struct {
		const char *name;
		int usage;
	} cases[] = {
	    {"a", 0x04},
	    {"z", 0x1D},
	    {"A", 0x04},
	    {"0", 0x27},
	    {"1", 0x1E},
	    {"9", 0x26},
	    {"f1", 0x3A},
	    {"f12", 0x45},
	    {"F12", 0x45},
	    {"f13", 0x68},
	    {"f24", 0x73},
	    {"enter", 0x28},
	    {"PGDN", 0x4E},
	    {"space", 0x2C},
	    {"kp0", 0x62},
	    /* raw usages, both spellings */
	    {"1a", 0x1A},
	    {"0x1a", 0x1A},
	    {"ff", 0xFF},
	};
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++)
		CHECK(naga_parse_usage(cases[i].name) == cases[i].usage);

	CHECK(naga_parse_usage("nope") == -1);
	CHECK(naga_parse_usage("") == -1);
	CHECK(naga_parse_usage(NULL) == -1);
	CHECK(naga_parse_usage("12zz") == -1);
	CHECK(naga_parse_usage("f25") == -1);
}

static void test_keyname_roundtrip(void)
{
	for (int usage = 0; usage <= 0xFF; usage++) {
		const char *name = naga_keyname(usage);

		if (!name)
			continue;
		if (naga_parse_usage(name) != usage) {
			fprintf(stderr, "roundtrip failed: usage %02X -> %s -> %d\n",
				usage, name, naga_parse_usage(name));
			failures++;
		}
	}

	CHECK(!strcmp(naga_keyname(0x04), "a"));
	CHECK(!strcmp(naga_keyname(0x27), "0"));
	CHECK(!strcmp(naga_keyname(0x3C), "f3"));
	CHECK(!strcmp(naga_keyname(0x68), "f13"));
	CHECK(naga_keyname(0x01) == NULL);
}

static void test_mods(void)
{
	CHECK(naga_parse_mod(NULL) == 0);
	CHECK(naga_parse_mod("") == 0);
	CHECK(naga_parse_mod("ctrl") == 0x01);
	CHECK(naga_parse_mod("shift") == 0x02);
	CHECK(naga_parse_mod("alt") == 0x04);
	CHECK(naga_parse_mod("super") == 0x08);
	CHECK(naga_parse_mod("ctrl+shift") == 0x03);
	CHECK(naga_parse_mod("ctrl+shift+alt+super") == 0x0F);
	CHECK(naga_parse_mod("3") == 0x03);
	CHECK(naga_parse_mod("bogus") == -1);
	CHECK(naga_parse_mod("ctrl+bogus") == -1);
}

static void test_binding_name(void)
{
	char buf[64];
	naga_rec rec = {0};

	/* unread / invalid record */
	CHECK(!strcmp(naga_binding_name(&rec, buf, sizeof buf), "?"));

	rec.valid = true;
	rec.type = NAGA_ASSIGN_DEFAULT;
	CHECK(!strcmp(naga_binding_name(&rec, buf, sizeof buf), "default"));

	rec.type = NAGA_ASSIGN_KEY;
	rec.mod = 0x03;
	rec.usage = 0x17; /* 't' */
	CHECK(!strcmp(naga_binding_name(&rec, buf, sizeof buf), "ctrl+shift+t"));

	rec.type = NAGA_ASSIGN_TURBO;
	rec.mod = 0;
	rec.usage = 0x3C; /* f3 */
	rec.rate = 333;	  /* ~3000 ms */
	CHECK(!strcmp(naga_binding_name(&rec, buf, sizeof buf), "turbo f3 @3003ms"));

	rec.type = NAGA_ASSIGN_MOUSE;
	rec.mouse = NAGA_MOUSE_MIDDLE;
	CHECK(!strcmp(naga_binding_name(&rec, buf, sizeof buf), "mouse middle"));

	rec.mouse = 9;
	CHECK(!strcmp(naga_binding_name(&rec, buf, sizeof buf), "mouse 09"));

	rec.type = NAGA_ASSIGN_HYPERSHIFT;
	CHECK(!strcmp(naga_binding_name(&rec, buf, sizeof buf), "hypershift"));

	rec.type = NAGA_ASSIGN_DPI;
	rec.param = 1;
	CHECK(!strcmp(naga_binding_name(&rec, buf, sizeof buf), "dpi function 1"));

	rec.type = NAGA_ASSIGN_PROFILE;
	rec.param = 4;
	CHECK(!strcmp(naga_binding_name(&rec, buf, sizeof buf), "profile function 04"));

	rec.type = NAGA_ASSIGN_TILT;
	CHECK(!strcmp(naga_binding_name(&rec, buf, sizeof buf), "wheel tilt"));

	rec.type = 0x42;
	CHECK(!strcmp(naga_binding_name(&rec, buf, sizeof buf), "type 42"));
}

static void test_mouse(void)
{
	CHECK(naga_parse_mouse("left") == NAGA_MOUSE_LEFT);
	CHECK(naga_parse_mouse("RIGHT") == NAGA_MOUSE_RIGHT);
	CHECK(naga_parse_mouse("middle") == NAGA_MOUSE_MIDDLE);
	CHECK(naga_parse_mouse("mouse:back") == NAGA_MOUSE_BACK);
	CHECK(naga_parse_mouse("forward") == NAGA_MOUSE_FORWARD);
	CHECK(naga_parse_mouse("4") == NAGA_MOUSE_BACK);
	CHECK(naga_parse_mouse("9") == 9);
	CHECK(naga_parse_mouse("bogus") == -1);
	CHECK(naga_parse_mouse("") == -1);
	CHECK(naga_parse_mouse(NULL) == -1);

	CHECK(!strcmp(naga_mouse_name(NAGA_MOUSE_LEFT), "left"));
	CHECK(!strcmp(naga_mouse_name(NAGA_MOUSE_FORWARD), "forward"));
	CHECK(naga_mouse_name(9) == NULL);
}

static void test_ctrls(void)
{
	CHECK(naga_ctrls_count == 19);

	CHECK(naga_ctrl_by_ctr(0x04) != NULL);
	CHECK(naga_ctrl_by_ctr(0x04)->host_only);
	CHECK(naga_ctrl_by_ctr(0x05)->host_only);
	CHECK(!naga_ctrl_by_ctr(0x40)->host_only);

	CHECK(naga_ctrl_by_ctr(0x01) == &naga_ctrls[0]);
	CHECK(naga_ctrl_by_name("left") == &naga_ctrls[0]);
	CHECK(naga_ctrl_by_name("=") != NULL);
	CHECK(naga_ctrl_by_name("=")->ctr == 0x4B);
	CHECK(naga_ctrl_by_name("-")->ctr == 0x4A);
	CHECK(naga_ctrl_by_ctr(0x4B)->group == NAGA_GROUP_THUMB);
	CHECK(naga_ctrl_by_ctr(0x7F) == NULL);
	CHECK(naga_ctrl_by_name("nope") == NULL);

	for (size_t i = 0; i < naga_ctrls_count; i++) {
		CHECK(naga_ctrl_by_ctr(naga_ctrls[i].ctr) == &naga_ctrls[i]);
		CHECK(naga_ctrl_by_name(naga_ctrls[i].name) == &naga_ctrls[i]);
		for (size_t j = i + 1; j < naga_ctrls_count; j++)
			CHECK(naga_ctrls[i].ctr != naga_ctrls[j].ctr);
	}
}

int main(void)
{
	test_named_keys();
	test_keyname_roundtrip();
	test_mods();
	test_binding_name();
	test_mouse();
	test_ctrls();

	if (failures) {
		fprintf(stderr, "%d check(s) failed\n", failures);
		return 1;
	}
	printf("all key/control tests passed\n");
	return 0;
}
