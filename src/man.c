/* SPDX-License-Identifier: CC0-1.0 */
#include "render.h"
#include <string.h>

int mmdoc_man(char *out, char *src, char *toc_path,
              AnchorLocationArray toc_anchor_locations,
              AnchorLocationArray anchor_locations) {
  for (int i = 0; i < toc_anchor_locations.used; i++) {
    AnchorLocation *anchor_location = &toc_anchor_locations.array[i];
    FILE *output_file;
    output_file = fopen(anchor_location->man_output_file_path, "w");
    fputs(anchor_location->man_header, output_file);
    mmdoc_render_part(anchor_location->file_path, output_file, RENDER_TYPE_MAN,
                      anchor_location, anchor_locations, NULL, NULL);
    fclose(output_file);
  }

  return 0;
}
