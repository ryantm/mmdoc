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

/* Returns code-block presence (0/1), or -1 on input/output failure. */
int mmdoc_render_part(char *file_path, FILE *output_file,
                      render_type render_type, AnchorLocation *anchor_location,
                      AnchorLocationArray anchor_locations, char *multipage_url,
                      FILE *search_index_path);

char *mmdoc_render_get_title_from_file(char *file_path);

int mmdoc_render_collect_anchors(char *file_path, Array *anchors);

/* Collect both from one parse. Either output may be NULL; the caller owns
 * the returned title (an empty string when no anchored heading exists). */
int mmdoc_render_collect_metadata(char *file_path, Array *anchors,
                                  char **title);
