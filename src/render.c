/* SPDX-License-Identifier: CC0-1.0 */
#include "render.h"
#include "mkdir_p.h"
#include "parse.h"
#include "types.h"
#include <cmark-gfm-core-extensions.h>
#include <cmark-gfm-extension_api.h>
#include <cmark-gfm.h>
#include <errno.h>
#include <libfastjson/json_object.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

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
        new_node,
        "<div class='admonition attention'><h3 class='title'>Attention</h3>");
  if (strcmp(admonition_type, "caution") == 0)
    cmark_node_set_literal(
        new_node,
        "<div class='admonition caution'><h3 class='title'>Caution</h3>");
  if (strcmp(admonition_type, "danger") == 0)
    cmark_node_set_literal(
        new_node,
        "<div class='admonition danger'><h3 class='title'>Danger</h3>");
  if (strcmp(admonition_type, "error") == 0)
    cmark_node_set_literal(
        new_node, "<div class='admonition error'><h3 class='title'>Error</h3>");
  if (strcmp(admonition_type, "hint") == 0)
    cmark_node_set_literal(
        new_node, "<div class='admonition hint'><h3 class='title'>Hint</h3>");
  if (strcmp(admonition_type, "important") == 0)
    cmark_node_set_literal(
        new_node,
        "<div class='admonition important'><h3 class='title'>Important</h3>");
  if (strcmp(admonition_type, "danger") == 0)
    cmark_node_set_literal(
        new_node,
        "<div class='admonition danger'><h3 class='title'>Danger</h3>");
  if (strcmp(admonition_type, "note") == 0)
    cmark_node_set_literal(
        new_node, "<div class='admonition note'><h3 class='title'>Note</h3>");
  if (strcmp(admonition_type, "tip") == 0)
    cmark_node_set_literal(
        new_node, "<div class='admonition tip'><h3 class='title'>Tip</h3>");
  if (strcmp(admonition_type, "warning") == 0)
    cmark_node_set_literal(
        new_node,
        "<div class='admonition warning'><h3 class='title'>Warning</h3>");
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

  cmark_node *parent = cmark_node_parent(node);
  parent = cmark_node_previous(parent);
  if (parent == NULL)
    return 0;

  cmark_node *new_node = NULL;
  cmark_node *previous = NULL;

  new_node = cmark_node_new(CMARK_NODE_HTML_BLOCK);
  cmark_node_set_literal(new_node, "<dl>");
  cmark_node_insert_before(parent, new_node);

  for (;;) {
    if (parent == NULL)
      break;

    cmark_node *child = cmark_node_first_child(parent);
    lit = cmark_node_get_literal(child);
    pos = parse_dd(lit);
    if (pos != -1) {
      new_node = cmark_node_new(CMARK_NODE_HTML_BLOCK);
      cmark_node_set_literal(new_node, "<dd>");
      cmark_node_insert_before(parent, new_node);

      new_node = cmark_node_new(CMARK_NODE_HTML_BLOCK);
      cmark_node_set_literal(new_node, "</dd>");
      cmark_node_insert_after(parent, new_node);

      cmark_node_set_literal(child, lit + pos);

      previous = parent;
      parent = cmark_node_next(parent);
      previous = parent;
      parent = cmark_node_next(parent);
      continue;
    }
    previous = parent;
    parent = cmark_node_next(parent);
    if (parent == NULL)
      break;

    child = cmark_node_first_child(parent);
    lit = cmark_node_get_literal(child);
    pos = parse_dd(lit);
    if (pos != -1) {
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

      cmark_node_set_literal(child, lit + pos);

      previous = parent;
      parent = cmark_node_next(parent);
      previous = parent;
      parent = cmark_node_next(parent);
      continue;
    }
    break;
  }

  new_node = cmark_node_new(CMARK_NODE_HTML_BLOCK);
  cmark_node_set_literal(new_node, "</dl>");
  cmark_node_insert_before(previous, new_node);

  return 1;
}

void replace_link(cmark_node *node, char *input_file_path,
                  AnchorLocationArray anchor_locations) {
  const char *url = cmark_node_get_url(node);
  if (strlen(url) < 2)
    return;
  if (url[0] != '#')
    return;
  const char *anchor = url;

  AnchorLocation anchor_location;
  int found = 0;
  for (int i = 0; i < anchor_locations.used; i++) {
    if (strcmp(anchor, anchor_locations.array[i].anchor) == 0) {
      anchor_location = anchor_locations.array[i];
      found = 1;
      break;
    }
  }
  if (!found) {
    printf("Anchor \"%s\" referenced in %s not found.\n", anchor,
           input_file_path);
    return;
  }
  char *new_url =
      malloc(strlen(anchor_location.multipage_url) + strlen(url) + 1);
  strcpy(new_url, anchor_location.multipage_url);
  strcat(new_url, url);
  cmark_node_set_url(node, new_url);
  free(new_url);
}

void insert_search_index(FILE *search_index_path, const char *text,
                         const char *multipage_url) {
  fjson_object *obj = fjson_object_new_object();
  fjson_object *obj_url = fjson_object_new_string(multipage_url);
  fjson_object *obj_text = fjson_object_new_string(text);
  fjson_object_object_add(obj, "url", obj_url);
  fjson_object_object_add(obj, "text", obj_text);
  fputs(fjson_object_to_json_string(obj), search_index_path);
  fputs("\n,", search_index_path);
}

void cmark_rewrite(cmark_node *document, cmark_mem *mem, char *input_file_path,
                   render_type render_type,
                   AnchorLocationArray anchor_locations) {
  cmark_iter *iter = cmark_iter_new(document);

  cmark_event_type event;
  cmark_node *node;
  cmark_node_type type;

  while ((event = cmark_iter_next(iter))) {
    switch (event) {
    case CMARK_EVENT_NONE:
      break;
    case CMARK_EVENT_DONE:
      break;
    case CMARK_EVENT_EXIT:
      break;
    case CMARK_EVENT_ENTER:
      node = cmark_iter_get_node(iter);
      type = cmark_node_get_type(node);
      if (type == CMARK_NODE_LINK && render_type == RENDER_TYPE_MULTIPAGE) {
        replace_link(node, input_file_path, anchor_locations);
        continue;
      }
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
  while ((event = cmark_iter_next(iter))) {
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
    case CMARK_EVENT_DONE:
      break;
    case CMARK_EVENT_NONE:
      break;
    }
    if (event == CMARK_EVENT_DONE) {
      printf("DONE\n\n");
      break;
    }
  }
  cmark_iter_free(iter);
}

int mmdoc_render_cmark_options = CMARK_OPT_DEFAULT | CMARK_OPT_UNSAFE;
cmark_node *mmdoc_render_cmark_document(char *file_path, cmark_parser *parser) {
  char buffer[4096];
  size_t bytes;

  cmark_gfm_core_extensions_ensure_registered();

  cmark_syntax_extension *table_extension =
      cmark_find_syntax_extension("table");

  FILE *file = fopen(file_path, "rb");

  cmark_parser_attach_syntax_extension(parser, table_extension);
  while ((bytes = fread(buffer, 1, sizeof(buffer), file)) > 0) {
    cmark_parser_feed(parser, buffer, bytes);
    if (bytes < sizeof(buffer)) {
      break;
    }
  }
  fclose(file);

  return cmark_parser_finish(parser);
}

void mmdoc_render_part(char *file_path, FILE *output_file,
                       render_type render_type,
                       AnchorLocationArray anchor_locations,
                       char *multipage_url, FILE *search_index_path) {

  cmark_mem *mem = cmark_get_default_mem_allocator();
  cmark_parser *parser =
      cmark_parser_new_with_mem(mmdoc_render_cmark_options, mem);
  cmark_node *document = mmdoc_render_cmark_document(file_path, parser);

  /* printf("BEFORE\n"); */
  /* render_debug_cmark_node(document); */

  cmark_rewrite(document, mem, file_path, render_type, anchor_locations);

  /* printf("AFTER\n"); */
  /* render_debug_cmark_node(document); */

  if (render_type != RENDER_TYPE_MAN) {
    char *result = cmark_render_html_with_mem(
        document, mmdoc_render_cmark_options,
        cmark_parser_get_syntax_extensions(parser), mem);

    if (search_index_path != NULL) {
      char *plaintext_result = cmark_render_plaintext_with_mem(
          document, mmdoc_render_cmark_options, 120, mem);
      insert_search_index(search_index_path, plaintext_result, multipage_url);
      mem->free(plaintext_result);
    }
    fputs(result, output_file);
    mem->free(result);
  } else {
    char *result = cmark_render_man_with_mem(
        document, mmdoc_render_cmark_options, 240, mem);
    fputs(result, output_file);
    mem->free(result);
  }
  cmark_node_free(document);
  cmark_parser_free(parser);
}

char *mmdoc_render_get_title_from_file(char *file_path) {
  cmark_mem *mem = cmark_get_default_mem_allocator();
  cmark_parser *parser = cmark_parser_new_with_mem(mmdoc_render_cmark_options, mem);
  cmark_node *document = mmdoc_render_cmark_document(file_path, parser);
  cmark_iter *iter = cmark_iter_new(document);
  cmark_event_type event;
  cmark_node *node;
  cmark_node_type type;
  while ((event = cmark_iter_next(iter))) {
    switch (event) {
    case CMARK_EVENT_NONE:
      break;
    case CMARK_EVENT_DONE:
      break;
    case CMARK_EVENT_EXIT:
      break;
    case CMARK_EVENT_ENTER:
      node = cmark_iter_get_node(iter);
      type = cmark_node_get_type(node);
      if (type != CMARK_NODE_TEXT)
        continue;
      const char *lit = cmark_node_get_literal(node);
      char *id = malloc(strlen(lit) + 1);
      int pos = parse_heading_bracketed_span_id(lit, id);
      if (-1 == pos) {
        free(id);
        continue;
      }
      char *result = malloc(pos + 1);
      for (int i = 0; i < pos; i++) {
        result[i] = lit[i];
      }
      free(id);
      cmark_iter_free(iter);
      cmark_node_free(document);
      cmark_parser_free(parser);
      return result;
    }
    if (event == CMARK_EVENT_DONE) {
      break;
    }
  }

  cmark_iter_free(iter);
  cmark_node_free(document);
  cmark_parser_free(parser);
  return "";
}
