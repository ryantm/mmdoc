/* SPDX-License-Identifier: CC0-1.0 */
#include "render.h"
#include "asset.h"
#include <string.h>
#include <stdlib.h>

int mmdoc_multi_page(char *page_path, char *toc_path, char *input_path,
                            AnchorLocationArray anchor_locations,
                            char *multipage_url, FILE *search_index_file) {
  FILE *page_file;
  page_file = fopen(page_path, "w");
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
      "        let codeElems = Array.from(document.querySelectorAll('code')).filter(function (elem) {return !elem.parentElement.classList.contains('heade'); });\n"
      "        codeElems.forEach(function (e) { e.classList.add('hljs'); });\n"
      "      });\n"
      "    </script>\n"
      "    <script src='fuse.basic.min.js'></script>\n"
      "    <script src='search_index.js'></script>\n"
      "    <script src='search.js'></script>\n"
      "  </head>\n"
      "  <body>\n"
      "    <nav>\n"
      "      <div class='sidebar-scrollbox'>\n"
      "        <input type='search' id='search' placeholder='Search'>\n"
      "        <div id='search-results'></div>\n";
  fputs(html_head, page_file);
  mmdoc_render_part(toc_path, page_file, RENDER_TYPE_MULTIPAGE,
                    anchor_locations, NULL, NULL);
  fputs("      </div>\n", page_file);
  fputs("    </nav>\n", page_file);
  fputs("    <section>\n", page_file);
  fputs("      <main>\n", page_file);
  mmdoc_render_part(input_path, page_file, RENDER_TYPE_MULTIPAGE,
                    anchor_locations, multipage_url, search_index_file);
  char *html_foot = "      </main>\n"
                    "    </section>\n"
                    "  </body>\n"
                    "</html>\n";
  fputs(html_foot, page_file);
  fclose(page_file);
  return 0;
}

int mmdoc_multi(char *out, char *src, char *toc_path, Array toc_refs,
                       AnchorLocationArray anchor_locations) {
  asset_write_to_dir_search_js(out);
  asset_write_to_dir_fuse_basic_min_js(out);
  asset_write_to_dir_highlight_pack_js(out);
  asset_write_to_dir_minimal_css(out);
  asset_write_to_dir_mono_blue_css(out);

  char *search_index_js = "search_index.js";
  char *search_index_path = malloc(strlen(out) + 1 + strlen(search_index_js) + 1);
  sprintf(search_index_path, "%s/%s", out, search_index_js);
  FILE *search_index_file = fopen(search_index_path, "w");
  fputs("const corpus = [", search_index_file);
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

    mmdoc_multi_page(anchor_location.multipage_output_file_path,
                     toc_path, anchor_location.file_path,
                     anchor_locations, anchor_location.multipage_url,
                     search_index_file);
  }
  fseek(search_index_file, -1, SEEK_CUR);
  fputs("]", search_index_file);
  fclose(search_index_file);
  return 0;
}
