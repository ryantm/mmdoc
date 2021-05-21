/* SPDX-License-Identifier: CC0-1.0 */
#include "asset.h"
#include "render.h"
#include <stdlib.h>
#include <string.h>

int mmdoc_multi_page(char *toc_path, char *project_name,
                     AnchorLocationArray anchor_locations,
                     FILE *search_index_file, AnchorLocation *anchor_location,
                     AnchorLocation *prev_anchor_location,
                     AnchorLocation *next_anchor_location) {
  char *page_path = anchor_location->multipage_output_file_path;
  FILE *page_file;
  page_file = fopen(page_path, "w");
  char *html_head_top = "<!doctype html>\n"
                        "<html>\n"
                        "  <head>\n"
                        "    <base href='";
  fputs(html_head_top, page_file);
  fputs(anchor_location->multipage_base_href, page_file);
  char *html_head =
      "'>\n"
      "    <meta charset='utf-8'>\n"
      "    <meta name='viewport' content='width=device-width, "
      "initial-scale=1.0'>\n"
      "    <link rel='icon' href='favicon.svg'>\n"
      "    <link href='minimal.css' rel='stylesheet' type='text/css'>\n"
      "    <link rel='stylesheet' href='a11y-light.css' "
      "media='(prefers-color-scheme: light)'>\n"
      "    <link rel='stylesheet' href='a11y-dark.css' "
      "media='(prefers-color-scheme: dark)'>\n"
      "    <script src='highlight.pack.js'></script>\n"
      "    <script>hljs.highlightAll()</script>\n"
      "    <script>\n"
      "      window.addEventListener('load', (event) => { \n"
      "        let codeElems = "
      "Array.from(document.querySelectorAll('code')).filter(function (elem) "
      "{return !elem.parentElement.classList.contains('heade'); });\n"
      "        codeElems.forEach(function (e) { e.classList.add('hljs'); });\n"
      "      });\n"
      "    </script>\n"
      "    <script src='fuse.basic.min.js'></script>\n"
      "    <script src='search_index.js'></script>\n"
      "    <script src='search.js'></script>\n"
      "    <title>";
  fputs(html_head, page_file);
  fputs(anchor_location->title, page_file);
  fputs(" | ", page_file);
  fputs(project_name, page_file);

  char *html_head_end = "</title>\n"
                        "  </head>\n"
                        "  <body>\n";
  fputs(html_head_end, page_file);
  fputs("    <div class='nav-top-container'>\n", page_file);
  fputs("    <nav class='nav-top'>\n", page_file);
  fputs("      <button type='button' class='sidebar-toggle'>≡</button>",
        page_file);
  fputs("    <button type='button' class='search-toggle "
        "emoji'>🔍&#xFE0E;</button>",
        page_file);
  fputs(
      "    <button type='button' class='theme-toggle emoji'>🌘&#xFE0E;</button>",
      page_file);

  if (prev_anchor_location != NULL) {
    fputs("    <a class='chapter-previous' href='", page_file);
    fputs(prev_anchor_location->multipage_url, page_file);
    fputs("' title='", page_file);
    fputs(prev_anchor_location->title, page_file);
    fputs("'>&lt;</a>\n", page_file);
  } else
    fputs("    <a class='chapter-previous'>&nbsp;</a>", page_file);

  if (next_anchor_location != NULL) {
    fputs("    <a class='chapter-next' href='", page_file);
    fputs(next_anchor_location->multipage_url, page_file);
    fputs("' title='", page_file);
    fputs(next_anchor_location->title, page_file);
    fputs("'>&gt;</a>\n", page_file);
  } else
    fputs("    <a class='chapter-next'>&nbsp;</a>", page_file);

  fputs("    </nav>\n", page_file);
  fputs("    </div>\n", page_file);

  fputs("    <nav class='nav-search' style='display:none;'>\n", page_file);
  fputs("        <input type='search' id='search' placeholder='Search'>\n"
        "        <div id='search-results'></div>\n",
        page_file);
  fputs("    </nav>\n", page_file);

  fputs("    <nav class='sidebar'>\n", page_file);
  mmdoc_render_part(toc_path, page_file, RENDER_TYPE_MULTIPAGE, anchor_location,
                    anchor_locations, NULL, NULL);
  fputs("    </nav>\n", page_file);
  fputs("    <section>\n", page_file);

  fputs("      <main>\n", page_file);

  if (anchor_location->file_path != NULL)
    mmdoc_render_part(anchor_location->file_path, page_file,
                      RENDER_TYPE_MULTIPAGE, anchor_location, anchor_locations,
                      anchor_location->multipage_url, search_index_file);

  fputs("      </main>\n", page_file);

  char *html_foot = "    </section>\n"
                    "  </body>\n"
                    "</html>\n";
  fputs(html_foot, page_file);
  fclose(page_file);
  return 0;
}

int mmdoc_multi(char *out, char *src, char *toc_path,
                AnchorLocationArray toc_anchor_locations,
                AnchorLocationArray anchor_locations, char *project_name) {
  if (asset_write_to_dir_search_js(out) != 0 ||
      asset_write_to_dir_fuse_basic_min_js(out) != 0 ||
      asset_write_to_dir_highlight_pack_js(out) != 0 ||
      asset_write_to_dir_minimal_css(out) != 0 ||
      asset_write_to_dir_a11y_light_css(out) != 0 ||
      asset_write_to_dir_a11y_dark_css(out) != 0) {
    return -1;
  }

  char *search_index_js = "search_index.js";
  char *search_index_path =
      malloc(strlen(out) + 1 + strlen(search_index_js) + 1);
  sprintf(search_index_path, "%s/%s", out, search_index_js);
  FILE *search_index_file = fopen(search_index_path, "w");
  fputs("const corpus = [", search_index_file);

  AnchorLocation *prev_anchor_location = NULL;
  AnchorLocation *next_anchor_location = NULL;
  for (int i = 0; i < toc_anchor_locations.used; i++) {
    AnchorLocation *anchor_location = &toc_anchor_locations.array[i];
    if (i + 1 < toc_anchor_locations.used) {
      next_anchor_location = &toc_anchor_locations.array[i + 1];
    } else {
      next_anchor_location = NULL;
    }
    mmdoc_multi_page(toc_path, project_name, anchor_locations,
                     search_index_file, anchor_location, prev_anchor_location,
                     next_anchor_location);
    prev_anchor_location = anchor_location;
  }
  fseek(search_index_file, -1, SEEK_CUR);
  fputs("]", search_index_file);
  fclose(search_index_file);
  return 0;
}
