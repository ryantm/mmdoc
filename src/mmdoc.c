#include "render.h"
#include "types.h"
#include <dirent.h>
#include <errno.h>
#include <jq.h>
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
  printf("mmdoc SRC OUT\n");
  /* printf("mmdoc -w SRC OUT\n"); */
  /* printf("mmdoc -s SRC OUT\n"); */
  printf("\n");
  printf("options:\n");
  printf("-h, --help                show help\n");
  /* printf("-v, --verbose             verbose output\n"); */
  /* printf("-w, --website             output multipage website\n"); */
  /* printf("-s, --single              output single page website\n"); */
  printf("\n");
  printf("SRC a directory containing Markdown files; a file called toc.md at "
         "the top level\n");
  printf("is required.\n");
  printf("\n");
  printf("OUT a directory where the website is written to\n");
}

void init_array(Array *a, size_t initialSize) {
  a->array = malloc(initialSize * sizeof *a->array);
  a->used = 0;
  a->size = initialSize;
}

void insert_array(Array *a, char *element) {
  if (a->used == a->size) {
    a->size *= 2;
    a->array = realloc(a->array, a->size * sizeof *a->array);
  }
  a->array[a->used] = malloc(strlen(element) + 1);
  strcpy(a->array[a->used], element);
  a->used++;
}

void free_array(Array *a) {
  for (int i = 0; i < a->used; i++)
    free(a->array[i]);
  free(a->array);
  a->array = NULL;
  a->used = a->size = 0;
}

int ends_with(const char *str, const char *suffix) {
  size_t str_len = strlen(str);
  size_t suffix_len = strlen(suffix);

  return (str_len >= suffix_len) &&
         (!memcmp(str + str_len - suffix_len, suffix, suffix_len));
}

void mmdoc_md_files(Array *md_files, char *base_path) {
  struct dirent *dp;
  DIR *dir = opendir(base_path);

  if (!dir)
    return;

  while ((dp = readdir(dir)) != NULL) {
    if (strcmp(dp->d_name, ".") != 0 && strcmp(dp->d_name, "..") != 0) {
      size_t size = strlen(base_path) + 1 + strlen(dp->d_name) + 1;
      char *path = malloc(size);
      strcpy(path, base_path);
      strcat(path, "/");
      strcat(path, dp->d_name);
      if (ends_with(dp->d_name, ".md"))
        insert_array(md_files, path);
      mmdoc_md_files(md_files, path);
      free(path);
    }
  }
  closedir(dir);
  return;
}

enum ref_parse_state { NONE, L, REF };

void mmdoc_refs(Array *md_refs, char *path) {
  char ref[1024];
  int refpos = 0;
  FILE *file;
  file = fopen(path, "r");
  int c;
  enum ref_parse_state state = NONE;

  while (1) {
    c = fgetc(file);
    if (c == EOF)
      break;
    if (state == NONE && c == '(') {
      state = L;
      continue;
    }
    if (state == L && c == '#') {
      state = REF;
      ref[refpos] = c;
      refpos += 1;
      continue;
    }
    if (state == REF && (c == '\n' || c == '\r')) {
    } else if (state == REF && c != ')') {
      ref[refpos] = c;
      refpos += 1;
      continue;
    } else if (state == REF && c == ')') {
      ref[refpos] = '\0';
      insert_array(md_refs, ref);
    }
    refpos = 0;
    state = NONE;
    continue;
  }
  fclose(file);
}

void init_anchor_location_array(AnchorLocationArray *a, size_t initialSize) {
  a->array = malloc(initialSize * sizeof(a->array));
  a->used = 0;
  a->size = initialSize;
}

void insert_anchor_location_array(AnchorLocationArray *a,
                                  AnchorLocation *element) {
  if (a->used == a->size) {
    a->size *= 2;
    a->array = realloc(a->array, a->size * sizeof a->array);
  }
  a->array[a->used] = *element;
  a->used++;
}

void free_anchor_location_array(AnchorLocationArray *a) {
  free(a->array);
  a->array = NULL;
  a->used = a->size = 0;
}

void mmdoc_anchors(Array *md_anchors, char *path) {
  char ref[1024];
  int refpos = 0;
  FILE *file;
  file = fopen(path, "r");
  int c;
  enum ref_parse_state state = NONE;

  while (1) {
    c = fgetc(file);
    if (c == EOF)
      break;
    if (state == NONE && c == '{') {
      state = L;
      continue;
    }
    if (state == L && c == '#') {
      state = REF;
      ref[refpos] = c;
      refpos += 1;
      continue;
    }
    if (state == REF && (c == '\n' || c == '\r')) {
    } else if (state == REF && c != '}') {
      ref[refpos] = c;
      refpos += 1;
      continue;
    } else if (state == REF && c == '}') {
      ref[refpos] = '\0';
      insert_array(md_anchors, ref);
    }
    refpos = 0;
    state = NONE;
    continue;
  }
  fclose(file);
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

  Array md_files;
  init_array(&md_files, 100);
  mmdoc_md_files(&md_files, src);

  Array toc_refs;
  init_array(&toc_refs, 500);
  mmdoc_refs(&toc_refs, toc_path);

  AnchorLocationArray anchor_locations;
  init_anchor_location_array(&anchor_locations, 500);

  for (int i = 0; i < md_files.used; i++) {
    Array anchors;
    init_array(&anchors, 500);
    mmdoc_anchors(&anchors, md_files.array[i]);
    for (int j = 0; j < anchors.used; j++) {
      AnchorLocation *al = malloc(sizeof *al);
      al->file_path = md_files.array[i];
      al->anchor = anchors.array[j];
      insert_anchor_location_array(&anchor_locations, al);
    }
  }

  if (0 != mkdir(out, S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP |
                          S_IXGRP | S_IROTH | S_IXOTH)) {

    printf("Couldn't create directory \"%s\": errno %d\n", out, errno);
    return 1;
  }

  char *single = "single";
  char *out_single = malloc(strlen(out) + 1 + strlen(single) + 1);
  strcpy(out_single, out);
  strcat(out_single, "/");
  strcat(out_single, single);

  if (0 != mkdir(out_single, S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP |
                                 S_IXGRP | S_IROTH | S_IXOTH)) {

    printf("Couldn't create directory \"%s\": errno %d\n", out_single, errno);
    return 1;
  }

  if (mmdoc_render_single(out_single, toc_path, toc_refs, anchor_locations) !=
      0)
    return 1;

  char *multi = "multi";
  char *out_multi = malloc(strlen(out) + 1 + strlen(multi) + 1);
  strcpy(out_multi, out);
  strcat(out_multi, "/");
  strcat(out_multi, multi);

  if (0 != mkdir(out_multi, S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP |
                                S_IXGRP | S_IROTH | S_IXOTH)) {

    printf("Couldn't create directory \"%s\": errno %d\n", out_multi, errno);
    return 1;
  }

  if (mmdoc_render_multi(out_multi, src, toc_path, toc_refs, anchor_locations) != 0)
    return 1;

  free_array(&md_files);
  free_array(&toc_refs);
  free_anchor_location_array(&anchor_locations);
  return 0;
}
