/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include "inputs.h"
#include "render.h"
#include "types.h"

[[nodiscard]] int mmdoc_single_write_from_chapters(
    Inputs inputs, AnchorLocationArray anchor_locations,
    const ChapterOutputs *chapters, size_t page_count);
