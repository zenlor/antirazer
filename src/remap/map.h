/* SPDX-License-Identifier: GPL-2.0-or-later OR BSD-2-Clause */
#ifndef NAGA_REMAP_MAP_H
#define NAGA_REMAP_MAP_H

#include <stdbool.h>
#include <stddef.h>

#include "naga.h"
#include "remap/arena.h"

enum remap_device {
	REMAP_DEVICE_AUTO,
	REMAP_DEVICE_2020,
	REMAP_DEVICE_2014,
};

enum remap_step_kind {
	REMAP_STEP_KEY,
	REMAP_STEP_TEXT,
	REMAP_STEP_MOUSE,
	REMAP_STEP_DELAY,
};

enum remap_mouse_action {
	REMAP_MOUSE_CLICK,
	REMAP_MOUSE_DOWN,
	REMAP_MOUSE_UP,
};

struct remap_step {
	enum remap_step_kind kind;
	unsigned usage; /* key: HID usage */
	unsigned mod;	/* key: HID modifier bitmap */
	unsigned hold_ms;
	unsigned delay_ms; /* text: gap between characters */
	const char *text;  /* text: arena */
	int mouse;	   /* enum naga_mouse */
	enum remap_mouse_action mouse_action;
};

struct remap_sequence {
	struct remap_step *steps; /* arena */
	size_t count;
};

struct remap_macro {
	const char *name; /* arena */
	struct remap_sequence seq;
};

struct remap_binding {
	char id[32];
	int layer; /* 0 normal, 1 hypershift */
	unsigned ctr;

	/* wheel-up/down: intercepted from REL_WHEEL by the daemon */
	bool host;

	const char *macro; /* macro:<name> reference, or NULL */
	bool is_sequence;
	const struct remap_sequence *seq; /* resolved macro */

	enum naga_assign type; /* single action */
	unsigned usage, mod, turbo_ms, mouse;

	/* filled in by remap_map_assign_triggers() */
	unsigned trigger_usage; /* 2020: HID usage of the trigger key */
	char trigger[32];	/* 2014: Linux key name */
};

struct remap_trigger {
	char id[32];
	int layer;
	char key[32];
};

struct remap_map {
	enum remap_device device;
	struct remap_binding *bindings; /* arena */
	size_t count;
	struct remap_macro *macros; /* arena */
	size_t macro_count;
	struct remap_trigger *triggers; /* arena */
	size_t trigger_count;
	struct arena arena;
};

/* Parse a JSON map. Returns 0 on success; fills err on failure. */
int remap_map_load(const char *path, struct remap_map *map, char *err,
		   size_t errlen);
int remap_map_load_string(const char *text, struct remap_map *map, char *err,
			  size_t errlen);
void remap_map_free(struct remap_map *map);

/*
 * Validate ids/actions and macro references for map->device (which must not be
 * AUTO), then assign the trigger keys used by `run`.
 */
int remap_map_validate(struct remap_map *map, char *err, size_t errlen);
int remap_map_assign_triggers(struct remap_map *map, char *err, size_t errlen);

/* Render a single action the way it is written in the JSON file. */
void remap_binding_string(const struct remap_binding *b, char *buf,
			  size_t len);

/* "ctrl+shift+t" from a modifier bitmap and a HID usage. */
void remap_key_combo(unsigned mod, unsigned usage, char *buf, size_t len);

/*
 * Write single actions and macro trigger keys to the 2020 device.
 * Macros themselves are host-side, so `run` is still needed for those.
 */
int remap_apply(const struct remap_map *map, bool dry_run, char *err,
		size_t errlen);

#endif /* NAGA_REMAP_MAP_H */
