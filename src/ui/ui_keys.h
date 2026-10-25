/* SPDX-License-Identifier: GPL-2.0-or-later OR BSD-2-Clause */
/*
 * ui_keys.h — keyboard catalog and event capture for naga-ui.
 *
 * The device stores HID keyboard usages, which are physical positions in the
 * HID keyboard page. This module turns them into labels for the current
 * keyboard layout (via GLFW) and captures a pressed key or mouse button from
 * GLFW callbacks.
 *
 * Nothing here talks to the device; it only translates between GLFW, HID
 * usages and the canonical names used by naga_keys.c.
 */
#ifndef ANTIRAZER_UI_KEYS_H
#define ANTIRAZER_UI_KEYS_H

#include <stdbool.h>
#include <stddef.h>

enum ui_key_group {
	UI_KEY_GROUP_LETTERS,
	UI_KEY_GROUP_DIGITS,
	UI_KEY_GROUP_PUNCTUATION,
	UI_KEY_GROUP_FUNCTION,
	UI_KEY_GROUP_NAVIGATION,
	UI_KEY_GROUP_KEYPAD,
	UI_KEY_GROUP_MEDIA,
	UI_KEY_GROUP_COUNT,
};

typedef struct {
	unsigned usage;	   /* HID keyboard usage, what the device stores */
	const char *label; /* label in the current keyboard layout */
	const char *name;  /* canonical naga name: "z", "f5", "1a" */
	enum ui_key_group group;
} ui_key_entry;

enum ui_key_capture {
	UI_KEY_CAPTURE_NONE,
	UI_KEY_CAPTURE_KEY,    /* *usage / *mod are filled in */
	UI_KEY_CAPTURE_MOUSE,  /* *usage is an enum naga_mouse id */
	UI_KEY_CAPTURE_CANCEL, /* Escape */
};

/* Build the catalog from the current GLFW keyboard layout. Call after glfwInit. */
void ui_keys_rebuild(void);

const ui_key_entry *ui_keys_entries(size_t *count);
const char *ui_key_group_name(enum ui_key_group group);
const ui_key_entry *ui_keys_find(unsigned usage);

/*
 * Translate one GLFW key callback into a binding.
 *
 * Keyboard keys and mouse buttons are both captured: a mouse press reports
 * UI_KEY_CAPTURE_MOUSE with *usage set to the enum naga_mouse id. Modifier
 * keys are not captured as the binding themselves; their state is folded into
 * *mod when a non-modifier key is pressed. Linux media keys arrive from GLFW
 * as GLFW_KEY_UNKNOWN with the evdev keycode in scancode and are mapped back
 * to their HID consumer usages.
 */
enum ui_key_capture ui_keys_capture_key(int key, int scancode, int action,
					int mods, unsigned *usage,
					unsigned *mod);
enum ui_key_capture ui_keys_capture_mouse(int button, unsigned *usage);

#endif /* ANTIRAZER_UI_KEYS_H */
