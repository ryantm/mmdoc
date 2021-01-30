#include "types.h"
#include <stdlib.h>
#include <string.h>

void init_anchor_location_array(AnchorLocationArray *a, size_t initialSize) {
  a->array = malloc(initialSize * sizeof(*a->array));
  a->used = 0;
  a->size = initialSize;
}

void insert_anchor_location_array(AnchorLocationArray *a,
                                  AnchorLocation *element) {
  if (a->used == a->size) {
    a->size *= 2;
    a->array = realloc(a->array, a->size * sizeof(*a->array));
  }
  a->array[a->used] = *element;
  a->used++;
}

void free_anchor_location_array(AnchorLocationArray *a) {
  free(a->array);
  a->array = NULL;
  a->used = a->size = 0;
}

void init_array(Array *a, size_t initialSize) {
  a->array = malloc(initialSize * sizeof *a->array);
  a->used = 0;
  a->size = initialSize;
}

void insert_array(Array *a, char *element) {
  if (a->used == a->size) {
    a->size *= 2;
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
