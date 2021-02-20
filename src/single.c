/* SPDX-License-Identifier: CC0-1.0 */
#include "types.h"
#include "render.h"
#include "asset.h"
#include <string.h>

int mmdoc_single(char *out, char *toc_path, Array toc_refs,
                 AnchorLocationArray anchor_locations) {
  asset_write_to_dir_highlight_pack_js(out);
  asset_write_to_dir_minimal_css(out);
  asset_write_to_dir_mono_blue_css(out);

  char index_path[2048];
  FILE *index_file;
  strcpy(index_path, out);
  strcat(index_path, "/index.html");
  index_file = fopen(index_path, "w");

  char *html_head =
      "<!doctype html>\n"
      "<html>\n"
      "  <head>\n"
      "    <base href='/'>\n"
      "    <meta charset='utf-8'>\n"
      "    <link href='minimal.css' rel='stylesheet' type='text/css'>\n"
      "    <link rel='stylesheet' href='mono-blue.css'>\n"
      "    <script src='highlight.pack.js'></script>\n"
      "    <script>hljs.initHighlightingOnLoad();</script>\n"
      "  </head>\n"
      "  <body>\n"
      "    <nav>\n";
  fputs(html_head, index_file);
  mmdoc_render_part(toc_path, index_file, RENDER_TYPE_SINGLE, anchor_locations,
                    NULL, NULL);
  fputs("    </nav>\n", index_file);
  fputs("    <section>\n", index_file);

  for (int i = 0; i < toc_refs.used; i++) {
    char *file_path;
    int found = 0;
    for (int j = 0; j < anchor_locations.used; j++) {
      if (0 == strcmp(toc_refs.array[i], anchor_locations.array[j].anchor)) {
        file_path = anchor_locations.array[j].file_path;
        found = 1;
        break;
      }
    }
    if (!found) {
      printf("Anchor \"%s\" referenced in toc.md not found.\n",
             toc_refs.array[i]);
      return 1;
    }
    AnchorLocationArray empty_anchor_locations;
    init_anchor_location_array(&empty_anchor_locations, 0);
    mmdoc_render_part(file_path, index_file, RENDER_TYPE_SINGLE,
                      empty_anchor_locations, NULL, NULL);
    free_anchor_location_array(&empty_anchor_locations);
  }
  char *html_foot = "    </section>\n"
                    "  </body>\n"
                    "</html>\n";
  fputs(html_foot, index_file);
  fclose(index_file);
  return 0;
}
