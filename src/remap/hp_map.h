/* SPDX-License-Identifier: GPL-2.0-or-later OR BSD-2-Clause */
#ifndef NAGA_REMAP_HP_H
#define NAGA_REMAP_HP_H

#include <cJSON.h>

#include <stdbool.h>
#include <stddef.h>

#include "remap/map.h"

/* Write settings and sector bindings for an hp-300 map. */
int remap_hp_apply(const struct remap_map *map, bool dry_run, char *err,
		   size_t errlen);

/* Read the mouse and return the shared-envelope object, or NULL. */
cJSON *remap_hp_dump(char *err, size_t errlen);

#endif /* NAGA_REMAP_HP_H */
