/* SPDX-License-Identifier: GPL-2.0-or-later OR BSD-2-Clause */
/*
 * ui_state.c — device and editor state for naga-ui.
 *
 * Everything here is free of SDL and ImGui so the behaviour can be reasoned
 * about (and tested) without a window. Only the functions in naga.c talk to
 * the mouse.
 */
#include "ui.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

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

void ui_state_init(ui_state *s)
{
	memset(s, 0, sizeof *s);
	s->selected = first_thumbctrl();
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
	return s->connected && ui_state_selected(s) != NULL;
}

void ui_state_select(ui_state *s, int index)
{
	const naga_rec *rec;
	const char *name;
	int mod;

	if (index < 0 || (size_t)index >= naga_ctrls_count) {
		s->selected = -1;
		return;
	}

	s->selected = index;
	s->recording_key = false;
	rec = &s->recs[index];
	mod = rec->mod & 0x0F;

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
	int usage, mod, rc;

	if (!ctrl) {
		ui_state_set_status(s, true, "select a control first");
		return;
	}
	if (!s->connected) {
		ui_state_set_status(s, true, "not connected");
		return;
	}

	usage = naga_parse_usage(s->key);
	if (usage < 0) {
		ui_state_set_status(s, true, "unknown key \"%s\" (x, f5, enter, 1a)",
				    s->key);
		return;
	}

	mod = (s->mod_ctrl ? 0x01 : 0) | (s->mod_shift ? 0x02 : 0) |
	      (s->mod_alt ? 0x04 : 0) | (s->mod_super ? 0x08 : 0);

	if (s->turbo)
		rc = naga_set_turbo(ctrl->ctr, (unsigned)usage, (unsigned)mod,
				    (unsigned)s->turbo_ms);
	else
		rc = naga_set_key(ctrl->ctr, (unsigned)usage, (unsigned)mod,
				  (unsigned)s->layer);

	if (rc != NAGA_OK) {
		ui_state_set_status(s, true, "%s: write failed: %s", ctrl->name,
				    naga_strerror(rc));
		return;
	}

	s->rec_ok[s->selected] =
	    naga_get_rec(ctrl->ctr, &s->recs[s->selected]) == NAGA_OK;
	ui_state_set_status(s, false, "%s: %s%s committed", ctrl->name,
			    s->turbo ? "turbo " : "", s->key);
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
