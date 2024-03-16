/* SPDX-License-Identifier: CC0-1.0 */
#include "asset.h"
#include "html.h"
#include "inputs.h"
#include "render.h"
#include <stdlib.h>
#include <string.h>

int mmdoc_multi_page(Inputs inputs, AnchorLocationArray anchor_locations,
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
  fputs("'>\n"
        "    <meta charset='utf-8'>\n"
        "    <meta name='viewport' content='width=device-width, "
        "initial-scale=1.0'>\n"
        "    <link rel='icon' href='favicon.svg'>\n",
        page_file);
  html_css(page_file);
  fputs("    <script src='fuse.basic.min.js'></script>\n"
        "    <script src='search_index.js'></script>\n"
        "    <title>",
        page_file);
  fputs(anchor_location->title, page_file);
  fputs(" | ", page_file);
  fputs(inputs.project_name, page_file);
  fputs("</title>\n"
        "  </head>\n"
        "  <body>\n",
        page_file);
  fputs("  <input type='checkbox' id='sidebar-checkbox' style='display: "
        "none;'/>\n",
        page_file);
  fputs("  <div class='body'>\n", page_file);
  fputs("    <div class='nav-top-container'>\n", page_file);
  fputs("    <nav class='nav-top'>\n", page_file);
  fputs("      <label for='sidebar-checkbox' class='sidebar-toggle'>≡</label>",
        page_file);
  fputs("    <button type='button' class='search-toggle "
        "emoji'>🔍&#xFE0E;</button>",
        page_file);
  fputs(
      "    <button type='button' class='theme-toggle emoji'>🌘&#xFE0E;</button>",
      page_file);

  if (prev_anchor_location != NULL) {
    fputs("    <a id='chapter-previous-button' class='chapter-previous' href='", page_file);
    fputs(prev_anchor_location->multipage_url, page_file);
    fputs("' title='", page_file);
    fputs(prev_anchor_location->title, page_file);
    fputs("'>&lt;</a>\n", page_file);
  } else
    fputs("    <a class='chapter-previous'>&nbsp;</a>", page_file);

  if (next_anchor_location != NULL) {
    fputs("    <a id='chapter-next-button' class='chapter-next' href='", page_file);
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
  mmdoc_render_part(inputs.toc_path, page_file, RENDER_TYPE_MULTIPAGE,
                    anchor_location, anchor_locations,
                    anchor_location->multipage_url, NULL);
  fputs("    </nav>\n", page_file);
  fputs("      <section id='main'>\n", page_file);
  fputs("      <main>\n", page_file);

  if (anchor_location->file_path != NULL)
    mmdoc_render_part(anchor_location->file_path, page_file,
                      RENDER_TYPE_MULTIPAGE, anchor_location, anchor_locations,
                      anchor_location->multipage_url, search_index_file);

  fputs("      </main>\n", page_file);
  fputs("    <nav class='sidebar2'>\n", page_file);
  fputs("    </nav>\n", page_file);
  fputs("      </section>\n", page_file);

  html_js(page_file);
  html_search_js(page_file);
  html_highlight_js(page_file);

  char *html_foot = "  </div>\n"
                    "  </body>\n"
                    "</html>\n";
  fputs(html_foot, page_file);
  fclose(page_file);
  return 0;
}

int mmdoc_multi(Inputs inputs, AnchorLocationArray toc_anchor_locations,
                AnchorLocationArray anchor_locations) {
  char *out = inputs.out_multi;
  if (asset_write_to_dir_fuse_basic_min_js(out) != 0) {
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
    mmdoc_multi_page(inputs, anchor_locations, search_index_file,
                     anchor_location, prev_anchor_location,
                     next_anchor_location);
    prev_anchor_location = anchor_location;
  }
  fseek(search_index_file, -1, SEEK_CUR);
  fputs("]", search_index_file);
  fclose(search_index_file);
  return 0;
}
