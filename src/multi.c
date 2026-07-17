/* SPDX-License-Identifier: CC0-1.0 */
#include "multi.h"
#include "asset.h"
#include "render.h"
#include <stdlib.h>
#include <string.h>

static char toc_current_page_url[] = "__MMDOC_CURRENT_PAGE_URL__";
static char search_index_placeholder[] = "search_index.0000000000000000.js";

static char *asset_path(const char *dir, const char *file_name) {
  char *path = malloc(strlen(dir) + 1 + strlen(file_name) + 1);
  if (path != NULL)
    sprintf(path, "%s/%s", dir, file_name);
  return path;
}

static int replace_file_name(const char *path, long offset,
                             const char *old_file_name,
                             const char *new_file_name) {
  if (strlen(old_file_name) != strlen(new_file_name))
    return -1;

  FILE *file = fopen(path, "r+b");
  if (file == NULL)
    return -1;
  if (fseek(file, offset, SEEK_SET) != 0) {
    fclose(file);
    return -1;
  }

  char existing_file_name[ASSET_FILE_NAME_SIZE];
  size_t file_name_length = strlen(old_file_name);
  size_t bytes_read = fread(existing_file_name, 1, file_name_length, file);
  existing_file_name[bytes_read] = '\0';
  if (bytes_read != file_name_length ||
      (strcmp(existing_file_name, old_file_name) != 0 &&
       strcmp(existing_file_name, new_file_name) != 0)) {
    fclose(file);
    return -1;
  }

  int failed = strcmp(existing_file_name, new_file_name) != 0 &&
               (fseek(file, offset, SEEK_SET) != 0 ||
                fwrite(new_file_name, 1, strlen(new_file_name), file) !=
                    strlen(new_file_name));
  return fclose(file) != 0 || failed ? -1 : 0;
}

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
                     const AssetFileNames *asset_names,
                     const char *search_index_name, FILE *search_index_file,
                     long *search_index_name_offset,
                     AnchorLocation *anchor_location,
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
  fputs("    <link rel='stylesheet' href='", page_file);
  fputs(asset_names->a11y_dark_css, page_file);
  fputs("'>\n    <link rel='stylesheet' href='", page_file);
  fputs(asset_names->a11y_light_css, page_file);
  fputs("'>\n    <link rel='stylesheet' href='", page_file);
  fputs(asset_names->mmdoc_css, page_file);
  fputs("'>\n", page_file);
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

  if (has_code_block) {
    fputs("<script defer src='", page_file);
    fputs(asset_names->highlight_pack_js, page_file);
    fputs("'></script>\n", page_file);
  }
  fputs("<script defer src='", page_file);
  fputs(asset_names->mmdoc_js, page_file);
  fputs("'></script>\n<script defer src='", page_file);
  fputs(asset_names->mmdoc_search_js, page_file);
  fputs("' data-search-index='", page_file);
  if (search_index_name_offset != NULL)
    *search_index_name_offset = ftell(page_file);
  fputs(search_index_name, page_file);
  fputs("' data-fuse='", page_file);
  fputs(asset_names->fuse_basic_min_js, page_file);
  fputs("'></script>\n", page_file);

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
  AssetFileNames asset_names;
  asset_file_names(&asset_names);
  if (asset_write_to_dir_fuse_basic_min_js(
          out, asset_names.fuse_basic_min_js) != 0) {
    return -1;
  }
  if (asset_write_to_dir_a11y_dark_css(out, asset_names.a11y_dark_css) != 0 ||
      asset_write_to_dir_a11y_light_css(out, asset_names.a11y_light_css) != 0 ||
      asset_write_to_dir_mmdoc_css(out, asset_names.mmdoc_css) != 0 ||
      asset_write_to_dir_mmdoc_js(out, asset_names.mmdoc_js) != 0 ||
      asset_write_to_dir_mmdoc_search_js(out, asset_names.mmdoc_search_js) !=
          0) {
    return -1;
  }

  char *search_index_path = asset_path(out, "search_index.tmp");
  if (search_index_path == NULL)
    return -1;
  FILE *search_index_file = fopen(search_index_path, "w");
  if (search_index_file == NULL) {
    free(search_index_path);
    return -1;
  }
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
  long *search_index_name_offsets = NULL;
  if (toc_anchor_locations.used > 0) {
    search_index_name_offsets =
        malloc(toc_anchor_locations.used * sizeof(*search_index_name_offsets));
    if (search_index_name_offsets == NULL) {
      free(toc_html);
      fclose(search_index_file);
      free(search_index_path);
      return -1;
    }
  }
  for (int i = 0; i < toc_anchor_locations.used; i++) {
    AnchorLocation *anchor_location = &toc_anchor_locations.array[i];
    if (i + 1 < toc_anchor_locations.used) {
      next_anchor_location = &toc_anchor_locations.array[i + 1];
    } else {
      next_anchor_location = NULL;
    }
    if (mmdoc_multi_page(inputs, anchor_locations, toc_html, toc_has_code_block,
                         &asset_names, search_index_placeholder,
                         search_index_file, &search_index_name_offsets[i],
                         anchor_location, prev_anchor_location,
                         next_anchor_location, &has_code_blocks) != 0) {
      free(toc_html);
      free(search_index_name_offsets);
      fclose(search_index_file);
      free(search_index_path);
      return -1;
    }
    prev_anchor_location = anchor_location;
  }
  free(toc_html);
  int search_index_failed = toc_anchor_locations.used > 0 &&
                            fseek(search_index_file, -1, SEEK_CUR) != 0;
  if (!search_index_failed && fputs("]", search_index_file) == EOF)
    search_index_failed = 1;
  if (fclose(search_index_file) != 0)
    search_index_failed = 1;
  if (search_index_failed) {
    free(search_index_path);
    free(search_index_name_offsets);
    return -1;
  }

  char search_index_name[ASSET_FILE_NAME_SIZE];
  if (asset_hashed_file_name_from_path(search_index_path, "search_index", "js",
                                       search_index_name,
                                       sizeof(search_index_name)) != 0) {
    free(search_index_path);
    free(search_index_name_offsets);
    return -1;
  }
  char *final_search_index_path = asset_path(out, search_index_name);
  if (final_search_index_path == NULL ||
      rename(search_index_path, final_search_index_path) != 0) {
    free(final_search_index_path);
    free(search_index_path);
    free(search_index_name_offsets);
    return -1;
  }
  free(final_search_index_path);
  free(search_index_path);

  for (int i = 0; i < toc_anchor_locations.used; i++)
    if (replace_file_name(
            toc_anchor_locations.array[i].multipage_output_file_path,
            search_index_name_offsets[i], search_index_placeholder,
            search_index_name) != 0) {
      free(search_index_name_offsets);
      return -1;
    }
  free(search_index_name_offsets);

  if (has_code_blocks && asset_write_to_dir_highlight_pack_js(
                             out, asset_names.highlight_pack_js) != 0)
    return -1;
  return 0;
}
