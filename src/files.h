/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include "inputs.h"

void mmdoc_md_files(Array *md_files, char *base_path);
void mmdoc_img_files(Array *img_files, char *base_path);
int copy_imgs(Inputs inputs);
