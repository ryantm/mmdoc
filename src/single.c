/* SPDX-License-Identifier: CC0-1.0 */
#include "asset.h"
#include "render.h"
#include "types.h"
#include <string.h>

int mmdoc_single(char *out, char *toc_path,
                 AnchorLocationArray toc_anchor_locations) {
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
      "    <script>hljs.initHighlightingOnLoad()</script>\n"
      "    <script>\n"
      "      window.addEventListener('load', (event) => { \n"
      "        let codeElems = "
      "Array.from(document.querySelectorAll('code')).filter(function (elem) "
      "{return !elem.parentElement.classList.contains('heade'); });\n"
      "        codeElems.forEach(function (e) { e.classList.add('hljs'); });\n"
      "      });\n"
      "    </script>\n"
      "  </head>\n"
      "  <body>\n"
      "    <nav>\n"
      "      <div class='sidebar-scrollbox'>\n";
  fputs(html_head, index_file);
  AnchorLocation al;
  mmdoc_render_part(toc_path, index_file, RENDER_TYPE_SINGLE, &al,
                    toc_anchor_locations, NULL, NULL);
  fputs("      </div>\n", index_file);
  fputs("    </nav>\n", index_file);
  fputs("    <section>\n", index_file);
  fputs("      <main>\n", index_file);

  for (int i = 0; i < toc_anchor_locations.used; i++) {
    AnchorLocationArray empty_anchor_locations;
    init_anchor_location_array(&empty_anchor_locations, 0);
    mmdoc_render_part(toc_anchor_locations.array[i].file_path, index_file,
                      RENDER_TYPE_SINGLE, &toc_anchor_locations.array[i],
                      empty_anchor_locations, NULL, NULL);
    free_anchor_location_array(&empty_anchor_locations);
  }

  char *html_foot = "      </main>\n"
                    "    </section>\n"
                    "  </body>\n"
                    "</html>\n";
  fputs(html_foot, index_file);
  fclose(index_file);
  return 0;
}
