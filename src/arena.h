/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
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
[[nodiscard]] void *mmdoc_arena_alloc(MmdocArena *arena, size_t size);
[[nodiscard]] char *mmdoc_arena_strdup(MmdocArena *arena, const char *s);
