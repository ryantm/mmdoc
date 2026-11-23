/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include "inputs.h"
#include "types.h"

char *mmdoc_file_path(const char *directory, const char *name);
int mmdoc_md_files(Array *md_files, Inputs inputs);
int copy_imgs(Inputs inputs);
