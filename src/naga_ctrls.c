/* SPDX-License-Identifier: GPL-2.0-or-later OR BSD-2-Clause */
/*
 * naga_ctrls.c — the controls this device exposes, in physical order.
 *
 * Control instances (ctr) are the values the firmware uses in a mapping
 * record. Only the thumb-grid entries have been verified to accept writes so
 * far; the rest are read back from the device for display.
 */
#include "naga.h"

#include <string.h>

const naga_ctrl naga_ctrls[] = {
    /* main buttons */
    {"left", 0x01, NAGA_GROUP_MAIN, false},
    {"right", 0x02, NAGA_GROUP_MAIN, false},
    {"middle", 0x03, NAGA_GROUP_MAIN, false},

    /* wheel rotation is a HID axis, handled by the userspace daemon */
    {"wheel-up", 0x04, NAGA_GROUP_WHEEL, true},
    {"wheel-down", 0x05, NAGA_GROUP_WHEEL, true},

    /* the wheel click is a main button; scroll up/down is a HID axis */

    /* DPI keys */
    {"dpi-up", 0x0B, NAGA_GROUP_DPI, false},
    {"dpi-down", 0x0C, NAGA_GROUP_DPI, false},

    /* thumb grid: three columns, four rows, labelled as on the device */
    {"1", 0x40, NAGA_GROUP_THUMB, false},
    {"2", 0x41, NAGA_GROUP_THUMB, false},
    {"3", 0x42, NAGA_GROUP_THUMB, false},
    {"4", 0x43, NAGA_GROUP_THUMB, false},
    {"5", 0x44, NAGA_GROUP_THUMB, false},
    {"6", 0x45, NAGA_GROUP_THUMB, false},
    {"7", 0x46, NAGA_GROUP_THUMB, false},
    {"8", 0x47, NAGA_GROUP_THUMB, false},
    {"9", 0x48, NAGA_GROUP_THUMB, false},
    {"0", 0x49, NAGA_GROUP_THUMB, false},
    {"=", 0x4B, NAGA_GROUP_THUMB, false},
    {"-", 0x4A, NAGA_GROUP_THUMB, false},
};

const size_t naga_ctrls_count = sizeof naga_ctrls / sizeof naga_ctrls[0];

const naga_ctrl *naga_ctrl_by_ctr(unsigned ctr)
{
	for (size_t i = 0; i < naga_ctrls_count; i++)
		if (naga_ctrls[i].ctr == ctr)
			return &naga_ctrls[i];
	return NULL;
}

const naga_ctrl *naga_ctrl_by_name(const char *name)
{
	if (!name)
		return NULL;
	for (size_t i = 0; i < naga_ctrls_count; i++)
		if (!strcmp(naga_ctrls[i].name, name))
			return &naga_ctrls[i];
	return NULL;
}
