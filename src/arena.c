/* SPDX-License-Identifier: CC0-1.0 */
#include "arena.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define ARENA_ALIGN 16
#define ARENA_HEADER_SIZE 16

typedef struct {
  size_t size;
  size_t reserved;
} ArenaAllocHeader;

static _Thread_local MmdocArena *current_thread_arena = NULL;

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

MmdocArena *mmdoc_arena_bind(MmdocArena *arena) {
  MmdocArena *previous = current_thread_arena;
  current_thread_arena = arena;
  return previous;
}

static void *arena_alloc_raw(MmdocArena *arena, size_t size, int zero) {
  size_t payload = align_up(size == 0 ? 1 : size);
  if (payload > SIZE_MAX - ARENA_HEADER_SIZE)
    return NULL;
  size_t total = ARENA_HEADER_SIZE + payload;

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
    if (arena->head == NULL) {
      arena->head = slab;
    } else {
      MmdocArenaSlab *tail =
          arena->current != NULL ? arena->current : arena->head;
      while (tail->next != NULL)
        tail = tail->next;
      tail->next = slab;
    }
  }

  arena->current = slab;
  size_t offset = slab->used;
  slab->last_offset = offset;
  slab->used = offset + total;

  ArenaAllocHeader *header = (ArenaAllocHeader *)(slab->data + offset);
  header->size = size;
  header->reserved = 0;
  void *ptr = slab->data + offset + ARENA_HEADER_SIZE;
  if (zero && size > 0)
    memset(ptr, 0, size);
  return ptr;
}

static void *arena_cmark_calloc(size_t nmem, size_t size) {
  if (size != 0 && nmem > SIZE_MAX / size)
    return NULL;
  return arena_alloc_raw(current_thread_arena, nmem * size, 1);
}

static void *arena_cmark_realloc(void *ptr, size_t size) {
  MmdocArena *arena = current_thread_arena;
  if (ptr == NULL)
    return arena_alloc_raw(arena, size, 0);
  if (size == 0)
    return NULL;

  ArenaAllocHeader *header =
      (ArenaAllocHeader *)((unsigned char *)ptr - ARENA_HEADER_SIZE);
  size_t old_size = header->size;
  if (size <= old_size) {
    header->size = size;
    return ptr;
  }

  /* Grow in place if ptr is the most recent allocation in the current slab. */
  MmdocArenaSlab *slab = arena->current;
  if (slab != NULL && slab->last_offset != SIZE_MAX) {
    unsigned char *last_ptr =
        slab->data + slab->last_offset + ARENA_HEADER_SIZE;
    if (last_ptr == (unsigned char *)ptr) {
      size_t new_total = ARENA_HEADER_SIZE + align_up(size);
      if (slab->last_offset + new_total <= slab->capacity) {
        slab->used = slab->last_offset + new_total;
        header->size = size;
        return ptr;
      }
    }
  }

  void *new_ptr = arena_alloc_raw(arena, size, 0);
  if (new_ptr == NULL)
    return NULL;
  memcpy(new_ptr, ptr, old_size);
  return new_ptr;
}

static void arena_cmark_free(void *ptr) {
  if (ptr == NULL)
    return;
  MmdocArena *arena = current_thread_arena;
  MmdocArenaSlab *slab = arena->current;
  if (slab != NULL && slab->last_offset != SIZE_MAX) {
    unsigned char *last_ptr =
        slab->data + slab->last_offset + ARENA_HEADER_SIZE;
    if (last_ptr == (unsigned char *)ptr) {
      slab->used = slab->last_offset;
      slab->last_offset = SIZE_MAX;
    }
  }
}

void *mmdoc_arena_alloc(MmdocArena *arena, size_t size) {
  return arena_alloc_raw(arena, size, 0);
}

char *mmdoc_arena_strdup(MmdocArena *arena, const char *s) {
  size_t len = strlen(s) + 1;
  char *copy = arena_alloc_raw(arena, len, 0);
  if (copy != NULL)
    memcpy(copy, s, len);
  return copy;
}

static cmark_mem arena_allocator = {
    .calloc = arena_cmark_calloc,
    .realloc = arena_cmark_realloc,
    .free = arena_cmark_free,
};

cmark_mem *mmdoc_arena_cmark_mem(void) { return &arena_allocator; }

cmark_mem *mmdoc_arena_current_cmark_mem(void) {
  return current_thread_arena != NULL ? &arena_allocator
                                      : cmark_get_default_mem_allocator();
}
