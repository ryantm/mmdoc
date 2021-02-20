/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include "types.h"

int mmdoc_epub(char *out, char *out_epub_file, char *toc_path,
               AnchorLocationArray toc_anchor_locations, char *project_name);
