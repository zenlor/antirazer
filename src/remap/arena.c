/* SPDX-License-Identifier: GPL-2.0-or-later OR BSD-2-Clause */
#include "remap/arena.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct arena_block {
	struct arena_block *next;
	size_t used;
	size_t cap;
	unsigned char data[];
};

void arena_init(struct arena *a, size_t block_size)
{
	a->first = NULL;
	a->block_size = block_size ? block_size : 4096;
}

void *arena_alloc(struct arena *a, size_t size, size_t align)
{
	struct arena_block *b;
	uintptr_t base, aligned;
	size_t off, cap;

	if (!a)
		return NULL;
	if (!align)
		align = _Alignof(max_align_t);
	if (size == 0)
		size = 1;
	if (align & (align - 1))
		return NULL;

	b = a->first;
	if (b) {
		off = (b->used + align - 1) & ~(align - 1);
		if (off + size <= b->cap) {
			b->used = off + size;
			return b->data + off;
		}
	}

	cap = a->block_size;
	if (cap < size + align)
		cap = size + align;
	b = malloc(sizeof *b + cap);
	if (!b)
		return NULL;
	b->next = a->first;
	b->used = 0;
	b->cap = cap;
	a->first = b;

	base = (uintptr_t)b->data;
	aligned = (base + align - 1) & ~((uintptr_t)align - 1);
	off = (size_t)(aligned - base);
	if (off + size > cap) {
		/* Cannot happen: cap >= size + align. */
		return NULL;
	}
	b->used = off + size;
	return b->data + off;
}

char *arena_strdup(struct arena *a, const char *s)
{
	size_t len;
	char *copy;

	if (!s)
		return NULL;
	len = strlen(s) + 1;
	copy = arena_alloc(a, len, 1);
	if (copy)
		memcpy(copy, s, len);
	return copy;
}

void arena_free(struct arena *a)
{
	struct arena_block *b = a->first;

	while (b) {
		struct arena_block *next = b->next;

		free(b);
		b = next;
	}
	a->first = NULL;
}
