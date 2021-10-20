/* SPDX-License-Identifier: CC0-1.0 */
#include "anchors.h"
#include "epub.h"
#include "files.h"
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
  char *project_name = NULL;
  char *src = NULL;
  char *out = NULL;
  if (argc != 4) {
    print_usage();
    return 1;
  }
  project_name = argv[1];
  src = argv[2];
  out = argv[3];

  char *src_relative_toc_path = "/toc.md";
  char *toc_path = malloc(strlen(src) + strlen(src_relative_toc_path) + 1);
  if (toc_path == NULL) {
    fprintf(stderr, "Fatal: failed to allocate memory for toc path.\n");
    return 1;
  }
  strcpy(toc_path, src);
  strcat(toc_path, src_relative_toc_path);
  if (access(toc_path, F_OK) != 0) {
    printf("Expected but did not find: \"%s\"", toc_path);
    return 1;
  }

  char *multi = "multi";
  char *out_multi = malloc(strlen(out) + 1 + strlen(multi) + 1);
  strcpy(out_multi, out);
  strcat(out_multi, "/");
  strcat(out_multi, multi);

  char *man = "/man/man1";
  char *out_man = malloc(strlen(out) + 1 + strlen(man) + 1);
  strcpy(out_man, out);
  strcat(out_man, man);
  if (mkdir_p(out_man) != 0) {
    printf("Error recursively making directory %s", out_man);
    return 1;
  }

  Array toc_refs;
  init_array(&toc_refs, 500);
  mmdoc_refs(&toc_refs, toc_path);

  if (toc_refs.used == 0) {
    printf("Error toc.md didn't reference any anchor.");
    return 1;
  }

  Array md_files;
  init_array(&md_files, 100);
  mmdoc_md_files(&md_files, src);

  AnchorLocationArray anchor_locations;
  init_anchor_location_array(&anchor_locations, 500);
  if (0 != mmdoc_anchors_locations(&anchor_locations, &md_files, &toc_refs, src,
                                   project_name, out_multi, out_man))
    return 1;

  AnchorLocationArray toc_anchor_locations;
  init_anchor_location_array(&toc_anchor_locations, toc_refs.used);
  if (0 != mmdoc_anchors_find_toc_anchors(&toc_anchor_locations, &toc_refs,
                                          &anchor_locations))
    return 1;
  free_array(&toc_refs);

  char *single = "single";
  char *out_single = malloc(strlen(out) + 1 + strlen(single) + 1);
  strcpy(out_single, out);
  strcat(out_single, "/");
  strcat(out_single, single);
  if (mkdir_p(out_single) != 0) {
    printf("Error recursively making directory %s", out_single);
    return 1;
  }

  if (mmdoc_single(out_single, toc_path, project_name, toc_anchor_locations) !=
      0)
    return 1;

  if (mmdoc_multi(out_multi, src, toc_path, toc_anchor_locations,
                  anchor_locations, project_name) != 0)
    return 1;

  if (mkdir_p(out_man) != 0) {
    printf("Error recursively making directory %s", out_man);
    return -1;
  }
  if (mmdoc_man(out_man, src, toc_path, toc_anchor_locations,
                anchor_locations) != 0)
    return 1;

  char *epub = "epub";
  char *out_epub = malloc(strlen(out) + 1 + strlen(epub) + 1);
  sprintf(out_epub, "%s/%s", out, epub);
  if (mkdir_p(out_epub) != 0) {
    printf("Error recursively making directory %s", out_epub);
    return 1;
  }

  char *epub_ext = ".epub";
  char *out_epub_file =
      malloc(strlen(out) + 1 + strlen(project_name) + strlen(epub_ext) + 1);
  sprintf(out_epub_file, "%s/%s%s", out, project_name, epub_ext);

  if (mmdoc_epub(out_epub, out_epub_file, toc_path, toc_anchor_locations,
                 project_name) != 0)
    return 1;

  if (0 != copy_imgs(src, out_multi, out_single))
    return 1;

  free_array(&md_files);
  free_anchor_location_array(&toc_anchor_locations);
  free_anchor_location_array(&anchor_locations);
  return 0;
}
