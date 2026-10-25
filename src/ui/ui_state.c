/* SPDX-License-Identifier: GPL-2.0-or-later OR BSD-2-Clause */
/*
 * ui_state.c — device and editor state for naga-ui.
 *
 * Everything here is free of SDL and ImGui so the behaviour can be reasoned
 * about (and tested) without a window. Only the functions in naga.c talk to
 * the mouse.
 */
#include "ui.h"

#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "remap/config.h"

/* The firmware needs a moment between reads; bursts of reads start echoing. */
#define READ_SETTLE_US 12000

/* The thumb grid is the interesting part of the device: select its first
 * control so the editor is populated from the start. */
static int first_thumbctrl(void)
{
	for (size_t i = 0; i < naga_ctrls_count; i++)
		if (naga_ctrls[i].group == NAGA_GROUP_THUMB)
			return (int)i;
	return 0;
}

static int host_index(const naga_ctrl *ctrl)
{
	if (!ctrl || !ctrl->host_only)
		return -1;
	if (ctrl->ctr == 0x04)
		return 0;
	if (ctrl->ctr == 0x05)
		return 1;
	return -1;
}

static void key_string(unsigned mod, unsigned usage, char *buf, size_t len)
{
	static const char *const names[] = {
		"ctrl+", "shift+", "alt+", "super+",
		"rctrl+", "rshift+", "ralt+", "rsuper+",
	};
	const char *name = naga_keyname((int)usage);
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
	if (name)
		snprintf(buf + off, len - off, "%s", name);
	else
		snprintf(buf + off, len - off, "%02x", usage);
}

static int record_action(const naga_rec *rec, char *buf, size_t len)
{
	switch (rec->type) {
	case NAGA_ASSIGN_KEY:
		key_string(rec->mod, rec->usage, buf, len);
		return 1;
	case NAGA_ASSIGN_MOUSE: {
		const char *name = naga_mouse_name(rec->mouse);

		snprintf(buf, len, "mouse:%s", name ? name : "?");
		return 1;
	}
	case NAGA_ASSIGN_HYPERSHIFT:
		snprintf(buf, len, "hypershift");
		return 1;
	case NAGA_ASSIGN_TURBO: {
		char combo[64];
		unsigned ms = rec->rate ? 1000000u / rec->rate : 0;

		if (!ms)
			return 0;
		key_string(rec->mod, rec->usage, combo, sizeof combo);
		snprintf(buf, len, "turbo:%s@%u", combo, ms);
		return 1;
	}
	case NAGA_ASSIGN_DEFAULT:
		snprintf(buf, len, "default");
		return 1;
	default:
		return 0;
	}
}

static int host_action(const struct ui_host_binding *h, char *buf, size_t len)
{
	if (!h->set)
		return 0;
	if (h->action == UI_ACTION_MOUSE) {
		const char *name = naga_mouse_name(h->mouse);

		snprintf(buf, len, "mouse:%s", name ? name : "?");
		return 1;
	}
	if (h->action == UI_ACTION_KEY) {
		key_string(h->mod, h->usage, buf, len);
		return 1;
	}
	return 0;
}

static void json_kv(FILE *fp, bool *first, const char *id, const char *value)
{
	if (!*first)
		fputs(",\n", fp);
	fprintf(fp, "      \"%s\": \"%s\"", id, value);
	*first = false;
}

void ui_state_init(ui_state *s)
{
	memset(s, 0, sizeof *s);
	s->selected = first_thumbctrl();
	s->action = UI_ACTION_KEY;
	s->mouse_button = NAGA_MOUSE_LEFT;
	s->turbo_ms = 500.0f;
	s->dpi_x = 1600;
	s->dpi_y = 1600;
	ui_state_set_status(s, false, "not connected - click Connect");
}

void ui_state_set_status(ui_state *s, bool is_error, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(s->status, sizeof s->status, fmt, ap);
	va_end(ap);
	s->status_is_error = is_error;
}

int ui_state_reload(ui_state *s)
{
	size_t count =
	    naga_ctrls_count < UI_MAX_CTRLS ? naga_ctrls_count : UI_MAX_CTRLS;
	int failed = 0;

	if (!s->connected)
		return (int)count;

	for (size_t i = 0; i < count; i++) {
		if (naga_ctrls[i].host_only) {
			s->rec_ok[i] = false;
			continue;
		}
		s->rec_ok[i] = naga_get_rec(naga_ctrls[i].ctr, &s->recs[i]) == NAGA_OK;
		if (!s->rec_ok[i])
			failed++;
		usleep(READ_SETTLE_US);
	}
	if (s->selected >= 0)
		ui_state_select(s, s->selected); /* refresh the editor fields */
	return failed;
}

int ui_state_connect(ui_state *s)
{
	int rc = naga_open();

	if (rc != NAGA_OK) {
		s->connected = false;
		ui_state_set_status(s, true, "%s", naga_strerror(rc));
		return rc;
	}

	s->connected = true;
	{
		int failed = ui_state_reload(s);

		if (failed)
			ui_state_set_status(s, false,
					    "connected, %d of %zu records unreadable",
					    failed, naga_ctrls_count);
		else
			ui_state_set_status(s, false, "connected, %zu records read",
					    naga_ctrls_count);
	}
	return NAGA_OK;
}

const naga_ctrl *ui_state_selected(const ui_state *s)
{
	if (s->selected < 0 || (size_t)s->selected >= naga_ctrls_count)
		return NULL;
	return &naga_ctrls[s->selected];
}

bool ui_state_can_apply(const ui_state *s)
{
	const naga_ctrl *ctrl = ui_state_selected(s);

	if (!ctrl)
		return false;
	if (ctrl->host_only)
		return true;
	return s->connected;
}

bool ui_state_selected_host(const ui_state *s)
{
	const naga_ctrl *ctrl = ui_state_selected(s);

	return ctrl && ctrl->host_only;
}

void ui_state_binding_string_at(const ui_state *s, int index, char *buf,
				size_t len)
{
	const naga_ctrl *ctrl;

	if (!len)
		return;
	buf[0] = '\0';
	if (index < 0 || (size_t)index >= naga_ctrls_count)
		return;
	ctrl = &naga_ctrls[index];
	if (ctrl->host_only) {
		int hi = host_index(ctrl);

		if (!host_action(&s->host[hi], buf, len))
			snprintf(buf, len, "unmapped");
		return;
	}
	if ((size_t)index < UI_MAX_CTRLS && s->rec_ok[index])
		naga_binding_name(&s->recs[index], buf, len);
	else
		snprintf(buf, len, "?");
}

void ui_state_select(ui_state *s, int index)
{
	const naga_rec *rec;
	const naga_ctrl *ctrl;
	const char *name;
	int mod;

	if (index < 0 || (size_t)index >= naga_ctrls_count) {
		s->selected = -1;
		return;
	}

	s->selected = index;
	s->recording_key = false;
	ctrl = &naga_ctrls[index];

	if (ctrl->host_only) {
		int hi = host_index(ctrl);
		const struct ui_host_binding *h = &s->host[hi];

		s->rec_ok[index] = false;
		s->action = h->set ? h->action : UI_ACTION_KEY;
		s->mouse_button = (h->set && h->action == UI_ACTION_MOUSE) ?
				      h->mouse :
				      NAGA_MOUSE_LEFT;
		s->key[0] = '\0';
		if (h->set && h->action == UI_ACTION_KEY) {
			const char *key = naga_keyname((int)h->usage);

			if (key)
				snprintf(s->key, sizeof s->key, "%s", key);
			else
				snprintf(s->key, sizeof s->key, "%02x",
					 h->usage);
		}
		s->mod_ctrl = h->set && (h->mod & 0x01);
		s->mod_shift = h->set && (h->mod & 0x02);
		s->mod_alt = h->set && (h->mod & 0x04);
		s->mod_super = h->set && (h->mod & 0x08);
		s->turbo = false;
		s->layer = 0;
		s->key_filter[0] = '\0';
		return;
	}

	rec = &s->recs[index];
	mod = rec->mod & 0x0F;

	s->action = UI_ACTION_KEY;
	if (rec->type == NAGA_ASSIGN_MOUSE) {
		s->action = UI_ACTION_MOUSE;
		s->mouse_button = rec->mouse;
	} else if (rec->type == NAGA_ASSIGN_HYPERSHIFT) {
		s->action = UI_ACTION_HYPERSHIFT;
	}

	s->mod_ctrl = (mod & 0x01) != 0;
	s->mod_shift = (mod & 0x02) != 0;
	s->mod_alt = (mod & 0x04) != 0;
	s->mod_super = (mod & 0x08) != 0;
	s->turbo = rec->type == NAGA_ASSIGN_TURBO;
	s->layer = rec->layer & 0x01;

	if (s->turbo && rec->rate)
		s->turbo_ms = 1000000.0f / (float)rec->rate;

	name = naga_keyname(rec->usage);
	if (name)
		snprintf(s->key, sizeof s->key, "%s", name);
	else if (rec->valid)
		snprintf(s->key, sizeof s->key, "%02x", rec->usage);
	else
		s->key[0] = '\0';
}

void ui_state_refresh_selected(ui_state *s)
{
	const naga_ctrl *ctrl = ui_state_selected(s);
	int rc;

	if (!ctrl) {
		ui_state_set_status(s, true, "select a control first");
		return;
	}
	if (ctrl->host_only) {
		ui_state_select(s, s->selected);
		ui_state_set_status(s, false, "%s: host binding reloaded",
				    ctrl->name);
		return;
	}
	rc = naga_get_rec(ctrl->ctr, &s->recs[s->selected]);
	s->rec_ok[s->selected] = rc == NAGA_OK;
	if (rc != NAGA_OK) {
		ui_state_set_status(s, true, "%s: read failed: %s", ctrl->name,
				    naga_strerror(rc));
		return;
	}
	ui_state_select(s, s->selected);
	ui_state_set_status(s, false, "%s: record re-read", ctrl->name);
}

void ui_state_set_key(ui_state *s, unsigned usage, unsigned mod)
{
	const char *name = naga_keyname((int)usage);

	s->action = UI_ACTION_KEY;
	if (name)
		snprintf(s->key, sizeof s->key, "%s", name);
	else
		snprintf(s->key, sizeof s->key, "%02x", usage);

	s->mod_ctrl = (mod & 0x01) != 0;
	s->mod_shift = (mod & 0x02) != 0;
	s->mod_alt = (mod & 0x04) != 0;
	s->mod_super = (mod & 0x08) != 0;
	s->recording_key = false;
}

void ui_state_set_mouse(ui_state *s, int button)
{
	s->action = UI_ACTION_MOUSE;
	s->mouse_button = button;
	s->recording_key = false;
}

void ui_state_set_hypershift(ui_state *s)
{
	if (ui_state_selected_host(s)) {
		ui_state_set_status(s, true,
				    "wheel up/down support a key or mouse button");
		return;
	}
	s->action = UI_ACTION_HYPERSHIFT;
	s->recording_key = false;
}

void ui_state_begin_key_record(ui_state *s)
{
	s->recording_key = true;
	ui_state_set_status(s, false, "listening - press a key, Esc cancels");
}

void ui_state_cancel_key_record(ui_state *s)
{
	if (!s->recording_key)
		return;
	s->recording_key = false;
	ui_state_set_status(s, false, "recording cancelled");
}

void ui_state_apply(ui_state *s)
{
	const naga_ctrl *ctrl = ui_state_selected(s);
	char what[64];
	int usage, mod, rc;

	if (!ctrl) {
		ui_state_set_status(s, true, "select a control first");
		return;
	}
	if (ctrl->host_only) {
		int hi = host_index(ctrl);
		struct ui_host_binding *h = &s->host[hi];

		if (s->action == UI_ACTION_HYPERSHIFT) {
			ui_state_set_status(
			    s, true,
			    "%s is host-side; choose a key or mouse button",
			    ctrl->name);
			return;
		}
		if (s->action == UI_ACTION_MOUSE) {
			h->action = UI_ACTION_MOUSE;
			h->mouse = s->mouse_button;
			h->usage = 0;
			h->mod = 0;
		} else {
			usage = naga_parse_usage(s->key);
			if (usage < 0) {
				ui_state_set_status(
				    s, true, "unknown key \"%s\"", s->key);
				return;
			}
			mod = (s->mod_ctrl ? 0x01 : 0) |
			      (s->mod_shift ? 0x02 : 0) |
			      (s->mod_alt ? 0x04 : 0) |
			      (s->mod_super ? 0x08 : 0);
			h->action = UI_ACTION_KEY;
			h->usage = (unsigned)usage;
			h->mod = (unsigned)mod;
			h->mouse = 0;
		}
		h->set = true;
		s->rec_ok[s->selected] = false;
		ui_state_set_status(
		    s, false,
		    "%s: saved for the daemon config (not written to the mouse)",
		    ctrl->name);
		return;
	}
	if (!s->connected) {
		ui_state_set_status(s, true, "not connected");
		return;
	}

	switch (s->action) {
	case UI_ACTION_MOUSE: {
		const char *name = naga_mouse_name(s->mouse_button);

		rc = naga_set_mouse(ctrl->ctr, (unsigned)s->mouse_button,
				    (unsigned)s->layer);
		snprintf(what, sizeof what, "mouse %s", name ? name : "?");
		break;
	}
	case UI_ACTION_HYPERSHIFT:
		rc = naga_set_hypershift(ctrl->ctr, (unsigned)s->layer);
		snprintf(what, sizeof what, "hypershift");
		break;
	case UI_ACTION_KEY:
	default:
		usage = naga_parse_usage(s->key);
		if (usage < 0) {
			ui_state_set_status(
			    s, true, "unknown key \"%s\" (x, f5, enter, 1a)", s->key);
			return;
		}
		mod = (s->mod_ctrl ? 0x01 : 0) | (s->mod_shift ? 0x02 : 0) |
		      (s->mod_alt ? 0x04 : 0) | (s->mod_super ? 0x08 : 0);

		if (s->turbo) {
			rc = naga_set_turbo(ctrl->ctr, (unsigned)usage, (unsigned)mod,
					    (unsigned)s->turbo_ms);
			snprintf(what, sizeof what, "turbo %s", s->key);
		} else {
			rc = naga_set_key(ctrl->ctr, (unsigned)usage, (unsigned)mod,
					  (unsigned)s->layer);
			snprintf(what, sizeof what, "%s%s%s%s%s",
				 mod & 0x01 ? "ctrl+" : "", mod & 0x02 ? "shift+" : "",
				 mod & 0x04 ? "alt+" : "", mod & 0x08 ? "super+" : "",
				 s->key);
		}
		break;
	}

	if (rc != NAGA_OK) {
		ui_state_set_status(s, true, "%s: write failed: %s", ctrl->name,
				    naga_strerror(rc));
		return;
	}

	s->rec_ok[s->selected] =
	    naga_get_rec(ctrl->ctr, &s->recs[s->selected]) == NAGA_OK;
	ui_state_set_status(s, false, "%s: %s committed", ctrl->name, what);
	/*
	 * Keep the editor fields as entered. The read-back layout of
	 * non-keyboard assignments is not fully understood, so re-deriving the
	 * form from it could contradict a write the device just accepted; the
	 * "current:" line shows what was read back instead.
	 */
}

void ui_state_apply_dpi(ui_state *s)
{
	int rc;

	if (!s->connected) {
		ui_state_set_status(s, true, "not connected");
		return;
	}
	rc = naga_set_dpi((unsigned)s->dpi_x, (unsigned)s->dpi_y);
	if (rc != NAGA_OK) {
		ui_state_set_status(s, true, "DPI: write failed: %s",
				    naga_strerror(rc));
		return;
	}
	ui_state_set_status(s, false, "DPI set to %dx%d and committed", s->dpi_x,
			    s->dpi_y);
}

void ui_state_export_config(ui_state *s)
{
	char path[REMAP_PATH_MAX];
	char err[256] = "";
	FILE *fp;
	bool first;

	if (!s->connected) {
		ui_state_set_status(
		    s, true, "connect first: records are exported from the mouse");
		return;
	}
	if (remap_ensure_config_dir(err, sizeof err) < 0) {
		ui_state_set_status(s, true, "%s", err);
		return;
	}
	remap_config_path(path, sizeof path);
	fp = fopen(path, "w");
	if (!fp) {
		ui_state_set_status(s, true, "%s: %s", path, strerror(errno));
		return;
	}

	fputs("{\n  \"device\": \"2020\",\n  \"bindings\": {\n", fp);

	fputs("    \"normal\": {\n", fp);
	first = true;
	for (size_t i = 0; i < naga_ctrls_count; i++) {
		const naga_ctrl *ctrl = &naga_ctrls[i];
		naga_rec rec;
		char value[96];

		if (ctrl->host_only)
			continue;
		if (naga_get_rec_layer(ctrl->ctr, 0, &rec) != NAGA_OK ||
		    !rec.valid)
			continue;
		if (!record_action(&rec, value, sizeof value))
			continue;
		json_kv(fp, &first, ctrl->name, value);
	}
	for (int hi = 0; hi < UI_HOST_COUNT; hi++) {
		char value[96];

		if (!host_action(&s->host[hi], value, sizeof value))
			continue;
		json_kv(fp, &first, hi == 0 ? "wheel-up" : "wheel-down",
			value);
	}
	fputs("\n    },\n", fp);

	fputs("    \"hypershift\": {\n", fp);
	first = true;
	for (size_t i = 0; i < naga_ctrls_count; i++) {
		const naga_ctrl *ctrl = &naga_ctrls[i];
		naga_rec rec;
		char value[96];

		if (ctrl->host_only)
			continue;
		if (naga_get_rec_layer(ctrl->ctr, 1, &rec) != NAGA_OK ||
		    !rec.valid)
			continue;
		if (!record_action(&rec, value, sizeof value))
			continue;
		json_kv(fp, &first, ctrl->name, value);
	}
	fputs("\n    }\n  }\n}\n", fp);

	if (fclose(fp) != 0) {
		ui_state_set_status(s, true, "%s: %s", path, strerror(errno));
		return;
	}
	if (remap_signal_daemon(err, sizeof err) == 0)
		ui_state_set_status(s, false, "wrote %s; daemon reloaded", path);
	else
		ui_state_set_status(s, false, "wrote %s; %s", path, err);
}
