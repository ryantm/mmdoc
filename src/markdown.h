/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include "inputs.h"
#include "render.h"
#include "types.h"

int mmdoc_markdown(Inputs inputs, AnchorLocationArray toc,
                   const size_t *page_indices, size_t page_count,
                   AnchorLocationArray anchors);

int mmdoc_markdown_write_from_chapters(Inputs inputs, AnchorLocationArray toc,
                                       const size_t *page_indices,
                                       size_t page_count,
                                       const ChapterOutputs *chapters);
int mmdoc_markdown_write_index_entry(FILE *index,
                                     const AnchorLocation *chapter);
