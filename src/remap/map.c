/* SPDX-License-Identifier: GPL-2.0-or-later OR BSD-2-Clause */
#include "remap/map.h"

#include <errno.h>
#include <jansson.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_BINDINGS 256
#define MAX_STEPS 512
#define MAX_TEXT 1024

static void seterr(char *err, size_t errlen, const char *fmt, ...)
{
	va_list ap;

	if (!err || !errlen)
		return;
	va_start(ap, fmt);
	vsnprintf(err, errlen, fmt, ap);
	va_end(ap);
}

static int parse_key_combo(const char *text, unsigned *usage, unsigned *mod,
			   char *err, size_t errlen)
{
	char buf[128];
	char *plus;
	int value;

	if (!text || !*text || strlen(text) >= sizeof buf) {
		seterr(err, errlen, "empty or overlong key \"%s\"", text ? text : "");
		return -1;
	}
	strcpy(buf, text);
	*mod = 0;

	plus = strrchr(buf, '+');
	if (plus) {
		*plus = '\0';
		value = naga_parse_mod(buf);
		if (value < 0) {
			seterr(err, errlen, "bad modifier list \"%s\"", buf);
			return -1;
		}
		*mod = (unsigned)value;
		text = plus + 1;
	}

	value = naga_parse_usage(text);
	if (value < 0) {
		seterr(err, errlen, "unknown key \"%s\"", text);
		return -1;
	}
	*usage = (unsigned)value;
	return 0;
}

static int parse_plain_key(const char *text, unsigned *usage, char *err,
			   size_t errlen)
{
	unsigned mod;
	int value;

	if (!text || !*text || strchr(text, '+')) {
		seterr(err, errlen, "trigger must be a single key, got \"%s\"",
		       text ? text : "");
		return -1;
	}
	value = naga_parse_usage(text);
	if (value < 0) {
		seterr(err, errlen, "unknown trigger key \"%s\"", text);
		return -1;
	}
	(void)mod;
	*usage = (unsigned)value;
	return 0;
}

static int parse_action(const char *text, struct remap_binding *b, char *err,
			size_t errlen)
{
	if (!strcmp(text, "default")) {
		b->type = NAGA_ASSIGN_DEFAULT;
		return 0;
	}
	if (!strcmp(text, "hypershift")) {
		b->type = NAGA_ASSIGN_HYPERSHIFT;
		return 0;
	}
	if (!strncmp(text, "mouse:", 6)) {
		int button = naga_parse_mouse(text + 6);

		if (button < 0) {
			seterr(err, errlen, "unknown mouse button \"%s\"",
			       text + 6);
			return -1;
		}
		b->type = NAGA_ASSIGN_MOUSE;
		b->mouse = (unsigned)button;
		return 0;
	}
	if (!strncmp(text, "turbo:", 6)) {
		char combo[96];
		const char *at = strchr(text + 6, '@');
		char *end;
		unsigned long ms;

		if (!at || (size_t)(at - (text + 6)) >= sizeof combo) {
			seterr(err, errlen, "turbo needs turbo:<key>@<ms>");
			return -1;
		}
		memcpy(combo, text + 6, (size_t)(at - (text + 6)));
		combo[at - (text + 6)] = '\0';
		if (parse_key_combo(combo, &b->usage, &b->mod, err, errlen) < 0)
			return -1;
		errno = 0;
		ms = strtoul(at + 1, &end, 10);
		if (errno || *end || ms < 1 || ms > 60000) {
			seterr(err, errlen, "turbo delay must be 1..60000 ms");
			return -1;
		}
		b->type = NAGA_ASSIGN_TURBO;
		b->turbo_ms = (unsigned)ms;
		return 0;
	}

	b->type = NAGA_ASSIGN_KEY;
	return parse_key_combo(text, &b->usage, &b->mod, err, errlen);
}

static int get_ms(json_t *obj, const char *name, unsigned def, unsigned *out,
		  char *err, size_t errlen)
{
	json_t *v = json_object_get(obj, name);

	if (!v) {
		*out = def;
		return 0;
	}
	if (!json_is_integer(v) || json_integer_value(v) < 0 ||
	    json_integer_value(v) > 60000) {
		seterr(err, errlen, "%s must be an integer 0..60000", name);
		return -1;
	}
	*out = (unsigned)json_integer_value(v);
	return 0;
}

static int parse_mods(json_t *obj, unsigned *mod, char *err, size_t errlen)
{
	json_t *v = json_object_get(obj, "mods");
	size_t i;
	json_t *item;

	*mod = 0;
	if (!v)
		return 0;
	if (!json_is_array(v)) {
		seterr(err, errlen, "\"mods\" must be an array");
		return -1;
	}
	json_array_foreach(v, i, item) {
		int bits;

		if (!json_is_string(item)) {
			seterr(err, errlen, "\"mods\" entries must be strings");
			return -1;
		}
		bits = naga_parse_mod(json_string_value(item));
		if (bits < 0) {
			seterr(err, errlen, "unknown modifier \"%s\"",
			       json_string_value(item));
			return -1;
		}
		*mod |= (unsigned)bits;
	}
	return 0;
}

static int parse_mouse_action(const char *s, enum remap_mouse_action *out,
			      char *err, size_t errlen)
{
	if (!s || !strcmp(s, "click")) {
		*out = REMAP_MOUSE_CLICK;
		return 0;
	}
	if (!strcmp(s, "down")) {
		*out = REMAP_MOUSE_DOWN;
		return 0;
	}
	if (!strcmp(s, "up")) {
		*out = REMAP_MOUSE_UP;
		return 0;
	}
	seterr(err, errlen, "mouse action must be click, down or up");
	return -1;
}

static int parse_step(json_t *obj, struct remap_step *step, char *err,
		      size_t errlen)
{
	json_t *v;
	int actions = 0;

	if (!json_is_object(obj)) {
		seterr(err, errlen, "sequence steps must be objects");
		return -1;
	}
	memset(step, 0, sizeof *step);

	if ((v = json_object_get(obj, "delay"))) {
		actions++;
		if (!json_is_integer(v) || json_integer_value(v) < 0 ||
		    json_integer_value(v) > 60000) {
			seterr(err, errlen, "delay must be 0..60000 ms");
			return -1;
		}
		step->kind = REMAP_STEP_DELAY;
		step->delay_ms = (unsigned)json_integer_value(v);
	}

	if ((v = json_object_get(obj, "key"))) {
		actions++;
		if (!json_is_string(v)) {
			seterr(err, errlen, "\"key\" must be a string");
			return -1;
		}
		step->kind = REMAP_STEP_KEY;
		if (parse_key_combo(json_string_value(v), &step->usage,
				    &step->mod, err, errlen) < 0)
			return -1;
		if (parse_mods(obj, &step->mod, err, errlen) < 0)
			return -1;
		if (get_ms(obj, "hold_ms", 20, &step->hold_ms, err, errlen) < 0)
			return -1;
	}

	if ((v = json_object_get(obj, "text"))) {
		actions++;
		if (!json_is_string(v) || !*json_string_value(v)) {
			seterr(err, errlen, "\"text\" must be a non-empty string");
			return -1;
		}
		if (strlen(json_string_value(v)) > MAX_TEXT) {
			seterr(err, errlen, "text is too long");
			return -1;
		}
		step->kind = REMAP_STEP_TEXT;
		step->text = strdup(json_string_value(v));
		if (!step->text) {
			seterr(err, errlen, "out of memory");
			return -1;
		}
		if (get_ms(obj, "hold_ms", 10, &step->hold_ms, err, errlen) < 0)
			goto fail;
		if (get_ms(obj, "delay_ms", 20, &step->delay_ms, err, errlen) < 0)
			goto fail;
	}

	if ((v = json_object_get(obj, "mouse"))) {
		int button;

		actions++;
		if (!json_is_string(v)) {
			seterr(err, errlen, "\"mouse\" must be a string");
			goto fail;
		}
		button = naga_parse_mouse(json_string_value(v));
		if (button < 0) {
			seterr(err, errlen, "unknown mouse button \"%s\"",
			       json_string_value(v));
			goto fail;
		}
		step->kind = REMAP_STEP_MOUSE;
		step->mouse = button;
		v = json_object_get(obj, "action");
		if (v && !json_is_string(v)) {
			seterr(err, errlen, "mouse action must be a string");
			goto fail;
		}
		if (parse_mouse_action(v ? json_string_value(v) : NULL,
				       &step->mouse_action, err, errlen) < 0)
			goto fail;
		if (get_ms(obj, "hold_ms", 20, &step->hold_ms, err, errlen) < 0)
			goto fail;
	}

	if (actions != 1) {
		seterr(err, errlen,
		       "step needs exactly one of key, text, mouse or delay");
		goto fail;
	}
	return 0;
fail:
	free(step->text);
	step->text = NULL;
	return -1;
}

static int parse_sequence(json_t *arr, struct remap_sequence *seq, char *err,
			  size_t errlen)
{
	size_t i;
	json_t *item;

	if (!json_is_array(arr) || json_array_size(arr) == 0) {
		seterr(err, errlen, "sequence must be a non-empty array");
		return -1;
	}
	if (json_array_size(arr) > MAX_STEPS) {
		seterr(err, errlen, "too many sequence steps (max %d)",
		       MAX_STEPS);
		return -1;
	}
	seq->steps = calloc(json_array_size(arr), sizeof *seq->steps);
	if (!seq->steps) {
		seterr(err, errlen, "out of memory");
		return -1;
	}
	json_array_foreach(arr, i, item) {
		if (parse_step(item, &seq->steps[i], err, errlen) < 0) {
			for (size_t j = 0; j < i; j++)
				free(seq->steps[j].text);
			free(seq->steps);
			seq->steps = NULL;
			return -1;
		}
		seq->count++;
	}
	return 0;
}

static int split_layer(const char *raw, char *id, size_t idlen, int *layer,
		       char *err, size_t errlen)
{
	const char *at = strchr(raw, '@');
	size_t n = at ? (size_t)(at - raw) : strlen(raw);

	if (n == 0 || n >= idlen) {
		seterr(err, errlen, "bad binding id \"%s\"", raw);
		return -1;
	}
	memcpy(id, raw, n);
	id[n] = '\0';
	*layer = 0;
	if (!at)
		return 0;
	if (!strcmp(at + 1, "hypershift") || !strcmp(at + 1, "1")) {
		*layer = 1;
		return 0;
	}
	if (!strcmp(at + 1, "normal") || !strcmp(at + 1, "0"))
		return 0;
	seterr(err, errlen, "layer must be normal/hypershift or 0/1");
	return -1;
}

static int parse_binding(const char *raw, json_t *value,
			 struct remap_binding *b, char *err, size_t errlen)
{
	memset(b, 0, sizeof *b);
	if (split_layer(raw, b->id, sizeof b->id, &b->layer, err, errlen) < 0)
		return -1;

	if (json_is_string(value)) {
		return parse_action(json_string_value(value), b, err, errlen);
	}
	if (json_is_object(value)) {
		json_t *seq = json_object_get(value, "sequence");

		if (!seq) {
			seterr(err, errlen,
			       "binding \"%s\" needs a sequence or a string",
			       raw);
			return -1;
		}
		b->is_sequence = true;
		return parse_sequence(seq, &b->seq, err, errlen);
	}
	seterr(err, errlen, "binding \"%s\" must be a string or object", raw);
	return -1;
}

int remap_map_load_string(const char *text, struct remap_map *map, char *err,
			  size_t errlen)
{
	json_error_t jerr;
	json_t *root = NULL;
	json_t *bindings = NULL;
	json_t *triggers = NULL;
	json_t *v;
	const char *key;
	size_t index = 0;

	memset(map, 0, sizeof *map);
	map->device = REMAP_DEVICE_AUTO;

	root = json_loads(text, 0, &jerr);
	if (!root) {
		seterr(err, errlen, "JSON: %s (line %d)", jerr.text,
		       jerr.line);
		return -1;
	}
	if (!json_is_object(root)) {
		seterr(err, errlen, "top level must be an object");
		goto fail;
	}

	v = json_object_get(root, "device");
	if (v) {
		const char *s;

		if (!json_is_string(v)) {
			seterr(err, errlen, "\"device\" must be a string");
			goto fail;
		}
		s = json_string_value(v);
		if (!strcmp(s, "auto"))
			map->device = REMAP_DEVICE_AUTO;
		else if (!strcmp(s, "2020") || !strcmp(s, "naga-2020") ||
			 !strcmp(s, "naga-lh-2020"))
			map->device = REMAP_DEVICE_2020;
		else if (!strcmp(s, "2014") || !strcmp(s, "naga-2014"))
			map->device = REMAP_DEVICE_2014;
		else {
			seterr(err, errlen, "unknown device \"%s\"", s);
			goto fail;
		}
	}

	triggers = json_object_get(root, "triggers");
	if (triggers) {
		if (!json_is_object(triggers)) {
			seterr(err, errlen, "\"triggers\" must be an object");
			goto fail;
		}
		map->triggers = calloc(json_object_size(triggers),
				       sizeof *map->triggers);
		if (!map->triggers) {
			seterr(err, errlen, "out of memory");
			goto fail;
		}
		json_object_foreach(triggers, key, v) {
			if (!json_is_string(v) ||
			    strlen(key) >= sizeof map->triggers[0].id ||
			    strlen(json_string_value(v)) >=
				    sizeof map->triggers[0].key) {
				seterr(err, errlen, "bad trigger \"%s\"", key);
				goto fail;
			}
			strcpy(map->triggers[map->trigger_count].id, key);
			strcpy(map->triggers[map->trigger_count].key,
			       json_string_value(v));
			map->trigger_count++;
		}
	}

	bindings = json_object_get(root, "bindings");
	if (!json_is_object(bindings) || json_object_size(bindings) == 0) {
		seterr(err, errlen, "\"bindings\" must be a non-empty object");
		goto fail;
	}
	if (json_object_size(bindings) > MAX_BINDINGS) {
		seterr(err, errlen, "too many bindings (max %d)", MAX_BINDINGS);
		goto fail;
	}
	map->bindings = calloc(json_object_size(bindings),
			       sizeof *map->bindings);
	if (!map->bindings) {
		seterr(err, errlen, "out of memory");
		goto fail;
	}
	json_object_foreach(bindings, key, v) {
		/* Count the slot before parsing so a partial binding is freed. */
		map->count = index + 1;
		if (parse_binding(key, v, &map->bindings[index], err, errlen) <
		    0)
			goto fail;
		index++;
	}

	json_decref(root);
	return 0;
fail:
	if (root)
		json_decref(root);
	remap_map_free(map);
	return -1;
}

int remap_map_load(const char *path, struct remap_map *map, char *err,
		   size_t errlen)
{
	FILE *fp;
	char *text;
	long size;

	fp = fopen(path, "rb");
	if (!fp) {
		seterr(err, errlen, "%s: %s", path, strerror(errno));
		return -1;
	}
	if (fseek(fp, 0, SEEK_END) < 0 || (size = ftell(fp)) < 0 ||
	    size > 1024 * 1024) {
		fclose(fp);
		seterr(err, errlen, "%s: file too large", path);
		return -1;
	}
	rewind(fp);
	text = malloc((size_t)size + 1);
	if (!text) {
		fclose(fp);
		seterr(err, errlen, "out of memory");
		return -1;
	}
	if (fread(text, 1, (size_t)size, fp) != (size_t)size) {
		free(text);
		fclose(fp);
		seterr(err, errlen, "%s: read failed", path);
		return -1;
	}
	text[size] = '\0';
	fclose(fp);
	if (remap_map_load_string(text, map, err, errlen) < 0) {
		free(text);
		return -1;
	}
	free(text);
	return 0;
}

void remap_map_free(struct remap_map *map)
{
	if (!map)
		return;
	for (size_t i = 0; i < map->count; i++) {
		for (size_t j = 0; j < map->bindings[i].seq.count; j++)
			free(map->bindings[i].seq.steps[j].text);
		free(map->bindings[i].seq.steps);
	}
	free(map->bindings);
	free(map->triggers);
	memset(map, 0, sizeof *map);
}

static const naga_ctrl *resolve_ctrl(const char *id, unsigned *ctr)
{
	const naga_ctrl *ctrl = naga_ctrl_by_name(id);
	char *end;
	unsigned long value;

	if (ctrl) {
		*ctr = ctrl->ctr;
		return ctrl;
	}
	errno = 0;
	value = strtoul(id, &end, 16);
	if (errno || *end || value > 0xFF)
		return NULL;
	ctrl = naga_ctrl_by_ctr((unsigned)value);
	if (ctrl)
		*ctr = ctrl->ctr;
	return ctrl;
}

static int is_thumb(const naga_ctrl *ctrl)
{
	return ctrl && ctrl->group == NAGA_GROUP_THUMB;
}

int remap_map_validate(struct remap_map *map, char *err, size_t errlen)
{
	if (map->device == REMAP_DEVICE_AUTO) {
		seterr(err, errlen, "device model is not resolved");
		return -1;
	}

	for (size_t i = 0; i < map->count; i++) {
		struct remap_binding *b = &map->bindings[i];
		const naga_ctrl *ctrl;

		if (b->id[0] == '\0') {
			seterr(err, errlen, "empty binding id");
			return -1;
		}

		if (map->device == REMAP_DEVICE_2020) {
			if (b->layer != 0 && b->layer != 1) {
				seterr(err, errlen, "bad layer for \"%s\"",
				       b->id);
				return -1;
			}
			ctrl = resolve_ctrl(b->id, &b->ctr);
			if (!ctrl) {
				seterr(err, errlen, "unknown control \"%s\"",
				       b->id);
				return -1;
			}
			if (!b->is_sequence) {
				if (b->type == NAGA_ASSIGN_TURBO &&
				    b->layer != 0) {
					seterr(err, errlen,
					       "turbo only works on the normal layer");
					return -1;
				}
				if (b->type != NAGA_ASSIGN_KEY &&
				    b->type != NAGA_ASSIGN_MOUSE &&
				    b->type != NAGA_ASSIGN_HYPERSHIFT &&
				    b->type != NAGA_ASSIGN_TURBO &&
				    b->type != NAGA_ASSIGN_DEFAULT) {
					seterr(err, errlen,
					       "unsupported action for \"%s\"",
					       b->id);
					return -1;
				}
			}
		} else {
			if (b->layer != 0) {
				seterr(err, errlen,
				       "the 2014 has no hypershift layer");
				return -1;
			}
			ctrl = naga_ctrl_by_name(b->id);
			if (!is_thumb(ctrl)) {
				seterr(err, errlen,
				       "the 2014 map only supports thumb ids 1..9,0,-,=");
				return -1;
			}
			if (!b->is_sequence &&
			    b->type != NAGA_ASSIGN_KEY &&
			    b->type != NAGA_ASSIGN_MOUSE &&
			    b->type != NAGA_ASSIGN_DEFAULT) {
				seterr(err, errlen,
				       "hypershift/turbo are onboard-only; "
				       "not valid for the 2014");
				return -1;
			}
		}

		for (size_t j = 0; j < i; j++) {
			const struct remap_binding *o = &map->bindings[j];

			if (map->device == REMAP_DEVICE_2020) {
				if (o->ctr == b->ctr && o->layer == b->layer) {
					seterr(err, errlen,
					       "duplicate binding %s@%d",
					       b->id, b->layer);
					return -1;
				}
			} else if (!strcmp(o->id, b->id)) {
				seterr(err, errlen, "duplicate binding %s",
				       b->id);
				return -1;
			}
		}
	}
	return 0;
}

static const char *find_trigger(const struct remap_map *map, const char *id)
{
	for (size_t i = 0; i < map->trigger_count; i++)
		if (!strcmp(map->triggers[i].id, id))
			return map->triggers[i].key;
	return NULL;
}

static const char *default_2014_trigger(const char *id)
{
	static const struct {
		const char *id;
		const char *key;
	} table[] = {
		{"1", "KEY_KP1"},     {"2", "KEY_KP2"},	 {"3", "KEY_KP3"},
		{"4", "KEY_KP4"},     {"5", "KEY_KP5"},	 {"6", "KEY_KP6"},
		{"7", "KEY_KP7"},     {"8", "KEY_KP8"},	 {"9", "KEY_KP9"},
		{"0", "KEY_KP0"},     {"-", "KEY_KPMINUS"},
		{"=", "KEY_KPPLUS"},
	};

	for (size_t i = 0; i < sizeof table / sizeof table[0]; i++)
		if (!strcmp(table[i].id, id))
			return table[i].key;
	return NULL;
}

int remap_map_assign_triggers(struct remap_map *map, char *err, size_t errlen)
{
	unsigned next_fkey = 0x68; /* F13 */

	for (size_t i = 0; i < map->count; i++) {
		struct remap_binding *b = &map->bindings[i];
		const char *explicit = find_trigger(map, b->id);

		if (map->device == REMAP_DEVICE_2020) {
			if (!b->is_sequence) {
				if (explicit) {
					seterr(err, errlen,
					       "trigger for \"%s\" is only valid "
					       "with a sequence", b->id);
					return -1;
				}
				b->trigger_usage = 0;
				b->trigger[0] = '\0';
				continue;
			}
			if (explicit) {
				if (parse_plain_key(explicit, &b->trigger_usage,
						    err, errlen) < 0)
					return -1;
			} else {
				if (next_fkey > 0x73) {
					seterr(err, errlen,
					       "more than 12 sequences need "
					       "explicit triggers (f13..f24)");
					return -1;
				}
				b->trigger_usage = next_fkey++;
			}
			{
				const char *name =
				    naga_keyname((int)b->trigger_usage);

				if (name)
					snprintf(b->trigger,
						 sizeof b->trigger, "%s", name);
				else
					snprintf(b->trigger,
						 sizeof b->trigger, "%02x",
						 b->trigger_usage);
			}
		} else {
			const char *key = explicit ? explicit :
						 default_2014_trigger(b->id);

			if (!key) {
				seterr(err, errlen,
				       "no trigger for \"%s\"", b->id);
				return -1;
			}
			if (strlen(key) >= sizeof b->trigger) {
				seterr(err, errlen, "trigger too long");
				return -1;
			}
			snprintf(b->trigger, sizeof b->trigger, "%s", key);
			b->trigger_usage = 0;
		}

		for (size_t j = 0; j < i; j++) {
			const struct remap_binding *o = &map->bindings[j];

			if (o->trigger_usage && b->trigger_usage &&
			    o->trigger_usage == b->trigger_usage) {
				seterr(err, errlen,
				       "duplicate trigger %s", b->trigger);
				return -1;
			}
			if (map->device == REMAP_DEVICE_2014 && o->trigger[0] &&
			    !strcmp(o->trigger, b->trigger)) {
				seterr(err, errlen,
				       "duplicate trigger %s", b->trigger);
				return -1;
			}
		}
	}

	for (size_t i = 0; i < map->trigger_count; i++) {
		bool found = false;

		for (size_t j = 0; j < map->count; j++) {
			if (!strcmp(map->triggers[i].id,
				    map->bindings[j].id)) {
				found = true;
				break;
			}
		}
		if (!found) {
			seterr(err, errlen,
			       "trigger for unknown binding \"%s\"",
			       map->triggers[i].id);
			return -1;
		}
	}

	return 0;
}

void remap_key_combo(unsigned mod, unsigned usage, char *buf, size_t len)
{
	static const char *const names[] = {
		"ctrl+", "shift+", "alt+", "super+",
		"rctrl+", "rshift+", "ralt+", "rsuper+",
	};
	const char *key = naga_keyname((int)usage);
	size_t off = 0;

	if (!len)
		return;
	buf[0] = '\0';
	for (int i = 0; i < 8; i++) {
		if (!(mod & (1u << i)))
			continue;
		off += (size_t)snprintf(buf + off, len - off, "%s", names[i]);
		if (off >= len)
			return;
	}
	if (key)
		snprintf(buf + off, len - off, "%s", key);
	else
		snprintf(buf + off, len - off, "%02x", usage);
}

void remap_binding_string(const struct remap_binding *b, char *buf, size_t len)
{
	char combo[64];

	if (!len)
		return;
	if (b->is_sequence) {
		snprintf(buf, len, "sequence (%zu steps)", b->seq.count);
		return;
	}
	switch (b->type) {
	case NAGA_ASSIGN_KEY:
		remap_key_combo(b->mod, b->usage, buf, len);
		break;
	case NAGA_ASSIGN_MOUSE: {
		const char *name = naga_mouse_name((int)b->mouse);

		snprintf(buf, len, "mouse:%s", name ? name : "?");
		break;
	}
	case NAGA_ASSIGN_HYPERSHIFT:
		snprintf(buf, len, "hypershift");
		break;
	case NAGA_ASSIGN_TURBO:
		remap_key_combo(b->mod, b->usage, combo, sizeof combo);
		snprintf(buf, len, "turbo:%s@%u", combo, b->turbo_ms);
		break;
	case NAGA_ASSIGN_DEFAULT:
		snprintf(buf, len, "default");
		break;
	default:
		snprintf(buf, len, "type %02X", b->type);
		break;
	}
}

int remap_apply(const struct remap_map *map, bool dry_run, char *err,
		size_t errlen)
{
	size_t sequences = 0;

	if (map->device != REMAP_DEVICE_2020) {
		seterr(err, errlen,
		       "apply only works on the 2020 LH (1532:008d); "
		       "use `run` for the 2014");
		return -1;
	}
	if (!dry_run) {
		int rc = naga_open();

		if (rc != NAGA_OK) {
			seterr(err, errlen, "open: %s", naga_strerror(rc));
			return -1;
		}
	}

	for (size_t i = 0; i < map->count; i++) {
		const struct remap_binding *b = &map->bindings[i];
		char label[48];
		int rc = NAGA_OK;

		snprintf(label, sizeof label, "%s%s", b->id,
			 b->layer ? "@hypershift" : "");
		if (b->is_sequence) {
			printf("%s %s -> %s (sequence; needs run)\n",
			       dry_run ? "would bind" : "bind", label,
			       b->trigger);
			sequences++;
			if (!dry_run)
				rc = naga_set_key(b->ctr, b->trigger_usage, 0,
						  (unsigned)b->layer);
		} else {
			printf("%s %s -> ", dry_run ? "would bind" : "bind",
			       label);
			{
				char value[96];

				remap_binding_string(b, value, sizeof value);
				printf("%s\n", value);
			}
			if (dry_run)
				continue;
			switch (b->type) {
			case NAGA_ASSIGN_KEY:
				rc = naga_set_key(b->ctr, b->usage, b->mod,
						  (unsigned)b->layer);
				break;
			case NAGA_ASSIGN_MOUSE:
				rc = naga_set_mouse(b->ctr, b->mouse,
						    (unsigned)b->layer);
				break;
			case NAGA_ASSIGN_HYPERSHIFT:
				rc = naga_set_hypershift(b->ctr,
							 (unsigned)b->layer);
				break;
			case NAGA_ASSIGN_TURBO:
				rc = naga_set_turbo(b->ctr, b->usage, b->mod,
						    b->turbo_ms);
				break;
			case NAGA_ASSIGN_DEFAULT:
				rc = naga_set_default(b->ctr,
						      (unsigned)b->layer);
				break;
			default:
				rc = NAGA_ERR_INVALID;
				break;
			}
		}
		if (rc != NAGA_OK) {
			seterr(err, errlen, "%s: %s", label,
			       naga_strerror(rc));
			if (!dry_run)
				naga_close();
			return -1;
		}
	}

	if (!dry_run)
		naga_close();
	printf("%s %zu binding(s)%s\n", dry_run ? "validated" : "applied",
	       map->count,
	       sequences ? "; run `naga-remap run` for the sequences" : "");
	return 0;
}
