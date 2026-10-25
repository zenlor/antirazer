/* SPDX-License-Identifier: GPL-2.0-or-later OR BSD-2-Clause */
/*
 * naga_keys.c — HID usage / modifier naming used by the CLI and the UI.
 *
 * Usages follow the HID Usage Tables, Keyboard/Keypad page (0x07): letters
 * 0x04..0x1D, digits 0x1E..0x27, F1..F12 0x3A..0x45, F13..F24 0x68..0x73.
 */
#include "naga.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

static const struct key_name {
	const char *name;
	uint8_t usage;
} key_names[] = {
    /* editing / navigation */
    {"enter", 0x28},
    {"esc", 0x29},
    {"backspace", 0x2A},
    {"tab", 0x2B},
    {"space", 0x2C},
    {"caps", 0x39},
    {"print", 0x46},
    {"scroll", 0x47},
    {"pause", 0x48},
    {"insert", 0x49},
    {"home", 0x4A},
    {"pgup", 0x4B},
    {"delete", 0x4C},
    {"end", 0x4D},
    {"pgdn", 0x4E},
    {"right", 0x4F},
    {"left", 0x50},
    {"down", 0x51},
    {"up", 0x52},

    /* punctuation */
    {"minus", 0x2D},
    {"equal", 0x2E},
    {"lbracket", 0x2F},
    {"rbracket", 0x30},
    {"bslash", 0x31},
    {"hash", 0x32},
    {"semicolon", 0x33},
    {"quote", 0x34},
    {"grave", 0x35},
    {"comma", 0x36},
    {"dot", 0x37},
    {"slash", 0x38},

    /* keypad */
    {"numlock", 0x53},
    {"kpdiv", 0x54},
    {"kpmul", 0x55},
    {"kpsub", 0x56},
    {"kpadd", 0x57},
    {"kpenter", 0x58},
    {"kp1", 0x59},
    {"kp2", 0x5A},
    {"kp3", 0x5B},
    {"kp4", 0x5C},
    {"kp5", 0x5D},
    {"kp6", 0x5E},
    {"kp7", 0x5F},
    {"kp8", 0x60},
    {"kp9", 0x61},
    {"kp0", 0x62},
    {"kpdot", 0x63},
    {"kpequal", 0x67},
    {"kpcomma", 0x85},

    /* system / media */
    {"application", 0x65},
    {"power", 0x66},
    {"mute", 0x7F},
    {"volup", 0x80},
    {"voldown", 0x81},
    {"next", 0xB5},
    {"prev", 0xB6},
    {"stop", 0xB7},
    {"eject", 0xB8},
    {"playpause", 0xCD},
};

static const struct mod_name {
	const char *name;
	uint8_t bit;
} mod_names[] = {
    {"ctrl", 0x01},   {"control", 0x01}, {"shift", 0x02},  {"alt", 0x04},
    {"super", 0x08},  {"meta", 0x08},	 {"gui", 0x08},	   {"rctrl", 0x10},
    {"rshift", 0x20}, {"ralt", 0x40},	 {"rsuper", 0x80},
};

const char *naga_keyname(int usage)
{
	static char buf[8];

	if (usage >= 0x04 && usage <= 0x1D) {
		buf[0] = (char)('a' + usage - 0x04);
		buf[1] = '\0';
		return buf;
	}
	if (usage >= 0x1E && usage <= 0x26) {
		buf[0] = (char)('1' + usage - 0x1E);
		buf[1] = '\0';
		return buf;
	}
	if (usage == 0x27)
		return "0";
	if (usage >= 0x3A && usage <= 0x45) {
		snprintf(buf, sizeof buf, "f%d", usage - 0x3A + 1);
		return buf;
	}
	if (usage >= 0x68 && usage <= 0x73) {
		snprintf(buf, sizeof buf, "f%d", usage - 0x68 + 13);
		return buf;
	}
	for (size_t i = 0; i < ARRAY_LEN(key_names); i++)
		if (key_names[i].usage == usage)
			return key_names[i].name;
	return NULL;
}

int naga_parse_usage(const char *s)
{
	unsigned v;

	if (!s || !*s)
		return -1;

	if (!s[1]) {
		if (s[0] >= 'a' && s[0] <= 'z')
			return 0x04 + s[0] - 'a';
		if (s[0] >= 'A' && s[0] <= 'Z')
			return 0x04 + s[0] - 'A';
		if (s[0] >= '1' && s[0] <= '9')
			return 0x1E + s[0] - '1';
		if (s[0] == '0')
			return 0x27;
	}

	/* f1..f24 before the hex fallback, so "f5" is the key and not 0xF5 */
	if (s[0] == 'f' || s[0] == 'F') {
		char *end;
		long n = strtol(s + 1, &end, 10);
		if (end != s + 1 && *end == '\0' && n >= 1 && n <= 24)
			return (int)(n <= 12 ? 0x3A + n - 1 : 0x68 + n - 13);
	}

	for (size_t i = 0; i < ARRAY_LEN(key_names); i++)
		if (!strcasecmp(s, key_names[i].name))
			return key_names[i].usage;

	{
		int used = 0;
		if (sscanf(s, "%x%n", &v, &used) == 1 && used == (int)strlen(s) &&
		    v <= 0xFF)
			return (int)v;
	}
	return -1;
}

int naga_parse_mod(const char *s)
{
	char buf[128];
	int mod = 0;

	if (!s || !*s)
		return 0;
	if (strlen(s) >= sizeof buf)
		return -1;
	strcpy(buf, s);

	for (char *tok = strtok(buf, "+"); tok; tok = strtok(NULL, "+")) {
		int hit = -1;

		for (size_t i = 0; i < ARRAY_LEN(mod_names); i++) {
			if (!strcasecmp(tok, mod_names[i].name)) {
				hit = mod_names[i].bit;
				break;
			}
		}
		if (hit < 0) {
			int used = 0;
			unsigned v;
			if (sscanf(tok, "%x%n", &v, &used) == 1 &&
			    used == (int)strlen(tok) && v <= 0xFF)
				hit = (int)v;
		}
		if (hit < 0)
			return -1;
		mod |= hit;
	}
	return mod;
}

static const struct mouse_name {
	const char *name;
	uint8_t id;
} mouse_names[] = {
    {"left", NAGA_MOUSE_LEFT},	     {"right", NAGA_MOUSE_RIGHT},
    {"middle", NAGA_MOUSE_MIDDLE},   {"back", NAGA_MOUSE_BACK},
    {"forward", NAGA_MOUSE_FORWARD},
};

const char *naga_mouse_name(int button)
{
	for (size_t i = 0; i < ARRAY_LEN(mouse_names); i++)
		if (mouse_names[i].id == button)
			return mouse_names[i].name;
	return NULL;
}

int naga_parse_mouse(const char *s)
{
	int used = 0, v;

	if (!s || !*s)
		return -1;
	if (!strncasecmp(s, "mouse:", 6))
		s += 6;
	for (size_t i = 0; i < ARRAY_LEN(mouse_names); i++)
		if (!strcasecmp(s, mouse_names[i].name))
			return mouse_names[i].id;
	if (sscanf(s, "%d%n", &v, &used) == 1 && used == (int)strlen(s) && v >= 1 &&
	    v <= 0xFF)
		return v;
	return -1;
}

static size_t put_mods(char *buf, size_t len, unsigned mod)
{
	static const char *const names[] = {
	    "ctrl+",  "shift+",	 "alt+",  "super+",
	    "rctrl+", "rshift+", "ralt+", "rsuper+",
	};
	size_t off = 0;

	for (int i = 0; i < 8; i++)
		if ((mod & (1u << i)) && off + 1 < len)
			off += (size_t)snprintf(buf + off, len - off, "%s", names[i]);
	return off;
}

const char *naga_binding_name(const naga_rec *rec, char *buf, size_t len)
{
	const char *key;
	size_t off;

	if (!buf || !len)
		return "";
	buf[0] = '\0';

	if (!rec || !rec->valid) {
		snprintf(buf, len, "?");
		return buf;
	}

	switch (rec->type) {
	case NAGA_ASSIGN_DEFAULT:
		snprintf(buf, len, "default");
		break;
	case NAGA_ASSIGN_KEY:
		off = put_mods(buf, len, rec->mod);
		key = naga_keyname(rec->usage);
		if (key)
			snprintf(buf + off, len - off, "%s", key);
		else
			snprintf(buf + off, len - off, "0x%02x", rec->usage);
		break;
	case NAGA_ASSIGN_MOUSE: {
		const char *name = naga_mouse_name(rec->mouse);

		if (name)
			snprintf(buf, len, "mouse %s", name);
		else
			snprintf(buf, len, "mouse %02x", rec->mouse);
		break;
	}
	case NAGA_ASSIGN_HYPERSHIFT:
		snprintf(buf, len, "hypershift");
		break;
	case NAGA_ASSIGN_DPI:
		snprintf(buf, len, "dpi function %u", rec->param);
		break;
	case NAGA_ASSIGN_PROFILE:
		snprintf(buf, len, "profile function %02x", rec->param);
		break;
	case NAGA_ASSIGN_TILT:
		snprintf(buf, len, "wheel tilt");
		break;
	case NAGA_ASSIGN_TURBO:
		off = put_mods(buf, len, rec->mod);
		key = naga_keyname(rec->usage);
		if (key)
			snprintf(buf + off, len - off, "turbo %s", key);
		else
			snprintf(buf + off, len - off, "turbo 0x%02x", rec->usage);
		if (rec->rate)
			snprintf(buf + strlen(buf), len - strlen(buf), " @%ums",
				 1000000u / rec->rate);
		break;
	default:
		snprintf(buf, len, "type %02X", rec->type);
		break;
	}
	return buf;
}
