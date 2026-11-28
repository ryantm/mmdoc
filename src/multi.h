/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <stdio.h>

#include "asset.h"
#include "inputs.h"
#include "render.h"
#include "types.h"

[[nodiscard]] int mmdoc_multi_page(
    Inputs inputs, AnchorLocationArray anchor_locations, const char *toc_html,
    bool toc_has_code_block, const AssetFileNames *asset_names,
    const char *search_index_name, FILE *search_index_file,
    long *search_index_name_offset, AnchorLocation *anchor_location,
    AnchorLocation *prev_anchor_location, AnchorLocation *next_anchor_location,
    bool *has_code_blocks);
[[nodiscard]] int mmdoc_multi(Inputs inputs,
                              AnchorLocationArray toc_anchor_locations,
                              AnchorLocationArray anchor_locations);
[[nodiscard]] int mmdoc_multi_write_from_chapters(
    Inputs inputs, AnchorLocationArray toc_anchor_locations,
    const size_t *page_indices, size_t page_count,
    AnchorLocationArray anchor_locations, const ChapterOutputs *chapters,
    size_t num_workers);
