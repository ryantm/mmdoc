/* SPDX-License-Identifier: CC0-1.0 */
#pragma once

#include "inputs.h"
#include "render.h"
#include "types.h"

[[nodiscard]] int mmdoc_multi_write_from_chapters(
    Inputs inputs, AnchorLocationArray toc_anchor_locations,
    const size_t *page_indices, size_t page_count,
    AnchorLocationArray anchor_locations, const ChapterOutputs *chapters,
    size_t num_workers);
