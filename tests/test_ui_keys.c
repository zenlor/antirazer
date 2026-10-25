/* SPDX-License-Identifier: GPL-2.0-or-later OR BSD-2-Clause */
/* Unit tests for GLFW key/mouse capture, including media scancodes. */
#include <GLFW/glfw3.h>
#include <linux/input-event-codes.h>
#include <stdio.h>

#include "naga.h"
#include "ui/ui_keys.h"

static int failures;

#define CHECK(cond)                                                            \
	do {                                                                   \
		if (!(cond)) {                                                 \
			fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, \
				__LINE__, #cond);                              \
			failures++;                                            \
		}                                                              \
	} while (0)

static void check_media(int scancode, unsigned want)
{
	unsigned usage = 0xDEAD;
	unsigned mod = 0xDEAD;

	CHECK(ui_keys_capture_key(GLFW_KEY_UNKNOWN, scancode, GLFW_PRESS, 0,
				  &usage, &mod) == UI_KEY_CAPTURE_KEY);
	CHECK(usage == want);
	CHECK(mod == 0);
}

int main(void)
{
	unsigned usage = 0;
	unsigned mod = 0;

	check_media(KEY_MUTE, 0x7F);
	check_media(KEY_VOLUMEUP, 0x80);
	check_media(KEY_VOLUMEDOWN, 0x81);
	check_media(KEY_NEXTSONG, 0xB5);
	check_media(KEY_PREVIOUSSONG, 0xB6);
	check_media(KEY_STOPCD, 0xB7);
	check_media(KEY_EJECTCD, 0xB8);
	check_media(KEY_PLAYPAUSE, 0xCD);
	check_media(KEY_POWER, 0x66);
	check_media(KEY_MENU, 0x65);

	CHECK(ui_keys_capture_key(GLFW_KEY_ESCAPE, 0, GLFW_PRESS, 0, &usage,
				  &mod) == UI_KEY_CAPTURE_CANCEL);
	CHECK(ui_keys_capture_key(GLFW_KEY_UNKNOWN, KEY_VOLUMEUP, GLFW_RELEASE,
				  0, &usage, &mod) == UI_KEY_CAPTURE_NONE);
	CHECK(ui_keys_capture_key(GLFW_KEY_LEFT_SHIFT, 0, GLFW_PRESS,
				  GLFW_MOD_SHIFT, &usage,
				  &mod) == UI_KEY_CAPTURE_NONE);

	CHECK(ui_keys_capture_mouse(GLFW_MOUSE_BUTTON_LEFT, &usage) ==
	      UI_KEY_CAPTURE_MOUSE);
	CHECK(usage == NAGA_MOUSE_LEFT);
	CHECK(ui_keys_capture_mouse(GLFW_MOUSE_BUTTON_4, &usage) ==
	      UI_KEY_CAPTURE_MOUSE);
	CHECK(usage == NAGA_MOUSE_BACK);

	if (failures)
		fprintf(stderr, "%d UI key check(s) failed\n", failures);
	else
		printf("all UI key capture tests passed\n");
	return failures ? 1 : 0;
}
