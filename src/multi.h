/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <stdio.h>

#include "asset.h"
#include "inputs.h"
#include "types.h"

int mmdoc_multi_page(Inputs inputs, AnchorLocationArray anchor_locations,
                     const char *toc_html, int toc_has_code_block,
                     const AssetFileNames *asset_names,
                     const char *search_index_name, FILE *search_index_file,
                     long *search_index_name_offset,
                     AnchorLocation *anchor_location,
                     AnchorLocation *prev_anchor_location,
                     AnchorLocation *next_anchor_location,
                     int *has_code_blocks);
int mmdoc_multi(Inputs inputs, AnchorLocationArray toc_anchor_locations,
                AnchorLocationArray anchor_locations);
