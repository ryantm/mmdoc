/* SPDX-License-Identifier: CC0-1.0 */
#include "inputs.h"
#include "mkdir_p.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char *dup_strip_trailing_slashes(const char *path) {
  size_t len = strlen(path);
  while (len > 1 && path[len - 1] == '/')
    len--;
  return strndup(path, len);
}

int mmdoc_inputs_derive(Inputs *inputs, char *argv[]) {
  *inputs = (Inputs){.project_name = argv[1]};

  inputs->src = dup_strip_trailing_slashes(argv[2]);
  inputs->out = dup_strip_trailing_slashes(argv[3]);
  if (inputs->src == nullptr || inputs->out == nullptr) {
    fprintf(stderr, "Fatal: failed to allocate memory for input paths.\n");
    mmdoc_inputs_free(inputs);
    return -1;
  }

  const char *src_relative_toc_path =
      strcmp(inputs->src, "/") == 0 ? "toc.md" : "/toc.md";
  char *toc_path =
      malloc(strlen(inputs->src) + strlen(src_relative_toc_path) + 1);
  if (toc_path == nullptr) {
    fprintf(stderr, "Fatal: failed to allocate memory for toc path.\n");
    mmdoc_inputs_free(inputs);
    return -1;
  }
  strcpy(toc_path, inputs->src);
  strcat(toc_path, src_relative_toc_path);
  if (access(toc_path, F_OK) != 0) {
    printf("Expected but did not find: \"%s\"\n", toc_path);
    free(toc_path);
    mmdoc_inputs_free(inputs);
    return -1;
  }
  inputs->toc_path = toc_path;

  const char *sep = strcmp(inputs->out, "/") == 0 ? "" : "/";
  const char *multi = "multi";
  char *out_multi =
      malloc(strlen(inputs->out) + strlen(sep) + strlen(multi) + 1);
  if (out_multi == nullptr) {
    fprintf(stderr,
            "Fatal: failed to allocate memory for multi output path.\n");
    mmdoc_inputs_free(inputs);
    return -1;
  }
  sprintf(out_multi, "%s%s%s", inputs->out, sep, multi);
  inputs->out_multi = out_multi;

  const char *single = "single";
  char *out_single =
      malloc(strlen(inputs->out) + strlen(sep) + strlen(single) + 1);
  if (out_single == nullptr) {
    fprintf(stderr,
            "Fatal: failed to allocate memory for single output path.\n");
    mmdoc_inputs_free(inputs);
    return -1;
  }
  sprintf(out_single, "%s%s%s", inputs->out, sep, single);
  inputs->out_single = out_single;

  return 0;
}

void mmdoc_inputs_free(Inputs *inputs) {
  free(inputs->src);
  free(inputs->out);
  free(inputs->toc_path);
  free(inputs->out_multi);
  free(inputs->out_single);
  inputs->src = nullptr;
  inputs->out = nullptr;
  inputs->toc_path = nullptr;
  inputs->out_multi = nullptr;
  inputs->out_single = nullptr;
}
