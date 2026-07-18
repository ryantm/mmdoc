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
    free(new_lit);
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
  free(on_enter);
  free(on_exit);
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

int replace_code_link_bracket_with_span(cmark_node *node) {
  if (cmark_node_get_type(node) != CMARK_NODE_CODE)
    return 0;

  cmark_node *previous = cmark_node_previous(node);
  cmark_node *next = cmark_node_next(node);
  if (previous == NULL || next == NULL ||
      cmark_node_get_type(previous) != CMARK_NODE_TEXT ||
      cmark_node_get_type(next) != CMARK_NODE_TEXT)
    return 0;

  const char *left = cmark_node_get_literal(previous);
  const char *right = cmark_node_get_literal(next);
  size_t left_length = strlen(left);
  if (left_length == 0 || left[left_length - 1] != '[' ||
      strncmp(right, "]{#", 3) != 0)
    return 0;

  const char *anchor_end = strchr(right + 3, '}');
  if (anchor_end == NULL || anchor_end == right + 3)
    return 0;

  size_t anchor_length = (size_t)(anchor_end - (right + 3));
  char *start = malloc(strlen("<span id=''></span>") + anchor_length + 1);
  if (start == NULL)
    return 0;
  snprintf(start, strlen("<span id=''></span>") + anchor_length + 1,
           "<span id='%.*s'>", (int)anchor_length, right + 3);

  char *new_left = malloc(left_length);
  char *new_right = malloc(strlen(anchor_end + 1) + 1);
  if (new_left == NULL || new_right == NULL) {
    free(start);
    free(new_left);
    free(new_right);
    return 0;
  }
  memcpy(new_left, left, left_length - 1);
  new_left[left_length - 1] = '\0';
  strcpy(new_right, anchor_end + 1);
  cmark_node_set_literal(previous, new_left);
  cmark_node_set_literal(next, new_right);

  cmark_node *start_node = cmark_node_new(CMARK_NODE_HTML_INLINE);
  cmark_node_set_literal(start_node, start);
  cmark_node_insert_before(node, start_node);
  cmark_node *end_node = cmark_node_new(CMARK_NODE_HTML_INLINE);
  cmark_node_set_literal(end_node, "</span>");
  cmark_node_insert_after(node, end_node);

  free(start);
  free(new_left);
  free(new_right);
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

static cmark_node *new_html_block(const char *literal) {
  cmark_node *node = cmark_node_new(CMARK_NODE_HTML_BLOCK);
  cmark_node_set_literal(node, literal);
  return node;
}

static int definition_position_for_text_node(cmark_node *node) {
  if (node == NULL || cmark_node_get_type(node) != CMARK_NODE_TEXT)
    return -1;
  const char *literal = cmark_node_get_literal(node);
  int position = parse_dd(literal);
  if (position != -1)
    return position;

  size_t i = 0;
  while (literal[i] == ' ')
    i++;
  if (literal[i] != ':')
    return -1;
  i++;
  if (literal[i] != ' ')
    return -1;
  while (literal[i] == ' ')
    i++;
  return literal[i] == '\0' && cmark_node_next(node) != NULL ? (int)i : -1;
}

static int paragraph_definition_position(cmark_node *node) {
  if (node == NULL || cmark_node_get_type(node) != CMARK_NODE_PARAGRAPH)
    return -1;
  cmark_node *child = cmark_node_first_child(node);
  return definition_position_for_text_node(child);
}

static void split_compact_definition_paragraph(cmark_node *paragraph) {
  cmark_node *current = paragraph;
  for (;;) {
    cmark_node *line_break = cmark_node_first_child(current);
    while (line_break != NULL) {
      cmark_node *next = cmark_node_next(line_break);
      if (cmark_node_get_type(line_break) == CMARK_NODE_SOFTBREAK &&
          next != NULL && cmark_node_get_type(next) == CMARK_NODE_TEXT &&
          definition_position_for_text_node(next) != -1)
        break;
      line_break = next;
    }
    if (line_break == NULL)
      return;

    cmark_node *definition = cmark_node_new(CMARK_NODE_PARAGRAPH);
    cmark_node_insert_after(current, definition);
    cmark_node *child = cmark_node_next(line_break);
    while (child != NULL) {
      cmark_node *next = cmark_node_next(child);
      cmark_node_unlink(child);
      cmark_node_append_child(definition, child);
      child = next;
    }
    cmark_node_unlink(line_break);
    cmark_node_free(line_break);
    current = definition;
  }
}

static void normalize_compact_definition_lists(cmark_node *container) {
  for (cmark_node *child = cmark_node_first_child(container); child != NULL;) {
    cmark_node *next = cmark_node_next(child);
    if (cmark_node_get_type(child) == CMARK_NODE_PARAGRAPH)
      split_compact_definition_paragraph(child);
    else
      normalize_compact_definition_lists(child);
    child = next;
  }
}

static void wrap_block(cmark_node *node, const char *start, const char *end) {
  cmark_node *start_node = new_html_block(start);
  cmark_node *end_node = new_html_block(end);
  cmark_node_insert_before(node, start_node);
  cmark_node_insert_after(node, end_node);
}

static void rewrite_definition_lists_in(cmark_node *container) {
  cmark_node *cursor = cmark_node_first_child(container);
  while (cursor != NULL) {
    cmark_node *first_definition = cmark_node_next(cursor);
    if (cmark_node_get_type(cursor) != CMARK_NODE_PARAGRAPH ||
        paragraph_definition_position(first_definition) == -1) {
      rewrite_definition_lists_in(cursor);
      cursor = cmark_node_next(cursor);
      continue;
    }

    cmark_node *open = new_html_block("<dl>");
    cmark_node_insert_before(cursor, open);
    for (;;) {
      cmark_node *definition = cmark_node_next(cursor);
      wrap_block(cursor, "<dt>", "</dt>");
      cursor = definition;

      while (paragraph_definition_position(cursor) != -1) {
        cmark_node *next = cmark_node_next(cursor);
        cmark_node *first_child = cmark_node_first_child(cursor);
        const char *literal = cmark_node_get_literal(first_child);
        int position = definition_position_for_text_node(first_child);
        cmark_node_set_literal(first_child, literal + position);
        wrap_block(cursor, "<dd>", "</dd>");
        cursor = next;
      }

      if (cursor == NULL ||
          cmark_node_get_type(cursor) != CMARK_NODE_PARAGRAPH ||
          paragraph_definition_position(cmark_node_next(cursor)) == -1)
        break;
    }

    cmark_node *close = new_html_block("</dl>");
    if (cursor == NULL)
      cmark_node_append_child(container, close);
    else
      cmark_node_insert_before(cursor, close);
  }
}

static void rewrite_definition_lists(cmark_node *document) {
  normalize_compact_definition_lists(document);
  rewrite_definition_lists_in(document);
}

void replace_link(cmark_node *node, char *input_file_path,
                  AnchorLocationArray anchor_locations, render_type render_type,
                  char *multipage_url) {
  if (render_type == RENDER_TYPE_SINGLE) {
    return;
  }
  const char *url = cmark_node_get_url(node);
  if (strlen(url) == 0)
    return;
  if (url[0] != '#')
    return;
  const char *anchor = url;

  AnchorLocation *anchor_location =
      find_anchor_location(&anchor_locations, anchor);
  if (anchor_location == NULL) {
    printf("Anchor \"%s\" referenced in %s not found.\n", anchor,
           input_file_path);
    if (render_type == RENDER_TYPE_MULTIPAGE) {
      char *new_url = malloc(strlen(multipage_url) + strlen(url) + 1);
      strcpy(new_url, multipage_url);
      strcat(new_url, url);
      cmark_node_set_url(node, new_url);
      free(new_url);
    }
    return;
  }
  if (render_type == RENDER_TYPE_MULTIPAGE) {
    char *new_url =
        malloc(strlen(anchor_location->multipage_url) + strlen(url) + 1);
    strcpy(new_url, anchor_location->multipage_url);
    strcat(new_url, url);
    cmark_node_set_url(node, new_url);
    free(new_url);
  }
  if (strlen(anchor_location->title) != 0) {
    cmark_node *child = cmark_node_first_child(node);
    if (child == NULL) {
      cmark_node *new_child = cmark_node_new(CMARK_NODE_TEXT);
      cmark_node_set_literal(new_child, anchor_location->title);
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
  fjson_object_put(obj);
}

/**
 * Moves custom syntax tokens from AST text nodes into custom data structures
 * attached to the nodes.
 */
int cmark_rewrite_syntax(cmark_node *document, cmark_mem *mem,
                         char *input_file_path, render_type render_type,
                         AnchorLocationArray anchor_locations) {
  cmark_iter *iter = cmark_iter_new(document);
  int has_code_block = 0;

  cmark_event_type event;
  cmark_node *node;

  while ((event = cmark_iter_next(iter))) {
    switch (event) {
    case CMARK_EVENT_NONE:
      break;
    case CMARK_EVENT_DONE:
      break;
    case CMARK_EVENT_ENTER:
      node = cmark_iter_get_node(iter);
      if (cmark_node_get_type(node) == CMARK_NODE_CODE_BLOCK)
        has_code_block = 1;
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
  return has_code_block;
}

void cmark_rewrite_spans(cmark_node *document) {
  int changed;
  do {
    changed = 0;
    cmark_iter *iter = cmark_iter_new(document);
    cmark_event_type event;
    while ((event = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
      if (event != CMARK_EVENT_ENTER)
        continue;
      cmark_node *node = cmark_iter_get_node(iter);
      if (replace_code_link_bracket_with_span(node) ||
          (cmark_node_get_type(node) == CMARK_NODE_TEXT &&
           replace_link_bracket_with_span(node))) {
        changed = 1;
        break;
      }
    }
    cmark_iter_free(iter);
  } while (changed);
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
        replace_link(node, input_file_path, anchor_locations, render_type,
                     multipage_url);
        continue;
      }
      if (type != CMARK_NODE_TEXT)
        continue;
      if (replace_admonition_start(multipage_url, node))
        continue;
      if (replace_admonition_end(node))
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

static int collect_anchors_from_text(Array *anchors, const char *text,
                                     const char *file_path) {
  for (size_t i = 0; text[i] != '\0'; i++) {
    if (text[i] != '{')
      continue;
    size_t close = i + 1;
    while (text[close] != '\0' && text[close] != '}')
      close++;
    if (text[close] != '}')
      continue;

    size_t hash = i + 1;
    while (hash < close && text[hash] != '#')
      hash++;
    if (hash == close)
      continue;

    size_t end = hash + 1;
    while (end < close && !isspace((unsigned char)text[end]))
      end++;
    size_t anchor_length = end - hash;
    if (anchor_length <= 1)
      continue;
    if (anchor_length >= 1024) {
      fprintf(stderr, "Anchor in %s exceeds 1023 bytes.\n", file_path);
      return -1;
    }
    char anchor[1024];
    memcpy(anchor, text + hash, anchor_length);
    anchor[anchor_length] = '\0';
    insert_array(anchors, anchor);
    i = close;
  }
  return 0;
}

int mmdoc_render_collect_anchors(char *file_path, Array *anchors) {
  cmark_mem *mem = cmark_get_default_mem_allocator();
  cmark_parser *parser =
      cmark_parser_new_with_mem(mmdoc_render_cmark_options, mem);
  cmark_node *document = mmdoc_render_cmark_document(file_path, parser);
  if (document == NULL) {
    cmark_parser_free(parser);
    return -1;
  }

  int failed = 0;
  cmark_iter *iter = cmark_iter_new(document);
  cmark_event_type event;
  while (!failed && (event = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
    if (event != CMARK_EVENT_ENTER)
      continue;
    cmark_node *node = cmark_iter_get_node(iter);
    if (cmark_node_get_type(node) == CMARK_NODE_TEXT)
      failed = collect_anchors_from_text(anchors, cmark_node_get_literal(node),
                                         file_path) != 0;
  }
  cmark_iter_free(iter);
  cmark_node_free(document);
  cmark_parser_free(parser);
  return failed ? -1 : 0;
}

int mmdoc_render_part(char *file_path, FILE *output_file,
                      render_type render_type, AnchorLocation *anchor_location,
                      AnchorLocationArray anchor_locations, char *multipage_url,
                      FILE *search_index_path) {

  cmark_mem *mem = cmark_get_default_mem_allocator();
  cmark_parser *parser =
      cmark_parser_new_with_mem(mmdoc_render_cmark_options, mem);
  cmark_node *document = mmdoc_render_cmark_document(file_path, parser);

  /* printf("BEFORE\n"); */
  /* render_debug_cmark_node(document); */

  int has_code_block = cmark_rewrite_syntax(document, mem, file_path,
                                            render_type, anchor_locations);

  cmark_rewrite_spans(document);
  rewrite_definition_lists(document);

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
  return has_code_block;
}

static char *mmdoc_render_get_heading_title(cmark_node *heading) {
  cmark_node *last_child = cmark_node_last_child(heading);
  if (last_child == NULL || cmark_node_get_type(last_child) != CMARK_NODE_TEXT)
    return NULL;

  const char *last_literal = cmark_node_get_literal(last_child);
  char *id = malloc(strlen(last_literal) + 1);
  int attribute_position = parse_heading_bracketed_span_id(last_literal, id);
  free(id);
  if (attribute_position == -1)
    return NULL;

  size_t title_length = 0;
  cmark_iter *iter = cmark_iter_new(heading);
  cmark_event_type event;
  while ((event = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
    if (event != CMARK_EVENT_ENTER)
      continue;
    cmark_node *node = cmark_iter_get_node(iter);
    cmark_node_type type = cmark_node_get_type(node);
    if (type != CMARK_NODE_TEXT && type != CMARK_NODE_CODE)
      continue;
    if (node == last_child)
      title_length += attribute_position;
    else
      title_length += strlen(cmark_node_get_literal(node));
  }
  cmark_iter_free(iter);

  char *result = malloc(title_length + 1);
  size_t result_position = 0;
  iter = cmark_iter_new(heading);
  while ((event = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
    if (event != CMARK_EVENT_ENTER)
      continue;
    cmark_node *node = cmark_iter_get_node(iter);
    cmark_node_type type = cmark_node_get_type(node);
    if (type != CMARK_NODE_TEXT && type != CMARK_NODE_CODE)
      continue;
    const char *literal = cmark_node_get_literal(node);
    size_t literal_length =
        node == last_child ? (size_t)attribute_position : strlen(literal);
    memcpy(result + result_position, literal, literal_length);
    result_position += literal_length;
  }
  cmark_iter_free(iter);
  result[result_position] = '\0';
  return result;
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
      if (type != CMARK_NODE_HEADER)
        continue;
      char *result = mmdoc_render_get_heading_title(node);
      if (result == NULL)
        continue;
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
  char *result = malloc(1);
  result[0] = '\0';
  return result;
}
