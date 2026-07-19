/* SPDX-License-Identifier: CC0-1.0 */
#include "anchors.h"
#include "files.h"
#include "inputs.h"
#include "multi.h"
#include "refs.h"
#include "render.h"
#include "single.h"
#include "types.h"
#include <mmdocconfig.h>

extern int errno;

void print_usage() {
  printf("mmdoc version %s - minimal markdown documentation\n", MMDOC_VERSION);
  printf("\n");
  printf("mmdoc PROJECT-NAME SRC OUT\n");
  printf("\n");
  printf("options:\n");
  printf("-h, --help                show help\n");
  printf("-v, --version             show version\n");
  printf("\n");
  printf("PROJECT-NAME is the name of the project the documentation is "
         "generated for.\n");
  printf("\n");
  printf("SRC a directory containing Markdown files; a file called toc.md at "
         "the top level\n");
  printf("is required.\n");
  printf("\n");
  printf("OUT a directory where the documentation is written to.\n");
}

int main(int argc, char *argv[]) {
  if (argc != 4) {
    print_usage();
    return 1;
  }
  int result = 1;
  Inputs inputs = {0};
  Array toc_refs = {0};
  Array md_files = {0};
  AnchorLocationArray anchor_locations = {0};
  AnchorLocationArray toc_anchor_locations = {0};

  if (0 != mmdoc_inputs_derive(&inputs, argv))
    goto cleanup;

  if (0 != mmdoc_refs(&toc_refs, inputs.toc_path))
    goto cleanup;

  init_array(&md_files, 100);
  mmdoc_md_files(&md_files, inputs.src);

  if (0 !=
      mmdoc_anchors_locations(&anchor_locations, &md_files, &toc_refs, inputs))
    goto cleanup;

  init_anchor_location_array(&toc_anchor_locations, toc_refs.used);
  if (0 != mmdoc_anchors_find_toc_anchors(&toc_anchor_locations, &toc_refs,
                                          &anchor_locations))
    goto cleanup;
  free_array(&toc_refs);

  if (0 != mmdoc_single(inputs, toc_anchor_locations))
    goto cleanup;

  if (0 != mmdoc_multi(inputs, toc_anchor_locations, anchor_locations))
    goto cleanup;

  if (0 != copy_imgs(inputs))
    goto cleanup;

  result = 0;

cleanup:
  free_array(&toc_refs);
  free_array(&md_files);
  free_anchor_location_array(&toc_anchor_locations);
  free_anchor_location_array_deep(&anchor_locations);
  mmdoc_inputs_free(&inputs);
  return result;
}
