/* SPDX-License-Identifier: CC0-1.0 */
#include "inputs.h"
#include "files.h"
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

  inputs->toc_path = mmdoc_file_path(inputs->src, "toc.md");
  if (inputs->toc_path == nullptr) {
    fprintf(stderr, "Fatal: failed to allocate memory for toc path.\n");
    mmdoc_inputs_free(inputs);
    return -1;
  }
  if (access(inputs->toc_path, F_OK) != 0) {
    printf("Expected but did not find: \"%s\"\n", inputs->toc_path);
    mmdoc_inputs_free(inputs);
    return -1;
  }

  inputs->out_multi = mmdoc_file_path(inputs->out, "multi");
  inputs->out_single = mmdoc_file_path(inputs->out, "single");
  if (inputs->out_multi == nullptr || inputs->out_single == nullptr) {
    fprintf(stderr, "Fatal: failed to allocate memory for output paths.\n");
    mmdoc_inputs_free(inputs);
    return -1;
  }
  return 0;
}

void mmdoc_inputs_free(Inputs *inputs) {
  free(inputs->src);
  free(inputs->out);
  free(inputs->toc_path);
  free(inputs->out_multi);
  free(inputs->out_single);
  *inputs = (Inputs){};
}
