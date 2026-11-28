/* SPDX-License-Identifier: CC0-1.0 */
#include "multi.h"
#include "asset.h"
#include "build.h"
#include "files.h"
#include "html.h"
#include "mkdir_p.h"
#include "render.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char toc_current_page_url[] = "__MMDOC_CURRENT_PAGE_URL__";

typedef struct {
  const char **parts;
  size_t *lens;
  size_t count;
} TocChunks;

static int split_toc_chunks(const char *toc_html, TocChunks *chunks) {
  *chunks = (TocChunks){};
  if (toc_html == nullptr)
    toc_html = "";

  size_t placeholder_len = strlen(toc_current_page_url);
  size_t count = 1;
  const char *p = toc_html;
  while ((p = strstr(p, toc_current_page_url)) != nullptr) {
    count++;
    p += placeholder_len;
  }

  chunks->parts = malloc(count * sizeof(*chunks->parts));
  chunks->lens = malloc(count * sizeof(*chunks->lens));
  if (chunks->parts == nullptr || chunks->lens == nullptr) {
    free(chunks->parts);
    free(chunks->lens);
    *chunks = (TocChunks){};
    return -1;
  }

  size_t idx = 0;
  const char *pos = toc_html;
  const char *hit;
  while ((hit = strstr(pos, toc_current_page_url)) != nullptr) {
    chunks->parts[idx] = pos;
    chunks->lens[idx] = (size_t)(hit - pos);
    idx++;
    pos = hit + placeholder_len;
  }
  chunks->parts[idx] = pos;
  chunks->lens[idx] = strlen(pos);
  chunks->count = idx + 1;
  return 0;
}

static void free_toc_chunks(TocChunks *chunks) {
  free(chunks->parts);
  free(chunks->lens);
  *chunks = (TocChunks){};
}

static int write_toc_chunks(FILE *page_file, const TocChunks *chunks,
                            const char *multipage_url) {
  size_t url_len = strlen(multipage_url);
  for (size_t i = 0; i < chunks->count; i++) {
    if (chunks->lens[i] > 0 && fwrite(chunks->parts[i], 1, chunks->lens[i],
                                      page_file) != chunks->lens[i])
      return -1;
    if (i + 1 < chunks->count && url_len > 0 &&
        fwrite(multipage_url, 1, url_len, page_file) != url_len)
      return -1;
  }
  return 0;
}

static int write_page_head_and_nav(FILE *page_file, Inputs inputs,
                                   const AnchorLocation *anchor_location,
                                   const AnchorLocation *prev_anchor_location,
                                   const AnchorLocation *next_anchor_location) {
  if (fputs("<!doctype html>\n"
            "<html lang='en'>\n"
            "  <head>\n"
            "    <base href='",
            page_file) == EOF ||
      fputs(anchor_location->multipage_base_href, page_file) == EOF ||
      fputs("'>\n"
            "    <meta charset='utf-8'>\n"
            "    <meta name='viewport' content='width=device-width, "
            "initial-scale=1.0, viewport-fit=cover'>\n",
            page_file) == EOF ||
      html_theme_init(page_file) != 0 ||
      fputs("    <meta name='description' content='", page_file) == EOF ||
      html_write_escaped(page_file, anchor_location->title) != 0 ||
      fputs("'>\n"
            "    <link rel='icon' href='favicon.svg'>\n"
            "    <link rel='stylesheet' href='",
            page_file) == EOF ||
      fputs(asset_file_name(ASSET_A11Y_DARK_CSS), page_file) == EOF ||
      fputs("'>\n    <link rel='stylesheet' href='", page_file) == EOF ||
      fputs(asset_file_name(ASSET_A11Y_LIGHT_CSS), page_file) == EOF ||
      fputs("'>\n    <link rel='stylesheet' href='", page_file) == EOF ||
      fputs(asset_file_name(ASSET_MMDOC_CSS), page_file) == EOF ||
      fputs("'>\n    <title>", page_file) == EOF ||
      html_write_escaped(page_file, anchor_location->title) != 0 ||
      fputs(" | ", page_file) == EOF ||
      html_write_escaped(page_file, inputs.project_name) != 0 ||
      fputs("</title>\n"
            "  </head>\n"
            "  <body>\n"
            "  <a class='skip-link' href='",
            page_file) == EOF ||
      fputs(anchor_location->multipage_url, page_file) == EOF ||
      fputs("#main-content'>Skip to main content</a>\n"
            "  <input type='checkbox' id='sidebar-checkbox' aria-hidden='true' "
            "tabindex='-1' style='display: none;'>\n"
            "  <div class='body'>\n"
            "    <header class='nav-top-container'>\n"
            "    <nav class='nav-top' aria-label='Documentation controls'>\n"
            "      <button type='button' class='sidebar-toggle' "
            "title='Toggle table of contents' "
            "aria-label='Toggle table of contents' aria-controls='sidebar' "
            "aria-expanded='true'></button>"
            "    <button type='button' class='search-toggle' "
            "title='Open search (/)' "
            "aria-label='Open search' aria-controls='search-panel' "
            "aria-expanded='false'></button>"
            "    <button type='button' class='theme-toggle' "
            "title='Switch to dark theme' "
            "aria-label='Switch to dark theme' "
            "aria-pressed='false'></button>",
            page_file) == EOF)
    return -1;

  if (prev_anchor_location != nullptr) {
    if (fputs("    <a id='chapter-previous-button' class='chapter-previous' "
              "href='",
              page_file) == EOF ||
        fputs(prev_anchor_location->multipage_url, page_file) == EOF ||
        html_write_escaped(page_file, prev_anchor_location->anchor) != 0 ||
        fputs("' title='", page_file) == EOF ||
        html_write_escaped(page_file, prev_anchor_location->title) != 0 ||
        fputs("' aria-label='Previous: ", page_file) == EOF ||
        html_write_escaped(page_file, prev_anchor_location->title) != 0 ||
        fputs("'>&lt;</a>\n", page_file) == EOF)
      return -1;
  } else if (fputs("    <span aria-hidden='true'>&nbsp;</span>", page_file) ==
             EOF) {
    return -1;
  }

  if (next_anchor_location != nullptr) {
    if (fputs("    <a id='chapter-next-button' class='chapter-next' href='",
              page_file) == EOF ||
        fputs(next_anchor_location->multipage_url, page_file) == EOF ||
        html_write_escaped(page_file, next_anchor_location->anchor) != 0 ||
        fputs("' title='", page_file) == EOF ||
        html_write_escaped(page_file, next_anchor_location->title) != 0 ||
        fputs("' aria-label='Next: ", page_file) == EOF ||
        html_write_escaped(page_file, next_anchor_location->title) != 0 ||
        fputs("'>&gt;</a>\n", page_file) == EOF)
      return -1;
  } else if (fputs("   <span aria-hidden='true'>&nbsp;</span>", page_file) ==
             EOF) {
    return -1;
  }

  return fputs("    </nav>\n"
               "    </header>\n"
               "    <nav id='sidebar' class='sidebar' "
               "aria-label='Table of contents'>\n",
               page_file) == EOF
             ? -1
             : 0;
}

static int write_page_after_toc(FILE *page_file) {
  return fputs("    </nav>\n"
               "    <section id='main'>\n"
               "      <div id='search-panel' class='nav-search' role='search' "
               "hidden>\n"
               "        <label class='visually-hidden' for='search'>Search "
               "documentation</label>\n"
               "        <input type='search' id='search' placeholder='Search "
               "documentation (/)' "
               "autocomplete='off' aria-controls='search-results'>\n"
               "        <p id='search-status' class='visually-hidden' "
               "role='status' aria-live='polite'></p>\n"
               "        <div id='search-results'></div>\n"
               "      </div>\n"
               "      <div class='main-scroll'>\n"
               "        <main id='main-content' tabindex='-1'>\n",
               page_file) == EOF
             ? -1
             : 0;
}

static int
write_chapter_pagination(FILE *page_file,
                         const AnchorLocation *prev_anchor_location,
                         const AnchorLocation *next_anchor_location) {
  if (prev_anchor_location == nullptr && next_anchor_location == nullptr)
    return 0;

  if (fputs("\n        <nav class='chapter-pagination' "
            "aria-label='Chapter pagination'>\n",
            page_file) == EOF)
    return -1;

  if (prev_anchor_location != nullptr) {
    if (fputs("          <a class='chapter-pagination-link "
              "chapter-pagination-previous' href='",
              page_file) == EOF ||
        fputs(prev_anchor_location->multipage_url, page_file) == EOF ||
        html_write_escaped(page_file, prev_anchor_location->anchor) != 0 ||
        fputs("'><span class='chapter-pagination-label'>Previous</span>"
              "<span class='chapter-pagination-title'>",
              page_file) == EOF ||
        html_write_escaped(page_file, prev_anchor_location->title) != 0 ||
        fputs("</span></a>\n", page_file) == EOF)
      return -1;
  }

  if (next_anchor_location != nullptr) {
    if (fputs("          <a class='chapter-pagination-link "
              "chapter-pagination-next' href='",
              page_file) == EOF ||
        fputs(next_anchor_location->multipage_url, page_file) == EOF ||
        html_write_escaped(page_file, next_anchor_location->anchor) != 0 ||
        fputs("'><span class='chapter-pagination-label'>Next</span>"
              "<span class='chapter-pagination-title'>",
              page_file) == EOF ||
        html_write_escaped(page_file, next_anchor_location->title) != 0 ||
        fputs("</span></a>\n", page_file) == EOF)
      return -1;
  }

  return fputs("        </nav>\n", page_file) == EOF ? -1 : 0;
}

static int write_page_footer(FILE *page_file, const char *search_index_name,
                             bool has_code_block,
                             const AnchorLocation *prev_anchor_location,
                             const AnchorLocation *next_anchor_location) {
  if (write_chapter_pagination(page_file, prev_anchor_location,
                               next_anchor_location) != 0 ||
      fputs("        </main>\n"
            "      </div>\n"
            "    </section>\n",
            page_file) == EOF)
    return -1;

  if (has_code_block) {
    if (fputs("<script defer src='", page_file) == EOF ||
        fputs(asset_file_name(ASSET_HIGHLIGHT_PACK_JS), page_file) == EOF ||
        fputs("'></script>\n", page_file) == EOF)
      return -1;
  }
  if (fputs("<script defer src='", page_file) == EOF ||
      fputs(asset_file_name(ASSET_MMDOC_JS), page_file) == EOF ||
      fputs("'></script>\n<script defer src='", page_file) == EOF ||
      fputs(asset_file_name(ASSET_MMDOC_SEARCH_JS), page_file) == EOF ||
      fputs("' data-search-index='", page_file) == EOF ||
      fputs(search_index_name, page_file) == EOF ||
      fputs("' data-fuse='", page_file) == EOF ||
      fputs(asset_file_name(ASSET_FUSE_BASIC_MIN_JS), page_file) == EOF ||
      fputs("'></script>\n"
            "  </div>\n"
            "  </body>\n"
            "</html>\n",
            page_file) == EOF)
    return -1;
  return 0;
}

static int write_multipage_from_buffer(
    Inputs inputs, const TocChunks *toc_chunks, bool toc_has_code_block,
    const char *search_index_name, const AnchorLocation *anchor_location,
    const AnchorLocation *prev_anchor_location,
    const AnchorLocation *next_anchor_location, const MmdocBuf *body_html,
    bool body_has_code_block) {
  FILE *page_file = fopen(anchor_location->multipage_output_file_path, "wb");
  if (page_file == nullptr)
    return -1;
  char io_buf[65536];
  setvbuf(page_file, io_buf, _IOFBF, sizeof(io_buf));

  bool has_code_block = toc_has_code_block || body_has_code_block;
  bool failed =
      write_page_head_and_nav(page_file, inputs, anchor_location,
                              prev_anchor_location,
                              next_anchor_location) != 0 ||
      write_toc_chunks(page_file, toc_chunks, anchor_location->multipage_url) !=
          0 ||
      write_page_after_toc(page_file) != 0 ||
      (body_html->len > 0 && fwrite(body_html->data, 1, body_html->len,
                                    page_file) != body_html->len) ||
      write_page_footer(page_file, search_index_name, has_code_block,
                        prev_anchor_location, next_anchor_location) != 0;

  if (ferror(page_file))
    failed = true;
  if (fclose(page_file) != 0)
    failed = true;
  return failed ? -1 : 0;
}

static char *chapter_md_url(const AnchorLocation *chapter) {
  const char *dir = chapter->multipage_url;
  if (strcmp(dir, "./") == 0)
    dir = "";
  char *url = malloc(3 * strlen(dir) + sizeof("index.md"));
  if (url == nullptr)
    return nullptr;
  char *out = url;
  for (const unsigned char *p = (const unsigned char *)dir; *p; p++) {
    if (isalnum(*p) || strchr("-._~/", *p)) {
      *out++ = (char)*p;
    } else if (*p == '%' && isxdigit(p[1]) && isxdigit(p[2])) {
      *out++ = (char)*p;
    } else {
      out += sprintf(out, "%%%02X", *p);
    }
  }
  memcpy(out, "index.md", sizeof("index.md"));
  return url;
}

static int write_markdown_index_entry(FILE *index,
                                      const AnchorLocation *chapter) {
  char *url = chapter_md_url(chapter);
  if (url == nullptr)
    return -1;
  const char *label = *chapter->title ? chapter->title : chapter->anchor + 1;
  bool failed = fputs("- [", index) == EOF;
  for (const unsigned char *p = (const unsigned char *)label; !failed && *p;
       p++) {
    if (*p == '[' || *p == ']' || *p == '\\' || *p == '*' || *p == '_' ||
        *p == '`' || *p == '<' || *p == '>' || *p == '~' || *p == '!') {
      if (fputc('\\', index) == EOF)
        failed = true;
    }
    if (!failed && fputc((int)*p, index) == EOF)
      failed = true;
  }
  if (!failed && fprintf(index, "](%s)\n", url) < 0)
    failed = true;
  free(url);
  return failed ? -1 : 0;
}

static int write_static_assets(const char *out, bool any_code_blocks) {
  static constexpr AssetId always_assets[] = {
      ASSET_FUSE_BASIC_MIN_JS, ASSET_A11Y_DARK_CSS, ASSET_A11Y_LIGHT_CSS,
      ASSET_MMDOC_CSS,         ASSET_MMDOC_JS,      ASSET_MMDOC_SEARCH_JS,
  };
  for (size_t i = 0; i < sizeof(always_assets) / sizeof(always_assets[0]);
       i++) {
    if (asset_write_to_dir(out, always_assets[i]) != 0)
      return -1;
  }
  if (any_code_blocks && asset_write_to_dir(out, ASSET_HIGHLIGHT_PACK_JS) != 0)
    return -1;
  return 0;
}

static int write_search_index_file(const char *out,
                                   const char *search_index_name,
                                   const ChapterOutputs *chapters,
                                   size_t page_count) {
  const char *prefix = "window.mmdocSearchCorpus = [";
  size_t prefix_len = strlen(prefix);
  char *search_index_path = mmdoc_file_path(out, search_index_name);
  FILE *search_file =
      search_index_path != nullptr ? fopen(search_index_path, "wb") : nullptr;
  free(search_index_path);
  if (search_file == nullptr)
    return -1;
  char search_io_buf[65536];
  setvbuf(search_file, search_io_buf, _IOFBF, sizeof(search_io_buf));
  bool failed = fwrite(prefix, 1, prefix_len, search_file) != prefix_len;
  for (size_t i = 0; !failed && i < page_count; i++) {
    if (i > 0 && fwrite("\n,", 1, 2, search_file) != 2) {
      failed = true;
      break;
    }
    if (chapters[i].search_json.len > 0 &&
        fwrite(chapters[i].search_json.data, 1, chapters[i].search_json.len,
               search_file) != chapters[i].search_json.len) {
      failed = true;
      break;
    }
  }
  if (!failed) {
    const char *suffix = page_count > 0 ? "\n]" : "]";
    size_t suffix_len = strlen(suffix);
    if (fwrite(suffix, 1, suffix_len, search_file) != suffix_len)
      failed = true;
  }
  if (fclose(search_file) != 0)
    failed = true;
  return failed ? -1 : 0;
}

static int write_staged_llms_files(Inputs inputs, const char *index_path,
                                   const char *full_path,
                                   AnchorLocationArray toc_anchor_locations,
                                   const size_t *page_indices,
                                   const ChapterOutputs *chapters,
                                   size_t page_count) {
  FILE *index_file = index_path != nullptr ? fopen(index_path, "wb") : nullptr;
  FILE *full_file = full_path != nullptr ? fopen(full_path, "wb") : nullptr;
  bool failed = false;
  if (index_file == nullptr || full_file == nullptr ||
      fprintf(index_file,
              "# %s\n\n[Full manual](llms-full.txt)\n\n## Chapters\n\n",
              inputs.project_name) < 0) {
    failed = true;
  } else {
    char full_buf[65536];
    setvbuf(full_file, full_buf, _IOFBF, sizeof(full_buf));
    for (size_t i = 0; i < page_count; i++) {
      const AnchorLocation *chapter =
          &toc_anchor_locations.array[page_indices[i]];
      if (write_markdown_index_entry(index_file, chapter) != 0 ||
          (i > 0 && fputc('\n', full_file) == EOF) ||
          (chapters[i].full_md.len > 0 &&
           fwrite(chapters[i].full_md.data, 1, chapters[i].full_md.len,
                  full_file) != chapters[i].full_md.len)) {
        failed = true;
        break;
      }
    }
  }
  if (index_file != nullptr && fclose(index_file) != 0)
    failed = true;
  if (full_file != nullptr && fclose(full_file) != 0)
    failed = true;
  return failed ? -1 : 0;
}

static int write_chapter_md(const AnchorLocation *loc,
                            const MmdocBuf *chapter_md) {
  char *dest =
      mmdoc_file_path(loc->multipage_output_directory_path, "index.md");
  if (dest == nullptr)
    return -1;
  FILE *f = fopen(dest, "wb");
  if (f == nullptr) {
    perror(dest);
    free(dest);
    return -1;
  }
  char io_buf[65536];
  setvbuf(f, io_buf, _IOFBF, sizeof(io_buf));
  bool failed =
      chapter_md->len > 0 &&
      fwrite(chapter_md->data, 1, chapter_md->len, f) != chapter_md->len;
  if (fclose(f) != 0)
    failed = true;
  free(dest);
  return failed ? -1 : 0;
}

typedef struct {
  Inputs inputs;
  const TocChunks *toc_chunks;
  bool toc_has_code_block;
  bool any_code_blocks;
  const char *search_index_name;
  const char *llms_index_path;
  const char *llms_full_path;
  AnchorLocationArray toc_anchor_locations;
  const size_t *page_indices;
  const size_t *write_order;
  size_t page_count;
  const ChapterOutputs *chapters;
  size_t next_job;
  int failed;
} ChapterWriteCtx;

static void *chapter_write_worker(void *arg) {
  ChapterWriteCtx *ctx = arg;
  size_t total_jobs = ctx->page_count + 3;
  for (;;) {
    size_t job = __atomic_fetch_add(&ctx->next_job, 1, __ATOMIC_RELAXED);
    if (job >= total_jobs)
      break;
    if (__atomic_load_n(&ctx->failed, __ATOMIC_RELAXED))
      break;

    if (job == 0) {
      if (write_staged_llms_files(ctx->inputs, ctx->llms_index_path,
                                  ctx->llms_full_path,
                                  ctx->toc_anchor_locations, ctx->page_indices,
                                  ctx->chapters, ctx->page_count) != 0) {
        __atomic_store_n(&ctx->failed, 1, __ATOMIC_RELAXED);
        break;
      }
      continue;
    }
    if (job == 1) {
      if (write_search_index_file(ctx->inputs.out_multi, ctx->search_index_name,
                                  ctx->chapters, ctx->page_count) != 0) {
        __atomic_store_n(&ctx->failed, 1, __ATOMIC_RELAXED);
        break;
      }
      continue;
    }
    if (job == 2) {
      if (write_static_assets(ctx->inputs.out_multi, ctx->any_code_blocks) !=
          0) {
        __atomic_store_n(&ctx->failed, 1, __ATOMIC_RELAXED);
        break;
      }
      continue;
    }

    size_t slot = job - 3;
    size_t i = ctx->write_order != nullptr ? ctx->write_order[slot] : slot;
    const AnchorLocation *loc =
        &ctx->toc_anchor_locations.array[ctx->page_indices[i]];
    const AnchorLocation *prev =
        i > 0 ? &ctx->toc_anchor_locations.array[ctx->page_indices[i - 1]]
              : nullptr;
    const AnchorLocation *next =
        i + 1 < ctx->page_count
            ? &ctx->toc_anchor_locations.array[ctx->page_indices[i + 1]]
            : nullptr;

    if (loc->multipage_output_directory_path != nullptr &&
        strcmp(loc->multipage_output_directory_path, ctx->inputs.out_multi) !=
            0 &&
        mkdir_p(loc->multipage_output_directory_path) != 0) {
      __atomic_store_n(&ctx->failed, 1, __ATOMIC_RELAXED);
      break;
    }

    const MmdocBuf *multi_html = ctx->chapters[i].multi_html.data != nullptr
                                     ? &ctx->chapters[i].multi_html
                                     : &ctx->chapters[i].single_html;
    const MmdocBuf *chap_md = ctx->chapters[i].chapter_md.data != nullptr
                                  ? &ctx->chapters[i].chapter_md
                                  : &ctx->chapters[i].full_md;
    if (write_multipage_from_buffer(
            ctx->inputs, ctx->toc_chunks, ctx->toc_has_code_block,
            ctx->search_index_name, loc, prev, next, multi_html,
            ctx->chapters[i].has_code_block) != 0 ||
        write_chapter_md(loc, chap_md) != 0) {
      __atomic_store_n(&ctx->failed, 1, __ATOMIC_RELAXED);
      break;
    }
  }
  return nullptr;
}

static int publish_staged_file(const char *temporary, const char *directory,
                               const char *name) {
  char *path = mmdoc_file_path(directory, name);
  if (path == nullptr)
    return -1;
  int res = rename(temporary, path);
  if (res != 0)
    perror(path);
  free(path);
  return res;
}

int mmdoc_multi_write_from_chapters(Inputs inputs,
                                    AnchorLocationArray toc_anchor_locations,
                                    const size_t *page_indices,
                                    size_t page_count,
                                    AnchorLocationArray anchor_locations,
                                    const ChapterOutputs *chapters,
                                    size_t num_workers) {
  char *out = inputs.out_multi;
  if (mkdir_p(out) != 0) {
    printf("Error recursively making directory %s\n", out);
    return -1;
  }

  MmdocBuf toc_html = {};
  int toc_rc =
      mmdoc_render_to_buf(inputs.toc_path, RENDER_TYPE_MULTIPAGE,
                          anchor_locations, toc_current_page_url, &toc_html);
  if (toc_rc < 0)
    return -1;
  bool toc_has_code_block = toc_rc != 0;

  TocChunks toc_chunks;
  if (split_toc_chunks(toc_html.data, &toc_chunks) != 0) {
    mmdoc_buf_free(&toc_html);
    return -1;
  }

  /* Fast 32-byte/iter XXH64 hash of search_index in memory so we know its final
   * filename before writing any HTML page (zero temp re-read, zero patching).
   */
  const char *prefix = "window.mmdocSearchCorpus = [";
  size_t prefix_len = strlen(prefix);
  AssetHashState hash_state;
  asset_hash_state_init(&hash_state);
  asset_hash_state_feed(&hash_state, (const unsigned char *)prefix, prefix_len);
  bool any_code_blocks = toc_has_code_block;
  for (size_t i = 0; i < page_count; i++) {
    any_code_blocks = any_code_blocks || chapters[i].has_code_block;
    if (i > 0)
      asset_hash_state_feed(&hash_state, (const unsigned char *)"\n,", 2);
    if (chapters[i].search_json.len > 0)
      asset_hash_state_feed(&hash_state,
                            (const unsigned char *)chapters[i].search_json.data,
                            chapters[i].search_json.len);
  }
  if (page_count > 0)
    asset_hash_state_feed(&hash_state, (const unsigned char *)"\n]", 2);
  else
    asset_hash_state_feed(&hash_state, (const unsigned char *)"]", 1);

  char search_index_name[ASSET_FILE_NAME_SIZE];
  asset_format_hashed_name("search_index", "js",
                           asset_hash_state_final(&hash_state),
                           search_index_name, sizeof(search_index_name));

  char *staging = mmdoc_file_path(out, ".mmdoc-XXXXXX");
  if (staging == nullptr || mkdtemp(staging) == nullptr) {
    free(staging);
    free_toc_chunks(&toc_chunks);
    mmdoc_buf_free(&toc_html);
    return -1;
  }
  char *index_path = mmdoc_file_path(staging, "llms.txt");
  char *full_path = mmdoc_file_path(staging, "llms-full.txt");

  /* Sort chapter writes by descending output size (LPT) so large chapters start
   * immediately while smaller chapters fill remaining worker slots. */
  size_t *write_order = nullptr;
  if (page_count > 1) {
    size_t *weights = malloc(page_count * sizeof(*weights));
    if (weights != nullptr) {
      for (size_t i = 0; i < page_count; i++) {
        weights[i] =
            (chapters[i].multi_html.data != nullptr
                 ? chapters[i].multi_html.len
                 : chapters[i].single_html.len) +
            (chapters[i].chapter_md.data != nullptr ? chapters[i].chapter_md.len
                                                    : chapters[i].full_md.len);
      }
      write_order = mmdoc_sort_desc_order(weights, page_count);
      free(weights);
    }
  }

  ChapterWriteCtx ctx = {
      .inputs = inputs,
      .toc_chunks = &toc_chunks,
      .toc_has_code_block = toc_has_code_block,
      .any_code_blocks = any_code_blocks,
      .search_index_name = search_index_name,
      .llms_index_path = index_path,
      .llms_full_path = full_path,
      .toc_anchor_locations = toc_anchor_locations,
      .page_indices = page_indices,
      .write_order = write_order,
      .page_count = page_count,
      .chapters = chapters,
      .next_job = 0,
      .failed = (index_path == nullptr || full_path == nullptr) ? 1 : 0,
  };

  size_t total_jobs = page_count + 3;
  size_t threads_to_use =
      num_workers > 1 ? (num_workers < total_jobs ? num_workers : total_jobs)
                      : 1;
  if (threads_to_use > 8)
    threads_to_use = 8;

  if (!ctx.failed)
    mmdoc_run_parallel(threads_to_use, chapter_write_worker, &ctx);

  free(write_order);
  free_toc_chunks(&toc_chunks);
  mmdoc_buf_free(&toc_html);

  /* Publish llms-full.txt and llms.txt LAST only after every chapter succeeded
   * (preserves atomic Markdown publishing guarantee). */
  int result = ctx.failed ? -1 : 0;
  if (result == 0)
    result = publish_staged_file(full_path, out, "llms-full.txt");
  if (result == 0)
    result = publish_staged_file(index_path, out, "llms.txt");

  if (full_path != nullptr)
    unlink(full_path);
  if (index_path != nullptr)
    unlink(index_path);
  rmdir(staging);
  free(full_path);
  free(index_path);
  free(staging);
  return result;
}
