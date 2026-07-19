/* SPDX-License-Identifier: CC0-1.0 */
#include "anchors.h"
#include "mkdir_p.h"
#include "render.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int mmdoc_anchors(Array *md_anchors, char *path) {
  return mmdoc_render_collect_anchors(path, md_anchors);
}

static char *copy_string(const char *source) {
  char *copy = malloc(strlen(source) + 1);
  if (copy != NULL)
    strcpy(copy, source);
  return copy;
}

static int init_anchor_location(AnchorLocation *al, const char *anchor,
                                const char *file_path, const char *title,
                                const char *index_anchor, Inputs inputs) {
  memset(al, 0, sizeof(*al));
  al->anchor = copy_string(anchor);
  al->file_path = copy_string(file_path);
  al->title = copy_string(title);
  if (al->anchor == NULL || al->file_path == NULL || al->title == NULL)
    goto allocation_failed;

  if (strcmp(al->anchor, index_anchor) == 0) {
    const char *index_html = "index.html";
    al->multipage_output_file_path =
        malloc(strlen(inputs.out_multi) + 1 + strlen(index_html) + 1);
    al->multipage_output_directory_path = copy_string(inputs.out_multi);
    al->multipage_url = copy_string("./");
    al->multipage_base_href = copy_string("");
    if (al->multipage_output_file_path == NULL ||
        al->multipage_output_directory_path == NULL ||
        al->multipage_url == NULL || al->multipage_base_href == NULL)
      goto allocation_failed;
    sprintf(al->multipage_output_file_path, "%s/%s", inputs.out_multi,
            index_html);
    return 0;
  }

  char *page_path =
      malloc(strlen(inputs.out_multi) + strlen(al->file_path) + 12);
  if (page_path == NULL)
    goto allocation_failed;
  strcpy(page_path, inputs.out_multi);
  strcat(page_path, al->file_path + strlen(inputs.src));
  char *last_slash = strrchr(page_path, '/');
  char *last_ext = strrchr(page_path, '.');
  while (last_ext != NULL && (last_slash == NULL || last_ext > last_slash)) {
    *last_ext = '\0';
    last_ext = strrchr(page_path, '.');
  }
  strcat(page_path, "/");

  al->multipage_output_directory_path = copy_string(page_path);
  strcat(page_path, "index.html");
  al->multipage_output_file_path = page_path;
  if (al->multipage_output_directory_path == NULL)
    goto allocation_failed;

  al->multipage_url = copy_string(al->multipage_output_directory_path +
                                  strlen(inputs.out_multi) + 1);
  if (al->multipage_url == NULL)
    goto allocation_failed;

  size_t directory_depth = 0;
  for (size_t i = 0; al->multipage_url[i] != '\0'; i++)
    if (al->multipage_url[i] == '/')
      directory_depth++;

  al->multipage_base_href = malloc(3 * directory_depth + 1);
  if (al->multipage_base_href == NULL)
    goto allocation_failed;
  al->multipage_base_href[0] = '\0';
  for (size_t i = 0; i < directory_depth; i++)
    strcat(al->multipage_base_href, "../");
  return 0;

allocation_failed:
  fprintf(stderr, "Fatal: failed to allocate an anchor location.\n");
  free_anchor_location(al);
  return -1;
}

int mmdoc_anchors_locations(AnchorLocationArray *anchor_locations,
                            Array *md_files, Array *toc_refs, Inputs inputs) {
  char *index_anchor = toc_refs->array[0];

  init_anchor_location_array(anchor_locations, 500);

  for (int i = 0; i < md_files->used; i++) {
    Array anchors;
    init_array(&anchors, 500);
    if (mmdoc_anchors(&anchors, md_files->array[i]) != 0) {
      free_array(&anchors);
      return -1;
    }
    char *title = NULL;
    if (anchors.used > 0)
      title = mmdoc_render_get_title_from_file(md_files->array[i]);
    if (anchors.used > 0 && title == NULL) {
      fprintf(stderr, "Fatal: failed to read the title from %s.\n",
              md_files->array[i]);
      free_array(&anchors);
      free_anchor_location_array_deep(anchor_locations);
      return -1;
    }
    for (int j = 0; j < anchors.used; j++) {
      AnchorLocation al;
      if (init_anchor_location(&al, anchors.array[j], md_files->array[i], title,
                               index_anchor, inputs) != 0) {
        free(title);
        free_array(&anchors);
        free_anchor_location_array_deep(anchor_locations);
        return -1;
      }

      if (mkdir_p(al.multipage_output_directory_path) != 0) {
        printf("Error recursively making directory %s",
               al.multipage_output_directory_path);
        free_anchor_location(&al);
        free(title);
        free_array(&anchors);
        free_anchor_location_array_deep(anchor_locations);
        return -1;
      }

      insert_anchor_location_array(anchor_locations, &al);
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

  for (int i = 0; i < toc_refs->used; i++) {
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
