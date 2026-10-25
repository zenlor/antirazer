/* SPDX-License-Identifier: GPL-2.0-or-later OR BSD-2-Clause */
/*
 * ui_layout.c — everything ImGui in naga-ui.
 *
 * The window is split into a toolbar, a two-column body (clickable device
 * drawing on the left, binding editor on the right) and a status line.
 * The device drawing is hit-tested with invisible buttons so every control
 * on the mouse is a real, focusable widget.
 */
#include <stdio.h>
#include <string.h>

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui.h"

#include "ui.h"

/* ---- small helpers ------------------------------------------------------ */

static ImVec2 V(float x, float y)
{
	ImVec2 v = {x, y};
	return v;
}

static ImVec4 V4(float r, float g, float b, float a)
{
	ImVec4 v = {r, g, b, a};
	return v;
}

static ImU32 rgba(int r, int g, int b, int a)
{
	return ((ImU32)(unsigned)a << 24) | ((ImU32)(unsigned)b << 16) |
	       ((ImU32)(unsigned)g << 8) | (ImU32)(unsigned)r;
}

#define COL_TEXT rgba(230, 232, 238, 255)
#define COL_MUTED rgba(150, 156, 168, 255)
#define COL_CELL rgba(50, 54, 63, 255)
#define COL_HOVER rgba(66, 72, 84, 255)
#define COL_SELECTED rgba(52, 104, 178, 255)
#define COL_SELECTED_EDGE rgba(120, 175, 245, 255)
#define COL_EDGE rgba(88, 94, 108, 255)
#define COL_BODY rgba(38, 41, 48, 255)
#define COL_HINT rgba(66, 72, 84, 255)

/* ---- device drawing ----------------------------------------------------- */

#define CELL_W 86.0f
#define CELL_H 52.0f
#define CELL_GAP 8.0f
#define GRID_COLS 3
#define GRID_ROWS 4
#define GRID_W (GRID_COLS * CELL_W + (GRID_COLS - 1) * CELL_GAP)
#define GRID_H (GRID_ROWS * CELL_H + (GRID_ROWS - 1) * CELL_GAP)
#define TOP_W GRID_W
#define VIEW_GAP 30.0f
#define CAPTION_H 20.0f

typedef struct {
	ImVec2 origin;
	float scale;
} canvas;

static ImVec2 cpos(const canvas *c, float x, float y)
{
	return V(c->origin.x + x * c->scale, c->origin.y + y * c->scale);
}

static ImVec2 csize(const canvas *c, float w, float h)
{
	return V(w * c->scale, h * c->scale);
}

static void text_left(ImDrawList *dl, ImVec2 pos, ImU32 col, const char *text)
{
	ImDrawList_AddText_Vec2(dl, pos, col, text, NULL);
}

static void text_centered(ImDrawList *dl, ImVec2 center, ImU32 col, const char *text)
{
	ImVec2 size = igCalcTextSize(text, NULL, true, 0.0f);

	ImDrawList_AddText_Vec2(
	    dl, V(center.x - size.x / 2.0f, center.y - size.y / 2.0f), col, text, NULL);
}

/*
 * One clickable control on the drawing. The invisible button is created
 * first so hover state can pick the colours, then the rectangle and its
 * labels are drawn into the window's draw list.
 */
static void draw_region(ui_state *s, ImDrawList *dl, const canvas *c,
			const naga_ctrl *ctrl, int index, float x, float y, float w,
			float h, float rounding, const char *label, bool show_binding)
{
	char id[32];
	char binding[64] = "";
	ImVec2 p = cpos(c, x, y);
	ImVec2 size = csize(c, w, h);
	ImVec2 end = V(p.x + size.x, p.y + size.y);
	ImVec2 center = V(p.x + size.x / 2.0f, p.y + size.y / 2.0f);
	bool selected = s->selected == index;
	bool hovered;
	ImU32 bg;

	snprintf(id, sizeof id, "##ctrl%02X", ctrl->ctr);
	igSetCursorScreenPos(p);
	igInvisibleButton(id, size, ImGuiButtonFlags_MouseButtonLeft);
	hovered = igIsItemHovered(ImGuiHoveredFlags_None);
	if (igIsItemClicked(ImGuiMouseButton_Left))
		ui_state_select(s, index);

	bg = selected ? COL_SELECTED : hovered ? COL_HOVER : COL_CELL;
	ImDrawList_AddRectFilled(dl, p, end, bg, rounding * c->scale, 0);
	ImDrawList_AddRect(dl, p, end, selected ? COL_SELECTED_EDGE : COL_EDGE,
			   rounding * c->scale, 1.0f, 0);

	text_centered(dl, V(center.x, center.y - (show_binding ? 7.0f : 0.0f)),
		      COL_TEXT, label);

	if (show_binding && s->rec_ok[index]) {
		naga_binding_name(&s->recs[index], binding, sizeof binding);
		text_centered(dl, V(center.x, center.y + 7.0f), COL_MUTED, binding);
	}

	if (hovered) {
		if (s->rec_ok[index])
			naga_binding_name(&s->recs[index], binding, sizeof binding);
		igSetTooltip("%s (ctr %02X)\n%s", ctrl->name, ctrl->ctr,
			     binding[0] ? binding : "not read");
	}
}

static void draw_thumb_panel(ui_state *s, ImDrawList *dl, canvas *c)
{
	int thumb = 0;

	for (size_t i = 0; i < naga_ctrls_count; i++) {
		const naga_ctrl *ctrl = &naga_ctrls[i];
		float x, y;

		if (ctrl->group != NAGA_GROUP_THUMB)
			continue;
		x = (float)(thumb % GRID_COLS) * (CELL_W + CELL_GAP);
		y = (float)(thumb / GRID_COLS) * (CELL_H + CELL_GAP);
		draw_region(s, dl, c, ctrl, (int)i, x, y, CELL_W, CELL_H, 6.0f,
			    ctrl->name, CELL_W * c->scale > 62.0f);
		thumb++;
	}
}

static void draw_top_view(ui_state *s, ImDrawList *dl, canvas *c)
{
	static const struct {
		const char *name;
		float x, y, w, h;
		const char *label;
	} regions[] = {
	    {"left", 82.0f, 10.0f, 38.0f, 58.0f, "L"},
	    {"right", 154.0f, 10.0f, 38.0f, 58.0f, "R"},
	    {"wheel-up", 122.0f, 10.0f, 30.0f, 16.0f, "^"},
	    {"middle", 124.0f, 28.0f, 26.0f, 24.0f, "M"},
	    {"wheel-down", 122.0f, 54.0f, 30.0f, 14.0f, "v"},
	    {"dpi-up", 122.0f, 74.0f, 30.0f, 16.0f, "DPI+"},
	    {"dpi-down", 122.0f, 94.0f, 30.0f, 16.0f, "DPI-"},
	};
	ImVec2 body_min = cpos(c, 82.0f, 8.0f);
	ImVec2 body_max = cpos(c, 192.0f, 182.0f);

	/* shell */
	ImDrawList_AddRectFilled(dl, body_min, body_max, COL_BODY, 26.0f * c->scale, 0);
	ImDrawList_AddRect(dl, body_min, body_max, COL_EDGE, 26.0f * c->scale, 1.0f, 0);

	/* hint at the thumb panel on the lower shell */
	for (int row = 0; row < GRID_ROWS; row++) {
		for (int col = 0; col < GRID_COLS; col++) {
			float x = 148.0f + (float)col * 13.0f;
			float y = 112.0f + (float)row * 15.0f;

			ImDrawList_AddRectFilled(dl, cpos(c, x, y),
						 cpos(c, x + 10.0f, y + 11.0f),
						 COL_HINT, 2.0f, 0);
		}
	}
	text_left(dl, cpos(c, 84.0f, 150.0f), COL_MUTED, "thumb panel");

	for (size_t i = 0; i < sizeof regions / sizeof regions[0]; i++) {
		const naga_ctrl *ctrl = naga_ctrl_by_name(regions[i].name);
		int index;

		if (!ctrl)
			continue;
		index = (int)(ctrl - naga_ctrls);
		draw_region(s, dl, c, ctrl, index, regions[i].x, regions[i].y,
			    regions[i].w, regions[i].h, 4.0f, regions[i].label, false);
	}
}

static void draw_device(ui_state *s)
{
	ImDrawList *dl = igGetWindowDrawList();
	ImVec2 start = igGetCursorScreenPos();
	float avail = igGetContentRegionAvail().x;
	float scale = avail / (GRID_W + VIEW_GAP + TOP_W);
	canvas side, top;

	if (scale > 1.25f)
		scale = 1.25f;
	if (scale < 0.7f)
		scale = 0.7f;
	side.origin = V(start.x, start.y + CAPTION_H);
	side.scale = scale;
	top.origin = V(start.x + (GRID_W + VIEW_GAP) * scale, start.y + CAPTION_H);
	top.scale = scale;

	text_left(dl, V(start.x, start.y + 4.0f), COL_MUTED, "thumb panel");
	text_left(dl, V(top.origin.x, start.y + 4.0f), COL_MUTED, "top");

	draw_thumb_panel(s, dl, &side);
	draw_top_view(s, dl, &top);

	/* tell the layout how much room the drawing used */
	igSetCursorScreenPos(V(start.x, start.y + CAPTION_H + GRID_H * scale));
	igDummy(V(1.0f, 1.0f));
}

/* ---- panels ------------------------------------------------------------- */

static void draw_toolbar(ui_state *s)
{
	if (igButton(s->connected ? "Reconnect" : "Connect", V(110.0f, 0.0f)))
		ui_state_connect(s);

	igSameLine(0.0f, 8.0f);
	igBeginDisabled(!s->connected);
	if (igButton("Read all", V(96.0f, 0.0f))) {
		int failed = ui_state_reload(s);

		if (failed)
			ui_state_set_status(s, true,
					    "%d of %zu records could not be read",
					    failed, naga_ctrls_count);
		else
			ui_state_set_status(s, false, "read %zu records",
					    naga_ctrls_count);
	}
	igEndDisabled();

	igSameLine(0.0f, 24.0f);
	if (s->connected)
		igTextColored(V4(0.55f, 0.85f, 0.55f, 1.0f), "device ready");
	else
		igTextColored(V4(0.95f, 0.62f, 0.42f, 1.0f), "no device");

	igSameLine(0.0f, 18.0f);
	igTextDisabled("Razer Naga Left-Handed Edition 2020 (1532:008d)");
}

static void draw_dpi(ui_state *s)
{
	igText("DPI");
	igBeginDisabled(!s->connected);
	igSetNextItemWidth(220.0f);
	igSliderInt("x", &s->dpi_x, 100, 16000, "%d", 0);
	igSetNextItemWidth(220.0f);
	igSliderInt("y", &s->dpi_y, 100, 16000, "%d", 0);
	if (igButton("Apply DPI", V(110.0f, 0.0f)))
		ui_state_apply_dpi(s);
	igEndDisabled();
}

static void draw_editor(ui_state *s)
{
	const naga_ctrl *ctrl = ui_state_selected(s);
	char binding[64];

	if (!ctrl) {
		igTextWrapped("Select a control on the mouse drawing to edit its "
			      "binding.");
	} else {
		igText("%s", ctrl->name);
		igSameLine(0.0f, 10.0f);
		igTextDisabled("ctr %02X", ctrl->ctr);
		igSameLine(0.0f, 10.0f);
		if (!s->rec_ok[s->selected])
			igTextDisabled("(unreadable)");

		if (s->rec_ok[s->selected])
			naga_binding_name(&s->recs[s->selected], binding,
					  sizeof binding);
		else
			snprintf(binding, sizeof binding, "?");
		igText("current: %s", binding);

		igBeginDisabled(!s->connected);
		igSpacing();

		igText("key");
		igSameLine(0.0f, 12.0f);
		igSetNextItemWidth(170.0f);
		if (igInputTextWithHint(
			"##key", "x / f5 / enter / 1a", s->key, sizeof s->key,
			ImGuiInputTextFlags_EnterReturnsTrue, NULL, NULL) &&
		    ui_state_can_apply(s))
			ui_state_apply(s);
		igSameLine(0.0f, 12.0f);
		igTextDisabled("HID usage");

		igCheckbox("ctrl", &s->mod_ctrl);
		igSameLine(0.0f, 14.0f);
		igCheckbox("shift", &s->mod_shift);
		igSameLine(0.0f, 14.0f);
		igCheckbox("alt", &s->mod_alt);
		igSameLine(0.0f, 14.0f);
		igCheckbox("super", &s->mod_super);

		igSetNextItemWidth(150.0f);
		igCombo_Str("layer", &s->layer, "normal\0hypershift\0", 0);

		igCheckbox("turbo (autofire while held)", &s->turbo);
		if (s->turbo) {
			igSetNextItemWidth(220.0f);
			igSliderFloat("delay", &s->turbo_ms, 30.0f, 12000.0f, "%.0f ms",
				      0);
			igSameLine(0.0f, 10.0f);
			igTextDisabled("%.1f s", s->turbo_ms / 1000.0f);
			igTextDisabled("turbo always binds the normal layer");
		}

		igSpacing();
		igBeginDisabled(!ui_state_can_apply(s));
		if (igButton("Write + commit", V(150.0f, 0.0f)))
			ui_state_apply(s);
		igEndDisabled();
		igSameLine(0.0f, 10.0f);
		if (igButton("Re-read", V(90.0f, 0.0f)))
			ui_state_refresh_selected(s);
		igTextWrapped("Writes are stored in the mouse and take effect "
			      "immediately.");
		igEndDisabled();
	}

	igSeparator();
	if (!s->connected)
		igTextWrapped("Start the mouse on this host, install the udev rule, "
			      "then click Connect.");
}

static void draw_status(const ui_state *s)
{
	if (s->status_is_error)
		igTextColored(V4(0.95f, 0.58f, 0.45f, 1.0f), "%s", s->status);
	else
		igText("%s", s->status);
}

void ui_draw(ui_state *s)
{
	ImGuiViewport *viewport = igGetMainViewport();
	ImVec2 avail;
	float table_height;

	igSetNextWindowPos(viewport->Pos, 0, V(0.0f, 0.0f));
	igSetNextWindowSize(viewport->Size, 0);
	igPushStyleVar_Vec2(ImGuiStyleVar_WindowPadding, V(12.0f, 10.0f));
	igBegin("antirazer", NULL,
		ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
		    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
		    ImGuiWindowFlags_NoBringToFrontOnFocus);
	igPopStyleVar(1);

	draw_toolbar(s);
	igSeparator();

	avail = igGetContentRegionAvail();
	table_height = avail.y - igGetFrameHeightWithSpacing() - 6.0f;
	if (table_height < 120.0f)
		table_height = 120.0f;

	if (igBeginTable("layout", 2,
			 ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV |
			     ImGuiTableFlags_SizingStretchProp |
			     ImGuiTableFlags_NoSavedSettings,
			 V(0.0f, table_height), 0.0f)) {
		igTableSetupColumn("device", ImGuiTableColumnFlags_WidthStretch, 1.25f,
				   0);
		igTableSetupColumn("editor", ImGuiTableColumnFlags_WidthStretch, 1.0f,
				   0);
		igTableNextColumn();
		draw_device(s);
		igSpacing();
		draw_dpi(s);
		igTableNextColumn();
		draw_editor(s);
		igEndTable();
	}

	igSeparator();
	draw_status(s);
	igEnd();
}
