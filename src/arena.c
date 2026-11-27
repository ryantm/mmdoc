/* SPDX-License-Identifier: CC0-1.0 */
#include "arena.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define ARENA_ALIGN 16

static size_t align_up(size_t value) {
  return (value + (ARENA_ALIGN - 1)) & ~(size_t)(ARENA_ALIGN - 1);
}

static MmdocArenaSlab *new_slab(size_t capacity) {
  MmdocArenaSlab *slab = malloc(sizeof(MmdocArenaSlab) + capacity);
  if (slab == NULL)
    return NULL;
  slab->next = NULL;
  slab->capacity = capacity;
  slab->used = 0;
  slab->last_offset = SIZE_MAX;
  return slab;
}

void mmdoc_arena_init(MmdocArena *arena, size_t initial_slab_size) {
  arena->head = NULL;
  arena->current = NULL;
  arena->default_slab_size =
      initial_slab_size >= 4096 ? initial_slab_size : 65536;
}

void mmdoc_arena_reset(MmdocArena *arena) {
  for (MmdocArenaSlab *slab = arena->head; slab != NULL; slab = slab->next) {
    slab->used = 0;
    slab->last_offset = SIZE_MAX;
  }
  arena->current = arena->head;
}

void mmdoc_arena_destroy(MmdocArena *arena) {
  MmdocArenaSlab *slab = arena->head;
  while (slab != NULL) {
    MmdocArenaSlab *next = slab->next;
    free(slab);
    slab = next;
  }
  arena->head = NULL;
  arena->current = NULL;
}

void *mmdoc_arena_alloc(MmdocArena *arena, size_t size) {
  size_t total = align_up(size == 0 ? 1 : size);
  MmdocArenaSlab *slab = arena->current;
  while (slab != NULL && slab->capacity - slab->used < total)
    slab = slab->next;
  if (slab == NULL) {
    size_t next_cap = arena->default_slab_size;
    if (arena->current != NULL && arena->current->capacity < 4 * 1024 * 1024)
      next_cap = arena->current->capacity * 2;
    if (next_cap < total)
      next_cap = total;
    slab = new_slab(next_cap);
    if (slab == NULL)
      return NULL;
    if (arena->head == NULL)
      arena->head = slab;
    else {
      MmdocArenaSlab *tail =
          arena->current != NULL ? arena->current : arena->head;
      while (tail->next != NULL)
        tail = tail->next;
      tail->next = slab;
    }
  }
  arena->current = slab;
  void *ptr = slab->data + slab->used;
  slab->used += total;
  return ptr;
}

char *mmdoc_arena_strdup(MmdocArena *arena, const char *s) {
  size_t len = strlen(s) + 1;
  char *copy = mmdoc_arena_alloc(arena, len);
  if (copy != NULL)
    memcpy(copy, s, len);
  return copy;
}
