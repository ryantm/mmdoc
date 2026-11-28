/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include "md.h"
#include "types.h"
#include <stddef.h>
#include <stdio.h>

typedef enum : uint8_t {
  RENDER_TYPE_SINGLE,
  RENDER_TYPE_MULTIPAGE,
} render_type;

typedef struct {
  char *data;
  size_t len;
  size_t cap;
} MmdocBuf;

void mmdoc_buf_init(MmdocBuf *buf, size_t initial_cap);
void mmdoc_buf_free(MmdocBuf *buf);

typedef struct {
  MmdocBuf single_html;
  MmdocBuf multi_html;
  MmdocBuf full_md;
  MmdocBuf chapter_md;
  MmdocBuf search_json;
  MmdocBuf warnings;
  bool has_code_block;
} ChapterOutputs;

void chapter_outputs_free(ChapterOutputs *out);

/* Returns code-block presence (0/1), or -1 on input/output failure. */
[[nodiscard]] int mmdoc_render_to_buf(const char *file_path,
                                      render_type render_type,
                                      AnchorLocationArray anchor_locations,
                                      const char *multipage_url,
                                      MmdocBuf *out_html);

[[nodiscard]] int mmdoc_render_part(const char *file_path, FILE *output_file,
                                    render_type render_type,
                                    AnchorLocationArray anchor_locations,
                                    const char *multipage_url);

/* Render all outputs (Markdown, search JSON, single HTML, multi HTML) for one
 * chapter from a single already-parsed MdDoc. */
[[nodiscard]] int
mmdoc_render_chapter_from_mddoc(const MdDoc *doc, const AnchorLocation *chapter,
                                AnchorLocationArray *anchors,
                                AnchorLocationArray *chapters_by_source,
                                MmdocBuf *scratch_plain, ChapterOutputs *out);
