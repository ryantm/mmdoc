/* SPDX-License-Identifier: CC0-1.0 */
#include "anchors.h"
#include "arena.h"
#include "html.h"
#include "render.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int mmdoc_anchors(Array *md_anchors, char *path) {
  return mmdoc_render_collect_anchors(path, md_anchors);
}

static char *copy_string(const char *source) {
  size_t len = strlen(source) + 1;
  char *copy = malloc(len);
  if (copy != NULL)
    memcpy(copy, source, len);
  return copy;
}

typedef struct {
  char *output_file_path;
  char *output_directory_path;
  char *multipage_url;
  char *multipage_base_href;
} PagePaths;

static void free_page_paths(PagePaths *p) {
  free(p->output_file_path);
  free(p->output_directory_path);
  free(p->multipage_url);
  free(p->multipage_base_href);
  memset(p, 0, sizeof(*p));
}

static int compute_page_paths(PagePaths *p, const char *file_path,
                              int is_index_page, Inputs inputs) {
  memset(p, 0, sizeof(*p));
  if (is_index_page) {
    const char *index_html = "index.html";
    p->output_file_path =
        malloc(strlen(inputs.out_multi) + 1 + strlen(index_html) + 1);
    p->output_directory_path = copy_string(inputs.out_multi);
    p->multipage_url = copy_string("./");
    p->multipage_base_href = copy_string("");
    if (p->output_file_path == NULL || p->output_directory_path == NULL ||
        p->multipage_url == NULL || p->multipage_base_href == NULL) {
      free_page_paths(p);
      return -1;
    }
    sprintf(p->output_file_path, "%s/%s", inputs.out_multi, index_html);
    return 0;
  }

  size_t src_len = strlen(inputs.src);
  const char *rel = file_path + src_len;
  int need_slash = rel[0] != '/';
  char *page_path =
      malloc(strlen(inputs.out_multi) + (size_t)need_slash + strlen(rel) + 13);
  if (page_path == NULL) {
    free_page_paths(p);
    return -1;
  }
  sprintf(page_path, "%s%s%s", inputs.out_multi, need_slash ? "/" : "", rel);

  char *last_slash = strrchr(page_path, '/');
  char *last_ext = strrchr(page_path, '.');
  while (last_ext != NULL && (last_slash == NULL ? last_ext > page_path
                                                 : last_ext > last_slash + 1)) {
    *last_ext = '\0';
    last_ext = strrchr(page_path, '.');
  }
  strcat(page_path, "/");

  p->output_directory_path = copy_string(page_path);
  strcat(page_path, "index.html");
  p->output_file_path = page_path;
  if (p->output_directory_path == NULL) {
    free_page_paths(p);
    return -1;
  }

  const char *raw_url = p->output_directory_path + strlen(inputs.out_multi) + 1;
  p->multipage_url = mmdoc_url_encode_path(raw_url);
  if (p->multipage_url == NULL) {
    free_page_paths(p);
    return -1;
  }

  size_t directory_depth = 0;
  for (size_t i = 0; raw_url[i] != '\0'; i++)
    if (raw_url[i] == '/')
      directory_depth++;

  p->multipage_base_href = malloc(3 * directory_depth + 1);
  if (p->multipage_base_href == NULL) {
    free_page_paths(p);
    return -1;
  }
  for (size_t i = 0; i < directory_depth; i++)
    memcpy(p->multipage_base_href + 3 * i, "../", 3);
  p->multipage_base_href[3 * directory_depth] = '\0';
  return 0;
}

int mmdoc_anchors_add_file(AnchorLocationArray *anchor_locations,
                           const char *file_path, const Array *anchors,
                           const char *title, int is_index_page,
                           Inputs inputs) {
  if (anchors->used == 0)
    return 0;

  if (anchor_locations->string_arena == NULL) {
    MmdocArena *arena = malloc(sizeof(*arena));
    if (arena == NULL) {
      fprintf(stderr, "Fatal: failed to allocate anchor string arena.\n");
      return -1;
    }
    mmdoc_arena_init(arena, 256 * 1024);
    anchor_locations->string_arena = arena;
  }
  MmdocArena *arena = anchor_locations->string_arena;

  PagePaths paths;
  if (compute_page_paths(&paths, file_path, is_index_page, inputs) != 0) {
    fprintf(stderr, "Fatal: failed to allocate page paths.\n");
    return -1;
  }

  /* Share the 6 per-file strings across every anchor in this file. */
  char *shared_file_path = mmdoc_arena_strdup(arena, file_path);
  char *shared_title = mmdoc_arena_strdup(arena, title);
  char *shared_out_file = mmdoc_arena_strdup(arena, paths.output_file_path);
  char *shared_out_dir = mmdoc_arena_strdup(arena, paths.output_directory_path);
  char *shared_url = mmdoc_arena_strdup(arena, paths.multipage_url);
  char *shared_base_href = mmdoc_arena_strdup(arena, paths.multipage_base_href);
  free_page_paths(&paths);

  if (shared_file_path == NULL || shared_title == NULL ||
      shared_out_file == NULL || shared_out_dir == NULL || shared_url == NULL ||
      shared_base_href == NULL ||
      reserve_anchor_location_array(anchor_locations, anchors->used) != 0) {
    fprintf(stderr, "Fatal: failed to allocate anchor location metadata.\n");
    return -1;
  }

  free(anchor_locations->anchor_index);
  anchor_locations->anchor_index = NULL;
  anchor_locations->anchor_index_size = 0;

  for (size_t j = 0; j < anchors->used; j++) {
    char *anchor_copy = mmdoc_arena_strdup(arena, anchors->array[j]);
    if (anchor_copy == NULL) {
      fprintf(stderr, "Fatal: failed to allocate an anchor location.\n");
      return -1;
    }
    AnchorLocation *al = &anchor_locations->array[anchor_locations->used++];
    al->anchor = anchor_copy;
    al->file_path = shared_file_path;
    al->title = shared_title;
    al->multipage_output_file_path = shared_out_file;
    al->multipage_output_directory_path = shared_out_dir;
    al->multipage_url = shared_url;
    al->multipage_base_href = shared_base_href;
  }
  return 0;
}

int mmdoc_anchors_locations(AnchorLocationArray *anchor_locations,
                            Array *md_files, Array *toc_refs, Inputs inputs) {
  const char *index_anchor = toc_refs->used > 0 ? toc_refs->array[0] : NULL;
  int found_index_page = 0;

  init_anchor_location_array(anchor_locations, 500);

  for (size_t i = 0; i < md_files->used; i++) {
    Array anchors;
    init_array(&anchors, 64);
    char *title = NULL;
    if (mmdoc_render_collect_metadata(md_files->array[i], &anchors, &title) !=
        0) {
      free_array(&anchors);
      free_anchor_location_array_deep(anchor_locations);
      return -1;
    }
    int is_index_page = 0;
    for (size_t j = 0;
         !found_index_page && index_anchor != NULL && j < anchors.used; j++)
      if (strcmp(anchors.array[j], index_anchor) == 0) {
        is_index_page = 1;
        found_index_page = 1;
      }

    if (mmdoc_anchors_add_file(anchor_locations, md_files->array[i], &anchors,
                               title, is_index_page, inputs) != 0) {
      free(title);
      free_array(&anchors);
      free_anchor_location_array_deep(anchor_locations);
      return -1;
    }
    free(title);
    free_array(&anchors);
  }
  if (build_anchor_location_index(anchor_locations) != 0) {
    free_anchor_location_array_deep(anchor_locations);
    return -1;
  }
  return 0;
}

int mmdoc_anchors_find_toc_anchors(AnchorLocationArray *toc_anchor_locations,
                                   Array *toc_refs,
                                   AnchorLocationArray *anchor_locations) {
  for (size_t i = 0; i < toc_refs->used; i++) {
    AnchorLocation *anchor_location =
        find_anchor_location(anchor_locations, toc_refs->array[i]);
    if (anchor_location == NULL) {
      printf("Anchor \"%s\" referenced in toc.md not found.\n",
             toc_refs->array[i]);
      return -1;
    }
    insert_anchor_location_array(toc_anchor_locations, anchor_location);
  }
  return 0;
}
