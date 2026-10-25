/* SPDX-License-Identifier: GPL-2.0-or-later OR BSD-2-Clause */
/* Unit tests for the UI staging queue (no GLFW/ImGui/device needed). */
#include <stdio.h>
#include <string.h>

#include "ui/ui.h"

static int failures;

#define CHECK(cond)                                                      \
	do {                                                             \
		if (!(cond)) {                                           \
			fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, \
				#cond);                                  \
			failures++;                                      \
		}                                                        \
	} while (0)

int main(void)
{
	ui_state s;
	int first, second, wheel, before;
	char buf[64];

	ui_state_init(&s);
	first = s.selected;
	CHECK(first >= 0);
	CHECK(ui_state_pending_count(&s) == 0);

	s.action = UI_ACTION_KEY;
	snprintf(s.key, sizeof s.key, "x");
	s.mod_ctrl = true;
	s.mod_shift = s.mod_alt = s.mod_super = false;
	s.turbo = false;
	ui_state_stage(&s);
	CHECK(ui_state_pending_count(&s) == 1);
	CHECK(ui_state_pending_at(&s, first));
	ui_state_pending_string_at(&s, first, buf, sizeof buf);
	CHECK(!strcmp(buf, "ctrl+x"));

	second = (first + 1) % (int)naga_ctrls_count;
	ui_state_select(&s, second);
	CHECK(!ui_state_pending_at(&s, second));
	s.action = UI_ACTION_KEY;
	snprintf(s.key, sizeof s.key, "y");
	s.mod_ctrl = false;
	ui_state_stage(&s);
	CHECK(ui_state_pending_count(&s) == 2);

	/* Selecting a staged control restores the staged edit. */
	ui_state_select(&s, first);
	CHECK(!strcmp(s.key, "x"));
	CHECK(s.mod_ctrl);

	ui_state_discard_selected(&s);
	CHECK(ui_state_pending_count(&s) == 1);
	CHECK(!ui_state_pending_at(&s, first));

	wheel = (int)(naga_ctrl_by_name("wheel-up") - naga_ctrls);
	CHECK(wheel >= 0 && wheel < (int)naga_ctrls_count);
	CHECK(naga_ctrls[wheel].host_only);
	ui_state_select(&s, wheel);
	CHECK(s.action == UI_ACTION_KEY);
	s.action = UI_ACTION_KEY;
	snprintf(s.key, sizeof s.key, "pgup");
	s.mod_ctrl = s.mod_shift = s.mod_alt = s.mod_super = false;
	ui_state_stage(&s);
	CHECK(ui_state_pending_count(&s) == 2);
	CHECK(ui_state_pending_at(&s, wheel));
	ui_state_pending_string_at(&s, wheel, buf, sizeof buf);
	CHECK(!strcmp(buf, "pgup"));

	/* Re-selecting restores host and device staged edits independently. */
	ui_state_select(&s, second);
	CHECK(!strcmp(s.key, "y"));
	ui_state_select(&s, wheel);
	CHECK(!strcmp(s.key, "pgup"));

	ui_state_select(&s, first);
	s.action = UI_ACTION_MOUSE;
	s.mouse_button = NAGA_MOUSE_MIDDLE;
	ui_state_stage(&s);
	CHECK(ui_state_pending_at(&s, first));
	ui_state_pending_string_at(&s, first, buf, sizeof buf);
	CHECK(!strcmp(buf, "mouse:middle"));

	/* Invalid editor state must not replace a queued change. */
	ui_state_select(&s, second);
	before = ui_state_pending_count(&s);
	s.action = UI_ACTION_KEY;
	snprintf(s.key, sizeof s.key, "bogus");
	ui_state_stage(&s);
	CHECK(ui_state_pending_count(&s) == before);
	CHECK(s.status_is_error);

	ui_state_discard_all(&s);
	CHECK(ui_state_pending_count(&s) == 0);

	if (failures) {
		fprintf(stderr, "%d queue check(s) failed\n", failures);
		return 1;
	}
	printf("all UI queue tests passed\n");
	return 0;
}
