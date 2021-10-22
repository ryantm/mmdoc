/* SPDX-License-Identifier: CC0-1.0 */
#include "inputs.h"
#include "types.h"

int mmdoc_anchors_locations(AnchorLocationArray *anchor_locations,
                            Array *md_files, Array *toc_refs, Inputs inputs);
int mmdoc_anchors_find_toc_anchors(AnchorLocationArray *toc_anchor_locations,
                                   Array *toc_refs,
                                   AnchorLocationArray *anchor_locations);
