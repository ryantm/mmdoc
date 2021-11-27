/* SPDX-License-Identifier: CC0-1.0 */
#include "anchors.h"
#include "epub.h"
#include "files.h"
#include "inputs.h"
#include "man.h"
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
  Inputs inputs;
  if (0 != mmdoc_inputs_derive(&inputs, argv))
    return 1;

  Array toc_refs;
  if (0 != mmdoc_refs(&toc_refs, inputs.toc_path))
    return 1;

  Array md_files;
  mmdoc_md_files(&md_files, inputs.src);

  AnchorLocationArray anchor_locations;
  if (0 !=
      mmdoc_anchors_locations(&anchor_locations, &md_files, &toc_refs, inputs))
    return 1;

  AnchorLocationArray toc_anchor_locations;
  init_anchor_location_array(&toc_anchor_locations, toc_refs.used);
  if (0 != mmdoc_anchors_find_toc_anchors(&toc_anchor_locations, &toc_refs,
                                          &anchor_locations))
    return 1;
  free_array(&toc_refs);

  if (0 != mmdoc_single(inputs, toc_anchor_locations))
    return 1;

  if (0 != mmdoc_multi(inputs,
                       toc_anchor_locations, anchor_locations))
    return 1;

  if (0 != mmdoc_man(inputs, toc_anchor_locations, anchor_locations))
    return 1;

  if (0 != mmdoc_epub(inputs, toc_anchor_locations))
    return 1;

  if (0 != copy_imgs(inputs))
    return 1;

  free_array(&md_files);
  free_anchor_location_array(&toc_anchor_locations);
  free_anchor_location_array(&anchor_locations);
  return 0;
}
