/* SPDX-License-Identifier: CC0-1.0 */
#include "build.h"
#include "inputs.h"
#include <mmdocconfig.h>
#include <stdio.h>
#include <string.h>
#ifdef __GLIBC__
#include <malloc.h>
#endif

static void print_usage(void) {
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
  if (argc == 2) {
    if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
      print_usage();
      return 0;
    }
    if (strcmp(argv[1], "-v") == 0 || strcmp(argv[1], "--version") == 0) {
      printf("mmdoc version %s\n", MMDOC_VERSION);
      return 0;
    }
  }
  if (argc != 4) {
    print_usage();
    return 1;
  }

#ifdef __GLIBC__
  /* Avoid per-buffer mmap/munmap page-fault churn across worker threads. */
  mallopt(M_MMAP_THRESHOLD, 32 * 1024 * 1024);
  mallopt(M_TRIM_THRESHOLD, -1);
#endif

  Inputs inputs = {};
  if (mmdoc_inputs_derive(&inputs, argv) != 0)
    return 1;
  int rc = mmdoc_build(inputs);
  mmdoc_inputs_free(&inputs);
  return rc == 0 ? 0 : 1;
}
