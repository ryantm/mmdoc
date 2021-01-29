#pragma once
#include "types.h"
#include <stdio.h>

int mmdoc_render_single(char *out, char *toc_path, Array toc_refs,
                        AnchorLocationArray anchor_locations);
int mmdoc_render_multi(char *out, char *src, char *toc_path, Array toc_refs,
                       AnchorLocationArray anchor_locations);
void mmdoc_render_part(char *file_path, FILE *output_file);
