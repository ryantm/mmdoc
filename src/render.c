#include "parse.h"
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
  if (strcmp(admonition_type, "attention") == 0) cmark_node_set_literal(new_node, "<div class='attention'><h3 class='title'>Attention</h3>");
  if (strcmp(admonition_type, "caution") == 0) cmark_node_set_literal(new_node, "<div class='caution'><h3 class='title'>Caution</h3>");
  if (strcmp(admonition_type, "danger") == 0) cmark_node_set_literal(new_node, "<div class='danger'><h3 class='title'>Danger</h3>");
  if (strcmp(admonition_type, "error") == 0) cmark_node_set_literal(new_node, "<div class='error'><h3 class='title'>Error</h3>");
  if (strcmp(admonition_type, "hint") == 0) cmark_node_set_literal(new_node, "<div class='hint'><h3 class='title'>Hint</h3>");
  if (strcmp(admonition_type, "important") == 0) cmark_node_set_literal(new_node, "<div class='important'><h3 class='title'>Important</h3>");
  if (strcmp(admonition_type, "danger") == 0) cmark_node_set_literal(new_node, "<div class='danger'><h3 class='title'>Danger</h3>");
  if (strcmp(admonition_type, "note") == 0) cmark_node_set_literal(new_node, "<div class='note'><h3 class='title'>Note</h3>");
  if (strcmp(admonition_type, "tip") == 0) cmark_node_set_literal(new_node, "<div class='tip'><h3 class='title'>Tip</h3>");
  if (strcmp(admonition_type, "warning") == 0) cmark_node_set_literal(new_node, "<div class='warning'><h3 class='title'>Warning</h3>");
  cmark_node_insert_before(parent, new_node);

  free(admonition_type);
  cmark_node_unlink(node);
  cmark_node_free(node);
  return 1;
}

int replace_admonition_end(cmark_node *node) {
  const char *lit = cmark_node_get_literal(node);
  int pos = parse_admonition_end(lit);
  if (-1 == pos) return 0;

  cmark_node *parent = cmark_node_parent(node);
  cmark_node *new_node = cmark_node_new(CMARK_NODE_HTML_BLOCK);
  cmark_node_set_literal(new_node, "</div>");
  cmark_node_insert_after(parent, new_node);
  cmark_node_unlink(node);
  cmark_node_free(node);
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
  while (event = cmark_iter_next(iter)) {
    switch (event) {
    case CMARK_EVENT_ENTER:
      node = cmark_iter_get_node(iter);
      printf("ENTER: %s\n", cmark_node_get_type_string(node));
      break;
    case CMARK_EVENT_EXIT:
      node = cmark_iter_get_node(iter);
      printf("EXIT: %s\n", cmark_node_get_type_string(node));
      break;
    }
    if (event == CMARK_EVENT_DONE) {
      printf("DONE\n");
      break;
    }
  }
  cmark_iter_free(iter);
}

void mmdoc_render(char *file_path, FILE *output_file) {
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
