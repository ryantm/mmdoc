/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include "inputs.h"
#include <stddef.h>

void mmdoc_run_parallel(size_t workers, void *(*fn)(void *), void *ctx);
[[nodiscard]] size_t *mmdoc_sort_desc_order(const size_t *weights,
                                            size_t count);
[[nodiscard]] int mmdoc_build(Inputs inputs);
