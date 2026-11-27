/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include "md.h"
#include "types.h"
#include <stddef.h>
#include <stdio.h>

typedef enum {
  RENDER_TYPE_SINGLE,
  RENDER_TYPE_MULTIPAGE,
  RENDER_TYPE_MAN,
  RENDER_TYPE_EPUB
} render_type;

typedef struct {
  char *data;
  size_t len;
  size_t cap;
} MmdocBuf;

void mmdoc_buf_init(MmdocBuf *buf, size_t initial_cap);
void mmdoc_buf_free(MmdocBuf *buf);
int mmdoc_buf_append(MmdocBuf *buf, const char *data, size_t len);
int mmdoc_buf_append_str(MmdocBuf *buf, const char *str);
int mmdoc_buf_append_char(MmdocBuf *buf, char c);

typedef struct {
  MmdocBuf single_html;
  MmdocBuf multi_html;
  MmdocBuf full_md;
  MmdocBuf chapter_md;
  MmdocBuf search_json;
  MmdocBuf warnings;
  int has_code_block;
} ChapterOutputs;

void chapter_outputs_free(ChapterOutputs *out);

/* Returns code-block presence (0/1), or -1 on input/output failure. */
int mmdoc_render_part(char *file_path, FILE *output_file,
                      render_type render_type, AnchorLocation *anchor_location,
                      AnchorLocationArray anchor_locations, char *multipage_url,
                      FILE *search_index_path);

char *mmdoc_render_get_title_from_file(char *file_path);

int mmdoc_render_collect_anchors(char *file_path, Array *anchors);

/* Collect both from one parse. Either output may be NULL; the caller owns
 * the returned title (an empty string when no anchored heading exists). */
int mmdoc_render_collect_metadata(char *file_path, Array *anchors,
                                  char **title);

/* Render all outputs (Markdown, search JSON, single HTML, multi HTML) for one
 * chapter from a single already-parsed MdDoc. */
int mmdoc_render_chapter_from_mddoc(const MdDoc *doc,
                                    const AnchorLocation *chapter,
                                    AnchorLocationArray *anchors,
                                    AnchorLocationArray *chapters_by_source,
                                    MmdocBuf *scratch_plain,
                                    ChapterOutputs *out);

/* Render dual Markdown buffers (llms-full.txt & chapter/index.md) from MdDoc.
 */
int mmdoc_render_markdown_dual(const MdDoc *doc, const AnchorLocation *chapter,
                               AnchorLocationArray *anchors,
                               AnchorLocationArray *chapters_by_source,
                               MmdocBuf *full_out, MmdocBuf *chap_out);
