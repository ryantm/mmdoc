/* SPDX-License-Identifier: CC0-1.0 */
#include "render.h"
#include "types.h"
#include <dirent.h>
#include <errno.h>
#include <regex.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define MMDOC_VERSION "0.1.2"

extern int errno;

void print_usage() {
  printf("mmdoc version %s - minimal markdown documentation\n", MMDOC_VERSION);
  printf("\n");
  printf("mmdoc PROJECT_NAME SRC OUT\n");
  printf("\n");
  printf("options:\n");
  printf("-h, --help                show help\n");
  printf("\n");
  printf("PROJECT_NAME is the name of the project the documentation is being "
         "generated for.\n");
  printf("\n");
  printf("SRC a directory containing Markdown files; a file called toc.md at "
         "the top level\n");
  printf("is required.\n");
  printf("\n");
  printf("OUT a directory where the website is written to\n");
}

int mkdir_p(const char *path) {
  const size_t len = strlen(path);
  char _path[PATH_MAX];
  char *p;

  errno = 0;

  if (len > sizeof(_path) - 1) {
    errno = ENAMETOOLONG;
    return -1;
  }
  strcpy(_path, path);

  for (p = _path + 1; *p; p++) {
    if (*p == '/') {
      *p = '\0';

      if (mkdir(_path, S_IRWXU) != 0) {
        if (errno != EEXIST)
          return -1;
      }

      *p = '/';
    }
  }

  if (mkdir(_path, S_IRWXU) != 0) {
    if (errno != EEXIST)
      return -1;
  }

  return 0;
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

  Array md_files;
  init_array(&md_files, 100);
  mmdoc_md_files(&md_files, src);

  Array toc_refs;
  init_array(&toc_refs, 500);
  mmdoc_refs(&toc_refs, toc_path);

  AnchorLocationArray anchor_locations;
  init_anchor_location_array(&anchor_locations, 500);

  int count = 0;
  for (int i = 0; i < md_files.used; i++) {
    Array anchors;
    init_array(&anchors, 500);
    mmdoc_anchors(&anchors, md_files.array[i]);
    for (int j = 0; j < anchors.used; j++) {
      AnchorLocation *al = malloc(sizeof *al);
      al->file_path = md_files.array[i];

      char *page_path = malloc(strlen(out_multi) + strlen(al->file_path));
      strcpy(page_path, out_multi);
      strcat(page_path, al->file_path + strlen(src));
      char *lastExt = strrchr(page_path, '.');
      while (lastExt != NULL) {
        *lastExt = '\0';
        lastExt = strrchr(page_path, '.');
      }
      strcat(page_path, "/");
      char *page_dir_path = malloc(strlen(page_path) + 1);
      strcpy(page_dir_path, page_path);
      strcat(page_path, "index.html");
      if (mkdir_p(page_dir_path) != 0) {
        printf("Error recursively making directory %s", page_dir_path);
        return 1;
      }
      al->multipage_output_file_path = page_path;
      al->multipage_output_directory_path = page_dir_path;
      al->multipage_url = page_dir_path + strlen(out_multi);

      char *man_path = malloc(strlen(out_man) + 1 + strlen(project_name) +
                              strlen(al->file_path) + 2);
      char *man_page_name = malloc(strlen(project_name) + strlen(al->file_path) + 1);
      man_page_name[0] = '\0';
      strcpy(man_path, out_man);
      strcat(man_path, "/");
      strcat(man_path, project_name);
      strcpy(man_page_name, project_name);
      for (int k = 0; *(al->file_path + strlen(src) + k) != '\0'; k++) {
        char *c = al->file_path + strlen(src) + k;
        if (c[0] == '/') {
          strcat(man_path, "-");
          strcat(man_page_name, "\\-");
        }
        else if (c[0] == '-') {
          strncat(man_path, c, 1);
          strcat(man_page_name, "\\-");
        } else {
          strncat(man_path, c, 1);
          strncat(man_page_name, c, 1);
        }
      }
      lastExt = strrchr(man_path, '.');
      while (lastExt != NULL) {
        *lastExt = '\0';
        lastExt = strrchr(man_path, '.');
      }
      lastExt = strrchr(man_page_name, '.');
      while (lastExt != NULL) {
        *lastExt = '\0';
        lastExt = strrchr(man_page_name, '.');
      }
      strcat(man_path, ".1");
      al->man_output_file_path = man_path;
      char *man_page_header = malloc(19 + strlen(man_path) * 2 + strlen(project_name) + 1);
      strcpy(man_page_header, ".TH \"");
      strcat(man_page_header, man_page_name);
      strcat(man_page_header, "\" \"1\" \"\" \"");
      strcat(man_page_header, project_name);
      strcat(man_page_header, "\" \"");
      strcat(man_page_header, man_page_name);
      strcat(man_page_header, "\"");
      al->man_header = man_page_header;
      al->anchor = anchors.array[j];
      insert_anchor_location_array(&anchor_locations, al);
      count++;
    }
  }

  char *single = "single";
  char *out_single = malloc(strlen(out) + 1 + strlen(single) + 1);
  strcpy(out_single, out);
  strcat(out_single, "/");
  strcat(out_single, single);
  if (mkdir_p(out_single) != 0) {
    printf("Error recursively making directory %s", out_single);
    return 1;
  }

  if (mmdoc_render_single(out_single, toc_path, toc_refs, anchor_locations) !=
      0)
    return 1;

  if (mmdoc_render_multi(out_multi, src, toc_path, toc_refs,
                         anchor_locations) != 0)
    return 1;

  if (mkdir_p(out_man) != 0) {
    printf("Error recursively making directory %s", out_man);
    return -1;
  }
  if (mmdoc_render_man(out_man, src, toc_path, toc_refs, anchor_locations) != 0)
    return 1;

  char *epub = ".epub";
  char *out_epub = malloc(strlen(out) + 1 + strlen(project_name) + strlen(epub) + 1);
  strcpy(out_epub, out);
  strcat(out_epub, "/");
  strcat(out_epub, project_name);
  strcat(out_epub, epub);
  if (mmdoc_render_epub(out_epub, out_single, project_name) != 0)
    return 1;

  free_array(&md_files);
  free_array(&toc_refs);
  free_anchor_location_array(&anchor_locations);
  return 0;
}
