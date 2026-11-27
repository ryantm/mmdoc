/* SPDX-License-Identifier: CC0-1.0 */
#include "multi.h"
#include "asset.h"
#include "files.h"
#include "html.h"
#include "markdown.h"
#include "mkdir_p.h"
#include "render.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

static char toc_current_page_url[] = "__MMDOC_CURRENT_PAGE_URL__";
static char search_index_placeholder[] = "search_index.0000000000000000.js";

typedef struct {
  const char **parts;
  size_t *lens;
  size_t count;
} TocChunks;

static int split_toc_chunks(const char *toc_html, TocChunks *chunks) {
  chunks->parts = NULL;
  chunks->lens = NULL;
  chunks->count = 0;

  size_t placeholder_len = strlen(toc_current_page_url);
  size_t count = 1;
  const char *p = toc_html;
  while ((p = strstr(p, toc_current_page_url)) != NULL) {
    count++;
    p += placeholder_len;
  }

  chunks->parts = malloc(count * sizeof(*chunks->parts));
  chunks->lens = malloc(count * sizeof(*chunks->lens));
  if (chunks->parts == NULL || chunks->lens == NULL) {
    free(chunks->parts);
    free(chunks->lens);
    chunks->parts = NULL;
    chunks->lens = NULL;
    return -1;
  }

  size_t idx = 0;
  const char *pos = toc_html;
  const char *hit;
  while ((hit = strstr(pos, toc_current_page_url)) != NULL) {
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
  chunks->parts = NULL;
  chunks->lens = NULL;
  chunks->count = 0;
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
    size_t prefix_length = (size_t)(placeholder - position);
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
  if (*has_code_block < 0) {
    fclose(toc_file);
    return -1;
  }
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

static int write_page_head_and_nav(FILE *page_file, Inputs inputs,
                                   const AssetFileNames *asset_names,
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
            "initial-scale=1.0'>\n",
            page_file) == EOF ||
      html_theme_init(page_file) != 0 ||
      fputs("    <meta name='description' content='", page_file) == EOF ||
      html_write_escaped(page_file, anchor_location->title) != 0 ||
      fputs("'>\n"
            "    <link rel='icon' href='favicon.svg'>\n"
            "    <link rel='stylesheet' href='",
            page_file) == EOF ||
      fputs(asset_names->a11y_dark_css, page_file) == EOF ||
      fputs("'>\n    <link rel='stylesheet' href='", page_file) == EOF ||
      fputs(asset_names->a11y_light_css, page_file) == EOF ||
      fputs("'>\n    <link rel='stylesheet' href='", page_file) == EOF ||
      fputs(asset_names->mmdoc_css, page_file) == EOF ||
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
            "    <div class='nav-top-container'>\n"
            "    <nav class='nav-top' aria-label='Documentation controls'>\n"
            "      <button type='button' class='sidebar-toggle' "
            "aria-label='Toggle table of contents' aria-controls='sidebar' "
            "aria-expanded='true'></button>"
            "    <button type='button' class='search-toggle' "
            "aria-label='Open search' aria-controls='search-panel' "
            "aria-expanded='false'></button>"
            "    <button type='button' class='theme-toggle' "
            "aria-label='Switch to dark theme' "
            "aria-pressed='false'></button>",
            page_file) == EOF)
    return -1;

  if (prev_anchor_location != NULL) {
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

  if (next_anchor_location != NULL) {
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
               "    </div>\n"
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
               "        <input type='search' id='search' placeholder='Search' "
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

static int write_page_footer(FILE *page_file, const AssetFileNames *asset_names,
                             const char *search_index_name,
                             long *search_index_name_offset,
                             int has_code_block) {
  if (fputs("        </main>\n"
            "      </div>\n"
            "    </section>\n",
            page_file) == EOF)
    return -1;

  if (has_code_block) {
    if (fputs("<script defer src='", page_file) == EOF ||
        fputs(asset_names->highlight_pack_js, page_file) == EOF ||
        fputs("'></script>\n", page_file) == EOF)
      return -1;
  }
  if (fputs("<script defer src='", page_file) == EOF ||
      fputs(asset_names->mmdoc_js, page_file) == EOF ||
      fputs("'></script>\n<script defer src='", page_file) == EOF ||
      fputs(asset_names->mmdoc_search_js, page_file) == EOF ||
      fputs("' data-search-index='", page_file) == EOF)
    return -1;
  if (search_index_name_offset != NULL)
    *search_index_name_offset = ftell(page_file);
  if (fputs(search_index_name, page_file) == EOF ||
      fputs("' data-fuse='", page_file) == EOF ||
      fputs(asset_names->fuse_basic_min_js, page_file) == EOF ||
      fputs("'></script>\n"
            "  </div>\n"
            "  </body>\n"
            "</html>\n",
            page_file) == EOF)
    return -1;
  return 0;
}

static int write_multipage_from_buffer(
    Inputs inputs, const TocChunks *toc_chunks, int toc_has_code_block,
    const AssetFileNames *asset_names, const char *search_index_name,
    const AnchorLocation *anchor_location,
    const AnchorLocation *prev_anchor_location,
    const AnchorLocation *next_anchor_location, const MmdocBuf *body_html,
    int body_has_code_block) {
  FILE *page_file = fopen(anchor_location->multipage_output_file_path, "wb");
  if (page_file == NULL)
    return -1;
  char io_buf[65536];
  setvbuf(page_file, io_buf, _IOFBF, sizeof(io_buf));

  int has_code_block = toc_has_code_block | body_has_code_block;
  int failed = write_page_head_and_nav(page_file, inputs, asset_names,
                                       anchor_location, prev_anchor_location,
                                       next_anchor_location) != 0 ||
               write_toc_chunks(page_file, toc_chunks,
                                anchor_location->multipage_url) != 0 ||
               write_page_after_toc(page_file) != 0 ||
               (body_html->len > 0 && fwrite(body_html->data, 1, body_html->len,
                                             page_file) != body_html->len) ||
               write_page_footer(page_file, asset_names, search_index_name,
                                 NULL, has_code_block) != 0;

  if (ferror(page_file))
    failed = 1;
  if (fclose(page_file) != 0)
    failed = 1;
  return failed ? -1 : 0;
}

typedef struct {
  Inputs inputs;
  const TocChunks *toc_chunks;
  int toc_has_code_block;
  const AssetFileNames *asset_names;
  const char *search_index_name;
  const char *md_staging_dir;
  AnchorLocationArray toc_anchor_locations;
  const size_t *page_indices;
  size_t page_count;
  const ChapterOutputs *chapters;
  size_t next_index;
  size_t next_worker_id;
  int failed;
} ChapterWriteCtx;

static int write_and_publish_chapter_md(const char *staging_dir,
                                        size_t worker_id,
                                        const AnchorLocation *loc,
                                        const MmdocBuf *chapter_md) {
  char tmp_name[48];
  snprintf(tmp_name, sizeof(tmp_name), "index-%zu.md", worker_id);
  char *tmp_path = mmdoc_file_path(staging_dir, tmp_name);
  if (tmp_path == NULL)
    return -1;
  FILE *f = fopen(tmp_path, "wb");
  if (f == NULL) {
    free(tmp_path);
    return -1;
  }
  int failed =
      chapter_md->len > 0 &&
      fwrite(chapter_md->data, 1, chapter_md->len, f) != chapter_md->len;
  if (fclose(f) != 0)
    failed = 1;
  if (!failed) {
    char *dest =
        mmdoc_file_path(loc->multipage_output_directory_path, "index.md");
    if (dest == NULL || rename(tmp_path, dest) != 0) {
      if (dest != NULL)
        perror(dest);
      failed = 1;
    }
    free(dest);
  }
  if (failed)
    unlink(tmp_path);
  free(tmp_path);
  return failed ? -1 : 0;
}

static void *chapter_write_worker(void *arg) {
  ChapterWriteCtx *ctx = arg;
  size_t worker_id =
      __atomic_fetch_add(&ctx->next_worker_id, 1, __ATOMIC_RELAXED);
  for (;;) {
    size_t i = __atomic_fetch_add(&ctx->next_index, 1, __ATOMIC_RELAXED);
    if (i >= ctx->page_count)
      break;
    if (__atomic_load_n(&ctx->failed, __ATOMIC_RELAXED))
      break;

    const AnchorLocation *loc =
        &ctx->toc_anchor_locations.array[ctx->page_indices[i]];
    const AnchorLocation *prev =
        i > 0 ? &ctx->toc_anchor_locations.array[ctx->page_indices[i - 1]]
              : NULL;
    const AnchorLocation *next =
        i + 1 < ctx->page_count
            ? &ctx->toc_anchor_locations.array[ctx->page_indices[i + 1]]
            : NULL;

    if (loc->multipage_output_directory_path != NULL &&
        strcmp(loc->multipage_output_directory_path, ctx->inputs.out_multi) !=
            0 &&
        mkdir_p(loc->multipage_output_directory_path) != 0) {
      __atomic_store_n(&ctx->failed, 1, __ATOMIC_RELAXED);
      break;
    }

    if (write_multipage_from_buffer(ctx->inputs, ctx->toc_chunks,
                                    ctx->toc_has_code_block, ctx->asset_names,
                                    ctx->search_index_name, loc, prev, next,
                                    &ctx->chapters[i].multi_html,
                                    ctx->chapters[i].has_code_block) != 0 ||
        write_and_publish_chapter_md(ctx->md_staging_dir, worker_id, loc,
                                     &ctx->chapters[i].chapter_md) != 0) {
      __atomic_store_n(&ctx->failed, 1, __ATOMIC_RELAXED);
      break;
    }
  }
  return NULL;
}

static int write_static_assets(char *out, const AssetFileNames *asset_names) {
  if (asset_write_to_dir_fuse_basic_min_js(
          out, asset_names->fuse_basic_min_js) != 0 ||
      asset_write_to_dir_a11y_dark_css(out, asset_names->a11y_dark_css) != 0 ||
      asset_write_to_dir_a11y_light_css(out, asset_names->a11y_light_css) !=
          0 ||
      asset_write_to_dir_mmdoc_css(out, asset_names->mmdoc_css) != 0 ||
      asset_write_to_dir_mmdoc_js(out, asset_names->mmdoc_js) != 0 ||
      asset_write_to_dir_mmdoc_search_js(out, asset_names->mmdoc_search_js) !=
          0)
    return -1;
  return 0;
}

static int publish_staged_file(const char *temporary, const char *directory,
                               const char *name) {
  char *path = mmdoc_file_path(directory, name);
  if (path == NULL)
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

  AssetFileNames asset_names;
  asset_file_names(&asset_names);
  if (write_static_assets(out, &asset_names) != 0)
    return -1;

  char *toc_html = NULL;
  int toc_has_code_block = 0;
  if (render_toc(inputs, anchor_locations, &toc_html, &toc_has_code_block) != 0)
    return -1;

  TocChunks toc_chunks;
  if (split_toc_chunks(toc_html, &toc_chunks) != 0) {
    free(toc_html);
    return -1;
  }

  /* Hash search_index directly in memory so we know its final filename before
   * writing any HTML page (zero temp re-read, zero HTML patching). */
  const char *prefix = "window.mmdocSearchCorpus = [";
  size_t prefix_len = strlen(prefix);
  uint64_t hash = asset_hash_feed(asset_hash_init(),
                                  (const unsigned char *)prefix, prefix_len);
  int any_code_blocks = toc_has_code_block;
  for (size_t i = 0; i < page_count; i++) {
    any_code_blocks |= chapters[i].has_code_block;
    if (i > 0)
      hash = asset_hash_feed(hash, (const unsigned char *)"\n,", 2);
    if (chapters[i].search_json.len > 0)
      hash = asset_hash_feed(
          hash, (const unsigned char *)chapters[i].search_json.data,
          chapters[i].search_json.len);
  }
  if (page_count > 0)
    hash = asset_hash_feed(hash, (const unsigned char *)"\n]", 2);
  else
    hash = asset_hash_feed(hash, (const unsigned char *)"]", 1);

  char search_index_name[ASSET_FILE_NAME_SIZE];
  asset_format_hashed_name("search_index", "js", hash, search_index_name,
                           sizeof(search_index_name));

  char *staging = mmdoc_file_path(out, ".mmdoc-XXXXXX");
  if (staging == NULL || mkdtemp(staging) == NULL) {
    free(staging);
    free_toc_chunks(&toc_chunks);
    free(toc_html);
    return -1;
  }

  /* Start parallel chapter HTML + Markdown workers while main thread writes
   * search_index, llms-full.txt, and llms.txt into staging. */
  ChapterWriteCtx ctx = {
      .inputs = inputs,
      .toc_chunks = &toc_chunks,
      .toc_has_code_block = toc_has_code_block,
      .asset_names = &asset_names,
      .search_index_name = search_index_name,
      .md_staging_dir = staging,
      .toc_anchor_locations = toc_anchor_locations,
      .page_indices = page_indices,
      .page_count = page_count,
      .chapters = chapters,
      .next_index = 0,
      .next_worker_id = 0,
      .failed = 0,
  };

  size_t threads_to_use =
      (page_count >= 8 && num_workers > 1)
          ? (num_workers < page_count ? num_workers : page_count)
          : 1;
  if (threads_to_use > 8)
    threads_to_use = 8;

  pthread_t *threads = NULL;
  size_t spawned = 0;
  if (threads_to_use > 1) {
    threads = malloc((threads_to_use - 1) * sizeof(*threads));
    if (threads != NULL) {
      for (size_t t = 0; t < threads_to_use - 1; t++) {
        if (pthread_create(&threads[t], NULL, chapter_write_worker, &ctx) == 0)
          spawned++;
        else
          break;
      }
    }
  }

  /* Write search_index.<hash>.js, staged llms-full.txt, and staged llms.txt
   * concurrently with the chapter writers! */
  int main_failed = 0;
  char *search_index_path = mmdoc_file_path(out, search_index_name);
  FILE *search_file =
      search_index_path != NULL ? fopen(search_index_path, "wb") : NULL;
  free(search_index_path);
  if (search_file == NULL) {
    main_failed = 1;
  } else {
    char search_io_buf[65536];
    setvbuf(search_file, search_io_buf, _IOFBF, sizeof(search_io_buf));
    if (fwrite(prefix, 1, prefix_len, search_file) != prefix_len)
      main_failed = 1;
    for (size_t i = 0; !main_failed && i < page_count; i++) {
      if (i > 0 && fwrite("\n,", 1, 2, search_file) != 2) {
        main_failed = 1;
        break;
      }
      if (chapters[i].search_json.len > 0 &&
          fwrite(chapters[i].search_json.data, 1, chapters[i].search_json.len,
                 search_file) != chapters[i].search_json.len) {
        main_failed = 1;
        break;
      }
    }
    if (!main_failed) {
      const char *suffix = page_count > 0 ? "\n]" : "]";
      size_t suffix_len = strlen(suffix);
      if (fwrite(suffix, 1, suffix_len, search_file) != suffix_len)
        main_failed = 1;
    }
    if (fclose(search_file) != 0)
      main_failed = 1;
  }

  char *index_path = mmdoc_file_path(staging, "llms.txt");
  char *full_path = mmdoc_file_path(staging, "llms-full.txt");
  if (!main_failed) {
    FILE *index_file = index_path != NULL ? fopen(index_path, "wb") : NULL;
    FILE *full_file = full_path != NULL ? fopen(full_path, "wb") : NULL;
    if (index_file == NULL || full_file == NULL ||
        fprintf(index_file,
                "# %s\n\n[Full manual](llms-full.txt)\n\n## Chapters\n\n",
                inputs.project_name) < 0) {
      main_failed = 1;
    } else {
      char full_buf[65536];
      setvbuf(full_file, full_buf, _IOFBF, sizeof(full_buf));
      for (size_t i = 0; i < page_count; i++) {
        const AnchorLocation *chapter =
            &toc_anchor_locations.array[page_indices[i]];
        if (mmdoc_markdown_write_index_entry(index_file, chapter) != 0 ||
            (i > 0 && fputc('\n', full_file) == EOF) ||
            (chapters[i].full_md.len > 0 &&
             fwrite(chapters[i].full_md.data, 1, chapters[i].full_md.len,
                    full_file) != chapters[i].full_md.len)) {
          main_failed = 1;
          break;
        }
      }
    }
    if (index_file != NULL && fclose(index_file) != 0)
      main_failed = 1;
    if (full_file != NULL && fclose(full_file) != 0)
      main_failed = 1;
  }

  if (!main_failed && any_code_blocks &&
      asset_write_to_dir_highlight_pack_js(out,
                                           asset_names.highlight_pack_js) != 0)
    main_failed = 1;

  /* Help finish any remaining chapter HTML + MD writes. */
  if (!main_failed)
    chapter_write_worker(&ctx);
  else
    __atomic_store_n(&ctx.failed, 1, __ATOMIC_RELAXED);

  for (size_t t = 0; t < spawned; t++)
    pthread_join(threads[t], NULL);
  free(threads);

  free_toc_chunks(&toc_chunks);
  free(toc_html);

  /* Publish llms-full.txt and llms.txt LAST only after every chapter succeeded
   * (preserves atomic Markdown publishing guarantee). */
  int result = (main_failed || ctx.failed) ? -1 : 0;
  if (result == 0)
    result = publish_staged_file(full_path, out, "llms-full.txt");
  if (result == 0)
    result = publish_staged_file(index_path, out, "llms.txt");

  if (full_path != NULL)
    unlink(full_path);
  if (index_path != NULL)
    unlink(index_path);
  rmdir(staging);
  free(full_path);
  free(index_path);
  free(staging);
  return result;
}

/* ---- Standalone mmdoc_multi_page & mmdoc_multi (used by unit tests) ---- */

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
  FILE *page_file = fopen(page_path, "w");
  if (page_file == NULL)
    return -1;

  if (write_page_head_and_nav(page_file, inputs, asset_names, anchor_location,
                              prev_anchor_location,
                              next_anchor_location) != 0 ||
      write_toc(page_file, toc_html, anchor_location->multipage_url) != 0 ||
      write_page_after_toc(page_file) != 0) {
    fclose(page_file);
    return -1;
  }

  int has_code_block = toc_has_code_block;
  if (anchor_location->file_path != NULL) {
    int rendered = mmdoc_render_part(
        anchor_location->file_path, page_file, RENDER_TYPE_MULTIPAGE,
        anchor_location, anchor_locations, anchor_location->multipage_url,
        search_index_file);
    if (rendered < 0) {
      fclose(page_file);
      return -1;
    }
    has_code_block |= rendered;
  }

  if (write_page_footer(page_file, asset_names, search_index_name,
                        search_index_name_offset, has_code_block) != 0) {
    fclose(page_file);
    return -1;
  }

  if (has_code_blocks != NULL)
    *has_code_blocks |= has_code_block;

  int failed = ferror(page_file);
  if (fclose(page_file) != 0)
    failed = 1;
  return failed ? -1 : 0;
}

int mmdoc_multi(Inputs inputs, AnchorLocationArray toc_anchor_locations,
                AnchorLocationArray anchor_locations) {
  char *out = inputs.out_multi;
  if (mkdir_p(out) != 0)
    return -1;
  AssetFileNames asset_names;
  asset_file_names(&asset_names);
  if (write_static_assets(out, &asset_names) != 0)
    return -1;

  char *search_index_path = mmdoc_file_path(out, "search_index.tmp");
  if (search_index_path == NULL)
    return -1;
  FILE *search_index_file = fopen(search_index_path, "w");
  if (search_index_file == NULL) {
    free(search_index_path);
    return -1;
  }
  fputs("window.mmdocSearchCorpus = [", search_index_file);
  long search_start_pos = ftell(search_index_file);

  char *toc_html = NULL;
  int toc_has_code_block = 0;
  if (render_toc(inputs, anchor_locations, &toc_html, &toc_has_code_block) !=
      0) {
    fclose(search_index_file);
    unlink(search_index_path);
    free(search_index_path);
    return -1;
  }

  AnchorLocation *prev_anchor_location = NULL;
  AnchorLocation *next_anchor_location = NULL;
  int has_code_blocks = 0;
  long *search_index_name_offsets = NULL;
  size_t *page_indices = NULL;
  size_t page_count = 0;
  if (toc_anchor_locations.used > 0) {
    page_indices = malloc(toc_anchor_locations.used * sizeof(*page_indices));
    if (page_indices == NULL) {
      free(toc_html);
      fclose(search_index_file);
      unlink(search_index_path);
      free(search_index_path);
      return -1;
    }
    page_count =
        mmdoc_collect_unique_page_indices(toc_anchor_locations, page_indices);
    search_index_name_offsets =
        malloc(page_count * sizeof(*search_index_name_offsets));
    if (search_index_name_offsets == NULL) {
      free(page_indices);
      free(toc_html);
      fclose(search_index_file);
      unlink(search_index_path);
      free(search_index_path);
      return -1;
    }
  }
  for (size_t i = 0; i < page_count; i++) {
    AnchorLocation *anchor_location =
        &toc_anchor_locations.array[page_indices[i]];
    if (anchor_location->multipage_output_directory_path != NULL &&
        mkdir_p(anchor_location->multipage_output_directory_path) != 0) {
      free(toc_html);
      free(page_indices);
      free(search_index_name_offsets);
      fclose(search_index_file);
      unlink(search_index_path);
      free(search_index_path);
      return -1;
    }
    if (i + 1 < page_count) {
      next_anchor_location = &toc_anchor_locations.array[page_indices[i + 1]];
    } else {
      next_anchor_location = NULL;
    }
    if (mmdoc_multi_page(inputs, anchor_locations, toc_html, toc_has_code_block,
                         &asset_names, search_index_placeholder,
                         search_index_file, &search_index_name_offsets[i],
                         anchor_location, prev_anchor_location,
                         next_anchor_location, &has_code_blocks) != 0) {
      free(toc_html);
      free(page_indices);
      free(search_index_name_offsets);
      fclose(search_index_file);
      unlink(search_index_path);
      free(search_index_path);
      return -1;
    }
    prev_anchor_location = anchor_location;
  }
  free(toc_html);
  long search_end_pos = ftell(search_index_file);
  int search_index_failed = search_end_pos < 0 || search_start_pos < 0 ||
                            (search_end_pos > search_start_pos &&
                             fseek(search_index_file, -1, SEEK_CUR) != 0);
  if (!search_index_failed && fputs("]", search_index_file) == EOF)
    search_index_failed = 1;
  if (fclose(search_index_file) != 0)
    search_index_failed = 1;
  if (search_index_failed) {
    unlink(search_index_path);
    free(search_index_path);
    free(page_indices);
    free(search_index_name_offsets);
    return -1;
  }

  char search_index_name[ASSET_FILE_NAME_SIZE];
  if (asset_hashed_file_name_from_path(search_index_path, "search_index", "js",
                                       search_index_name,
                                       sizeof(search_index_name)) != 0) {
    unlink(search_index_path);
    free(search_index_path);
    free(page_indices);
    free(search_index_name_offsets);
    return -1;
  }
  char *final_search_index_path = mmdoc_file_path(out, search_index_name);
  if (final_search_index_path == NULL ||
      rename(search_index_path, final_search_index_path) != 0) {
    free(final_search_index_path);
    unlink(search_index_path);
    free(search_index_path);
    free(page_indices);
    free(search_index_name_offsets);
    return -1;
  }
  free(final_search_index_path);
  free(search_index_path);

  for (size_t i = 0; i < page_count; i++) {
    if (replace_file_name(toc_anchor_locations.array[page_indices[i]]
                              .multipage_output_file_path,
                          search_index_name_offsets[i],
                          search_index_placeholder, search_index_name) != 0) {
      free(page_indices);
      free(search_index_name_offsets);
      return -1;
    }
  }
  free(search_index_name_offsets);

  int markdown_result = mmdoc_markdown(
      inputs, toc_anchor_locations, page_indices, page_count, anchor_locations);
  free(page_indices);
  if (markdown_result != 0)
    return -1;

  if (has_code_blocks && asset_write_to_dir_highlight_pack_js(
                             out, asset_names.highlight_pack_js) != 0)
    return -1;
  return 0;
}
