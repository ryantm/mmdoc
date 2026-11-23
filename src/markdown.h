/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include "inputs.h"
#include "types.h"

int mmdoc_markdown(Inputs inputs, AnchorLocationArray toc,
                   const size_t *page_indices, size_t page_count,
                   AnchorLocationArray anchors);
