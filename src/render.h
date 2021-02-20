/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include "types.h"
#include <stdio.h>

typedef enum {
  RENDER_TYPE_SINGLE,
  RENDER_TYPE_MULTIPAGE,
  RENDER_TYPE_MAN,
  RENDER_TYPE_EPUB
} render_type;

void mmdoc_render_part(char *file_path, FILE *output_file,
                       render_type render_type,
                       AnchorLocationArray anchor_locations,
                       char *multipage_url, FILE *search_index_path);
