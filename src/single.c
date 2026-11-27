/* SPDX-License-Identifier: CC0-1.0 */
#include "single.h"
#include "html.h"
#include "mkdir_p.h"
#include "render.h"
#include "types.h"
#include <stdlib.h>
#include <string.h>

static int write_single_header(FILE *index_file, Inputs inputs,
                               AnchorLocationArray anchor_locations,
                               int *has_code_block) {
  if (fputs("<!doctype html>\n"
            "<html lang='en'>\n"
            "  <head>\n"
            "    <meta charset='utf-8'>\n"
            "    <meta name='viewport' content='width=device-width, "
            "initial-scale=1.0, viewport-fit=cover'>\n",
            index_file) == EOF ||
      html_theme_init(index_file) != 0 ||
      fputs("    <link rel='icon' href='favicon.svg'>\n", index_file) == EOF ||
      html_css(index_file) != 0 || fputs("    <title>", index_file) == EOF ||
      html_write_escaped(index_file, inputs.project_name) != 0 ||
      fputs("</title>\n"
            "  </head>\n"
            "  <body>\n"
            "  <a class='skip-link' href='#main-content'>Skip to main "
            "content</a>\n"
            "  <input type='checkbox' id='sidebar-checkbox' aria-hidden='true' "
            "tabindex='-1' style='display: none;'>\n"
            "    <div class='body'>\n"
            "      <header class='nav-top-container'>\n"
            "    <nav class='nav-top' aria-label='Documentation controls'>\n"
            "      <button type='button' class='sidebar-toggle' "
            "title='Toggle table of contents' "
            "aria-label='Toggle table of contents' aria-controls='sidebar' "
            "aria-expanded='true'></button>"
            "    <button type='button' class='theme-toggle' "
            "title='Switch to dark theme' "
            "aria-label='Switch to dark theme' "
            "aria-pressed='false'></button>"
            "    </nav>\n"
            "    </header>\n"
            "    <nav id='sidebar' class='sidebar' "
            "aria-label='Table of contents'>\n",
            index_file) == EOF)
    return -1;

  AnchorLocation al = {0};
  int toc_code =
      mmdoc_render_part(inputs.toc_path, index_file, RENDER_TYPE_SINGLE, &al,
                        anchor_locations, "", NULL);
  if (toc_code < 0)
    return -1;
  *has_code_block = toc_code;

  if (fputs("    </nav>\n"
            "    <section id='main'>\n"
            "      <div class='main-scroll'>\n"
            "        <main id='main-content' tabindex='-1'>\n",
            index_file) == EOF)
    return -1;
  return 0;
}

static int write_single_footer(FILE *index_file, int has_code_block) {
  if (fputs("        </main>\n"
            "      </div>\n"
            "      </section>\n"
            "    </div>\n",
            index_file) == EOF ||
      html_js(index_file) != 0 ||
      (has_code_block && html_highlight_js(index_file) != 0) ||
      fputs("  </body>\n"
            "</html>\n",
            index_file) == EOF)
    return -1;
  return 0;
}

static FILE *open_single_index(const char *out) {
  if (mkdir_p(out) != 0) {
    printf("Error recursively making single directory %s\n", out);
    return NULL;
  }
  char *index_path = malloc(strlen(out) + sizeof("/index.html"));
  if (index_path == NULL)
    return NULL;
  sprintf(index_path, "%s/index.html", out);
  FILE *index_file = fopen(index_path, "w");
  free(index_path);
  return index_file;
}

int mmdoc_single_write_from_chapters(Inputs inputs,
                                     AnchorLocationArray anchor_locations,
                                     const ChapterOutputs *chapters,
                                     size_t page_count) {
  FILE *index_file = open_single_index(inputs.out_single);
  if (index_file == NULL)
    return -1;
  char io_buf[65536];
  setvbuf(index_file, io_buf, _IOFBF, sizeof(io_buf));

  int has_code_block = 0;
  int failed = write_single_header(index_file, inputs, anchor_locations,
                                   &has_code_block) != 0;

  for (size_t i = 0; !failed && i < page_count; i++) {
    if (chapters[i].single_html.len > 0 &&
        fwrite(chapters[i].single_html.data, 1, chapters[i].single_html.len,
               index_file) != chapters[i].single_html.len) {
      failed = 1;
      break;
    }
    has_code_block |= chapters[i].has_code_block;
  }

  if (!failed && write_single_footer(index_file, has_code_block) != 0)
    failed = 1;
  if (ferror(index_file))
    failed = 1;
  if (fclose(index_file) != 0)
    failed = 1;
  return failed ? -1 : 0;
}

int mmdoc_single_with_anchors(Inputs inputs,
                              AnchorLocationArray toc_anchor_locations,
                              AnchorLocationArray anchor_locations) {
  FILE *index_file = open_single_index(inputs.out_single);
  if (index_file == NULL)
    return -1;
  char io_buf[65536];
  setvbuf(index_file, io_buf, _IOFBF, sizeof(io_buf));

  AnchorLocationArray lookup =
      anchor_locations.used > 0 ? anchor_locations : toc_anchor_locations;
  int has_code_block = 0;
  int failed =
      write_single_header(index_file, inputs, lookup, &has_code_block) != 0;

  size_t *page_indices = NULL;
  size_t page_count = 0;
  if (!failed && toc_anchor_locations.used > 0) {
    page_indices = malloc(toc_anchor_locations.used * sizeof(*page_indices));
    if (page_indices == NULL) {
      failed = 1;
    } else {
      page_count =
          mmdoc_collect_unique_page_indices(toc_anchor_locations, page_indices);
    }
  }

  for (size_t i = 0; !failed && i < page_count; i++) {
    AnchorLocation *loc = &toc_anchor_locations.array[page_indices[i]];
    int rendered = mmdoc_render_part(loc->file_path, index_file,
                                     RENDER_TYPE_SINGLE, loc, lookup, "", NULL);
    if (rendered < 0) {
      failed = 1;
      break;
    }
    has_code_block |= rendered;
  }
  free(page_indices);

  if (!failed && write_single_footer(index_file, has_code_block) != 0)
    failed = 1;
  if (ferror(index_file))
    failed = 1;
  if (fclose(index_file) != 0)
    failed = 1;
  return failed ? -1 : 0;
}

int mmdoc_single(Inputs inputs, AnchorLocationArray toc_anchor_locations) {
  AnchorLocationArray empty = {0};
  return mmdoc_single_with_anchors(inputs, toc_anchor_locations, empty);
}
