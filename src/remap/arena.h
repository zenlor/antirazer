/* SPDX-License-Identifier: GPL-2.0-or-later OR BSD-2-Clause */
#ifndef NAGA_REMAP_ARENA_H
#define NAGA_REMAP_ARENA_H

#include <stddef.h>

struct arena_block;

struct arena {
	struct arena_block *first;
	size_t block_size;
};

void arena_init(struct arena *a, size_t block_size);
void *arena_alloc(struct arena *a, size_t size, size_t align);
char *arena_strdup(struct arena *a, const char *s);
void arena_free(struct arena *a);

#endif /* NAGA_REMAP_ARENA_H */
