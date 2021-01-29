#include "asset/fuse.basic.min.js.h"
#include "asset/highlight.pack.js.h"
#include "asset/minimal.css.h"
#include "asset/mono-blue.css.h"
#include "asset/search.js.h"
#include "parse.h"
#include "types.h"
#include <cmark-gfm-core-extensions.h>
#include <cmark-gfm-extension_api.h>
#include <cmark-gfm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int replace_bracket_with_span(cmark_node *node) {
  const char *lit = cmark_node_get_literal(node);
  char *id = malloc(strlen(lit) + 1);
  int pos = parse_heading_bracketed_span_id(lit, id);
  if (-1 == pos) {
    free(id);
    return 0;
  }
  char *new_lit = malloc(strlen(lit) + 1);
  int i;
  for (i = 0; i < pos; i++) {
    new_lit[i] = lit[i];
  }
  new_lit[i] = '\0';
  char *first_span = malloc(12 + strlen(lit) + 1);
  strcpy(first_span, "<span id='");
  strcat(first_span, id);
  strcat(first_span, "'>");
  cmark_node *new_node = cmark_node_new(CMARK_NODE_HTML_INLINE);
  cmark_node_set_literal(new_node, first_span);
  cmark_node_insert_before(node, new_node);
  new_node = cmark_node_new(CMARK_NODE_HTML_INLINE);
  cmark_node_set_literal(new_node, "</span>");
  cmark_node_insert_after(node, new_node);
  new_node = cmark_node_new(CMARK_NODE_TEXT);
  cmark_node_set_literal(new_node, new_lit);
  cmark_node_insert_after(node, new_node);
  cmark_node_replace(node, new_node);
  cmark_node_free(node);
  free(id);
  free(new_lit);
  free(first_span);
  return 1;
}

int replace_link_bracket_with_span(cmark_node *node) {
  const char *lit = cmark_node_get_literal(node);
  char *id = malloc(strlen(lit) + 1);
  char *span_text = malloc(strlen(lit) + 1);
  int pos = parse_link_bracketed_span_id(lit, span_text, id);
  if (-1 == pos) {
    free(id);
    free(span_text);
    return 0;
  }
  char *new_l_lit = malloc(strlen(lit) + 1);
  int i;
  for (i = 0; i < pos; i++) {
    new_l_lit[i] = lit[i];
  }
  new_l_lit[i] = '\0';
  cmark_node *new_node = cmark_node_new(CMARK_NODE_TEXT);
  cmark_node_set_literal(new_node, new_l_lit);
  cmark_node_insert_before(node, new_node);

  char *first_span = malloc(12 + strlen(lit) + 1);
  strcpy(first_span, "<span id='");
  strcat(first_span, id);
  strcat(first_span, "'>");
  new_node = cmark_node_new(CMARK_NODE_HTML_INLINE);
  cmark_node_set_literal(new_node, first_span);
  cmark_node_insert_before(node, new_node);

  new_node = cmark_node_new(CMARK_NODE_TEXT);
  cmark_node_set_literal(new_node, span_text);
  cmark_node_insert_before(node, new_node);

  new_node = cmark_node_new(CMARK_NODE_HTML_INLINE);
  cmark_node_set_literal(new_node, "</span>");
  cmark_node_insert_before(node, new_node);

  char *new_r_lit = malloc(strlen(lit) + 1);
  int end_of_span = pos + 1 + strlen(span_text) + 3 + strlen(id) + 2;
  for (i = end_of_span; lit[i] != '\0'; i++) {
    new_r_lit[i - end_of_span] = lit[i];
  }
  new_r_lit[i - end_of_span] = '\0';
  new_node = cmark_node_new(CMARK_NODE_TEXT);
  cmark_node_set_literal(new_node, new_r_lit);
  cmark_node_insert_before(node, new_node);

  cmark_node_unlink(node);
  cmark_node_free(node);
  free(id);
  free(span_text);
  free(first_span);
  free(new_r_lit);
  free(new_l_lit);
  return 1;
}

int replace_admonition_start(cmark_node *node) {
  const char *lit = cmark_node_get_literal(node);
  char *admonition_type = malloc(strlen(lit) + 1);
  int pos = parse_admonition_start(lit, admonition_type);
  if (-1 == pos) {
    free(admonition_type);
    return 0;
  }
  if (strcmp(admonition_type, "attention") != 0 &&
      strcmp(admonition_type, "caution") != 0 &&
      strcmp(admonition_type, "danger") != 0 &&
      strcmp(admonition_type, "error") != 0 &&
      strcmp(admonition_type, "hint") != 0 &&
      strcmp(admonition_type, "important") != 0 &&
      strcmp(admonition_type, "danger") != 0 &&
      strcmp(admonition_type, "note") != 0 &&
      strcmp(admonition_type, "tip") != 0 &&
      strcmp(admonition_type, "warning") != 0) {
    printf("admonition type '%s' doesn't match\n", admonition_type);
    free(admonition_type);
    return 0;
  }

  cmark_node *parent = cmark_node_parent(node);
  cmark_node *new_node = cmark_node_new(CMARK_NODE_HTML_BLOCK);
  if (strcmp(admonition_type, "attention") == 0)
    cmark_node_set_literal(
        new_node, "<div class='attention'><h3 class='title'>Attention</h3>");
  if (strcmp(admonition_type, "caution") == 0)
    cmark_node_set_literal(
        new_node, "<div class='caution'><h3 class='title'>Caution</h3>");
  if (strcmp(admonition_type, "danger") == 0)
    cmark_node_set_literal(new_node,
                           "<div class='danger'><h3 class='title'>Danger</h3>");
  if (strcmp(admonition_type, "error") == 0)
    cmark_node_set_literal(new_node,
                           "<div class='error'><h3 class='title'>Error</h3>");
  if (strcmp(admonition_type, "hint") == 0)
    cmark_node_set_literal(new_node,
                           "<div class='hint'><h3 class='title'>Hint</h3>");
  if (strcmp(admonition_type, "important") == 0)
    cmark_node_set_literal(
        new_node, "<div class='important'><h3 class='title'>Important</h3>");
  if (strcmp(admonition_type, "danger") == 0)
    cmark_node_set_literal(new_node,
                           "<div class='danger'><h3 class='title'>Danger</h3>");
  if (strcmp(admonition_type, "note") == 0)
    cmark_node_set_literal(new_node,
                           "<div class='note'><h3 class='title'>Note</h3>");
  if (strcmp(admonition_type, "tip") == 0)
    cmark_node_set_literal(new_node,
                           "<div class='tip'><h3 class='title'>Tip</h3>");
  if (strcmp(admonition_type, "warning") == 0)
    cmark_node_set_literal(
        new_node, "<div class='warning'><h3 class='title'>Warning</h3>");
  cmark_node_insert_before(parent, new_node);

  free(admonition_type);
  cmark_node_unlink(node);
  cmark_node_free(node);
  return 1;
}

int replace_admonition_end(cmark_node *node) {
  const char *lit = cmark_node_get_literal(node);
  int pos = parse_admonition_end(lit);
  if (-1 == pos)
    return 0;

  cmark_node *parent = cmark_node_parent(node);
  cmark_node *new_node = cmark_node_new(CMARK_NODE_HTML_BLOCK);
  cmark_node_set_literal(new_node, "</div>");
  cmark_node_insert_after(parent, new_node);
  cmark_node_unlink(node);
  cmark_node_free(node);
  return 1;
}

int replace_dd(cmark_node *node) {
  const char *lit = cmark_node_get_literal(node);
  int pos = parse_dd(lit);
  if (-1 == pos)
    return 0;

  cmark_node *new_node = NULL;

  cmark_node *parent = cmark_node_parent(node);
  cmark_node *previous = cmark_node_previous(parent);
  cmark_node *previous_previous = cmark_node_previous(previous);
  if (previous_previous == NULL) {
    new_node = cmark_node_new(CMARK_NODE_HTML_BLOCK);
    cmark_node_set_literal(new_node, "<dl>");
    cmark_node_insert_before(previous, new_node);
  } else {
    const char *prev_prev_lit = cmark_node_get_literal(previous_previous);
    cmark_node_type prev_prev_type = cmark_node_get_type(previous_previous);

    if (prev_prev_type != CMARK_NODE_HTML_BLOCK &&
        (prev_prev_lit == NULL || strcmp(prev_prev_lit, "</dd>") != 0)) {
      new_node = cmark_node_new(CMARK_NODE_HTML_BLOCK);
      cmark_node_set_literal(new_node, "<dl>");
      cmark_node_insert_before(previous, new_node);
    }
  }

  int make_dl_end = 0;
  cmark_node *next = parent;
  for (int i = 0; i < 2; i++) {
    next = cmark_node_next(next);
    if (next == NULL) {
      make_dl_end = 1;
      break;
    }
  }
  if (make_dl_end == 0 && next != NULL) {
    cmark_node *child = cmark_node_first_child(next);
    const char *next_dd_lit = cmark_node_get_literal(child);
    int pos = parse_dd(next_dd_lit);
    if (-1 == pos)
      make_dl_end = 1;
  }

  if (make_dl_end == 1) {
    new_node = cmark_node_new(CMARK_NODE_HTML_BLOCK);
    cmark_node_set_literal(new_node, "</dl>");
    cmark_node_insert_after(parent, new_node);
  }

  new_node = cmark_node_new(CMARK_NODE_HTML_BLOCK);
  cmark_node_set_literal(new_node, "<dt>");
  cmark_node_insert_before(previous, new_node);

  new_node = cmark_node_new(CMARK_NODE_HTML_BLOCK);
  cmark_node_set_literal(new_node, "</dt>");
  cmark_node_insert_after(previous, new_node);

  new_node = cmark_node_new(CMARK_NODE_HTML_BLOCK);
  cmark_node_set_literal(new_node, "<dd>");
  cmark_node_insert_before(parent, new_node);

  new_node = cmark_node_new(CMARK_NODE_HTML_BLOCK);
  cmark_node_set_literal(new_node, "</dd>");
  cmark_node_insert_after(parent, new_node);

  cmark_node_set_literal(node, lit + pos);

  return 1;
}

void cmark_rewrite_anchors(cmark_node *document, cmark_mem *mem) {
  cmark_iter *iter = cmark_iter_new(document);

  cmark_event_type event;
  cmark_node *node;
  cmark_node_type type;

  while (event = cmark_iter_next(iter)) {
    switch (event) {
    case CMARK_EVENT_ENTER:
      node = cmark_iter_get_node(iter);
      type = cmark_node_get_type(node);
      if (type != CMARK_NODE_TEXT)
        continue;
      if (replace_link_bracket_with_span(node))
        continue;
      if (replace_bracket_with_span(node))
        continue;
      if (replace_admonition_start(node))
        continue;
      if (replace_admonition_end(node))
        continue;
      if (replace_dd(node))
        continue;
    }
    if (CMARK_EVENT_DONE == event)
      break;
  }
  cmark_iter_free(iter);
}

void render_debug_cmark_node(cmark_node *document) {
  cmark_iter *iter = cmark_iter_new(document);

  cmark_event_type event;
  cmark_node *node;
  int indent = 0;
  while (event = cmark_iter_next(iter)) {
    switch (event) {
    case CMARK_EVENT_ENTER:
      node = cmark_iter_get_node(iter);
      for (int i = 0; i < indent; i++) {
        printf("  ");
      }
      printf("ENTER: %s LITERAL: %s\n", cmark_node_get_type_string(node),
             cmark_node_get_literal(node));

      switch (cmark_node_get_type(node)) {
      case CMARK_NODE_HTML_BLOCK:
      case CMARK_NODE_THEMATIC_BREAK:
      case CMARK_NODE_CODE_BLOCK:
      case CMARK_NODE_TEXT:
      case CMARK_NODE_SOFTBREAK:
      case CMARK_NODE_LINEBREAK:
      case CMARK_NODE_CODE:
      case CMARK_NODE_HTML_INLINE:
        break;
      default:
        indent++;
      }
      break;
    case CMARK_EVENT_EXIT:
      indent--;
      node = cmark_iter_get_node(iter);
      for (int i = 0; i < indent; i++) {
        printf("  ");
      }
      printf("EXIT: %s\n", cmark_node_get_type_string(node));
      break;
    }
    if (event == CMARK_EVENT_DONE) {
      printf("DONE\n\n");
      break;
    }
  }
  cmark_iter_free(iter);
}

void mmdoc_render_part(char *file_path, FILE *output_file) {
  char buffer[4096];
  size_t bytes;

  int options = CMARK_OPT_DEFAULT | CMARK_OPT_UNSAFE;

  cmark_gfm_core_extensions_ensure_registered();
  cmark_mem *mem = cmark_get_default_mem_allocator();

  cmark_syntax_extension *table_extension =
      cmark_find_syntax_extension("table");

  FILE *file = fopen(file_path, "rb");

  cmark_parser *parser = cmark_parser_new_with_mem(options, mem);
  cmark_parser_attach_syntax_extension(parser, table_extension);
  while ((bytes = fread(buffer, 1, sizeof(buffer), file)) > 0) {
    cmark_parser_feed(parser, buffer, bytes);
    if (bytes < sizeof(buffer)) {
      break;
    }
  }
  fclose(file);
  cmark_node *document = cmark_parser_finish(parser);

  /* printf("BEFORE\n"); */
  /* render_debug_cmark_node(document); */

  cmark_rewrite_anchors(document, mem);

  /* printf("AFTER\n"); */
  /* render_debug_cmark_node(document); */

  char *result = cmark_render_html_with_mem(
      document, options, cmark_parser_get_syntax_extensions(parser), mem);
  cmark_node_free(document);
  cmark_parser_free(parser);
  fputs(result, output_file);
  mem->free(result);
}

int mmdoc_render_single(char *out, char *toc_path, Array toc_refs,
                        AnchorLocationArray anchor_locations) {
  char asset_path[2048];
  FILE *asset_file;
  strcpy(asset_path, out);
  strcat(asset_path, "/highlight.pack.js");
  asset_file = fopen(asset_path, "w");
  for (int i = 0; i < src_asset_highlight_pack_js_len; i++) {
    fputc(src_asset_highlight_pack_js[i], asset_file);
  }
  fclose(asset_file);

  strcpy(asset_path, out);
  strcat(asset_path, "/minimal.css");
  asset_file = fopen(asset_path, "w");
  for (int i = 0; i < src_asset_minimal_css_len; i++) {
    fputc(src_asset_minimal_css[i], asset_file);
  }
  fclose(asset_file);

  strcpy(asset_path, out);
  strcat(asset_path, "/mono-blue.css");
  asset_file = fopen(asset_path, "w");
  for (int i = 0; i < src_asset_mono_blue_css_len; i++) {
    fputc(src_asset_mono_blue_css[i], asset_file);
  }
  fclose(asset_file);

  char index_path[2048];
  FILE *index_file;
  strcpy(index_path, out);
  strcat(index_path, "/index.html");
  index_file = fopen(index_path, "w");

  char *html_head =
      "<!doctype html>\n"
      "<html>\n"
      "  <head>\n"
      "    <base href='/'>\n"
      "    <meta charset='utf-8'>\n"
      "    <link href='minimal.css' rel='stylesheet' type='text/css'>\n"
      "    <link rel='stylesheet' href='mono-blue.css'>\n"
      "    <script src='highlight.pack.js'></script>\n"
      "    <script>hljs.initHighlightingOnLoad();</script>\n"
      "  </head>\n"
      "  <body>\n"
      "    <nav>\n";
  fputs(html_head, index_file);
  mmdoc_render_part(toc_path, index_file);
  fputs("    </nav>\n", index_file);
  fputs("    <section>\n", index_file);

  for (int i = 0; i < toc_refs.used; i++) {
    char *file_path;
    int found = 0;
    for (int j = 0; j < anchor_locations.used; j++) {
      if (0 == strcmp(toc_refs.array[i], anchor_locations.array[j].anchor)) {
        file_path = anchor_locations.array[j].file_path;
        found = 1;
        break;
      }
    }
    if (!found) {
      printf("Found anchor reference in toc.md \"%s\" but did not find anchor "
             "in any .md file.",
             toc_refs.array[i]);
      return 1;
    }
    mmdoc_render_part(file_path, index_file);
  }
  char *html_foot = "    </section>\n"
                    "  </body>\n"
                    "</html>\n";
  fputs(html_foot, index_file);
  fclose(index_file);
  return 0;
}

int mmdoc_render_multi_page(char *page_path, char *toc_path, char *input_path)
{
  FILE *page_file;
  page_file = fopen(page_path, "w");
  char *html_head =
      "<!doctype html>\n"
      "<html>\n"
      "  <head>\n"
      "    <base href='/'>\n"
      "    <meta charset='utf-8'>\n"
      "    <link href='minimal.css' rel='stylesheet' type='text/css'>\n"
      "    <link rel='stylesheet' href='mono-blue.css'>\n"
      "    <script src='highlight.pack.js'></script>\n"
      "    <script>hljs.initHighlightingOnLoad();</script>\n"
      "    <script src='fuse.basic.min.js'></script>\n"
      "    <script src='search_index.js'></script>\n"
      "    <script src='search.js'></script>\n"
      "  </head>\n"
      "  <body>\n"
      "    <nav>\n"
      "      <input type='search' id='search' placeholder='Search'>\n"
      "      <div id='search-results'></div>\n";
  fputs(html_head, page_file);
  mmdoc_render_part(toc_path, page_file);
  fputs("    </nav>\n", page_file);
  fputs("    <section>\n", page_file);
  mmdoc_render_part(input_path, page_file);
  char *html_foot = "    </section>\n"
                    "  </body>\n"
                    "</html>\n";
  fputs(html_foot, page_file);
  fclose(page_file);
}

int mmdoc_render_multi(char *out, char *src, char *toc_path, Array toc_refs,
                       AnchorLocationArray anchor_locations) {
  char asset_path[2048];
  FILE *asset_file;
  strcpy(asset_path, out);
  strcat(asset_path, "/search.js");
  asset_file = fopen(asset_path, "w");
  for (int i = 0; i < src_asset_search_js_len; i++) {
    fputc(src_asset_search_js[i], asset_file);
  }
  fclose(asset_file);

  strcpy(asset_path, out);
  strcat(asset_path, "/fuse.basic.min.js");
  asset_file = fopen(asset_path, "w");
  for (int i = 0; i < src_asset_fuse_basic_min_js_len; i++) {
    fputc(src_asset_fuse_basic_min_js[i], asset_file);
  }
  fclose(asset_file);

  strcpy(asset_path, out);
  strcat(asset_path, "/highlight.pack.js");
  asset_file = fopen(asset_path, "w");
  for (int i = 0; i < src_asset_highlight_pack_js_len; i++) {
    fputc(src_asset_highlight_pack_js[i], asset_file);
  }
  fclose(asset_file);

  strcpy(asset_path, out);
  strcat(asset_path, "/minimal.css");
  asset_file = fopen(asset_path, "w");
  for (int i = 0; i < src_asset_minimal_css_len; i++) {
    fputc(src_asset_minimal_css[i], asset_file);
  }
  fclose(asset_file);

  strcpy(asset_path, out);
  strcat(asset_path, "/mono-blue.css");
  asset_file = fopen(asset_path, "w");
  for (int i = 0; i < src_asset_mono_blue_css_len; i++) {
    fputc(src_asset_mono_blue_css[i], asset_file);
  }
  fclose(asset_file);

  char index_path[2048];
  strcpy(index_path, out);
  strcat(index_path, "/index.html");

  for (int i = 0; i < toc_refs.used; i++) {
    char *file_path;
    int found = 0;
    for (int j = 0; j < anchor_locations.used; j++) {
      if (0 == strcmp(toc_refs.array[i], anchor_locations.array[j].anchor)) {
        file_path = anchor_locations.array[j].file_path;
        found = 1;
        break;
      }
    }
    if (!found) {
      printf("Found anchor reference in toc.md \"%s\" but did not find anchor "
             "in any .md file.",
             toc_refs.array[i]);
      return 1;
    }
    char *page_path = malloc(strlen(out) + strlen(file_path));
    char *src_file_path_end = file_path + strlen(src);
    strcpy(page_path, out);
    strcat(page_path, src_file_path_end);
    char *lastExt = strrchr(page_path, '.');
    while(lastExt != NULL){
      *lastExt = '\0';
      lastExt = strrchr(page_path, '.');
    }
    strcat(page_path, ".html");

    /* printf("%s\n", src); */
    /* printf("%s\n", out); */
    /* printf("%s\n", file_path); */
    /* printf("page_path: %s\n", page_path); */
    mmdoc_render_multi_page(index_path, toc_path, file_path);
    free(page_path);
  }

  return 0;
}
