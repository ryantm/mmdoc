/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <cmark-gfm.h>
#include <stddef.h>

#define MMDOC_HINT_DEFLIST (1u << 0)
#define MMDOC_HINT_ADMON (1u << 1)
#define MMDOC_HINT_SPAN (1u << 2)
#define MMDOC_HINT_ALL (MMDOC_HINT_DEFLIST | MMDOC_HINT_ADMON | MMDOC_HINT_SPAN)

void mmdoc_cmark_init(void);
cmark_syntax_extension *mmdoc_table_extension(void);
cmark_node *mmdoc_render_cmark_document(char *file_path, cmark_parser *parser);
cmark_node *mmdoc_render_cmark_document_ex(char *file_path,
                                           cmark_parser *parser,
                                           unsigned int *syntax_hints_out);
cmark_node *mmdoc_parse_cmark_buffer(const char *data, size_t len,
                                     cmark_parser *parser);
void cmark_rewrite_spans(cmark_node *document, int markdown);
int cmark_rewrite_syntax(cmark_node *document);

int parse_heading_bracketed_span_id(const char *text, char *id);
int parse_link_bracketed_span_id(const char *text, char *span_text, char *id);
int parse_admonition_start(const char *text, char *admonition_type,
                           char *admonition_anchor);
int parse_admonition_end(const char *text);
int parse_dd(const char *text);
