/* SPDX-License-Identifier: CC0-1.0 */
#pragma once

typedef struct {
  char *project_name;
  char *src;
  char *out;
  char *toc_path;
  char *out_multi;
  char *out_single;
  char *out_man;
  char *out_epub_dir;
  char *out_epub_file;
} Inputs;

int mmdoc_inputs_derive(Inputs *inputs, char *argv[]);
