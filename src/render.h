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

int mmdoc_render_single(char *out, char *toc_path, Array toc_refs,
                        AnchorLocationArray anchor_locations);
int mmdoc_render_epub(char *out, char *out_single, char *project_name);
int mmdoc_render_multi(char *out, char *src, char *toc_path, Array toc_refs,
                       AnchorLocationArray anchor_locations);
int mmdoc_render_man(char *out, char *src, char *toc_path, Array toc_refs,
                     AnchorLocationArray anchor_locations);
void mmdoc_render_part(char *file_path, FILE *output_file,
                       render_type render_type,
                       AnchorLocationArray anchor_locations,
                       char *multipage_url, FILE *search_index_path);
