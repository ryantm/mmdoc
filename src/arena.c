/* SPDX-License-Identifier: CC0-1.0 */
#include "arena.h"
#include <stdckdint.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static constexpr size_t ARENA_ALIGN = 16;

static bool align_up(size_t value, size_t *out) {
  size_t bumped = 0;
  if (ckd_add(&bumped, value, ARENA_ALIGN - 1))
    return false;
  *out = bumped & ~(ARENA_ALIGN - 1);
  return true;
}

static MmdocArenaSlab *new_slab(size_t capacity) {
  size_t alloc_size = 0;
  if (ckd_add(&alloc_size, sizeof(MmdocArenaSlab), capacity))
    return nullptr;
  MmdocArenaSlab *slab = malloc(alloc_size);
  if (slab == nullptr)
    return nullptr;
  slab->next = nullptr;
  slab->capacity = capacity;
  slab->used = 0;
  return slab;
}

void mmdoc_arena_init(MmdocArena *arena, size_t initial_slab_size) {
  arena->head = nullptr;
  arena->current = nullptr;
  arena->default_slab_size =
      initial_slab_size >= 4096 ? initial_slab_size : 65536;
}

void mmdoc_arena_destroy(MmdocArena *arena) {
  MmdocArenaSlab *slab = arena->head;
  while (slab != nullptr) {
    MmdocArenaSlab *next = slab->next;
    free(slab);
    slab = next;
  }
  arena->head = nullptr;
  arena->current = nullptr;
}

void *mmdoc_arena_alloc(MmdocArena *arena, size_t size) {
  size_t total = 0;
  if (!align_up(size == 0 ? 1 : size, &total))
    return nullptr;
  MmdocArenaSlab *slab = arena->current;
  while (slab != nullptr && slab->capacity - slab->used < total)
    slab = slab->next;
  if (slab == nullptr) {
    size_t next_cap = arena->default_slab_size;
    if (arena->current != nullptr && arena->current->capacity < 4 * 1024 * 1024)
      next_cap = arena->current->capacity * 2;
    if (next_cap < total)
      next_cap = total;
    slab = new_slab(next_cap);
    if (slab == nullptr)
      return nullptr;
    if (arena->head == nullptr)
      arena->head = slab;
    else {
      MmdocArenaSlab *tail =
          arena->current != nullptr ? arena->current : arena->head;
      while (tail->next != nullptr)
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
  if (copy != nullptr)
    memcpy(copy, s, len);
  return copy;
}
