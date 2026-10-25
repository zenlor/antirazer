/* SPDX-License-Identifier: GPL-2.0-or-later OR BSD-2-Clause */
#ifndef NAGA_REMAP_RUN_H
#define NAGA_REMAP_RUN_H

#include <stddef.h>

#include "remap/map.h"

/*
 * Work out which mouse the evdev node belongs to. If evdev is NULL, scan
 * /dev/input for a Naga; returns REMAP_DEVICE_AUTO and fills err when the
 * answer is ambiguous or nothing is found.
 */
enum remap_device remap_guess_model(const char *evdev, char *err,
				    size_t errlen);

/* Print candidate Naga event nodes. */
void remap_list_devices(void);

/*
 * Grab the device and replay the map's sequence bindings (2020) or every
 * binding (2014) through uinput. For the 2020 the onboard bindings/triggers
 * must already have been applied; the caller does that.
 */
int remap_run(const struct remap_map *map, enum remap_device model,
	      const char *evdev, char *err, size_t errlen);

#endif /* NAGA_REMAP_RUN_H */
