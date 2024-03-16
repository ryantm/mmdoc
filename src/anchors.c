/* SPDX-License-Identifier: CC0-1.0 */
#include "inputs.h"
#include "mkdir_p.h"
#include "render.h"
#include "types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum ref_parse_state { NONE, L, REF };

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

int mmdoc_anchors_locations(AnchorLocationArray *anchor_locations,
                            Array *md_files, Array *toc_refs, Inputs inputs) {
  char *index_anchor = toc_refs->array[0];

  init_anchor_location_array(anchor_locations, 500);

  int count = 0;
  for (int i = 0; i < md_files->used; i++) {
    Array anchors;
    init_array(&anchors, 500);
    mmdoc_anchors(&anchors, md_files->array[i]);
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

      al->title = mmdoc_render_get_title_from_file(al->file_path);

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
  }
  return 0;
}

int mmdoc_anchors_find_toc_anchors(AnchorLocationArray *toc_anchor_locations,
                                   Array *toc_refs,
                                   AnchorLocationArray *anchor_locations) {

  for (int i = 0; i < toc_refs->used; i++) {
    AnchorLocation *anchor_location;
    int found = 0;
    for (int j = 0; j < anchor_locations->used; j++) {
      if (strcmp(toc_refs->array[i], anchor_locations->array[j].anchor) == 0) {
        anchor_location = &anchor_locations->array[j];
        found = 1;
        break;
      }
    }
    if (!found) {
      printf("Anchor \"%s\" referenced in toc.md not found.\n",
             toc_refs->array[i]);
      return -1;
    }
    insert_anchor_location_array(toc_anchor_locations, anchor_location);
  }
  return 0;
}
