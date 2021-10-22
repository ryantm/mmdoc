/* SPDX-License-Identifier: CC0-1.0 */
#pragma once

typedef struct {
  char *project_name;
  char *src;
  char *out;
  char *toc_path;
  char *out_multi;
  char *out_man;
} Inputs;

int mmdoc_inputs_derive(Inputs *inputs, char *argv[]);
