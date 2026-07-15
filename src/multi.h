/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <stdio.h>

#include "inputs.h"
#include "types.h"

int mmdoc_multi_page(Inputs inputs, AnchorLocationArray anchor_locations,
                     FILE *search_index_file, AnchorLocation *anchor_location,
                     AnchorLocation *prev_anchor_location,
                     AnchorLocation *next_anchor_location);
int mmdoc_multi(Inputs inputs, AnchorLocationArray toc_anchor_locations,
                AnchorLocationArray anchor_locations);
