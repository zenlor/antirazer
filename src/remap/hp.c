/* SPDX-License-Identifier: GPL-2.0-or-later OR BSD-2-Clause */
/*
 * remap/hp.c — map-level apply and dump for the HP Pavilion Gaming Mouse 300.
 *
 * The transport lives in src/hp.c. This file translates the shared JSON
 * envelope (sector-* ids, key/mouse/device actions, settings) to the 0x200
 * byte button sectors and the settings block.
 */
#include "remap/hp_map.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hp.h"

static void seterr(char *err, size_t errlen, const char *fmt, ...)
{
	va_list ap;

	if (!err || !errlen)
		return;
	va_start(ap, fmt);
	vsnprintf(err, errlen, fmt, ap);
	va_end(ap);
}

static int settings_valid(const unsigned char *raw)
{
	unsigned s = 0x8032;
	unsigned stored;

	for (int i = 0; i < 0x25; i++)
		s += raw[i];
	s &= 0xffff;
	stored = (unsigned)raw[0x25] | ((unsigned)raw[0x26] << 8);
	return stored == s;
}

static unsigned poll_code(unsigned hz)
{
	switch (hz) {
	case 1000: return 1;
	case 500: return 2;
	case 250: return 4;
	case 125: return 8;
	default: return 1;
	}
}

static unsigned poll_hz(unsigned code)
{
	switch (code) {
	case 1: return 1000;
	case 2: return 500;
	case 4: return 250;
	case 8: return 125;
	default: return 0;
	}
}

static int mouse_bit(unsigned mouse)
{
	switch (mouse) {
	case 1: return 1;  /* left */
	case 2: return 2;  /* right */
	case 3: return 4;  /* middle */
	case 4: return 8;  /* back */
	case 5: return 16; /* forward */
	default: return -1;
	}
}

static unsigned mouse_id(unsigned bit)
{
	switch (bit) {
	case 1: return 1;
	case 2: return 2;
	case 4: return 3;
	case 8: return 4;
	case 16: return 5;
	default: return 0;
	}
}

/* <type><label><mode><n> <value,kind>... at +0 (normal) / +0xFE (shift). */
static int put_layer(unsigned char sec[HP_SECTOR_SIZE], int layer,
		     const struct remap_binding *b, char *err, size_t errlen)
{
	unsigned off = layer ? 0xfe : 0;
	int bit;

	memset(sec + off, 0, 0xfe);
	if (b->type == NAGA_ASSIGN_DEFAULT)
		return 0;

	sec[off + 3] = 1;
	if (b->type == REMAP_HP_DEVICE) {
		sec[off] = 0x09;
		sec[off + 4] = (unsigned char)b->usage;
		sec[off + 5] = 0x30;
		return 0;
	}
	switch (b->type) {
	case NAGA_ASSIGN_KEY:
		if (b->mod > 0x0f) {
			seterr(err, errlen,
			       "%s: HP supports ctrl/shift/alt/win only", b->id);
			return -1;
		}
		sec[off] = 0x06;
		sec[off + 1] = 0x13;
		sec[off + 4] = (unsigned char)b->usage;
		sec[off + 5] = (unsigned char)b->mod;
		break;
	case NAGA_ASSIGN_MOUSE:
		bit = mouse_bit(b->mouse);
		if (bit < 0) {
			seterr(err, errlen, "%s: bad mouse button", b->id);
			return -1;
		}
		sec[off] = 0x00;
		sec[off + 4] = (unsigned char)bit;
		sec[off + 5] = 0x20;
		break;
	default:
		seterr(err, errlen, "%s: unsupported HP action", b->id);
		return -1;
	}
	return 0;
}

static int apply_settings(const struct remap_map *map, char *err,
			  size_t errlen)
{
	const struct remap_hp_settings *s = &map->hp_settings;
	unsigned char full[HP_SETTINGS_READ_LEN];
	unsigned char raw[HP_SETTINGS_WRITE_LEN];
	int rc;

	rc = hp_read_settings(full);
	if (rc != HP_OK) {
		seterr(err, errlen, "settings: %s", hp_strerror(rc));
		return -1;
	}
	if (!settings_valid(full)) {
		seterr(err, errlen, "settings checksum mismatch");
		return -1;
	}
	if (!settings_valid(full)) {
		seterr(err, errlen, "settings checksum mismatch");
		return -1;
	}
	memcpy(raw, full, sizeof raw);
	if (s->have_polling)
		raw[0] = (unsigned char)poll_code(s->polling);
	if (s->have_active_slot)
		raw[1] = (unsigned char)s->active_slot;
	if (s->have_slot_count)
		raw[5] = (unsigned char)s->slot_count;
	for (int i = 0; i < REMAP_HP_SECTORS; i++) {
		const struct remap_hp_slot *slot = &s->slots[i];
		unsigned o = 6 + 5 * (unsigned)i;

		if (!slot->used)
			continue;
		if (slot->have_enabled)
			raw[o] = slot->enabled ? 1 : 0;
		if (slot->have_level)
			raw[o + 1] = (unsigned char)slot->level;
		if (slot->have_color) {
			raw[o + 2] = (unsigned char)(slot->rgb >> 16);
			raw[o + 3] = (unsigned char)(slot->rgb >> 8);
			raw[o + 4] = (unsigned char)slot->rgb;
		}
	}
	hp_checksum(raw);
	rc = hp_write_settings(raw, sizeof raw);
	if (rc != HP_OK) {
		seterr(err, errlen, "settings write: %s", hp_strerror(rc));
		return -1;
	}
	printf("settings written (applies at the next replug)\n");
	return 0;
}

int remap_hp_apply(const struct remap_map *map, bool dry_run, char *err,
		   size_t errlen)
{
	unsigned char sectors[REMAP_HP_SECTORS][HP_SECTOR_SIZE];
	bool loaded[REMAP_HP_SECTORS] = {false};
	bool dirty[REMAP_HP_SECTORS] = {false};
	int rc;

	if (map->device != REMAP_DEVICE_HP300) {
		seterr(err, errlen, "not an hp-300 map");
		return -1;
	}

	if (dry_run) {
		for (size_t i = 0; i < map->count; i++) {
			char value[96];

			remap_binding_string(&map->bindings[i], value,
					     sizeof value);
			printf("would bind sector-%u%s -> %s\n",
			       map->bindings[i].ctr,
			       map->bindings[i].layer ? "@hypershift" : "",
			       value);
		}
		if (map->hp_settings.present)
			printf("would write settings\n");
		printf("validated %zu binding(s)\n", map->count);
		return 0;
	}

	rc = hp_open();
	if (rc != HP_OK) {
		seterr(err, errlen, "open: %s", hp_strerror(rc));
		return -1;
	}
	if (map->hp_settings.present && apply_settings(map, err, errlen) < 0) {
		hp_close();
		return -1;
	}

	for (size_t i = 0; i < map->count; i++) {
		const struct remap_binding *b = &map->bindings[i];
		unsigned n = b->ctr;

		if (n >= REMAP_HP_SECTORS) {
			seterr(err, errlen, "%s: bad sector", b->id);
			hp_close();
			return -1;
		}
		if (!loaded[n]) {
			rc = hp_read_sector(n, sectors[n]);
			if (rc != HP_OK) {
				seterr(err, errlen, "sector %u: %s", n,
				       hp_strerror(rc));
				hp_close();
				return -1;
			}
			loaded[n] = true;
		}
		if (put_layer(sectors[n], b->layer, b, err, errlen) < 0) {
			hp_close();
			return -1;
		}
		hp_checksum_sector(sectors[n]);
		dirty[n] = true;
	}
	for (unsigned n = 0; n < REMAP_HP_SECTORS; n++) {
		if (!dirty[n])
			continue;
		rc = hp_write_sector(n, sectors[n]);
		if (rc != HP_OK) {
			seterr(err, errlen, "sector %u write: %s", n,
			       hp_strerror(rc));
			hp_close();
			return -1;
		}
		printf("bound sector-%u\n", n);
	}
	hp_close();
	printf("applied %zu binding(s)\n", map->count);
	return 0;
}

/* ---- dump ------------------------------------------------------------- */

static void hex(const unsigned char *p, size_t n, char *out)
{
	static const char digits[] = "0123456789abcdef";

	for (size_t i = 0; i < n; i++) {
		out[2 * i] = digits[p[i] >> 4];
		out[2 * i + 1] = digits[p[i] & 0x0f];
	}
	out[2 * n] = '\0';
}

static void add_bindings(cJSON *normal, cJSON *hyper,
			 const unsigned char *sectors)
{
	for (unsigned n = 0; n < REMAP_HP_SECTORS; n++) {
		const unsigned char *sec = sectors + n * HP_SECTOR_SIZE;

		for (int layer = 0; layer <= 1; layer++) {
			const unsigned char *p = sec + (layer ? 0xfe : 0);
			struct remap_binding b = {0};
			char id[24];
			char value[96];
			cJSON *target = layer ? hyper : normal;

			if (!p[3])
				continue;
			b.layer = layer;
			switch (p[0]) {
			case 0x00:
				if (p[3] < 1 || p[5] != 0x20)
					continue;
				b.type = NAGA_ASSIGN_MOUSE;
				b.mouse = mouse_id(p[4]);
				if (!b.mouse)
					continue;
				break;
			case 0x06:
				if (p[3] < 1 || p[5] > 0x0f)
					continue;
				b.type = NAGA_ASSIGN_KEY;
				b.usage = p[4];
				b.mod = p[5];
				break;
			case 0x09:
				if (p[3] < 1 || p[5] != 0x30)
					continue;
				b.type = REMAP_HP_DEVICE;
				b.usage = p[4];
				break;
			default:
				continue;
			}
			remap_binding_string(&b, value, sizeof value);
			snprintf(id, sizeof id, "sector-%u", n);
			cJSON_AddItemToObject(target, id,
					      cJSON_CreateString(value));
		}
	}
}

cJSON *remap_hp_dump(char *err, size_t errlen)
{
	cJSON *root = NULL, *bindings = NULL, *normal = NULL, *hyper = NULL;
	cJSON *settings = NULL, *slots = NULL, *backup = NULL, *arr = NULL;
	cJSON *match = NULL;
	unsigned char full[HP_SETTINGS_READ_LEN];
	unsigned char status[64];
	unsigned char *sectors = NULL;
	unsigned char *raw;
	char *hexbuf = NULL;
	int rc;

	rc = hp_open();
	if (rc != HP_OK) {
		seterr(err, errlen, "open: %s", hp_strerror(rc));
		return NULL;
	}
	if (hp_hello(status) != HP_OK) {
		seterr(err, errlen, "hello: %s", hp_strerror(HP_ERR_IO));
		goto fail;
	}
	rc = hp_read_settings(full);
	if (rc != HP_OK) {
		seterr(err, errlen, "settings: %s", hp_strerror(rc));
		goto fail;
	}
	if (!settings_valid(full)) {
		seterr(err, errlen, "settings checksum mismatch");
		goto fail;
	}
	sectors = malloc(REMAP_HP_SECTORS * HP_SECTOR_SIZE);
	hexbuf = malloc(2 * (REMAP_HP_SECTORS * HP_SECTOR_SIZE) + 1);
	if (!sectors || !hexbuf) {
		seterr(err, errlen, "out of memory");
		goto fail;
	}
	for (unsigned n = 0; n < REMAP_HP_SECTORS; n++) {
		rc = hp_read_sector(n, sectors + n * HP_SECTOR_SIZE);
		if (rc != HP_OK) {
			seterr(err, errlen, "sector %u: %s", n,
			       hp_strerror(rc));
			goto fail;
		}
	}

	root = cJSON_CreateObject();
	bindings = cJSON_CreateObject();
	normal = cJSON_CreateObject();
	hyper = cJSON_CreateObject();
	settings = cJSON_CreateObject();
	slots = cJSON_CreateArray();
	backup = cJSON_CreateObject();
	arr = cJSON_CreateArray();
	match = cJSON_CreateObject();
	if (!root || !bindings || !normal || !hyper || !settings || !slots ||
	    !backup || !arr || !match) {
		seterr(err, errlen, "out of memory");
		goto fail;
	}

	cJSON_AddItemToObject(root, "version", cJSON_CreateNumber(1));
	cJSON_AddItemToObject(root, "device", cJSON_CreateString("hp-300"));
	cJSON_AddItemToObject(match, "vendorId", cJSON_CreateString("03f0"));
	cJSON_AddItemToObject(match, "productId", cJSON_CreateString("2941"));
	cJSON_AddItemToObject(root, "match", match);
	match = NULL;
	cJSON_AddItemToObject(bindings, "normal", normal);
	cJSON_AddItemToObject(bindings, "hypershift", hyper);
	cJSON_AddItemToObject(root, "bindings", bindings);
	bindings = NULL;

	add_bindings(normal, hyper, sectors);
	normal = NULL;
	hyper = NULL;

	raw = full;
	cJSON_AddItemToObject(settings, "polling",
			      cJSON_CreateNumber(poll_hz(raw[0])));
	cJSON_AddItemToObject(settings, "activeSlot",
			      cJSON_CreateNumber(raw[1]));
	cJSON_AddItemToObject(settings, "slotCount",
			      cJSON_CreateNumber(raw[5]));
	for (unsigned i = 0; i < raw[5] && i < REMAP_HP_SECTORS; i++) {
		unsigned o = 6 + 5 * i;
		cJSON *slot = cJSON_CreateObject();
		char color[8];

		snprintf(color, sizeof color, "#%02x%02x%02x", raw[o + 2],
			 raw[o + 3], raw[o + 4]);
		cJSON_AddItemToObject(slot, "enabled",
				      cJSON_CreateBool(raw[o] != 0));
		cJSON_AddItemToObject(slot, "level",
				      cJSON_CreateNumber(raw[o + 1]));
		cJSON_AddItemToObject(slot, "color",
				      cJSON_CreateString(color));
		cJSON_AddItemToArray(slots, slot);
	}
	cJSON_AddItemToObject(settings, "slots", slots);
	cJSON_AddItemToObject(root, "settings", settings);
	settings = NULL;
	slots = NULL;

	hex(full, HP_SETTINGS_WRITE_LEN, hexbuf);
	cJSON_AddItemToObject(backup, "settings", cJSON_CreateString(hexbuf));
	for (unsigned n = 0; n < REMAP_HP_SECTORS; n++) {
		hex(sectors + n * HP_SECTOR_SIZE, HP_SECTOR_SIZE, hexbuf);
		cJSON_AddItemToArray(arr, cJSON_CreateString(hexbuf));
	}
	cJSON_AddItemToObject(backup, "sectors", arr);
	cJSON_AddItemToObject(root, "backup", backup);
	arr = NULL;
	backup = NULL;

	add_bindings(cJSON_GetObjectItem(root, "bindings")->child,
		     cJSON_GetObjectItem(root, "bindings")->child->next,
		     sectors);

	hp_close();
	free(sectors);
	free(hexbuf);
	return root;

fail:
	hp_close();
	free(sectors);
	free(hexbuf);
	cJSON_Delete(root);
	cJSON_Delete(bindings);
	cJSON_Delete(normal);
	cJSON_Delete(hyper);
	cJSON_Delete(settings);
	cJSON_Delete(slots);
	cJSON_Delete(backup);
	cJSON_Delete(arr);
	cJSON_Delete(match);
	return NULL;
}
