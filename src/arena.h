/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <cmark-gfm.h>
#include <stddef.h>

typedef struct MmdocArenaSlab {
  struct MmdocArenaSlab *next;
  size_t capacity;
  size_t used;
  size_t last_offset;
  unsigned char data[];
} MmdocArenaSlab;

typedef struct {
  MmdocArenaSlab *head;
  MmdocArenaSlab *current;
  size_t default_slab_size;
} MmdocArena;

void mmdoc_arena_init(MmdocArena *arena, size_t initial_slab_size);
void mmdoc_arena_reset(MmdocArena *arena);
void mmdoc_arena_destroy(MmdocArena *arena);
void *mmdoc_arena_alloc(MmdocArena *arena, size_t size);
char *mmdoc_arena_strdup(MmdocArena *arena, const char *s);

/* Bind arena to the current thread for mmdoc_arena_cmark_mem callbacks. */
MmdocArena *mmdoc_arena_bind(MmdocArena *arena);
cmark_mem *mmdoc_arena_cmark_mem(void);
cmark_mem *mmdoc_arena_current_cmark_mem(void);
