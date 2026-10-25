/* SPDX-License-Identifier: GPL-2.0-or-later OR BSD-2-Clause */
#include "remap/plan.h"

#include <linux/input-event-codes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void seterr(char *err, size_t errlen, const char *fmt, ...)
{
	va_list ap;

	if (!err || !errlen)
		return;
	va_start(ap, fmt);
	vsnprintf(err, errlen, fmt, ap);
	va_end(ap);
}

int remap_usage_to_keycode(unsigned usage)
{
	static const int letters[] = {
		KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H,
		KEY_I, KEY_J, KEY_K, KEY_L, KEY_M, KEY_N, KEY_O, KEY_P,
		KEY_Q, KEY_R, KEY_S, KEY_T, KEY_U, KEY_V, KEY_W, KEY_X,
		KEY_Y, KEY_Z,
	};

	if (usage >= 0x04 && usage <= 0x1D)
		return letters[usage - 0x04];
	if (usage >= 0x1E && usage <= 0x26)
		return KEY_1 + (int)(usage - 0x1E);
	if (usage == 0x27)
		return KEY_0;
	if (usage >= 0x3A && usage <= 0x45)
		return KEY_F1 + (int)(usage - 0x3A);
	if (usage >= 0x68 && usage <= 0x73)
		return KEY_F13 + (int)(usage - 0x68);
	if (usage >= 0x59 && usage <= 0x61)
		return KEY_KP1 + (int)(usage - 0x59);
	if (usage >= 0xE0 && usage <= 0xE7) {
		static const int mods[] = {
			KEY_LEFTCTRL,  KEY_LEFTSHIFT, KEY_LEFTALT,  KEY_LEFTMETA,
			KEY_RIGHTCTRL, KEY_RIGHTSHIFT, KEY_RIGHTALT, KEY_RIGHTMETA,
		};

		return mods[usage - 0xE0];
	}

	switch (usage) {
	case 0x28:
		return KEY_ENTER;
	case 0x29:
		return KEY_ESC;
	case 0x2A:
		return KEY_BACKSPACE;
	case 0x2B:
		return KEY_TAB;
	case 0x2C:
		return KEY_SPACE;
	case 0x2D:
		return KEY_MINUS;
	case 0x2E:
		return KEY_EQUAL;
	case 0x2F:
		return KEY_LEFTBRACE;
	case 0x30:
		return KEY_RIGHTBRACE;
	case 0x31:
		return KEY_BACKSLASH;
	case 0x32:
		return KEY_102ND;
	case 0x33:
		return KEY_SEMICOLON;
	case 0x34:
		return KEY_APOSTROPHE;
	case 0x35:
		return KEY_GRAVE;
	case 0x36:
		return KEY_COMMA;
	case 0x37:
		return KEY_DOT;
	case 0x38:
		return KEY_SLASH;
	case 0x39:
		return KEY_CAPSLOCK;
	case 0x46:
		return KEY_SYSRQ;
	case 0x47:
		return KEY_SCROLLLOCK;
	case 0x48:
		return KEY_PAUSE;
	case 0x49:
		return KEY_INSERT;
	case 0x4A:
		return KEY_HOME;
	case 0x4B:
		return KEY_PAGEUP;
	case 0x4C:
		return KEY_DELETE;
	case 0x4D:
		return KEY_END;
	case 0x4E:
		return KEY_PAGEDOWN;
	case 0x4F:
		return KEY_RIGHT;
	case 0x50:
		return KEY_LEFT;
	case 0x51:
		return KEY_DOWN;
	case 0x52:
		return KEY_UP;
	case 0x53:
		return KEY_NUMLOCK;
	case 0x54:
		return KEY_KPSLASH;
	case 0x55:
		return KEY_KPASTERISK;
	case 0x56:
		return KEY_KPMINUS;
	case 0x57:
		return KEY_KPPLUS;
	case 0x58:
		return KEY_KPENTER;
	case 0x62:
		return KEY_KP0;
	case 0x63:
		return KEY_KPDOT;
	case 0x65:
		return KEY_COMPOSE;
	case 0x67:
		return KEY_KPEQUAL;
	case 0x7F:
		return KEY_MUTE;
	case 0x80:
		return KEY_VOLUMEUP;
	case 0x81:
		return KEY_VOLUMEDOWN;
	case 0xB5:
		return KEY_NEXTSONG;
	case 0xB6:
		return KEY_PREVIOUSSONG;
	case 0xB7:
		return KEY_STOPCD;
	case 0xB8:
		return KEY_EJECTCD;
	case 0xCD:
		return KEY_PLAYPAUSE;
	default:
		return -1;
	}
}

int remap_mouse_to_keycode(unsigned mouse)
{
	switch (mouse) {
	case NAGA_MOUSE_LEFT:
		return BTN_LEFT;
	case NAGA_MOUSE_RIGHT:
		return BTN_RIGHT;
	case NAGA_MOUSE_MIDDLE:
		return BTN_MIDDLE;
	case NAGA_MOUSE_BACK:
		return BTN_SIDE;
	case NAGA_MOUSE_FORWARD:
		return BTN_EXTRA;
	default:
		return -1;
	}
}

static int mod_keycode(unsigned bit)
{
	switch (bit) {
	case 0x01:
		return KEY_LEFTCTRL;
	case 0x02:
		return KEY_LEFTSHIFT;
	case 0x04:
		return KEY_LEFTALT;
	case 0x08:
		return KEY_LEFTMETA;
	case 0x10:
		return KEY_RIGHTCTRL;
	case 0x20:
		return KEY_RIGHTSHIFT;
	case 0x40:
		return KEY_RIGHTALT;
	case 0x80:
		return KEY_RIGHTMETA;
	default:
		return -1;
	}
}

static int add_event(struct remap_plan *plan, uint32_t at, uint16_t type,
		     uint16_t code, int32_t value, char *err, size_t errlen)
{
	if (plan->count == plan->capacity) {
		size_t cap = plan->capacity ? plan->capacity * 2 : 32;
		struct remap_event *events;

		if (cap > SIZE_MAX / sizeof *events) {
			seterr(err, errlen, "plan is too large");
			return -1;
		}
		events = arena_alloc(&plan->arena, cap * sizeof *events,
				     _Alignof(struct remap_event));
		if (!events) {
			seterr(err, errlen, "out of memory");
			return -1;
		}
		if (plan->events)
			memcpy(events, plan->events,
			       plan->count * sizeof *events);
		plan->events = events;
		plan->capacity = cap;
	}
	plan->events[plan->count].at_ms = at;
	plan->events[plan->count].type = type;
	plan->events[plan->count].code = code;
	plan->events[plan->count].value = value;
	plan->count++;
	return 0;
}

static int add_key(struct remap_plan *plan, uint32_t at, unsigned usage,
		   unsigned mod, unsigned hold, char *err, size_t errlen)
{
	int code = remap_usage_to_keycode(usage);
	int mods[8];
	int nmods = 0;

	if (code < 0) {
		seterr(err, errlen, "key usage %02x has no Linux code", usage);
		return -1;
	}
	for (unsigned bit = 0; bit < 8; bit++) {
		if (!(mod & (1u << bit)))
			continue;
		mods[nmods] = mod_keycode(1u << bit);
		if (mods[nmods] < 0) {
			seterr(err, errlen, "bad modifier bit %02x", 1u << bit);
			return -1;
		}
		if (add_event(plan, at, EV_KEY, (uint16_t)mods[nmods], 1, err,
			      errlen) < 0)
			return -1;
		nmods++;
	}
	if (add_event(plan, at, EV_KEY, (uint16_t)code, 1, err, errlen) < 0)
		return -1;
	if (add_event(plan, at + hold, EV_KEY, (uint16_t)code, 0, err, errlen) < 0)
		return -1;
	for (int i = nmods - 1; i >= 0; i--)
		if (add_event(plan, at + hold, EV_KEY, (uint16_t)mods[i], 0, err,
			      errlen) < 0)
			return -1;
	return 0;
}

static int text_char(unsigned char c, unsigned *usage, unsigned *mod)
{
	*mod = 0;
	if (c >= 'a' && c <= 'z') {
		*usage = 0x04 + (c - 'a');
		return 0;
	}
	if (c >= 'A' && c <= 'Z') {
		*usage = 0x04 + (c - 'A');
		*mod = 0x02;
		return 0;
	}
	if (c >= '1' && c <= '9') {
		*usage = 0x1E + (c - '1');
		return 0;
	}
	if (c == '0') {
		*usage = 0x27;
		return 0;
	}

	switch (c) {
	case ' ':
		*usage = 0x2C;
		return 0;
	case '\n':
		*usage = 0x28;
		return 0;
	case '\t':
		*usage = 0x2B;
		return 0;
	case '\b':
		*usage = 0x2A;
		return 0;
	case '-':
		*usage = 0x2D;
		return 0;
	case '=':
		*usage = 0x2E;
		return 0;
	case '[':
		*usage = 0x2F;
		return 0;
	case ']':
		*usage = 0x30;
		return 0;
	case '\\':
		*usage = 0x31;
		return 0;
	case ';':
		*usage = 0x33;
		return 0;
	case '\'':
		*usage = 0x34;
		return 0;
	case '`':
		*usage = 0x35;
		return 0;
	case ',':
		*usage = 0x36;
		return 0;
	case '.':
		*usage = 0x37;
		return 0;
	case '/':
		*usage = 0x38;
		return 0;
	case '!':
		*usage = 0x1E;
		*mod = 0x02;
		return 0;
	case '@':
		*usage = 0x1F;
		*mod = 0x02;
		return 0;
	case '#':
		*usage = 0x20;
		*mod = 0x02;
		return 0;
	case '$':
		*usage = 0x21;
		*mod = 0x02;
		return 0;
	case '%':
		*usage = 0x22;
		*mod = 0x02;
		return 0;
	case '^':
		*usage = 0x23;
		*mod = 0x02;
		return 0;
	case '&':
		*usage = 0x24;
		*mod = 0x02;
		return 0;
	case '*':
		*usage = 0x25;
		*mod = 0x02;
		return 0;
	case '(':
		*usage = 0x26;
		*mod = 0x02;
		return 0;
	case ')':
		*usage = 0x27;
		*mod = 0x02;
		return 0;
	case '_':
		*usage = 0x2D;
		*mod = 0x02;
		return 0;
	case '+':
		*usage = 0x2E;
		*mod = 0x02;
		return 0;
	case '{':
		*usage = 0x2F;
		*mod = 0x02;
		return 0;
	case '}':
		*usage = 0x30;
		*mod = 0x02;
		return 0;
	case '|':
		*usage = 0x31;
		*mod = 0x02;
		return 0;
	case ':':
		*usage = 0x33;
		*mod = 0x02;
		return 0;
	case '"':
		*usage = 0x34;
		*mod = 0x02;
		return 0;
	case '~':
		*usage = 0x35;
		*mod = 0x02;
		return 0;
	case '<':
		*usage = 0x36;
		*mod = 0x02;
		return 0;
	case '>':
		*usage = 0x37;
		*mod = 0x02;
		return 0;
	case '?':
		*usage = 0x38;
		*mod = 0x02;
		return 0;
	default:
		return -1;
	}
}

static int add_mouse(struct remap_plan *plan, uint32_t at, unsigned mouse,
		     enum remap_mouse_action action, unsigned hold, char *err,
		     size_t errlen)
{
	int code = remap_mouse_to_keycode(mouse);

	if (code < 0) {
		seterr(err, errlen, "unknown mouse button %u", mouse);
		return -1;
	}
	if (action != REMAP_MOUSE_UP &&
	    add_event(plan, at, EV_KEY, (uint16_t)code, 1, err, errlen) < 0)
		return -1;
	if (action != REMAP_MOUSE_DOWN &&
	    add_event(plan, at + hold, EV_KEY, (uint16_t)code, 0, err, errlen) <
		0)
		return -1;
	return 0;
}

int remap_plan_build(const struct remap_binding *b, struct remap_plan *plan,
		     char *err, size_t errlen)
{
	uint32_t t = 0;

	memset(plan, 0, sizeof *plan);
	arena_init(&plan->arena, 8192);

	if (!b->is_sequence) {
		switch (b->type) {
		case NAGA_ASSIGN_KEY:
			if (add_key(plan, 0, b->usage, b->mod, 20, err,
				    errlen) < 0)
				goto fail;
			t = 20;
			break;
		case NAGA_ASSIGN_MOUSE:
			if (add_mouse(plan, 0, b->mouse, REMAP_MOUSE_CLICK, 20,
				      err, errlen) < 0)
				goto fail;
			t = 20;
			break;
		default:
			seterr(err, errlen,
			       "%s is onboard-only and cannot be run host-side",
			       b->type == NAGA_ASSIGN_HYPERSHIFT ? "hypershift" :
			       b->type == NAGA_ASSIGN_TURBO ? "turbo" :
			       b->type == NAGA_ASSIGN_DEFAULT ? "default" :
			       "this action");
			goto fail;
		}
	} else {
		if (!b->seq) {
			seterr(err, errlen, "macro \"%s\" is not resolved",
			       b->macro ? b->macro : b->id);
			goto fail;
		}
		for (size_t i = 0; i < b->seq->count; i++) {
			const struct remap_step *s = &b->seq->steps[i];

			switch (s->kind) {
			case REMAP_STEP_KEY:
				if (add_key(plan, t, s->usage, s->mod,
					    s->hold_ms, err, errlen) < 0)
					goto fail;
				t += s->hold_ms;
				break;
			case REMAP_STEP_TEXT:
				for (const unsigned char *p = (const unsigned char *)
					     s->text;
				     *p; p++) {
					unsigned usage, mod;

					if (text_char(*p, &usage, &mod) < 0) {
						seterr(err, errlen,
						       "text: no US mapping for byte 0x%02x",
						       *p);
						goto fail;
					}
					if (add_key(plan, t, usage, mod,
						    s->hold_ms, err, errlen) < 0)
						goto fail;
					t += s->hold_ms + s->delay_ms;
				}
				break;
			case REMAP_STEP_MOUSE:
				if (add_mouse(plan, t, (unsigned)s->mouse,
					      s->mouse_action, s->hold_ms, err,
					      errlen) < 0)
					goto fail;
				t += s->hold_ms;
				break;
			case REMAP_STEP_DELAY:
				t += s->delay_ms;
				break;
			}
		}
	}

	plan->duration_ms = t;
	return 0;
fail:
	remap_plan_free(plan);
	return -1;
}

void remap_plan_free(struct remap_plan *plan)
{
	arena_free(&plan->arena);
	plan->events = NULL;
	plan->count = 0;
	plan->capacity = 0;
	plan->duration_ms = 0;
}
