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
#define UI_HOST_COUNT 2

/* What the editor is currently building. */
enum ui_action {
	UI_ACTION_KEY,
	UI_ACTION_MOUSE,
	UI_ACTION_HYPERSHIFT,
};

/* Host-side wheel binding, stored in the daemon config instead of onboard. */
struct ui_host_binding {
	enum ui_action action;
	unsigned usage;
	unsigned mod;
	int mouse;
	bool set;
};

typedef struct {
	/* device */
	bool connected;
	int selected; /* index into naga_ctrls, -1 = nothing selected */
	naga_rec recs[UI_MAX_CTRLS];
	bool rec_ok[UI_MAX_CTRLS];

	/* editor fields for the selected control */
	enum ui_action action;
	int mouse_button; /* enum naga_mouse */
	char key[UI_KEY_MAX];
	char key_filter[32]; /* filter text of the key chooser popup */
	bool recording_key;  /* record button armed, waiting for a key */
	bool mod_ctrl;
	bool mod_shift;
	bool mod_alt;
	bool mod_super;
	bool turbo;
	float turbo_ms;
	int layer; /* enum naga_layer */

	/* wheel-up/down: host-side, exported to the daemon config */
	struct ui_host_binding host[UI_HOST_COUNT];

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

/*
 * Set the editor fields from a captured binding: the canonical key name and
 * the modifier checkboxes. Used by the key chooser and the record button.
 */
void ui_state_set_key(ui_state *s, unsigned usage, unsigned mod);

/* Make the selected control act as a mouse button (enum naga_mouse). */
void ui_state_set_mouse(ui_state *s, int button);

/* Make the selected control the Hypershift layer switch. */
void ui_state_set_hypershift(ui_state *s);

/* Arm the record button; the next key press is captured by main.c. */
void ui_state_begin_key_record(ui_state *s);
void ui_state_cancel_key_record(ui_state *s);

/* Apply the DPI sliders. */
void ui_state_apply_dpi(ui_state *s);

void ui_state_set_status(ui_state *s, bool is_error, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

const naga_ctrl *ui_state_selected(const ui_state *s);
bool ui_state_can_apply(const ui_state *s);
bool ui_state_selected_host(const ui_state *s);

/* Current binding string for a control index (device record or host wheel). */
void ui_state_binding_string_at(const ui_state *s, int index, char *buf,
				size_t len);

/* Write the daemon config from the current records and signal the daemon. */
void ui_state_export_config(ui_state *s);

/* Draw the whole UI. Implemented in ui_layout.c (needs ImGui). */
void ui_draw(ui_state *s);

#endif /* ANTIRAZER_UI_H */
