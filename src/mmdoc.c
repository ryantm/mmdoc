#include "cmark-gfm-core-extensions.h"
#include "cmark-gfm-extension_api.h"
#include "cmark-gfm.h"
#include <dirent.h>
#include <errno.h>
#include <regex.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define MMDOC_VERSION "0.0.8"

#define MMDOC_COMMAND_OPTION_DEFAULT 0
#define MMDOC_COMMAND_OPTION_WEBSITE (1 << 1)
#define MMDOC_COMMAND_OPTION_SINGLE (1 << 2)
#define MMDOC_COMMAND_OPTION_VERBOSE (1 << 3)

extern int errno;

void print_usage() {
  printf("mmdoc version %s - minimal markdown documentation\n", MMDOC_VERSION);
  printf("\n");
  printf("mmdoc -w SRC OUT\n");
  printf("mmdoc -s SRC OUT\n");
  printf("\n");
  printf("options:\n");
  printf("-h, --help                show help\n");
  printf("-v, --verbose             verbose output\n");
  printf("-w, --website             output multipage website\n");
  printf("-s, --single              output single page website\n");
  printf("\n");
  printf("SRC a directory containing Markdown files; a file called toc.md at "
         "the top level\n");
  printf("is required.\n");
  printf("\n");
  printf("OUT a directory where the website is written to\n");
}

int ends_with(const char *str, const char *suffix) {
  size_t str_len = strlen(str);
  size_t suffix_len = strlen(suffix);

  return (str_len >= suffix_len) &&
         (!memcmp(str + str_len - suffix_len, suffix, suffix_len));
}

void mmdoc_md_files(char * md_files[], char *base_path) {
  char path[2048];
  struct dirent *dp;
  DIR *dir = opendir(base_path);

  if (!dir)
    return;

  while ((dp = readdir(dir)) != NULL) {
    if (strcmp(dp->d_name, ".") != 0 && strcmp(dp->d_name, "..") != 0) {
      if (ends_with(dp->d_name, ".md")) {
        printf("%s\n", dp->d_name);
      }
      strcpy(path, base_path);
      strcat(path, "/");
      strcat(path, dp->d_name);
      mmdoc_md_files(NULL, path);
    }
  }

  closedir(dir);
  return;
}

int main(int argc, char *argv[]) {
  int command_options = MMDOC_COMMAND_OPTION_DEFAULT;
  char *src = NULL;
  char *out = NULL;
  for (int i = 1; i < argc; i++) {
    if ((strcmp(argv[i], "-h")) == 0 || (strcmp(argv[i], "--help") == 0)) {
      print_usage();
      return 0;
    } else if ((strcmp(argv[i], "-w")) == 0 ||
               (strcmp(argv[i], "--website") == 0)) {
      command_options |= MMDOC_COMMAND_OPTION_WEBSITE;
    } else if ((strcmp(argv[i], "-s")) == 0 ||
               (strcmp(argv[i], "--single") == 0)) {
      command_options |= MMDOC_COMMAND_OPTION_SINGLE;
    } else if ((strcmp(argv[i], "-v")) == 0 ||
               (strcmp(argv[i], "--verbose") == 0)) {
      command_options |= MMDOC_COMMAND_OPTION_VERBOSE;
    } else if (i == (argc - 2)) {
      src = argv[i];
      out = argv[i + 1];
      break;
    } else {
      print_usage();
      return 1;
    }
  }
  if (src == NULL || out == NULL) {
    print_usage();
    return 1;
  }

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
  free(toc_path);

  mmdoc_md_files(NULL, src);

  if (0 != mkdir(out, S_IRUSR | S_IRGRP | S_IROTH | S_IWUSR)) {
    printf("Couldn't create directory \"%s\": errno %d\n", out, errno);
    return 1;
  }


  char buffer[4096];
  size_t bytes;

  int options = CMARK_OPT_DEFAULT | CMARK_OPT_UNSAFE;

  cmark_gfm_core_extensions_ensure_registered();
  cmark_mem *mem = cmark_get_default_mem_allocator();
  cmark_syntax_extension *table_extension =
      cmark_find_syntax_extension("table");
  FILE *file = fopen("test/blah.md", "rb");

  cmark_parser *parser = cmark_parser_new_with_mem(options, mem);
  cmark_parser_attach_syntax_extension(parser, table_extension);
  while ((bytes = fread(buffer, 1, sizeof(buffer), file)) > 0) {
    cmark_parser_feed(parser, buffer, bytes);
    if (bytes < sizeof(buffer)) {
      break;
    }
  }
  fclose(file);
  cmark_node *document = cmark_parser_finish(parser);
  char *result = cmark_render_html_with_mem(
      document, options, cmark_parser_get_syntax_extensions(parser), mem);
  cmark_node_free(document);
  cmark_parser_free(parser);
  printf("%s", result);

  mem->free(result);
  return 0;
}
