#include <cmark-gfm-core-extensions.h>
#include <cmark-gfm-extension_api.h>
#include <cmark-gfm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum parse_heading_bracketed_span_state {
  HEADING_NONE,
  HEADING_SPACE,
  HEADING_L,
  HEADING_ID
};

int heading_bracketed_span_id(const char *text, char *id) {
  if (text == NULL)
    return -1;
  int id_pos = 0;
  int bracket_found = 0;
  int bracket_start = 0;

  enum parse_heading_bracketed_span_state state = HEADING_NONE;

  for (int i = 0; text[i] != '\0'; i++) {
    if (state == HEADING_NONE && text[i] == ' ') {
      state = HEADING_SPACE;
      bracket_start = i;
      continue;
    }
    if (state == HEADING_SPACE && text[i] == '{') {
      state = HEADING_L;
      continue;
    }
    if (state == HEADING_L && text[i] == '#') {
      state = HEADING_ID;
      continue;
    }
    if (state == HEADING_ID && text[i] != '}') {
      id[id_pos] = text[i];
      id_pos++;
      continue;
    } else if (state == HEADING_ID && text[i] == '}') {
      id[id_pos] = '\0';
      bracket_found = 1;
      break;
    }
    id_pos = 0;
    state = HEADING_NONE;
    continue;
  }

  if (bracket_found == 1)
    return bracket_start;
  else
    return -1;
}

enum parse_link_bracketed_span_state {
  LINK_NONE,
  LINK_TEXT,
  LINK_R_SQUARE,
  LINK_L,
  LINK_ID
};

int link_bracketed_span_id(const char *text, char *span_text, char *id) {
  if (text == NULL)
    return -1;

  int text_pos = 0;
  int id_pos = 0;
  int bracket_found = 0;
  int bracket_start = 0;
  enum parse_link_bracketed_span_state state = LINK_NONE;

  for (int i = 0; text[i] != '\0'; i++) {
    if (state == LINK_NONE && text[i] == '[') {
      state = LINK_TEXT;
      bracket_start = i;
      continue;
    }
    if (state == LINK_TEXT && text[i] != ']') {
      span_text[text_pos] = text[i];
      text_pos++;
      continue;
    }
    if (state == LINK_TEXT && text[i] == ']') {
      span_text[text_pos] = '\0';
      state = LINK_R_SQUARE;
      continue;
    }
    if (state == LINK_R_SQUARE && text[i] == '{') {
      state = LINK_L;
      continue;
    }
    if (state == LINK_L && text[i] == '#') {
      state = LINK_ID;
      continue;
    }
    if (state == LINK_ID && text[i] != '}') {
      id[id_pos] = text[i];
      id_pos++;
      continue;
    } else if (state == LINK_ID && text[i] == '}') {
      id[id_pos] = '\0';
      bracket_found = 1;
      break;
    }
    text_pos = 0;
    id_pos = 0;
    state = LINK_NONE;
    continue;
  }

  if (bracket_found == 1)
    return bracket_start;
  else
    return -1;
}

int replace_bracket_with_span(cmark_node *node) {
  const char *lit = cmark_node_get_literal(node);
  char *id = malloc(strlen(lit) + 1);
  int pos = heading_bracketed_span_id(lit, id);
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
  int pos = link_bracketed_span_id(lit, span_text, id);
  if (-1 == pos) {
    free(id);
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
    }
    if (CMARK_EVENT_DONE == event)
      break;
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
  cmark_rewrite_anchors(document, mem);
  char *result = cmark_render_html_with_mem(
      document, options, cmark_parser_get_syntax_extensions(parser), mem);
  cmark_node_free(document);
  cmark_parser_free(parser);
  fputs(result, output_file);
  mem->free(result);
}
