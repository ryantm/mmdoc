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
  char *multipage_output_file_path;
  char *multipage_output_directory_path;
  char *multipage_url;
  char *multipage_base_href;
  char *man_output_file_path;
  char *man_header;
  char *anchor;
  char *title;
} AnchorLocation;

typedef struct {
  AnchorLocation *array;
  size_t used;
  size_t size;
} AnchorLocationArray;

typedef struct {
  char *anchor;
} HeadingInfo;

void init_anchor_location_array(AnchorLocationArray *a, size_t initialSize);
void insert_anchor_location_array(AnchorLocationArray *a,
                                  AnchorLocation *element);
void free_anchor_location_array(AnchorLocationArray *a);
void init_array(Array *a, size_t initialSize);
void insert_array(Array *a, char *element);
void free_array(Array *a);
