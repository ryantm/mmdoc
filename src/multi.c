/* SPDX-License-Identifier: CC0-1.0 */
#include "multi.h"
#include "asset.h"
#include "render.h"
#include <stdlib.h>
#include <string.h>

static char toc_current_page_url[] = "__MMDOC_CURRENT_PAGE_URL__";

static int write_toc(FILE *page_file, const char *toc_html,
                     const char *multipage_url) {
  size_t placeholder_length = strlen(toc_current_page_url);
  const char *position = toc_html;
  const char *placeholder;
  while ((placeholder = strstr(position, toc_current_page_url)) != NULL) {
    size_t prefix_length = placeholder - position;
    if (fwrite(position, 1, prefix_length, page_file) != prefix_length ||
        fputs(multipage_url, page_file) == EOF)
      return -1;
    position = placeholder + placeholder_length;
  }
  return fputs(position, page_file) == EOF ? -1 : 0;
}

static int render_toc(Inputs inputs, AnchorLocationArray anchor_locations,
                      char **toc_html, int *has_code_block) {
  FILE *toc_file = tmpfile();
  if (toc_file == NULL)
    return -1;

  *has_code_block =
      mmdoc_render_part(inputs.toc_path, toc_file, RENDER_TYPE_MULTIPAGE, NULL,
                        anchor_locations, toc_current_page_url, NULL);
  if (fflush(toc_file) != 0 || fseek(toc_file, 0, SEEK_END) != 0) {
    fclose(toc_file);
    return -1;
  }
  long toc_length = ftell(toc_file);
  if (toc_length < 0 || fseek(toc_file, 0, SEEK_SET) != 0) {
    fclose(toc_file);
    return -1;
  }

  *toc_html = malloc((size_t)toc_length + 1);
  if (*toc_html == NULL) {
    fclose(toc_file);
    return -1;
  }
  size_t bytes_read = fread(*toc_html, 1, (size_t)toc_length, toc_file);
  int read_failed = bytes_read != (size_t)toc_length || ferror(toc_file);
  int close_failed = fclose(toc_file) != 0;
  if (read_failed || close_failed) {
    free(*toc_html);
    *toc_html = NULL;
    return -1;
  }
  (*toc_html)[toc_length] = '\0';
  return 0;
}

int mmdoc_multi_page(Inputs inputs, AnchorLocationArray anchor_locations,
                     const char *toc_html, int toc_has_code_block,
                     FILE *search_index_file, AnchorLocation *anchor_location,
                     AnchorLocation *prev_anchor_location,
                     AnchorLocation *next_anchor_location,
                     int *has_code_blocks) {
  char *page_path = anchor_location->multipage_output_file_path;
  FILE *page_file;
  page_file = fopen(page_path, "w");
  char *html_head_top = "<!doctype html>\n"
                        "<html lang='en'>\n"
                        "  <head>\n"
                        "    <base href='";
  fputs(html_head_top, page_file);
  fputs(anchor_location->multipage_base_href, page_file);
  fputs("'>\n"
        "    <meta charset='utf-8'>\n"
        "    <meta name='viewport' content='width=device-width, "
        "initial-scale=1.0'>\n"
        "    <meta name='description' content='",
        page_file);
  fputs(anchor_location->title, page_file);
  fputs("'>\n"
        "    <link rel='icon' href='favicon.svg'>\n",
        page_file);
  fputs("    <link rel='stylesheet' href='a11y-dark.css'>\n"
        "    <link rel='stylesheet' href='a11y-light.css'>\n"
        "    <link rel='stylesheet' href='mmdoc.css'>\n",
        page_file);
  fputs("    <title>", page_file);
  fputs(anchor_location->title, page_file);
  fputs(" | ", page_file);
  fputs(inputs.project_name, page_file);
  fputs("</title>\n"
        "  </head>\n"
        "  <body>\n",
        page_file);
  fputs("  <a class='skip-link' href='#main-content'>Skip to main "
        "content</a>\n"
        "  <input type='checkbox' id='sidebar-checkbox' aria-hidden='true' "
        "tabindex='-1' style='display: none;'>\n",
        page_file);
  fputs("  <div class='body'>\n", page_file);
  fputs("    <div class='nav-top-container'>\n", page_file);
  fputs("    <nav class='nav-top' aria-label='Documentation controls'>\n",
        page_file);
  fputs("      <button type='button' class='sidebar-toggle' "
        "aria-label='Toggle table of contents' aria-controls='sidebar' "
        "aria-expanded='true'></button>",
        page_file);
  fputs("    <button type='button' class='search-toggle' "
        "aria-label='Open search' aria-controls='search-panel' "
        "aria-expanded='false'></button>",
        page_file);
  fputs("    <button type='button' class='theme-toggle' "
        "aria-label='Switch to dark theme' "
        "aria-pressed='false'></button>",
        page_file);

  if (prev_anchor_location != NULL) {
    fputs("    <a id='chapter-previous-button' class='chapter-previous' href='",
          page_file);
    fputs(prev_anchor_location->multipage_url, page_file);
    fputs(prev_anchor_location->anchor, page_file);
    fputs("' title='", page_file);
    fputs(prev_anchor_location->title, page_file);
    fputs("' aria-label='Previous: ", page_file);
    fputs(prev_anchor_location->title, page_file);
    fputs("'>&lt;</a>\n", page_file);
  } else
    fputs("    <span aria-hidden='true'>&nbsp;</span>", page_file);

  if (next_anchor_location != NULL) {
    fputs("    <a id='chapter-next-button' class='chapter-next' href='",
          page_file);
    fputs(next_anchor_location->multipage_url, page_file);
    fputs(next_anchor_location->anchor, page_file);
    fputs("' title='", page_file);
    fputs(next_anchor_location->title, page_file);
    fputs("' aria-label='Next: ", page_file);
    fputs(next_anchor_location->title, page_file);
    fputs("'>&gt;</a>\n", page_file);
  } else
    fputs("   <span aria-hidden='true'>&nbsp;</span>", page_file);

  fputs("    </nav>\n", page_file);
  fputs("    </div>\n", page_file);
  fputs("    <nav id='sidebar' class='sidebar' "
        "aria-label='Table of contents'>\n",
        page_file);
  int has_code_block = toc_has_code_block;
  if (write_toc(page_file, toc_html, anchor_location->multipage_url) != 0) {
    fclose(page_file);
    return -1;
  }
  fputs("    </nav>\n", page_file);
  fputs("    <section id='main'>\n", page_file);
  fputs("      <div id='search-panel' class='nav-search' role='search' "
        "hidden>\n",
        page_file);
  fputs("        <label class='visually-hidden' for='search'>Search "
        "documentation</label>\n"
        "        <input type='search' id='search' placeholder='Search' "
        "autocomplete='off' aria-controls='search-results'>\n"
        "        <p id='search-status' class='visually-hidden' "
        "role='status' aria-live='polite'></p>\n"
        "        <div id='search-results'></div>\n",
        page_file);
  fputs("      </div>\n", page_file);
  fputs("      <div class='main-scroll'>\n", page_file);
  fputs("        <main id='main-content' tabindex='-1'>\n", page_file);

  if (anchor_location->file_path != NULL)
    has_code_block |= mmdoc_render_part(
        anchor_location->file_path, page_file, RENDER_TYPE_MULTIPAGE,
        anchor_location, anchor_locations, anchor_location->multipage_url,
        search_index_file);

  fputs("        </main>\n", page_file);
  fputs("      </div>\n", page_file);
  fputs("    </section>\n", page_file);

  if (has_code_block)
    fputs("<script defer src='highlight.pack.js'></script>\n", page_file);
  fputs("<script defer src='mmdoc.js'></script>\n"
        "<script defer src='mmdoc_search.js'></script>\n",
        page_file);

  if (has_code_blocks != NULL)
    *has_code_blocks |= has_code_block;

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
  if (asset_write_to_dir_a11y_dark_css(out) != 0 ||
      asset_write_to_dir_a11y_light_css(out) != 0 ||
      asset_write_to_dir_mmdoc_css(out) != 0 ||
      asset_write_to_dir_mmdoc_js(out) != 0 ||
      asset_write_to_dir_mmdoc_search_js(out) != 0) {
    return -1;
  }

  char *search_index_js = "search_index.js";
  char *search_index_path =
      malloc(strlen(out) + 1 + strlen(search_index_js) + 1);
  sprintf(search_index_path, "%s/%s", out, search_index_js);
  FILE *search_index_file = fopen(search_index_path, "w");
  fputs("window.mmdocSearchCorpus = [", search_index_file);

  char *toc_html = NULL;
  int toc_has_code_block = 0;
  if (render_toc(inputs, anchor_locations, &toc_html, &toc_has_code_block) !=
      0) {
    fclose(search_index_file);
    free(search_index_path);
    return -1;
  }

  AnchorLocation *prev_anchor_location = NULL;
  AnchorLocation *next_anchor_location = NULL;
  int has_code_blocks = 0;
  for (int i = 0; i < toc_anchor_locations.used; i++) {
    AnchorLocation *anchor_location = &toc_anchor_locations.array[i];
    if (i + 1 < toc_anchor_locations.used) {
      next_anchor_location = &toc_anchor_locations.array[i + 1];
    } else {
      next_anchor_location = NULL;
    }
    if (mmdoc_multi_page(inputs, anchor_locations, toc_html, toc_has_code_block,
                         search_index_file, anchor_location,
                         prev_anchor_location, next_anchor_location,
                         &has_code_blocks) != 0) {
      free(toc_html);
      fclose(search_index_file);
      free(search_index_path);
      return -1;
    }
    prev_anchor_location = anchor_location;
  }
  free(toc_html);
  fseek(search_index_file, -1, SEEK_CUR);
  fputs("]", search_index_file);
  fclose(search_index_file);
  free(search_index_path);
  if (has_code_blocks && asset_write_to_dir_highlight_pack_js(out) != 0)
    return -1;
  return 0;
}
