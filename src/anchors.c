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

int mmdoc_anchors_locations(AnchorLocationArray *anchor_locations,
                            Array *md_files, Array *toc_refs, Inputs inputs) {
  char *index_anchor = toc_refs->array[0];

  init_anchor_location_array(anchor_locations, 500);

  int count = 0;
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
    for (int j = 0; j < anchors.used; j++) {
      AnchorLocation *al = malloc(sizeof *al);
      al->anchor = anchors.array[j];
      al->file_path = md_files->array[i];

      char *page_path =
          malloc(strlen(inputs.out_multi) + strlen(al->file_path) + 12);
      strcpy(page_path, inputs.out_multi);
      strcat(page_path, al->file_path + strlen(inputs.src));
      char *lastExt = strrchr(page_path, '.');
      while (lastExt != NULL) {
        *lastExt = '\0';
        lastExt = strrchr(page_path, '.');
        if (lastExt < strrchr(page_path, '/'))
          break;
      }
      strcat(page_path, "/");
      char *page_dir_path = malloc(strlen(page_path) + 1);
      strcpy(page_dir_path, page_path);
      strcat(page_path, "index.html");
      al->multipage_output_file_path = page_path;
      al->multipage_output_directory_path = page_dir_path;
      al->multipage_url = page_dir_path + strlen(inputs.out_multi) + 1;

      int directory_depth = 0;
      for (int k = 0; k < strlen(al->multipage_url); k++)
        if (al->multipage_url[k] == '/')
          directory_depth++;

      al->multipage_base_href = malloc(3 * directory_depth + 1);
      strcpy(al->multipage_base_href, "");
      for (int k = 0; k < directory_depth; k++)
        strcat(al->multipage_base_href, "../");

      al->title = malloc(strlen(title) + 1);
      strcpy(al->title, title);

      if (strcmp(al->anchor, index_anchor) == 0) {
        char *index_html = "index.html";
        char *index_file_path =
            malloc(strlen(inputs.out_multi) + 1 + strlen(index_html) + 1);
        sprintf(index_file_path, "%s/%s", inputs.out_multi, index_html);
        al->multipage_output_file_path = index_file_path;
        al->multipage_output_directory_path = inputs.out_multi;
        al->multipage_url = "./";
        al->multipage_base_href = "";
      }

      if (mkdir_p(al->multipage_output_directory_path) != 0) {
        printf("Error recursively making directory %s",
               al->multipage_output_directory_path);
        return -1;
      }

      int dash_count = 0;
      for (int k = 0; *(al->file_path + strlen(inputs.src) + k) != '\0'; k++) {
        char *c = al->file_path + strlen(inputs.src) + k;
        if (c[0] == '/')
          dash_count++;
        if (c[0] == '-')
          dash_count++;
      }

      insert_anchor_location_array(anchor_locations, al);
      count++;
    }
    free(title);
  }
  return build_anchor_location_index(anchor_locations);
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
