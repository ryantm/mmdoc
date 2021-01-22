#include <cmark-gfm-core-extensions.h>
#include <cmark-gfm-extension_api.h>
#include <cmark-gfm.h>
#include <string.h>
#include <stdlib.h>

enum parse_heading_bracketed_span_state {
  HEADING_NONE,
  HEADING_SPACE,
  HEADING_L,
  HEADING_ID
};

int heading_bracketed_span_id(const char * text, char * id) {
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
  LINK_ID };

int link_bracketed_span_id(const char * text, char * span_text, char * id) {
  if (text == NULL)
    return -1;

  printf("checking for: %s\n", text);

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


int main(int argc, char *argv[]) {
  char buffer[4096];
  size_t bytes;

  int options = CMARK_OPT_DEFAULT | CMARK_OPT_UNSAFE;

  cmark_gfm_core_extensions_ensure_registered();
  cmark_mem *mem = cmark_get_default_mem_allocator();

  cmark_syntax_extension *table_extension =
      cmark_find_syntax_extension("table");

  FILE *file = fopen("test/section3.md", "rb");

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

  cmark_iter *iter = cmark_iter_new(document);

  cmark_event_type event;
  cmark_node *node;
  cmark_node *new_node;
  cmark_node_type type;
  cmark_node_type prev_node_type;

  while (event = cmark_iter_next(iter)) {
      switch (event) {
      case CMARK_EVENT_ENTER:
        node = cmark_iter_get_node(iter);
        prev_node_type = type;
        type = cmark_node_get_type(node);
        if (prev_node_type == CMARK_NODE_HEADING && type == CMARK_NODE_TEXT) {

          const char * lit = cmark_node_get_literal(node);
          char id[2048];
          int pos = heading_bracketed_span_id(lit, id);
          if (-1 == pos)
            continue;
          char *new_lit = malloc(strlen(lit) + 1);
          int i;
          for(i = 0; i < pos; i++) { new_lit[i] = lit[i]; }
          new_lit[i] = '\0';
          char first_span[2500] = "<span id='";
          strcat(first_span, id);
          strcat(first_span, "'>");
          new_node = cmark_node_new(CMARK_NODE_HTML_INLINE);
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
        }

        if (prev_node_type == CMARK_NODE_PARAGRAPH && type == CMARK_NODE_TEXT) {

          const char * lit = cmark_node_get_literal(node);
          char id[2048];
          char span_text[2048];
          int pos = link_bracketed_span_id(lit, span_text, id);
          if (-1 == pos)
            continue;
          char *new_l_lit = malloc(strlen(lit) + 1);
          int i;
          for(i = 0; i < pos; i++) { new_l_lit[i] = lit[i]; }
          new_l_lit[i] = '\0';
          new_node = cmark_node_new(CMARK_NODE_TEXT);
          cmark_node_set_literal(new_node, new_l_lit);
          cmark_node_insert_before(node, new_node);

          char first_span[2500] = "<span id='";
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
          for(i = end_of_span; lit[i] != '\0'; i++) { new_r_lit[i-end_of_span] = lit[i]; }
          new_r_lit[i-end_of_span] = '\0';
          new_node = cmark_node_new(CMARK_NODE_TEXT);
          cmark_node_set_literal(new_node, new_r_lit);
          cmark_node_insert_before(node, new_node);

          cmark_node_unlink(node);
          cmark_node_free(node);
        }


        break;
      }
      if (CMARK_EVENT_DONE == event)
        break;
  }

  cmark_iter_reset(iter, document, CMARK_EVENT_ENTER);

  while (event = cmark_iter_next(iter)) {
      switch (event) {
      case CMARK_EVENT_NONE:
        printf("NONE\n");
        break;
      case CMARK_EVENT_DONE:
        printf("DONE\n");
        break;
      case CMARK_EVENT_ENTER:
        node = cmark_iter_get_node(iter);
        printf("ENTER %s \"%s\"\n", cmark_node_get_type_string(node), cmark_node_get_literal(node));
        break;
      case CMARK_EVENT_EXIT:
        printf("EXIT %s\n", cmark_node_get_type_string(cmark_iter_get_node(iter)));
        break;
      }
      if (CMARK_EVENT_DONE == event)
        break;
  }

  cmark_iter_free(iter);
  char *result = cmark_render_html_with_mem(
      document, options, cmark_parser_get_syntax_extensions(parser), mem);
  printf("HTML\n");
  printf("%s", result);

  result = cmark_render_man_with_mem(document, options, 120, mem);
  printf("\nMAN\n");
  printf("%s", result);

  cmark_node_free(document);
  cmark_parser_free(parser);

  mem->free(result);
  return 0;
}
