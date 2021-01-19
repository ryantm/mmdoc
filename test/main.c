#include <cmark-gfm-core-extensions.h>
#include <cmark-gfm-extension_api.h>
#include <cmark-gfm.h>
#include <string.h>
#include <stdlib.h>

enum bracket_parse_state { NONE, SPACE, L, BRKT };

int heading_bracketed_span_id(const char * text, char * id) {
  if (text == NULL)
    return -1;
  int id_pos = 0;
  int i = 0;
  int bracket_found = 0;
  int bracket_start = 0;
  enum bracket_parse_state state = NONE;

  for (int i = 1; text[i] != '\0'; i++) {
    if (state == NONE && text[i] == ' ') {
      state = SPACE;
      bracket_start = i;
      continue;
    }
    if (state == SPACE && text[i] == '{') {
      state = L;
      continue;
    }
    if (state == L && text[i] == '#') {
      state = BRKT;
      continue;
    }
    if (state == BRKT && text[i] != '}') {
      id[id_pos] = text[i];
      id_pos++;
      continue;
    } else if (state == BRKT && text[i] == '}') {
      id[id_pos] = '\0';
      bracket_found = 1;
      break;
    }
    id_pos = 0;
    state = NONE;
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
          for(int i = 0; i < pos; i++) { new_lit[i] = lit[i]; }
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
