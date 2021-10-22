/* SPDX-License-Identifier: CC0-1.0 */
#include "anchors.h"
#include "epub.h"
#include "files.h"
#include "inputs.h"
#include "man.h"
#include "mkdir_p.h"
#include "multi.h"
#include "refs.h"
#include "render.h"
#include "single.h"
#include "types.h"
#include <dirent.h>
#include <errno.h>
#include <mmdocconfig.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

extern int errno;

void print_usage() {
  printf("mmdoc version %s - minimal markdown documentation\n", MMDOC_VERSION);
  printf("\n");
  printf("mmdoc PROJECT_NAME SRC OUT\n");
  printf("\n");
  printf("options:\n");
  printf("-h, --help                show help\n");
  printf("\n");
  printf("PROJECT_NAME is the name of the project the documentation is "
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
  init_array(&toc_refs, 500);
  mmdoc_refs(&toc_refs, inputs.toc_path);

  if (toc_refs.used == 0) {
    printf("Error toc.md didn't reference any anchor.");
    return 1;
  }

  Array md_files;
  init_array(&md_files, 100);
  mmdoc_md_files(&md_files, inputs.src);

  AnchorLocationArray anchor_locations;
  init_anchor_location_array(&anchor_locations, 500);
  if (0 !=
      mmdoc_anchors_locations(&anchor_locations, &md_files, &toc_refs, inputs))
    return 1;

  AnchorLocationArray toc_anchor_locations;
  init_anchor_location_array(&toc_anchor_locations, toc_refs.used);
  if (0 != mmdoc_anchors_find_toc_anchors(&toc_anchor_locations, &toc_refs,
                                          &anchor_locations))
    return 1;
  free_array(&toc_refs);

  char *single = "single";
  char *out_single = malloc(strlen(inputs.out) + 1 + strlen(single) + 1);
  strcpy(out_single, inputs.out);
  strcat(out_single, "/");
  strcat(out_single, single);
  if (mkdir_p(out_single) != 0) {
    printf("Error recursively making directory %s", out_single);
    return 1;
  }

  if (mmdoc_single(out_single, inputs.toc_path, inputs.project_name,
                   toc_anchor_locations) != 0)
    return 1;

  if (mmdoc_multi(inputs.out_multi, inputs.src, inputs.toc_path,
                  toc_anchor_locations, anchor_locations,
                  inputs.project_name) != 0)
    return 1;

  if (mkdir_p(inputs.out_man) != 0) {
    printf("Error recursively making directory %s", inputs.out_man);
    return -1;
  }
  if (mmdoc_man(inputs.out_man, inputs.src, inputs.toc_path,
                toc_anchor_locations, anchor_locations) != 0)
    return 1;

  char *epub = "epub";
  char *out_epub = malloc(strlen(inputs.out) + 1 + strlen(epub) + 1);
  sprintf(out_epub, "%s/%s", inputs.out, epub);
  if (mkdir_p(out_epub) != 0) {
    printf("Error recursively making directory %s", out_epub);
    return 1;
  }

  char *epub_ext = ".epub";
  char *out_epub_file =
      malloc(strlen(inputs.out) + 1 + strlen(inputs.project_name) +
             strlen(epub_ext) + 1);
  sprintf(out_epub_file, "%s/%s%s", inputs.out, inputs.project_name, epub_ext);

  if (mmdoc_epub(out_epub, out_epub_file, inputs.toc_path, toc_anchor_locations,
                 inputs.project_name) != 0)
    return 1;

  if (0 != copy_imgs(inputs.src, inputs.out_multi, out_single))
    return 1;

  free_array(&md_files);
  free_anchor_location_array(&toc_anchor_locations);
  free_anchor_location_array(&anchor_locations);
  return 0;
}
