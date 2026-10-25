/* SPDX-License-Identifier: GPL-2.0-or-later OR BSD-2-Clause */
#include "remap/map.h"

#include <cJSON.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_BINDINGS 256
#define MAX_MACROS 64
#define MAX_STEPS 512
#define MAX_TEXT 1024
#define MAX_MACRO_NAME 64

static void seterr(char *err, size_t errlen, const char *fmt, ...)
{
	va_list ap;

	if (!err || !errlen)
		return;
	va_start(ap, fmt);
	vsnprintf(err, errlen, fmt, ap);
	va_end(ap);
}

static cJSON *member(cJSON *obj, const char *name)
{
	return cJSON_GetObjectItemCaseSensitive(obj, name);
}

static const char *jstr(cJSON *v)
{
	return cJSON_IsString(v) ? v->valuestring : NULL;
}

static int get_uint(cJSON *v, unsigned max, unsigned *out)
{
	double d;

	if (!cJSON_IsNumber(v))
		return -1;
	d = v->valuedouble;
	if (d < 0.0 || d > (double)max || d != (double)(unsigned)d)
		return -1;
	*out = (unsigned)d;
	return 0;
}

static int parse_key_combo(const char *text, unsigned *usage, unsigned *mod,
			   char *err, size_t errlen)
{
	char buf[128];
	char *plus;
	int value;

	if (!text || !*text || strlen(text) >= sizeof buf) {
		seterr(err, errlen, "empty or overlong key \"%s\"",
		       text ? text : "");
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
	*usage = (unsigned)value;
	return 0;
}

static int parse_action(const char *text, struct remap_binding *b,
			struct arena *arena, char *err, size_t errlen)
{
	if (!strcmp(text, "default")) {
		b->type = NAGA_ASSIGN_DEFAULT;
		return 0;
	}
	if (!strcmp(text, "hypershift")) {
		b->type = NAGA_ASSIGN_HYPERSHIFT;
		return 0;
	}
	if (!strncmp(text, "macro:", 6)) {
		const char *name = text + 6;

		if (!*name || strlen(name) >= MAX_MACRO_NAME) {
			seterr(err, errlen, "bad macro name \"%s\"", name);
			return -1;
		}
		b->macro = arena_strdup(arena, name);
		if (!b->macro) {
			seterr(err, errlen, "out of memory");
			return -1;
		}
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

static int get_ms(cJSON *obj, const char *name, unsigned def, unsigned *out,
		  char *err, size_t errlen)
{
	cJSON *v = member(obj, name);

	if (!v) {
		*out = def;
		return 0;
	}
	if (get_uint(v, 60000, out) < 0) {
		seterr(err, errlen, "%s must be an integer 0..60000", name);
		return -1;
	}
	return 0;
}

static int parse_mods(cJSON *obj, unsigned *mod, char *err, size_t errlen)
{
	cJSON *v = member(obj, "mods");
	cJSON *item;

	*mod = 0;
	if (!v)
		return 0;
	if (!cJSON_IsArray(v)) {
		seterr(err, errlen, "\"mods\" must be an array");
		return -1;
	}
	cJSON_ArrayForEach(item, v) {
		int bits;

		if (!jstr(item)) {
			seterr(err, errlen, "\"mods\" entries must be strings");
			return -1;
		}
		bits = naga_parse_mod(jstr(item));
		if (bits < 0) {
			seterr(err, errlen, "unknown modifier \"%s\"",
			       jstr(item));
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

static int parse_step(cJSON *obj, struct remap_step *step,
		      struct arena *arena, char *err, size_t errlen)
{
	cJSON *v;
	int actions = 0;

	if (!cJSON_IsObject(obj)) {
		seterr(err, errlen, "macro steps must be objects");
		return -1;
	}
	memset(step, 0, sizeof *step);

	if ((v = member(obj, "delay"))) {
		actions++;
		if (get_uint(v, 60000, &step->delay_ms) < 0) {
			seterr(err, errlen, "delay must be 0..60000 ms");
			return -1;
		}
		step->kind = REMAP_STEP_DELAY;
	}

	if ((v = member(obj, "key"))) {
		actions++;
		if (!jstr(v)) {
			seterr(err, errlen, "\"key\" must be a string");
			return -1;
		}
		step->kind = REMAP_STEP_KEY;
		if (parse_key_combo(jstr(v), &step->usage, &step->mod, err,
				    errlen) < 0)
			return -1;
		if (parse_mods(obj, &step->mod, err, errlen) < 0)
			return -1;
		if (get_ms(obj, "hold_ms", 20, &step->hold_ms, err, errlen) < 0)
			return -1;
	}

	if ((v = member(obj, "text"))) {
		actions++;
		if (!jstr(v) || !*jstr(v)) {
			seterr(err, errlen,
			       "\"text\" must be a non-empty string");
			return -1;
		}
		if (strlen(jstr(v)) > MAX_TEXT) {
			seterr(err, errlen, "text is too long");
			return -1;
		}
		step->kind = REMAP_STEP_TEXT;
		step->text = arena_strdup(arena, jstr(v));
		if (!step->text) {
			seterr(err, errlen, "out of memory");
			return -1;
		}
		if (get_ms(obj, "hold_ms", 10, &step->hold_ms, err, errlen) < 0)
			return -1;
		if (get_ms(obj, "delay_ms", 20, &step->delay_ms, err, errlen) < 0)
			return -1;
	}

	if ((v = member(obj, "mouse"))) {
		int button;

		actions++;
		if (!jstr(v)) {
			seterr(err, errlen, "\"mouse\" must be a string");
			return -1;
		}
		button = naga_parse_mouse(jstr(v));
		if (button < 0) {
			seterr(err, errlen, "unknown mouse button \"%s\"",
			       jstr(v));
			return -1;
		}
		step->kind = REMAP_STEP_MOUSE;
		step->mouse = button;
		v = member(obj, "action");
		if (v && !jstr(v)) {
			seterr(err, errlen, "mouse action must be a string");
			return -1;
		}
		if (parse_mouse_action(jstr(v), &step->mouse_action, err,
				       errlen) < 0)
			return -1;
		if (get_ms(obj, "hold_ms", 20, &step->hold_ms, err, errlen) < 0)
			return -1;
	}

	if (actions != 1) {
		seterr(err, errlen,
		       "step needs exactly one of key, text, mouse or delay");
		return -1;
	}
	return 0;
}

static int parse_sequence(cJSON *arr, struct remap_sequence *seq,
			  struct arena *arena, char *err, size_t errlen)
{
	size_t count;
	size_t index = 0;
	cJSON *item;

	if (!cJSON_IsArray(arr) || (count = cJSON_GetArraySize(arr)) == 0) {
		seterr(err, errlen, "macro must be a non-empty array");
		return -1;
	}
	if (count > MAX_STEPS) {
		seterr(err, errlen, "too many macro steps (max %d)", MAX_STEPS);
		return -1;
	}
	seq->steps = arena_alloc(arena, count * sizeof *seq->steps,
				 _Alignof(struct remap_step));
	if (!seq->steps) {
		seterr(err, errlen, "out of memory");
		return -1;
	}
	memset(seq->steps, 0, count * sizeof *seq->steps);
	cJSON_ArrayForEach(item, arr) {
		if (parse_step(item, &seq->steps[index], arena, err, errlen) < 0)
			return -1;
		index++;
	}
	seq->count = index;
	return 0;
}

static int parse_macros(struct remap_map *map, cJSON *root, char *err,
			size_t errlen)
{
	cJSON *macros = member(root, "macros");
	cJSON *item;
	size_t count;

	if (!macros)
		return 0;
	if (!cJSON_IsObject(macros)) {
		seterr(err, errlen, "\"macros\" must be an object");
		return -1;
	}
	count = cJSON_GetArraySize(macros);
	if (count > MAX_MACROS) {
		seterr(err, errlen, "too many macros (max %d)", MAX_MACROS);
		return -1;
	}
	map->macros = arena_alloc(&map->arena, count * sizeof *map->macros,
				  _Alignof(struct remap_macro));
	if (!map->macros) {
		seterr(err, errlen, "out of memory");
		return -1;
	}
	memset(map->macros, 0, count * sizeof *map->macros);

	cJSON_ArrayForEach(item, macros) {
		struct remap_macro *macro = &map->macros[map->macro_count];
		const char *name = item->string;

		if (!name || !*name || strlen(name) >= MAX_MACRO_NAME) {
			seterr(err, errlen, "bad macro name");
			return -1;
		}
		for (size_t i = 0; i < map->macro_count; i++) {
			if (!strcmp(map->macros[i].name, name)) {
				seterr(err, errlen, "duplicate macro \"%s\"",
				       name);
				return -1;
			}
		}
		macro->name = arena_strdup(&map->arena, name);
		if (!macro->name) {
			seterr(err, errlen, "out of memory");
			return -1;
		}
		if (parse_sequence(item, &macro->seq, &map->arena, err,
				   errlen) < 0)
			return -1;
		map->macro_count++;
	}
	return 0;
}

static int parse_binding(const char *raw, cJSON *value, int layer,
			 struct remap_binding *b, struct arena *arena,
			 char *err, size_t errlen)
{
	const char *text = jstr(value);

	memset(b, 0, sizeof *b);
	if (!raw || !*raw || strlen(raw) >= sizeof b->id) {
		seterr(err, errlen, "bad binding id \"%s\"", raw ? raw : "");
		return -1;
	}
	if (strchr(raw, '@')) {
		seterr(err, errlen,
		       "binding \"%s\": use bindings.normal/hypershift, "
		       "not @layer",
		       raw);
		return -1;
	}
	strcpy(b->id, raw);
	b->layer = layer;

	if (!text) {
		seterr(err, errlen,
		       "binding \"%s\" must be a string; define sequences "
		       "under \"macros\" and use \"macro:<name>\"",
		       raw);
		return -1;
	}
	return parse_action(text, b, arena, err, errlen);
}

static int parse_section(struct remap_map *map, cJSON *section, int layer,
			 char *err, size_t errlen)
{
	cJSON *item;

	if (!section)
		return 0;
	cJSON_ArrayForEach(item, section) {
		if (parse_binding(item->string, item, layer,
				  &map->bindings[map->count], &map->arena, err,
				  errlen) < 0)
			return -1;
		map->count++;
	}
	return 0;
}

static int parse_bindings(struct remap_map *map, cJSON *root, char *err,
			  size_t errlen)
{
	cJSON *bindings = member(root, "bindings");
	cJSON *normal;
	cJSON *hyper;
	cJSON *item;
	size_t total;

	if (!cJSON_IsObject(bindings)) {
		seterr(err, errlen, "\"bindings\" must be an object");
		return -1;
	}
	cJSON_ArrayForEach(item, bindings) {
		if (strcmp(item->string, "normal") &&
		    strcmp(item->string, "hypershift")) {
			seterr(err, errlen,
			       "bindings must have \"normal\" and/or "
			       "\"hypershift\"; unknown key \"%s\"",
			       item->string);
			return -1;
		}
	}
	normal = member(bindings, "normal");
	hyper = member(bindings, "hypershift");
	if ((normal && !cJSON_IsObject(normal)) ||
	    (hyper && !cJSON_IsObject(hyper))) {
		seterr(err, errlen, "bindings.normal/hypershift must be objects");
		return -1;
	}
	total = (normal ? (size_t)cJSON_GetArraySize(normal) : 0) +
		(hyper ? (size_t)cJSON_GetArraySize(hyper) : 0);
	if (total == 0) {
		seterr(err, errlen, "no bindings");
		return -1;
	}
	if (total > MAX_BINDINGS) {
		seterr(err, errlen, "too many bindings (max %d)", MAX_BINDINGS);
		return -1;
	}
	map->bindings = arena_alloc(&map->arena, total * sizeof *map->bindings,
				    _Alignof(struct remap_binding));
	if (!map->bindings) {
		seterr(err, errlen, "out of memory");
		return -1;
	}
	memset(map->bindings, 0, total * sizeof *map->bindings);

	if (parse_section(map, normal, 0, err, errlen) < 0)
		return -1;
	return parse_section(map, hyper, 1, err, errlen);
}

static int parse_triggers(struct remap_map *map, cJSON *root, char *err,
			  size_t errlen)
{
	cJSON *triggers = member(root, "triggers");
	cJSON *section;
	cJSON *item;
	size_t count = 0;

	if (!triggers)
		return 0;
	if (!cJSON_IsObject(triggers)) {
		seterr(err, errlen, "\"triggers\" must be an object");
		return -1;
	}
	cJSON_ArrayForEach(item, triggers) {
		if (strcmp(item->string, "normal") &&
		    strcmp(item->string, "hypershift")) {
			seterr(err, errlen,
			       "triggers must have \"normal\" and/or "
			       "\"hypershift\"; unknown key \"%s\"",
			       item->string);
			return -1;
		}
		if (!cJSON_IsObject(item)) {
			seterr(err, errlen,
			       "triggers.normal/hypershift must be objects");
			return -1;
		}
		count += (size_t)cJSON_GetArraySize(item);
	}
	if (count > MAX_BINDINGS)
		count = MAX_BINDINGS;
	map->triggers = arena_alloc(&map->arena,
				    (count ? count : 1) * sizeof *map->triggers,
				    _Alignof(struct remap_trigger));
	if (!map->triggers) {
		seterr(err, errlen, "out of memory");
		return -1;
	}
	memset(map->triggers, 0, (count ? count : 1) * sizeof *map->triggers);

	static const char *const names[] = {"normal", "hypershift"};
	for (int layer = 0; layer < 2; layer++) {
		section = member(triggers, names[layer]);
		if (!section)
			continue;
		cJSON_ArrayForEach(item, section) {
			struct remap_trigger *t;

			if (map->trigger_count >= count) {
				seterr(err, errlen, "too many triggers");
				return -1;
			}
			if (!item->string || strlen(item->string) >=
						 sizeof map->triggers[0].id ||
			    !jstr(item) ||
			    strlen(jstr(item)) >=
				sizeof map->triggers[0].key) {
				seterr(err, errlen, "bad trigger");
				return -1;
			}
			t = &map->triggers[map->trigger_count++];
			strcpy(t->id, item->string);
			strcpy(t->key, jstr(item));
			t->layer = layer;
		}
	}
	return 0;
}

int remap_map_load_string(const char *text, struct remap_map *map, char *err,
			  size_t errlen)
{
	cJSON *root;
	const char *s;

	memset(map, 0, sizeof *map);
	map->device = REMAP_DEVICE_AUTO;
	arena_init(&map->arena, 4096);

	root = cJSON_ParseWithLength(text, strlen(text));
	if (!root) {
		const char *ep = cJSON_GetErrorPtr();

		seterr(err, errlen, "JSON: %s", ep ? ep : "parse error");
		goto fail;
	}
	if (!cJSON_IsObject(root)) {
		seterr(err, errlen, "top level must be an object");
		goto fail_root;
	}

	if ((s = jstr(member(root, "device")))) {
		if (!strcmp(s, "auto"))
			map->device = REMAP_DEVICE_AUTO;
		else if (!strcmp(s, "2020") || !strcmp(s, "naga-2020") ||
			 !strcmp(s, "naga-lh-2020"))
			map->device = REMAP_DEVICE_2020;
		else if (!strcmp(s, "2014") || !strcmp(s, "naga-2014"))
			map->device = REMAP_DEVICE_2014;
		else {
			seterr(err, errlen, "unknown device \"%s\"", s);
			goto fail_root;
		}
	}

	if (parse_macros(map, root, err, errlen) < 0 ||
	    parse_triggers(map, root, err, errlen) < 0 ||
	    parse_bindings(map, root, err, errlen) < 0)
		goto fail_root;

	cJSON_Delete(root);
	return 0;

fail_root:
	cJSON_Delete(root);
fail:
	remap_map_free(map);
	return -1;
}

int remap_map_load(const char *path, struct remap_map *map, char *err,
		   size_t errlen)
{
	FILE *fp;
	long size;
	struct arena file_arena;
	char *text;
	int rc;

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
	arena_init(&file_arena, (size_t)size + 1);
	text = arena_alloc(&file_arena, (size_t)size + 1, 1);
	if (!text) {
		fclose(fp);
		arena_free(&file_arena);
		seterr(err, errlen, "out of memory");
		return -1;
	}
	if (fread(text, 1, (size_t)size, fp) != (size_t)size) {
		fclose(fp);
		arena_free(&file_arena);
		seterr(err, errlen, "%s: read failed", path);
		return -1;
	}
	text[size] = '\0';
	fclose(fp);
	rc = remap_map_load_string(text, map, err, errlen);
	arena_free(&file_arena);
	return rc;
}

void remap_map_free(struct remap_map *map)
{
	if (!map)
		return;
	arena_free(&map->arena);
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

static const struct remap_macro *find_macro(const struct remap_map *map,
					     const char *name)
{
	for (size_t i = 0; i < map->macro_count; i++)
		if (!strcmp(map->macros[i].name, name))
			return &map->macros[i];
	return NULL;
}

static struct remap_binding *find_binding(struct remap_map *map,
					  const char *id, int layer)
{
	for (size_t i = 0; i < map->count; i++)
		if (map->bindings[i].layer == layer &&
		    !strcmp(map->bindings[i].id, id))
			return &map->bindings[i];
	return NULL;
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

		if (b->macro) {
			const struct remap_macro *macro =
			    find_macro(map, b->macro);

			if (!macro) {
				seterr(err, errlen,
				       "binding \"%s\" references unknown macro "
				       "\"%s\"",
				       b->id, b->macro);
				return -1;
			}
			b->seq = &macro->seq;
			b->is_sequence = true;
		}

		ctrl = naga_ctrl_by_name(b->id);
		b->host = ctrl && ctrl->host_only;
		if (b->host) {
			b->ctr = ctrl->ctr;
			if (b->layer != 0) {
				seterr(err, errlen,
				       "wheel up/down have no hypershift layer");
				return -1;
			}
			if (!b->is_sequence &&
			    b->type != NAGA_ASSIGN_KEY &&
			    b->type != NAGA_ASSIGN_MOUSE) {
				seterr(err, errlen,
				       "wheel up/down support key, mouse or macro");
				return -1;
			}
		} else if (map->device == REMAP_DEVICE_2020) {
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
					       "turbo only works on the normal "
					       "layer");
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
				       "the 2014 map only supports thumb ids "
				       "1..9,0,-,=");
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

	for (size_t i = 0; i < map->trigger_count; i++) {
		const struct remap_trigger *t = &map->triggers[i];
		struct remap_binding *b = find_binding(map, t->id, t->layer);

		if (!b) {
			seterr(err, errlen,
			       "trigger for unknown binding \"%s\"@%d", t->id,
			       t->layer);
			return -1;
		}
		if (b->host) {
			seterr(err, errlen,
			       "trigger for \"%s\" is unused; wheel is "
			       "event-driven",
			       t->id);
			return -1;
		}
		if (map->device == REMAP_DEVICE_2020 && !b->is_sequence) {
			seterr(err, errlen,
			       "trigger for \"%s\" needs a macro binding",
			       t->id);
			return -1;
		}
		if (map->device == REMAP_DEVICE_2014 && t->layer != 0) {
			seterr(err, errlen,
			       "the 2014 has no hypershift triggers");
			return -1;
		}
	}
	return 0;
}

static const char *find_trigger(const struct remap_map *map, const char *id,
				int layer)
{
	for (size_t i = 0; i < map->trigger_count; i++)
		if (map->triggers[i].layer == layer &&
		    !strcmp(map->triggers[i].id, id))
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
		const char *explicit = find_trigger(map, b->id, b->layer);

		if (b->host)
			continue;

		if (map->device == REMAP_DEVICE_2020) {
			if (!b->is_sequence) {
				if (explicit) {
					seterr(err, errlen,
					       "trigger for \"%s\" is only "
					       "valid with a macro",
					       b->id);
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
					       "more than 12 macro bindings need "
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

	if (map->device == REMAP_DEVICE_2020) {
		for (size_t i = 0; i < map->count; i++) {
			const struct remap_binding *b = &map->bindings[i];

			if (b->is_sequence || b->host)
				continue;
			if (b->type != NAGA_ASSIGN_KEY &&
			    b->type != NAGA_ASSIGN_TURBO)
				continue;
			for (size_t j = 0; j < map->count; j++) {
				const struct remap_binding *o = &map->bindings[j];

				if (o->is_sequence && o->trigger_usage &&
				    o->trigger_usage == b->usage) {
					seterr(err, errlen,
					       "binding %s%s emits trigger %s; "
					       "choose another trigger",
					       b->id,
					       b->layer ? "@hypershift" : "",
					       o->trigger);
					return -1;
				}
			}
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
	if (b->macro) {
		snprintf(buf, len, "macro:%s", b->macro);
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
		if (b->host) {
			printf("%s %s -> host-side (daemon)\n",
			       dry_run ? "would bind" : "bind", b->id);
			continue;
		}
		if (b->is_sequence) {
			printf("%s %s -> %s (macro; needs run)\n",
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
	       sequences ? "; run `naga-remap run` for the macros" : "");
	return 0;
}
