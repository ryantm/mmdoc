/* SPDX-License-Identifier: CC0-1.0 */
#include "types.h"
#include "arena.h"
#include <stdckdint.h>
#include <stdlib.h>
#include <string.h>

#if __has_include(<stdbit.h>)
#include <stdbit.h>
static size_t bit_ceil_size(size_t n) { return stdc_bit_ceil(n); }
#else
static size_t bit_ceil_size(size_t n) {
  size_t pow2 = 1;
  while (pow2 < n) {
    if (ckd_mul(&pow2, pow2, 2))
      return 0;
  }
  return pow2;
}
#endif

void init_anchor_location_array(AnchorLocationArray *a, size_t initialSize) {
  size_t bytes = 0;
  a->array =
      (initialSize > 0 && !ckd_mul(&bytes, initialSize, sizeof(*a->array)))
          ? malloc(bytes)
          : nullptr;
  a->used = 0;
  a->size = a->array != nullptr ? initialSize : 0;
  a->anchor_index = nullptr;
  a->anchor_index_size = 0;
  a->string_arena = nullptr;
}

int reserve_anchor_location_array(AnchorLocationArray *a, size_t additional) {
  size_t needed = 0;
  if (ckd_add(&needed, a->used, additional))
    return -1;
  if (needed <= a->size)
    return 0;
  size_t next_size = a->size == 0 ? 64 : a->size;
  while (next_size < needed) {
    if (ckd_mul(&next_size, next_size, 2))
      return -1;
  }
  size_t bytes = 0;
  if (ckd_mul(&bytes, next_size, sizeof(*a->array)))
    return -1;
  AnchorLocation *next = realloc(a->array, bytes);
  if (next == nullptr)
    return -1;
  a->array = next;
  a->size = next_size;
  return 0;
}

void insert_anchor_location_array(AnchorLocationArray *a,
                                  AnchorLocation *element) {
  free(a->anchor_index);
  a->anchor_index = nullptr;
  a->anchor_index_size = 0;
  if (a->used == a->size) {
    size_t next_size = 1;
    if (a->size != 0 && ckd_mul(&next_size, a->size, 2))
      return;
    size_t bytes = 0;
    if (ckd_mul(&bytes, next_size, sizeof(*a->array)))
      return;
    AnchorLocation *next = realloc(a->array, bytes);
    if (next == nullptr)
      return;
    a->array = next;
    a->size = next_size;
  }
  a->array[a->used] = *element;
  a->used++;
}

static size_t anchor_hash(const char *anchor) {
  size_t hash = 5381;
  for (const unsigned char *c = (const unsigned char *)anchor; *c != '\0'; c++)
    hash = ((hash << 5) + hash) ^ *c;
  return hash;
}

int build_anchor_location_index(AnchorLocationArray *a) {
  free(a->anchor_index);
  a->anchor_index = nullptr;
  a->anchor_index_size = 0;

  if (a->used == 0)
    return 0;

  size_t min_slots = 0;
  if (ckd_mul(&min_slots, a->used, 2))
    return -1;
  size_t index_size = bit_ceil_size(min_slots);
  if (index_size == 0)
    return -1;

  size_t *index = calloc(index_size, sizeof(*index));
  if (index == nullptr)
    return -1;

  for (size_t i = 0; i < a->used; i++) {
    size_t slot = anchor_hash(a->array[i].anchor) & (index_size - 1);
    while (index[slot] != 0) {
      size_t existing = index[slot] - 1;
      if (strcmp(a->array[existing].anchor, a->array[i].anchor) == 0)
        break;
      slot = (slot + 1) & (index_size - 1);
    }
    if (index[slot] == 0)
      index[slot] = i + 1;
  }

  a->anchor_index = index;
  a->anchor_index_size = index_size;
  return 0;
}

AnchorLocation *find_anchor_location(AnchorLocationArray *a,
                                     const char *anchor) {
  if (a->anchor_index != nullptr) {
    size_t slot = anchor_hash(anchor) & (a->anchor_index_size - 1);
    while (a->anchor_index[slot] != 0) {
      AnchorLocation *location = &a->array[a->anchor_index[slot] - 1];
      if (strcmp(anchor, location->anchor) == 0)
        return location;
      slot = (slot + 1) & (a->anchor_index_size - 1);
    }
    return nullptr;
  }

  for (size_t i = 0; i < a->used; i++)
    if (strcmp(anchor, a->array[i].anchor) == 0)
      return &a->array[i];
  return nullptr;
}

void free_anchor_location(AnchorLocation *location) {
  free(location->file_path);
  free(location->multipage_output_file_path);
  free(location->multipage_output_directory_path);
  free(location->multipage_url);
  free(location->multipage_base_href);
  free(location->anchor);
  free(location->title);
  *location = (AnchorLocation){};
}

void free_anchor_location_array(AnchorLocationArray *a) {
  free(a->anchor_index);
  free(a->array);
  a->array = nullptr;
  a->anchor_index = nullptr;
  a->anchor_index_size = 0;
  a->used = a->size = 0;
}

void free_anchor_location_array_deep(AnchorLocationArray *a) {
  if (a->string_arena != nullptr) {
    mmdoc_arena_destroy(a->string_arena);
    free(a->string_arena);
    a->string_arena = nullptr;
  } else {
    for (size_t i = 0; i < a->used; i++)
      free_anchor_location(&a->array[i]);
  }
  free_anchor_location_array(a);
}

void init_array(Array *a, size_t initialSize) {
  size_t bytes = 0;
  a->array =
      (initialSize > 0 && !ckd_mul(&bytes, initialSize, sizeof(*a->array)))
          ? malloc(bytes)
          : nullptr;
  a->used = 0;
  a->size = a->array != nullptr ? initialSize : 0;
}

void insert_array(Array *a, char *element) {
  if (a->used == a->size) {
    size_t next_size = 1;
    if (a->size != 0 && ckd_mul(&next_size, a->size, 2))
      return;
    size_t bytes = 0;
    if (ckd_mul(&bytes, next_size, sizeof(*a->array)))
      return;
    char **next = realloc(a->array, bytes);
    if (next == nullptr)
      return;
    a->array = next;
    a->size = next_size;
  }
  char *copy = strdup(element);
  if (copy == nullptr)
    return;
  a->array[a->used] = copy;
  a->used++;
}

void free_array(Array *a) {
  for (size_t i = 0; i < a->used; i++)
    free(a->array[i]);
  free(a->array);
  a->array = nullptr;
  a->used = a->size = 0;
}

static const char *page_dedup_key(const AnchorLocation *loc) {
  if (loc->multipage_output_file_path != nullptr)
    return loc->multipage_output_file_path;
  return loc->file_path;
}

size_t mmdoc_collect_unique_page_indices(AnchorLocationArray toc_locations,
                                         size_t *page_indices) {
  if (toc_locations.used == 0)
    return 0;

  size_t min_slots = 0;
  size_t table_size =
      ckd_mul(&min_slots, toc_locations.used, 2) ? 0 : bit_ceil_size(min_slots);
  size_t *seen = table_size > 0 ? calloc(table_size, sizeof(*seen)) : nullptr;
  size_t page_count = 0;

  if (seen != nullptr) {
    for (size_t i = 0; i < toc_locations.used; i++) {
      const char *key = page_dedup_key(&toc_locations.array[i]);
      if (key == nullptr) {
        page_indices[page_count++] = i;
        continue;
      }
      size_t slot = anchor_hash(key) & (table_size - 1);
      bool duplicate = false;
      while (seen[slot] != 0) {
        const char *existing =
            page_dedup_key(&toc_locations.array[seen[slot] - 1]);
        if (existing != nullptr && strcmp(existing, key) == 0) {
          duplicate = true;
          break;
        }
        slot = (slot + 1) & (table_size - 1);
      }
      if (!duplicate) {
        seen[slot] = i + 1;
        page_indices[page_count++] = i;
      }
    }
    free(seen);
    return page_count;
  }

  for (size_t i = 0; i < toc_locations.used; i++) {
    const char *key = page_dedup_key(&toc_locations.array[i]);
    bool duplicate = false;
    if (key != nullptr) {
      for (size_t j = 0; j < page_count; j++) {
        const char *existing =
            page_dedup_key(&toc_locations.array[page_indices[j]]);
        if (existing != nullptr && strcmp(existing, key) == 0) {
          duplicate = true;
          break;
        }
      }
    }
    if (!duplicate)
      page_indices[page_count++] = i;
  }
  return page_count;
}
