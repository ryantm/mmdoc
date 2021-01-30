/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <unistd.h>

typedef struct {
  char **array;
  size_t used;
  size_t size;
} Array;

typedef struct {
  char *file_path;
  char *anchor;
} AnchorLocation;

typedef struct {
  AnchorLocation *array;
  size_t used;
  size_t size;
} AnchorLocationArray;
