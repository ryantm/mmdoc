#include "inputs.h"
#include "mkdir_p.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int mmdoc_inputs_derive(Inputs *inputs, char *argv[]) {
  inputs->project_name = argv[1];
  inputs->src = argv[2];
  inputs->out = argv[3];

  char *src_relative_toc_path = "/toc.md";
  char *toc_path =
      malloc(strlen(inputs->src) + strlen(src_relative_toc_path) + 1);
  if (toc_path == NULL) {
    fprintf(stderr, "Fatal: failed to allocate memory for toc path.\n");
    return -1;
  }
  strcpy(toc_path, inputs->src);
  strcat(toc_path, src_relative_toc_path);
  if (access(toc_path, F_OK) != 0) {
    printf("Expected but did not find: \"%s\"", toc_path);
    return -1;
  }
  inputs->toc_path = toc_path;

  char *multi = "multi";
  char *out_multi = malloc(strlen(inputs->out) + 1 + strlen(multi) + 1);
  strcpy(out_multi, inputs->out);
  strcat(out_multi, "/");
  strcat(out_multi, multi);
  inputs->out_multi = out_multi;

  char *single = "single";
  char *out_single = malloc(strlen(inputs->out) + 1 + strlen(single) + 1);
  strcpy(out_single, inputs->out);
  strcat(out_single, "/");
  strcat(out_single, single);
  inputs->out_single = out_single;

  return 0;
}
