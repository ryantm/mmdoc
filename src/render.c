/* SPDX-License-Identifier: CC0-1.0 */
#include "render.h"
#include "mkdir_p.h"
#include "parse.h"
#include "types.h"
#include <cmark-gfm-core-extensions.h>
#include <cmark-gfm-extension_api.h>
#include <cmark-gfm.h>
#include <ctype.h>
#include <errno.h>
#include <libfastjson/json_object.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/**
 * Replaces 'oldnode' with 'newnode', reconnects children of 'oldnode' to
 * 'newnode' and unlinks 'oldnode' (but does not free its memory). Can fail if
 * 'newnode' does not like to contain child’s node type. Returns 1 on success, 0
 * on failure.
 */
int cmark_node_replace_with_children(cmark_node *oldnode, cmark_node *newnode) {
  cmark_node_replace(oldnode, newnode);

  cmark_node *child;
  cmark_node *next;

  while ((child = cmark_node_first_child(oldnode)) != NULL) {
    next = cmark_node_next(child);
    if (!cmark_node_append_child(newnode, child)) {
      return 0;
    }
    child = next;
  }

  return 1;
}

/**
 * Extracts id attribute from header and attaches it to the node as custom
 * metadata.
 */
int replace_header_attributes(cmark_node *node, char *input_file_path,
                              AnchorLocationArray anchor_locations) {
  if (cmark_node_get_type(node) != CMARK_NODE_HEADER)
    return 0;
  cmark_node *last_child = cmark_node_last_child(node);

  if (cmark_node_get_type(last_child) != CMARK_NODE_TEXT)
    return 0;

  const char *lit = cmark_node_get_literal(last_child);
  char *id = malloc(strlen(lit) + 1);
  int pos = parse_heading_bracketed_span_id(lit, id);
  if (-1 == pos) {
    free(id);
    return 0;
  }

  HeadingInfo *info = malloc(sizeof *info);
  info->anchor = malloc(strlen(id) + 1);
  strcpy(info->anchor, id);
  cmark_node_set_user_data(node, info);

  // TODO: populate anchor_locations here

  if (pos == 0) {
    cmark_node_unlink(last_child);
  } else {
    char *new_lit = malloc(strlen(lit) + 1);
    int i;
    for (i = 0; i < pos; i++) {
      new_lit[i] = lit[i];
    }
    new_lit[i] = '\0';
    cmark_node *new_node = cmark_node_new(CMARK_NODE_TEXT);
    cmark_node_set_literal(new_node, new_lit);
    cmark_node_replace(last_child, new_node);
  }

  cmark_node_free(last_child);
  free(id);
  return 1;
}

/**
 * Converts headers with custom metadata into HTML heading node.
 */
int replace_headers_with_attributes_for_html(char *multipage_url,
                                             cmark_node *node) {
  if (cmark_node_get_type(node) != CMARK_NODE_HEADER)
    return 0;

  HeadingInfo *info = cmark_node_get_user_data(node);

  if (!info)
    return 0;

  char level[2];
  snprintf(level, sizeof(level), "%d", cmark_node_get_heading_level(node));

  int on_enter_size = 2 * strlen(info->anchor) + strlen(multipage_url) + 50;
  char *on_enter = malloc(on_enter_size);
  snprintf(on_enter, on_enter_size, "<h%s id='%s'><a href='%s#%s'>", level,
           info->anchor, multipage_url, info->anchor);

  int on_exit_size = 10;
  char *on_exit = malloc(on_exit_size);
  snprintf(on_exit, on_exit_size, "</a></h%s>", level);

  cmark_node *new_node = cmark_node_new(CMARK_NODE_CUSTOM_BLOCK);
  cmark_node_set_on_enter(new_node, on_enter);
  cmark_node_set_on_exit(new_node, on_exit);
  cmark_node_replace_with_children(node, new_node);

  free(info->anchor);
  free(info);
  cmark_node_set_user_data(node, NULL);
  cmark_node_free(node);
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
  int end_of_span = pos + 1 + strlen(span_text) + 3 + strlen(id) + 1;
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

int replace_admonition_start(char *multipage_url, cmark_node *node) {
  const char *lit = cmark_node_get_literal(node);
  char *admonition_type = malloc(strlen(lit) + 1);
  admonition_type[0] = '\0';
  char *admonition_anchor = malloc(strlen(lit) + 1);
  admonition_anchor[0] = '\0';
  int pos = parse_admonition_start(lit, admonition_type, admonition_anchor);
  if (-1 == pos) {
    free(admonition_type);
    free(admonition_anchor);
    return 0;
  }
  if (admonition_type[0] == '\0') {
    printf("no admonition type specified\n");
    free(admonition_type);
    free(admonition_anchor);
    return 0;
  }

  char *start = NULL;
  cmark_node *parent = cmark_node_parent(node);
  cmark_node *new_node = cmark_node_new(CMARK_NODE_HTML_BLOCK);

  if (admonition_anchor[0] == '\0') {
    const char *fmt = "<div class='admonition %s'><h3 class='title'>%c%s</h3>";
    size_t len = strlen(fmt) + strlen(admonition_type) * 2 + 1;
    start = malloc(len);
    snprintf(start, len, fmt, admonition_type, toupper(admonition_type[0]),
             admonition_type + 1);
  } else {
    const char *fmt = "<div class='admonition %s'>"
                      "<h3 id='%s' class='title'>"
                      "<a href='%s#%s'>%c%s</a></h3>";
    size_t len = strlen(fmt) + strlen(multipage_url) +
                 strlen(admonition_anchor) * 2 + strlen(admonition_type) * 2 +
                 1;
    start = malloc(len);
    snprintf(start, len, fmt, admonition_type, admonition_anchor, multipage_url,
             admonition_anchor, toupper(admonition_type[0]),
             admonition_type + 1);
  }

  cmark_node_set_literal(new_node, start);
  cmark_node_insert_before(parent, new_node);
  free(admonition_type);
  free(admonition_anchor);
  free(start);
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
                  AnchorLocationArray anchor_locations,
                  render_type render_type) {
  if (render_type == RENDER_TYPE_SINGLE) {
    return;
  }
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
  if (render_type == RENDER_TYPE_MULTIPAGE) {
    char *new_url =
        malloc(strlen(anchor_location.multipage_url) + strlen(url) + 1);
    strcpy(new_url, anchor_location.multipage_url);
    strcat(new_url, url);
    cmark_node_set_url(node, new_url);
    free(new_url);
  }
  if (strlen(anchor_location.title) != 0) {
    cmark_node *child = cmark_node_first_child(node);
    if (child == NULL) {
      cmark_node *new_child = cmark_node_new(CMARK_NODE_TEXT);
      cmark_node_set_literal(new_child, anchor_location.title);
      cmark_node_prepend_child(node, new_child);
    }
  }
}

void insert_search_index(FILE *search_index_path, const char *text,
                         const char *title, const char *multipage_url) {
  fjson_object *obj = fjson_object_new_object();
  fjson_object *obj_url = fjson_object_new_string(multipage_url);
  fjson_object *obj_title = fjson_object_new_string(title);
  fjson_object *obj_text = fjson_object_new_string(text);
  fjson_object_object_add(obj, "url", obj_url);
  fjson_object_object_add(obj, "title", obj_title);
  fjson_object_object_add(obj, "text", obj_text);
  fputs(fjson_object_to_json_string(obj), search_index_path);
  fputs("\n,", search_index_path);
}

/**
 * Moves custom syntax tokens from AST text nodes into custom data structures
 * attached to the nodes.
 */
void cmark_rewrite_syntax(cmark_node *document, cmark_mem *mem,
                          char *input_file_path, render_type render_type,
                          AnchorLocationArray anchor_locations) {
  cmark_iter *iter = cmark_iter_new(document);

  cmark_event_type event;
  cmark_node *node;

  while ((event = cmark_iter_next(iter))) {
    switch (event) {
    case CMARK_EVENT_NONE:
      break;
    case CMARK_EVENT_DONE:
      break;
    case CMARK_EVENT_ENTER:
      break;
    case CMARK_EVENT_EXIT:
      node = cmark_iter_get_node(iter);
      if (replace_header_attributes(node, input_file_path, anchor_locations))
        continue;
    }
    if (CMARK_EVENT_DONE == event)
      break;
  }
  cmark_iter_free(iter);
}

void cmark_rewrite(cmark_node *document, cmark_mem *mem, char *input_file_path,
                   render_type render_type, char *multipage_url,
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
      if (type == CMARK_NODE_LINK) {
        replace_link(node, input_file_path, anchor_locations, render_type);
        continue;
      }
      if (type != CMARK_NODE_TEXT)
        continue;
      if (replace_link_bracket_with_span(node))
        continue;
      if (replace_admonition_start(multipage_url, node))
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

/**
 * Turns CommonMark nodes annotated with our custom data into HTML elements.
 */
void cmark_rewrite_html(char *multipage_url, cmark_node *document) {
  cmark_iter *iter = cmark_iter_new(document);

  cmark_event_type event;
  cmark_node *node;

  while ((event = cmark_iter_next(iter))) {
    switch (event) {
    case CMARK_EVENT_NONE:
      break;
    case CMARK_EVENT_DONE:
      break;
    case CMARK_EVENT_ENTER:
      break;
    case CMARK_EVENT_EXIT:
      node = cmark_iter_get_node(iter);
      if (replace_headers_with_attributes_for_html(multipage_url, node))
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
                       render_type render_type, AnchorLocation *anchor_location,
                       AnchorLocationArray anchor_locations,
                       char *multipage_url, FILE *search_index_path) {

  cmark_mem *mem = cmark_get_default_mem_allocator();
  cmark_parser *parser =
      cmark_parser_new_with_mem(mmdoc_render_cmark_options, mem);
  cmark_node *document = mmdoc_render_cmark_document(file_path, parser);

  /* printf("BEFORE\n"); */
  /* render_debug_cmark_node(document); */

  cmark_rewrite_syntax(document, mem, file_path, render_type, anchor_locations);

  cmark_rewrite(document, mem, file_path, render_type, multipage_url,
                anchor_locations);

  if (render_type != RENDER_TYPE_MAN) {
    cmark_rewrite_html(multipage_url, document);
  }

  /* printf("AFTER\n"); */
  /* render_debug_cmark_node(document); */

  if (render_type != RENDER_TYPE_MAN) {
    char *result = cmark_render_html_with_mem(
        document, mmdoc_render_cmark_options,
        cmark_parser_get_syntax_extensions(parser), mem);

    if (search_index_path != NULL) {
      char *plaintext_result = cmark_render_plaintext_with_mem(
          document, mmdoc_render_cmark_options, 120, mem);
      insert_search_index(search_index_path, plaintext_result,
                          anchor_location->title, multipage_url);
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
  cmark_parser *parser =
      cmark_parser_new_with_mem(mmdoc_render_cmark_options, mem);
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
      result[pos] = '\0';
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
