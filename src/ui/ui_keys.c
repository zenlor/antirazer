/* SPDX-License-Identifier: GPL-2.0-or-later OR BSD-2-Clause */
/*
 * ui_keys.c — layout-aware keyboard labels and key capture.
 *
 * The catalog is expressed in HID usages (what the mouse stores). GLFW key
 * tokens are mapped to those usages; printable-key labels come from the
 * current keyboard layout via glfwGetKeyName(). Keys GLFW cannot express
 * (the consumer-control media keys) stay in the catalog but cannot be
 * recorded from a GLFW callback.
 */
#include "ui_keys.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

#include <GLFW/glfw3.h>

#include "naga.h"

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))
#define MAX_ENTRIES 160
#define LABEL_LEN 40
#define NAME_LEN 16

typedef struct {
	unsigned usage;
	enum ui_key_group group;
} usage_def;

static const usage_def usage_defs[] = {
    /* letters and digits */
    {0x04, UI_KEY_GROUP_LETTERS}, {0x05, UI_KEY_GROUP_LETTERS},
    {0x06, UI_KEY_GROUP_LETTERS}, {0x07, UI_KEY_GROUP_LETTERS},
    {0x08, UI_KEY_GROUP_LETTERS}, {0x09, UI_KEY_GROUP_LETTERS},
    {0x0A, UI_KEY_GROUP_LETTERS}, {0x0B, UI_KEY_GROUP_LETTERS},
    {0x0C, UI_KEY_GROUP_LETTERS}, {0x0D, UI_KEY_GROUP_LETTERS},
    {0x0E, UI_KEY_GROUP_LETTERS}, {0x0F, UI_KEY_GROUP_LETTERS},
    {0x10, UI_KEY_GROUP_LETTERS}, {0x11, UI_KEY_GROUP_LETTERS},
    {0x12, UI_KEY_GROUP_LETTERS}, {0x13, UI_KEY_GROUP_LETTERS},
    {0x14, UI_KEY_GROUP_LETTERS}, {0x15, UI_KEY_GROUP_LETTERS},
    {0x16, UI_KEY_GROUP_LETTERS}, {0x17, UI_KEY_GROUP_LETTERS},
    {0x18, UI_KEY_GROUP_LETTERS}, {0x19, UI_KEY_GROUP_LETTERS},
    {0x1A, UI_KEY_GROUP_LETTERS}, {0x1B, UI_KEY_GROUP_LETTERS},
    {0x1C, UI_KEY_GROUP_LETTERS}, {0x1D, UI_KEY_GROUP_LETTERS},

    {0x1E, UI_KEY_GROUP_DIGITS}, {0x1F, UI_KEY_GROUP_DIGITS},
    {0x20, UI_KEY_GROUP_DIGITS}, {0x21, UI_KEY_GROUP_DIGITS},
    {0x22, UI_KEY_GROUP_DIGITS}, {0x23, UI_KEY_GROUP_DIGITS},
    {0x24, UI_KEY_GROUP_DIGITS}, {0x25, UI_KEY_GROUP_DIGITS},
    {0x26, UI_KEY_GROUP_DIGITS}, {0x27, UI_KEY_GROUP_DIGITS},

    /* punctuation */
    {0x2D, UI_KEY_GROUP_PUNCTUATION}, {0x2E, UI_KEY_GROUP_PUNCTUATION},
    {0x2F, UI_KEY_GROUP_PUNCTUATION}, {0x30, UI_KEY_GROUP_PUNCTUATION},
    {0x31, UI_KEY_GROUP_PUNCTUATION}, {0x32, UI_KEY_GROUP_PUNCTUATION},
    {0x33, UI_KEY_GROUP_PUNCTUATION}, {0x34, UI_KEY_GROUP_PUNCTUATION},
    {0x35, UI_KEY_GROUP_PUNCTUATION}, {0x36, UI_KEY_GROUP_PUNCTUATION},
    {0x37, UI_KEY_GROUP_PUNCTUATION}, {0x38, UI_KEY_GROUP_PUNCTUATION},

    /* function keys */
    {0x3A, UI_KEY_GROUP_FUNCTION}, {0x3B, UI_KEY_GROUP_FUNCTION},
    {0x3C, UI_KEY_GROUP_FUNCTION}, {0x3D, UI_KEY_GROUP_FUNCTION},
    {0x3E, UI_KEY_GROUP_FUNCTION}, {0x3F, UI_KEY_GROUP_FUNCTION},
    {0x40, UI_KEY_GROUP_FUNCTION}, {0x41, UI_KEY_GROUP_FUNCTION},
    {0x42, UI_KEY_GROUP_FUNCTION}, {0x43, UI_KEY_GROUP_FUNCTION},
    {0x44, UI_KEY_GROUP_FUNCTION}, {0x45, UI_KEY_GROUP_FUNCTION},
    {0x68, UI_KEY_GROUP_FUNCTION}, {0x69, UI_KEY_GROUP_FUNCTION},
    {0x6A, UI_KEY_GROUP_FUNCTION}, {0x6B, UI_KEY_GROUP_FUNCTION},
    {0x6C, UI_KEY_GROUP_FUNCTION}, {0x6D, UI_KEY_GROUP_FUNCTION},
    {0x6E, UI_KEY_GROUP_FUNCTION}, {0x6F, UI_KEY_GROUP_FUNCTION},
    {0x70, UI_KEY_GROUP_FUNCTION}, {0x71, UI_KEY_GROUP_FUNCTION},
    {0x72, UI_KEY_GROUP_FUNCTION}, {0x73, UI_KEY_GROUP_FUNCTION},

    /* editing and navigation */
    {0x28, UI_KEY_GROUP_NAVIGATION}, {0x29, UI_KEY_GROUP_NAVIGATION},
    {0x2A, UI_KEY_GROUP_NAVIGATION}, {0x2B, UI_KEY_GROUP_NAVIGATION},
    {0x2C, UI_KEY_GROUP_NAVIGATION}, {0x39, UI_KEY_GROUP_NAVIGATION},
    {0x46, UI_KEY_GROUP_NAVIGATION}, {0x47, UI_KEY_GROUP_NAVIGATION},
    {0x48, UI_KEY_GROUP_NAVIGATION}, {0x49, UI_KEY_GROUP_NAVIGATION},
    {0x4A, UI_KEY_GROUP_NAVIGATION}, {0x4B, UI_KEY_GROUP_NAVIGATION},
    {0x4C, UI_KEY_GROUP_NAVIGATION}, {0x4D, UI_KEY_GROUP_NAVIGATION},
    {0x4E, UI_KEY_GROUP_NAVIGATION}, {0x4F, UI_KEY_GROUP_NAVIGATION},
    {0x50, UI_KEY_GROUP_NAVIGATION}, {0x51, UI_KEY_GROUP_NAVIGATION},
    {0x52, UI_KEY_GROUP_NAVIGATION},

    /* keypad */
    {0x53, UI_KEY_GROUP_KEYPAD}, {0x54, UI_KEY_GROUP_KEYPAD},
    {0x55, UI_KEY_GROUP_KEYPAD}, {0x56, UI_KEY_GROUP_KEYPAD},
    {0x57, UI_KEY_GROUP_KEYPAD}, {0x58, UI_KEY_GROUP_KEYPAD},
    {0x59, UI_KEY_GROUP_KEYPAD}, {0x5A, UI_KEY_GROUP_KEYPAD},
    {0x5B, UI_KEY_GROUP_KEYPAD}, {0x5C, UI_KEY_GROUP_KEYPAD},
    {0x5D, UI_KEY_GROUP_KEYPAD}, {0x5E, UI_KEY_GROUP_KEYPAD},
    {0x5F, UI_KEY_GROUP_KEYPAD}, {0x60, UI_KEY_GROUP_KEYPAD},
    {0x61, UI_KEY_GROUP_KEYPAD}, {0x62, UI_KEY_GROUP_KEYPAD},
    {0x63, UI_KEY_GROUP_KEYPAD}, {0x67, UI_KEY_GROUP_KEYPAD},
    {0x85, UI_KEY_GROUP_KEYPAD},

    /* media and system */
    {0x65, UI_KEY_GROUP_MEDIA}, {0x66, UI_KEY_GROUP_MEDIA},
    {0x7F, UI_KEY_GROUP_MEDIA}, {0x80, UI_KEY_GROUP_MEDIA},
    {0x81, UI_KEY_GROUP_MEDIA}, {0xB5, UI_KEY_GROUP_MEDIA},
    {0xB6, UI_KEY_GROUP_MEDIA}, {0xB7, UI_KEY_GROUP_MEDIA},
    {0xB8, UI_KEY_GROUP_MEDIA}, {0xCD, UI_KEY_GROUP_MEDIA},
};

static ui_key_entry entries[MAX_ENTRIES];
static char labels[MAX_ENTRIES][LABEL_LEN];
static char names[MAX_ENTRIES][NAME_LEN];
static size_t entry_count;

static const char *const group_names[UI_KEY_GROUP_COUNT] = {
    "letters", "digits", "punctuation", "function keys",
    "navigation", "keypad", "media & system",
};

static int glfw_key_for(unsigned usage)
{
	if (usage >= 0x04 && usage <= 0x1D)
		return GLFW_KEY_A + (int)(usage - 0x04);
	if (usage >= 0x1E && usage <= 0x26)
		return GLFW_KEY_1 + (int)(usage - 0x1E);
	if (usage == 0x27)
		return GLFW_KEY_0;
	if (usage >= 0x3A && usage <= 0x45)
		return GLFW_KEY_F1 + (int)(usage - 0x3A);
	if (usage >= 0x68 && usage <= 0x73)
		return GLFW_KEY_F13 + (int)(usage - 0x68);
	if (usage >= 0x59 && usage <= 0x61)
		return GLFW_KEY_KP_1 + (int)(usage - 0x59);

	switch (usage) {
	case 0x28:
		return GLFW_KEY_ENTER;
	case 0x29:
		return GLFW_KEY_ESCAPE;
	case 0x2A:
		return GLFW_KEY_BACKSPACE;
	case 0x2B:
		return GLFW_KEY_TAB;
	case 0x2C:
		return GLFW_KEY_SPACE;
	case 0x2D:
		return GLFW_KEY_MINUS;
	case 0x2E:
		return GLFW_KEY_EQUAL;
	case 0x2F:
		return GLFW_KEY_LEFT_BRACKET;
	case 0x30:
		return GLFW_KEY_RIGHT_BRACKET;
	case 0x31:
		return GLFW_KEY_BACKSLASH;
	case 0x33:
		return GLFW_KEY_SEMICOLON;
	case 0x34:
		return GLFW_KEY_APOSTROPHE;
	case 0x35:
		return GLFW_KEY_GRAVE_ACCENT;
	case 0x36:
		return GLFW_KEY_COMMA;
	case 0x37:
		return GLFW_KEY_PERIOD;
	case 0x38:
		return GLFW_KEY_SLASH;
	case 0x39:
		return GLFW_KEY_CAPS_LOCK;
	case 0x46:
		return GLFW_KEY_PRINT_SCREEN;
	case 0x47:
		return GLFW_KEY_SCROLL_LOCK;
	case 0x48:
		return GLFW_KEY_PAUSE;
	case 0x49:
		return GLFW_KEY_INSERT;
	case 0x4A:
		return GLFW_KEY_HOME;
	case 0x4B:
		return GLFW_KEY_PAGE_UP;
	case 0x4C:
		return GLFW_KEY_DELETE;
	case 0x4D:
		return GLFW_KEY_END;
	case 0x4E:
		return GLFW_KEY_PAGE_DOWN;
	case 0x4F:
		return GLFW_KEY_RIGHT;
	case 0x50:
		return GLFW_KEY_LEFT;
	case 0x51:
		return GLFW_KEY_DOWN;
	case 0x52:
		return GLFW_KEY_UP;
	case 0x53:
		return GLFW_KEY_NUM_LOCK;
	case 0x54:
		return GLFW_KEY_KP_DIVIDE;
	case 0x55:
		return GLFW_KEY_KP_MULTIPLY;
	case 0x56:
		return GLFW_KEY_KP_SUBTRACT;
	case 0x57:
		return GLFW_KEY_KP_ADD;
	case 0x58:
		return GLFW_KEY_KP_ENTER;
	case 0x62:
		return GLFW_KEY_KP_0;
	case 0x63:
		return GLFW_KEY_KP_DECIMAL;
	case 0x67:
		return GLFW_KEY_KP_EQUAL;
	default:
		return GLFW_KEY_UNKNOWN;
	}
}

static bool ascii_only(const char *s)
{
	for (; *s; s++)
		if ((unsigned char)*s > 0x7F)
			return false;
	return true;
}

/*
 * The default ImGui font only has ASCII glyphs, so a locale-specific label
 * that is not ASCII falls back to the canonical HID name.
 */
static const struct {
	unsigned usage;
	const char *label;
} special_labels[] = {
    {0x28, "Enter"},	    {0x29, "Escape"},	   {0x2A, "Backspace"},
    {0x2B, "Tab"},	    {0x2C, "Space"},	   {0x39, "Caps Lock"},
    {0x46, "Print Screen"}, {0x47, "Scroll Lock"}, {0x48, "Pause"},
    {0x49, "Insert"},	    {0x4A, "Home"},	   {0x4B, "Page Up"},
    {0x4C, "Delete"},	    {0x4D, "End"},	   {0x4E, "Page Down"},
    {0x4F, "Right"},	    {0x50, "Left"},	   {0x51, "Down"},
    {0x52, "Up"},	    {0x53, "Num Lock"},	   {0x54, "Keypad /"},
    {0x55, "Keypad *"},	    {0x56, "Keypad -"},	   {0x57, "Keypad +"},
    {0x58, "Keypad Enter"}, {0x59, "Keypad 1"},	   {0x5A, "Keypad 2"},
    {0x5B, "Keypad 3"},	    {0x5C, "Keypad 4"},	   {0x5D, "Keypad 5"},
    {0x5E, "Keypad 6"},	    {0x5F, "Keypad 7"},	   {0x60, "Keypad 8"},
    {0x61, "Keypad 9"},	    {0x62, "Keypad 0"},	   {0x63, "Keypad ."},
    {0x67, "Keypad ="},	    {0x85, "Keypad ,"},	   {0x65, "Menu"},
    {0x66, "Power"},	    {0x7F, "Mute"},	   {0x80, "Volume up"},
    {0x81, "Volume down"},  {0xB5, "Next track"},  {0xB6, "Previous track"},
    {0xB7, "Stop"},	    {0xB8, "Eject"},	   {0xCD, "Play/pause"},
};

static void label_for(unsigned usage, char *out, size_t len)
{
	const char *canonical = naga_keyname((int)usage);
	int key = glfw_key_for(usage);

	for (size_t i = 0; i < ARRAY_LEN(special_labels); i++) {
		if (special_labels[i].usage == usage) {
			snprintf(out, len, "%s", special_labels[i].label);
			return;
		}
	}
	if (key != GLFW_KEY_UNKNOWN) {
		int scancode = glfwGetKeyScancode(key);
		const char *name = glfwGetKeyName(key, scancode >= 0 ? scancode : 0);

		if (name && *name && ascii_only(name) &&
		    strcasecmp(name, "Unknown") != 0) {
			snprintf(out, len, "%s", name);
			return;
		}
	}
	if (canonical)
		snprintf(out, len, "%s", canonical);
	else
		snprintf(out, len, "0x%02x", usage);
}

void ui_keys_rebuild(void)
{
	entry_count = 0;
	for (size_t i = 0; i < ARRAY_LEN(usage_defs) && entry_count < MAX_ENTRIES;
	     i++) {
		const usage_def *def = &usage_defs[i];
		ui_key_entry *entry = &entries[entry_count];
		const char *canonical = naga_keyname((int)def->usage);

		label_for(def->usage, labels[entry_count], LABEL_LEN);
		if (canonical)
			snprintf(names[entry_count], NAME_LEN, "%s", canonical);
		else
			snprintf(names[entry_count], NAME_LEN, "%02x", def->usage);

		entry->usage = def->usage;
		entry->label = labels[entry_count];
		entry->name = names[entry_count];
		entry->group = def->group;
		entry_count++;
	}
}

const ui_key_entry *ui_keys_entries(size_t *count)
{
	if (count)
		*count = entry_count;
	return entries;
}

const char *ui_key_group_name(enum ui_key_group group)
{
	if (group < 0 || group >= UI_KEY_GROUP_COUNT)
		return "";
	return group_names[group];
}

const ui_key_entry *ui_keys_find(unsigned usage)
{
	for (size_t i = 0; i < entry_count; i++)
		if (entries[i].usage == usage)
			return &entries[i];
	return NULL;
}

static unsigned mod_bits(int mods)
{
	unsigned bits = 0;

	if (mods & GLFW_MOD_CONTROL)
		bits |= 0x01;
	if (mods & GLFW_MOD_SHIFT)
		bits |= 0x02;
	if (mods & GLFW_MOD_ALT)
		bits |= 0x04;
	if (mods & GLFW_MOD_SUPER)
		bits |= 0x08;
	return bits;
}

static bool is_modifier(int key)
{
	switch (key) {
	case GLFW_KEY_LEFT_CONTROL:
	case GLFW_KEY_RIGHT_CONTROL:
	case GLFW_KEY_LEFT_SHIFT:
	case GLFW_KEY_RIGHT_SHIFT:
	case GLFW_KEY_LEFT_ALT:
	case GLFW_KEY_RIGHT_ALT:
	case GLFW_KEY_LEFT_SUPER:
	case GLFW_KEY_RIGHT_SUPER:
		return true;
	default:
		return false;
	}
}

enum ui_key_capture ui_keys_capture_key(int key, int action, int mods,
					unsigned *usage, unsigned *mod)
{
	if (action != GLFW_PRESS)
		return UI_KEY_CAPTURE_NONE;
	if (key == GLFW_KEY_ESCAPE)
		return UI_KEY_CAPTURE_CANCEL;
	if (is_modifier(key))
		return UI_KEY_CAPTURE_NONE;

	for (size_t i = 0; i < entry_count; i++) {
		if (entries[i].usage == 0x32 || entries[i].usage == 0x85 ||
		    entries[i].usage == 0x65 || entries[i].usage == 0x66 ||
		    entries[i].usage == 0x7F || entries[i].usage == 0x80 ||
		    entries[i].usage == 0x81 || entries[i].usage == 0xB5 ||
		    entries[i].usage == 0xB6 || entries[i].usage == 0xB7 ||
		    entries[i].usage == 0xB8 || entries[i].usage == 0xCD)
			continue;
		if (glfw_key_for(entries[i].usage) == key) {
			*usage = entries[i].usage;
			*mod = mod_bits(mods);
			return UI_KEY_CAPTURE_KEY;
		}
	}
	return UI_KEY_CAPTURE_NONE;
}

enum ui_key_capture ui_keys_capture_mouse(int button, unsigned *usage)
{
	switch (button) {
	case GLFW_MOUSE_BUTTON_LEFT:
		*usage = NAGA_MOUSE_LEFT;
		break;
	case GLFW_MOUSE_BUTTON_RIGHT:
		*usage = NAGA_MOUSE_RIGHT;
		break;
	case GLFW_MOUSE_BUTTON_MIDDLE:
		*usage = NAGA_MOUSE_MIDDLE;
		break;
	case GLFW_MOUSE_BUTTON_4:
		*usage = NAGA_MOUSE_BACK;
		break;
	case GLFW_MOUSE_BUTTON_5:
		*usage = NAGA_MOUSE_FORWARD;
		break;
	default:
		return UI_KEY_CAPTURE_NONE;
	}
	return UI_KEY_CAPTURE_MOUSE;
}
