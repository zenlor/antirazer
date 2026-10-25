/* SPDX-License-Identifier: GPL-2.0-or-later OR BSD-2-Clause */
#ifndef NAGA_REMAP_PLAN_H
#define NAGA_REMAP_PLAN_H

#include <stddef.h>
#include <stdint.h>

#include "remap/map.h"

struct remap_event {
	uint32_t at_ms;
	uint16_t type; /* EV_KEY */
	uint16_t code; /* Linux key/button code */
	int32_t value;
};

struct remap_plan {
	struct remap_event *events;
	size_t count;
	uint32_t duration_ms;
};

/*
 * Turn a binding into a timed list of Linux input events. Single actions are
 * a one-step plan; sequences expand key, text, mouse and delay steps.
 *
 * Hypershift and turbo are onboard-only and are rejected here.
 */
int remap_plan_build(const struct remap_binding *b, struct remap_plan *plan,
		     char *err, size_t errlen);
void remap_plan_free(struct remap_plan *plan);

/* HID usage -> Linux evdev key code, or -1. */
int remap_usage_to_keycode(unsigned usage);
/* enum naga_mouse -> Linux BTN_* code. */
int remap_mouse_to_keycode(unsigned mouse);

#endif /* NAGA_REMAP_PLAN_H */
