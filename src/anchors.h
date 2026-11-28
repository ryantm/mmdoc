/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include "inputs.h"
#include "types.h"

[[nodiscard]] int mmdoc_anchors(Array *md_anchors, char *path);
[[nodiscard]] int mmdoc_anchors_add_file(AnchorLocationArray *anchor_locations,
                                         const char *file_path,
                                         const Array *anchors,
                                         const char *title, bool is_index_page,
                                         Inputs inputs);
[[nodiscard]] int mmdoc_anchors_locations(AnchorLocationArray *anchor_locations,
                                          Array *md_files, Array *toc_refs,
                                          Inputs inputs);
[[nodiscard]] int
mmdoc_anchors_find_toc_anchors(AnchorLocationArray *toc_anchor_locations,
                               Array *toc_refs,
                               AnchorLocationArray *anchor_locations);
