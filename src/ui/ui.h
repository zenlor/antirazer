/* SPDX-License-Identifier: GPL-2.0-or-later OR BSD-2-Clause */
/*
 * ui.h — state and entry points for naga-ui.
 *
 * The UI is split into three modules:
 *   ui_state.c   device/editor state and every action that touches the device
 *   ui_layout.c  all widget and ImGui drawing code
 *   main.c       SDL3 window, renderer and frame loop
 *
 * ui_state contains no ImGui or SDL types so the device logic stays testable
 * and independent of the rendering path.
 */
#ifndef ANTIRAZER_UI_H
#define ANTIRAZER_UI_H

#include <stdbool.h>
#include <stddef.h>

#include "naga.h"

#define UI_MAX_CTRLS 32
#define UI_KEY_MAX 32
#define UI_STATUS_MAX 160

typedef struct {
	/* device */
	bool connected;
	int selected; /* index into naga_ctrls, -1 = nothing selected */
	naga_rec recs[UI_MAX_CTRLS];
	bool rec_ok[UI_MAX_CTRLS];

	/* editor fields for the selected control */
	char key[UI_KEY_MAX];
	bool mod_ctrl;
	bool mod_shift;
	bool mod_alt;
	bool mod_super;
	bool turbo;
	float turbo_ms;
	int layer; /* enum naga_layer */

	/* device DPI */
	int dpi_x;
	int dpi_y;

	/* status line */
	char status[UI_STATUS_MAX];
	bool status_is_error;
} ui_state;

void ui_state_init(ui_state *s);

/* Open the device and read every record. Returns 0 on success. */
int ui_state_connect(ui_state *s);

/* Read every record; returns the number of failed reads. */
int ui_state_reload(ui_state *s);

/* Select a control by index into naga_ctrls and load the editor fields. */
void ui_state_select(ui_state *s, int index);

/* Write the editor fields to the selected control and commit. */
void ui_state_apply(ui_state *s);

/* Re-read the selected record and reload the editor fields. */
void ui_state_refresh_selected(ui_state *s);

/* Apply the DPI sliders. */
void ui_state_apply_dpi(ui_state *s);

void ui_state_set_status(ui_state *s, bool is_error, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

const naga_ctrl *ui_state_selected(const ui_state *s);
bool ui_state_can_apply(const ui_state *s);

/* Draw the whole UI. Implemented in ui_layout.c (needs ImGui). */
void ui_draw(ui_state *s);

#endif /* ANTIRAZER_UI_H */
