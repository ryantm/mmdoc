/* SPDX-License-Identifier: CC0-1.0 */
#include "render.h"
#include <string.h>

int mmdoc_man(char *out, char *src, char *toc_path, Array toc_refs,
                     AnchorLocationArray anchor_locations) {
  for (int i = 0; i < toc_refs.used; i++) {
    AnchorLocation anchor_location;
    int found = 0;
    for (int j = 0; j < anchor_locations.used; j++) {
      if (0 == strcmp(toc_refs.array[i], anchor_locations.array[j].anchor)) {
        anchor_location = anchor_locations.array[j];
        found = 1;
        break;
      }
    }
    if (!found) {
      printf("Anchor \"%s\" referenced in toc.md not found.\n",
             toc_refs.array[i]);
      return 1;
    }
    FILE *output_file;
    output_file = fopen(anchor_location.man_output_file_path, "w");
    fputs(anchor_location.man_header, output_file);
    mmdoc_render_part(anchor_location.file_path, output_file, RENDER_TYPE_MAN, anchor_locations, NULL, NULL);
    fclose(output_file);
  }

  return 0;
}
