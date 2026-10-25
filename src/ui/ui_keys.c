/* SPDX-License-Identifier: GPL-2.0-or-later OR BSD-2-Clause */
/*
 * ui_keys.c — layout-aware keyboard labels and key capture.
 *
 * SDL scancodes are HID keyboard usages for the classic key range, so labels
 * can be produced by asking SDL for the keycode of each usage under the
 * current layout. The few media keys whose SDL scancode differs from the HID
 * usage are translated explicitly.
 */
#include "ui_keys.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

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
    {0x04, UI_KEY_GROUP_LETTERS},
    {0x05, UI_KEY_GROUP_LETTERS},
    {0x06, UI_KEY_GROUP_LETTERS},
    {0x07, UI_KEY_GROUP_LETTERS},
    {0x08, UI_KEY_GROUP_LETTERS},
    {0x09, UI_KEY_GROUP_LETTERS},
    {0x0A, UI_KEY_GROUP_LETTERS},
    {0x0B, UI_KEY_GROUP_LETTERS},
    {0x0C, UI_KEY_GROUP_LETTERS},
    {0x0D, UI_KEY_GROUP_LETTERS},
    {0x0E, UI_KEY_GROUP_LETTERS},
    {0x0F, UI_KEY_GROUP_LETTERS},
    {0x10, UI_KEY_GROUP_LETTERS},
    {0x11, UI_KEY_GROUP_LETTERS},
    {0x12, UI_KEY_GROUP_LETTERS},
    {0x13, UI_KEY_GROUP_LETTERS},
    {0x14, UI_KEY_GROUP_LETTERS},
    {0x15, UI_KEY_GROUP_LETTERS},
    {0x16, UI_KEY_GROUP_LETTERS},
    {0x17, UI_KEY_GROUP_LETTERS},
    {0x18, UI_KEY_GROUP_LETTERS},
    {0x19, UI_KEY_GROUP_LETTERS},
    {0x1A, UI_KEY_GROUP_LETTERS},
    {0x1B, UI_KEY_GROUP_LETTERS},
    {0x1C, UI_KEY_GROUP_LETTERS},
    {0x1D, UI_KEY_GROUP_LETTERS},

    {0x1E, UI_KEY_GROUP_DIGITS},
    {0x1F, UI_KEY_GROUP_DIGITS},
    {0x20, UI_KEY_GROUP_DIGITS},
    {0x21, UI_KEY_GROUP_DIGITS},
    {0x22, UI_KEY_GROUP_DIGITS},
    {0x23, UI_KEY_GROUP_DIGITS},
    {0x24, UI_KEY_GROUP_DIGITS},
    {0x25, UI_KEY_GROUP_DIGITS},
    {0x26, UI_KEY_GROUP_DIGITS},
    {0x27, UI_KEY_GROUP_DIGITS},

    /* punctuation */
    {0x2D, UI_KEY_GROUP_PUNCTUATION},
    {0x2E, UI_KEY_GROUP_PUNCTUATION},
    {0x2F, UI_KEY_GROUP_PUNCTUATION},
    {0x30, UI_KEY_GROUP_PUNCTUATION},
    {0x31, UI_KEY_GROUP_PUNCTUATION},
    {0x32, UI_KEY_GROUP_PUNCTUATION},
    {0x33, UI_KEY_GROUP_PUNCTUATION},
    {0x34, UI_KEY_GROUP_PUNCTUATION},
    {0x35, UI_KEY_GROUP_PUNCTUATION},
    {0x36, UI_KEY_GROUP_PUNCTUATION},
    {0x37, UI_KEY_GROUP_PUNCTUATION},
    {0x38, UI_KEY_GROUP_PUNCTUATION},

    /* function keys */
    {0x3A, UI_KEY_GROUP_FUNCTION},
    {0x3B, UI_KEY_GROUP_FUNCTION},
    {0x3C, UI_KEY_GROUP_FUNCTION},
    {0x3D, UI_KEY_GROUP_FUNCTION},
    {0x3E, UI_KEY_GROUP_FUNCTION},
    {0x3F, UI_KEY_GROUP_FUNCTION},
    {0x40, UI_KEY_GROUP_FUNCTION},
    {0x41, UI_KEY_GROUP_FUNCTION},
    {0x42, UI_KEY_GROUP_FUNCTION},
    {0x43, UI_KEY_GROUP_FUNCTION},
    {0x44, UI_KEY_GROUP_FUNCTION},
    {0x45, UI_KEY_GROUP_FUNCTION},
    {0x68, UI_KEY_GROUP_FUNCTION},
    {0x69, UI_KEY_GROUP_FUNCTION},
    {0x6A, UI_KEY_GROUP_FUNCTION},
    {0x6B, UI_KEY_GROUP_FUNCTION},
    {0x6C, UI_KEY_GROUP_FUNCTION},
    {0x6D, UI_KEY_GROUP_FUNCTION},
    {0x6E, UI_KEY_GROUP_FUNCTION},
    {0x6F, UI_KEY_GROUP_FUNCTION},
    {0x70, UI_KEY_GROUP_FUNCTION},
    {0x71, UI_KEY_GROUP_FUNCTION},
    {0x72, UI_KEY_GROUP_FUNCTION},
    {0x73, UI_KEY_GROUP_FUNCTION},

    /* editing and navigation */
    {0x28, UI_KEY_GROUP_NAVIGATION},
    {0x29, UI_KEY_GROUP_NAVIGATION},
    {0x2A, UI_KEY_GROUP_NAVIGATION},
    {0x2B, UI_KEY_GROUP_NAVIGATION},
    {0x2C, UI_KEY_GROUP_NAVIGATION},
    {0x39, UI_KEY_GROUP_NAVIGATION},
    {0x46, UI_KEY_GROUP_NAVIGATION},
    {0x47, UI_KEY_GROUP_NAVIGATION},
    {0x48, UI_KEY_GROUP_NAVIGATION},
    {0x49, UI_KEY_GROUP_NAVIGATION},
    {0x4A, UI_KEY_GROUP_NAVIGATION},
    {0x4B, UI_KEY_GROUP_NAVIGATION},
    {0x4C, UI_KEY_GROUP_NAVIGATION},
    {0x4D, UI_KEY_GROUP_NAVIGATION},
    {0x4E, UI_KEY_GROUP_NAVIGATION},
    {0x4F, UI_KEY_GROUP_NAVIGATION},
    {0x50, UI_KEY_GROUP_NAVIGATION},
    {0x51, UI_KEY_GROUP_NAVIGATION},
    {0x52, UI_KEY_GROUP_NAVIGATION},

    /* keypad */
    {0x53, UI_KEY_GROUP_KEYPAD},
    {0x54, UI_KEY_GROUP_KEYPAD},
    {0x55, UI_KEY_GROUP_KEYPAD},
    {0x56, UI_KEY_GROUP_KEYPAD},
    {0x57, UI_KEY_GROUP_KEYPAD},
    {0x58, UI_KEY_GROUP_KEYPAD},
    {0x59, UI_KEY_GROUP_KEYPAD},
    {0x5A, UI_KEY_GROUP_KEYPAD},
    {0x5B, UI_KEY_GROUP_KEYPAD},
    {0x5C, UI_KEY_GROUP_KEYPAD},
    {0x5D, UI_KEY_GROUP_KEYPAD},
    {0x5E, UI_KEY_GROUP_KEYPAD},
    {0x5F, UI_KEY_GROUP_KEYPAD},
    {0x60, UI_KEY_GROUP_KEYPAD},
    {0x61, UI_KEY_GROUP_KEYPAD},
    {0x62, UI_KEY_GROUP_KEYPAD},
    {0x63, UI_KEY_GROUP_KEYPAD},
    {0x67, UI_KEY_GROUP_KEYPAD},
    {0x85, UI_KEY_GROUP_KEYPAD},

    /* media and system */
    {0x65, UI_KEY_GROUP_MEDIA},
    {0x66, UI_KEY_GROUP_MEDIA},
    {0x7F, UI_KEY_GROUP_MEDIA},
    {0x80, UI_KEY_GROUP_MEDIA},
    {0x81, UI_KEY_GROUP_MEDIA},
    {0xB5, UI_KEY_GROUP_MEDIA},
    {0xB6, UI_KEY_GROUP_MEDIA},
    {0xB7, UI_KEY_GROUP_MEDIA},
    {0xB8, UI_KEY_GROUP_MEDIA},
    {0xCD, UI_KEY_GROUP_MEDIA},
};

static ui_key_entry entries[MAX_ENTRIES];
static char labels[MAX_ENTRIES][LABEL_LEN];
static char names[MAX_ENTRIES][NAME_LEN];
static size_t entry_count;

static const char *const group_names[UI_KEY_GROUP_COUNT] = {
    "letters",	  "digits", "punctuation",    "function keys",
    "navigation", "keypad", "media & system",
};

/* SDL scancodes match HID usages except for the consumer-control keys. */
static SDL_Scancode scancode_for(unsigned usage)
{
	switch (usage) {
	case 0xB5:
		return SDL_SCANCODE_MEDIA_NEXT_TRACK;
	case 0xB6:
		return SDL_SCANCODE_MEDIA_PREVIOUS_TRACK;
	case 0xB7:
		return SDL_SCANCODE_MEDIA_STOP;
	case 0xB8:
		return SDL_SCANCODE_MEDIA_EJECT;
	case 0xCD:
		return SDL_SCANCODE_MEDIA_PLAY_PAUSE;
	default:
		return (SDL_Scancode)usage;
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
	SDL_Scancode scancode = scancode_for(usage);
	SDL_Keycode keycode = SDL_GetKeyFromScancode(scancode, SDL_KMOD_NONE, false);
	const char *sdl_name = SDL_GetKeyName(keycode);

	for (size_t i = 0; i < ARRAY_LEN(special_labels); i++) {
		if (special_labels[i].usage == usage) {
			snprintf(out, len, "%s", special_labels[i].label);
			return;
		}
	}
	if (sdl_name && *sdl_name && strcasecmp(sdl_name, "Unknown") != 0 &&
	    ascii_only(sdl_name)) {
		snprintf(out, len, "%s", sdl_name);
		return;
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

static unsigned mod_bits(SDL_Keymod mod)
{
	unsigned bits = 0;

	if (mod & (SDL_KMOD_LCTRL | SDL_KMOD_RCTRL))
		bits |= 0x01;
	if (mod & (SDL_KMOD_LSHIFT | SDL_KMOD_RSHIFT))
		bits |= 0x02;
	if (mod & (SDL_KMOD_LALT | SDL_KMOD_RALT))
		bits |= 0x04;
	if (mod & (SDL_KMOD_LGUI | SDL_KMOD_RGUI))
		bits |= 0x08;
	return bits;
}

static bool is_modifier(SDL_Scancode scancode)
{
	switch (scancode) {
	case SDL_SCANCODE_LCTRL:
	case SDL_SCANCODE_RCTRL:
	case SDL_SCANCODE_LSHIFT:
	case SDL_SCANCODE_RSHIFT:
	case SDL_SCANCODE_LALT:
	case SDL_SCANCODE_RALT:
	case SDL_SCANCODE_LGUI:
	case SDL_SCANCODE_RGUI:
		return true;
	default:
		return false;
	}
}

enum ui_key_capture ui_keys_capture(const SDL_Event *event, unsigned *usage,
				    unsigned *mod)
{
	SDL_Scancode scancode;

	if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
		switch (event->button.button) {
		case SDL_BUTTON_LEFT:
			*usage = NAGA_MOUSE_LEFT;
			break;
		case SDL_BUTTON_RIGHT:
			*usage = NAGA_MOUSE_RIGHT;
			break;
		case SDL_BUTTON_MIDDLE:
			*usage = NAGA_MOUSE_MIDDLE;
			break;
		case SDL_BUTTON_X1:
			*usage = NAGA_MOUSE_BACK;
			break;
		case SDL_BUTTON_X2:
			*usage = NAGA_MOUSE_FORWARD;
			break;
		default:
			return UI_KEY_CAPTURE_NONE;
		}
		*mod = 0;
		return UI_KEY_CAPTURE_MOUSE;
	}
	if (event->type != SDL_EVENT_KEY_DOWN || event->key.repeat)
		return UI_KEY_CAPTURE_NONE;

	scancode = event->key.scancode;
	if (scancode == SDL_SCANCODE_ESCAPE)
		return UI_KEY_CAPTURE_CANCEL;
	if (is_modifier(scancode))
		return UI_KEY_CAPTURE_NONE;

	for (size_t i = 0; i < entry_count; i++) {
		if (scancode_for(entries[i].usage) == scancode) {
			*usage = entries[i].usage;
			*mod = mod_bits(event->key.mod);
			return UI_KEY_CAPTURE_KEY;
		}
	}

	/* Unknown but plausible HID usage: keep the raw scancode. */
	if (scancode > 0 && scancode <= 0xFF) {
		*usage = (unsigned)scancode;
		*mod = mod_bits(event->key.mod);
		return UI_KEY_CAPTURE_KEY;
	}
	return UI_KEY_CAPTURE_NONE;
}
