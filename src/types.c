/* SPDX-License-Identifier: CC0-1.0 */
#include "types.h"
#include <stdlib.h>
#include <string.h>

void init_anchor_location_array(AnchorLocationArray *a, size_t initialSize) {
  a->array = malloc(initialSize * sizeof(*a->array));
  a->used = 0;
  a->size = initialSize;
  a->anchor_index = NULL;
  a->anchor_index_size = 0;
}

void insert_anchor_location_array(AnchorLocationArray *a,
                                  AnchorLocation *element) {
  free(a->anchor_index);
  a->anchor_index = NULL;
  a->anchor_index_size = 0;
  if (a->used == a->size) {
    a->size = a->size == 0 ? 1 : a->size * 2;
    a->array = realloc(a->array, a->size * sizeof(*a->array));
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
  a->anchor_index = NULL;
  a->anchor_index_size = 0;

  if (a->used == 0)
    return 0;

  size_t index_size = 1;
  while (index_size < a->used * 2)
    index_size *= 2;

  size_t *index = calloc(index_size, sizeof(*index));
  if (index == NULL)
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
  if (a->anchor_index != NULL) {
    size_t slot = anchor_hash(anchor) & (a->anchor_index_size - 1);
    while (a->anchor_index[slot] != 0) {
      AnchorLocation *location = &a->array[a->anchor_index[slot] - 1];
      if (strcmp(anchor, location->anchor) == 0)
        return location;
      slot = (slot + 1) & (a->anchor_index_size - 1);
    }
    return NULL;
  }

  for (size_t i = 0; i < a->used; i++)
    if (strcmp(anchor, a->array[i].anchor) == 0)
      return &a->array[i];
  return NULL;
}

void free_anchor_location_array(AnchorLocationArray *a) {
  free(a->anchor_index);
  free(a->array);
  a->array = NULL;
  a->anchor_index = NULL;
  a->anchor_index_size = 0;
  a->used = a->size = 0;
}

void init_array(Array *a, size_t initialSize) {
  a->array = malloc(initialSize * sizeof *a->array);
  a->used = 0;
  a->size = initialSize;
}

void insert_array(Array *a, char *element) {
  if (a->used == a->size) {
    a->size = a->size == 0 ? 1 : a->size * 2;
    a->array = realloc(a->array, a->size * sizeof *a->array);
  }
  a->array[a->used] = malloc(strlen(element) + 1);
  strcpy(a->array[a->used], element);
  a->used++;
}

void free_array(Array *a) {
  for (int i = 0; i < a->used; i++)
    free(a->array[i]);
  free(a->array);
  a->array = NULL;
  a->used = a->size = 0;
}
