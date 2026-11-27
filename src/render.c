/* SPDX-License-Identifier: CC0-1.0 */
#include "render.h"
#include "arena.h"
#include "html.h"
#include "mkdir_p.h"
#include "parse.h"
#include "types.h"
#include <cmark-gfm-core-extensions.h>
#include <cmark-gfm-extension_api.h>
#include <cmark-gfm.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* ---- Growable byte buffer ---- */

void mmdoc_buf_init(MmdocBuf *buf, size_t initial_cap) {
  buf->len = 0;
  buf->cap = 0;
  buf->data = NULL;
  if (initial_cap > 0) {
    buf->data = malloc(initial_cap);
    if (buf->data != NULL) {
      buf->cap = initial_cap;
      buf->data[0] = '\0';
    }
  }
}

void mmdoc_buf_free(MmdocBuf *buf) {
  free(buf->data);
  buf->data = NULL;
  buf->len = 0;
  buf->cap = 0;
}

static int mmdoc_buf_reserve(MmdocBuf *buf, size_t extra) {
  if (extra > SIZE_MAX - buf->len - 1)
    return -1;
  size_t needed = buf->len + extra + 1;
  if (needed <= buf->cap)
    return 0;
  size_t new_cap = buf->cap == 0 ? 256 : buf->cap;
  while (new_cap < needed) {
    if (new_cap > SIZE_MAX / 2) {
      new_cap = needed;
      break;
    }
    new_cap *= 2;
  }
  char *next = realloc(buf->data, new_cap);
  if (next == NULL)
    return -1;
  buf->data = next;
  buf->cap = new_cap;
  return 0;
}

int mmdoc_buf_append(MmdocBuf *buf, const char *data, size_t len) {
  if (mmdoc_buf_reserve(buf, len) != 0)
    return -1;
  if (len > 0)
    memcpy(buf->data + buf->len, data, len);
  buf->len += len;
  buf->data[buf->len] = '\0';
  return 0;
}

int mmdoc_buf_append_str(MmdocBuf *buf, const char *str) {
  return mmdoc_buf_append(buf, str, strlen(str));
}

int mmdoc_buf_append_char(MmdocBuf *buf, char c) {
  if (mmdoc_buf_reserve(buf, 1) != 0)
    return -1;
  buf->data[buf->len++] = c;
  buf->data[buf->len] = '\0';
  return 0;
}

void chapter_outputs_free(ChapterOutputs *out) {
  mmdoc_buf_free(&out->single_html);
  mmdoc_buf_free(&out->multi_html);
  mmdoc_buf_free(&out->full_md);
  mmdoc_buf_free(&out->chapter_md);
  mmdoc_buf_free(&out->search_json);
  mmdoc_buf_free(&out->warnings);
  out->has_code_block = 0;
}

/* ---- Thread-safe one-time cmark-gfm initialization ---- */

static pthread_once_t cmark_init_once = PTHREAD_ONCE_INIT;
static cmark_syntax_extension *cached_table_extension = NULL;

static void init_cmark_extensions(void) {
  cmark_gfm_core_extensions_ensure_registered();
  cached_table_extension = cmark_find_syntax_extension("table");
}

void mmdoc_cmark_init(void) {
  pthread_once(&cmark_init_once, init_cmark_extensions);
}

cmark_syntax_extension *mmdoc_table_extension(void) {
  mmdoc_cmark_init();
  return cached_table_extension;
}

/* ---- AST helpers ---- */

static char *mem_strdup(cmark_mem *mem, const char *s) {
  size_t len = strlen(s);
  char *copy = mem->calloc(1, len + 1);
  if (copy != NULL)
    memcpy(copy, s, len + 1);
  return copy;
}

static char *mem_strndup(cmark_mem *mem, const char *s, size_t len) {
  char *copy = mem->calloc(1, len + 1);
  if (copy != NULL) {
    memcpy(copy, s, len);
    copy[len] = '\0';
  }
  return copy;
}

int cmark_node_replace_with_children(cmark_node *oldnode, cmark_node *newnode) {
  cmark_node_replace(oldnode, newnode);

  cmark_node *child;
  while ((child = cmark_node_first_child(oldnode)) != NULL) {
    if (!cmark_node_append_child(newnode, child)) {
      return 0;
    }
  }

  return 1;
}

static void free_heading_info(cmark_mem *mem, void *data) {
  HeadingInfo *info = data;
  if (info == NULL)
    return;
  mem->free(info->anchor);
  mem->free(info);
}

static int replace_header_attributes(cmark_node *node, cmark_mem *mem) {
  if (cmark_node_get_type(node) != CMARK_NODE_HEADER)
    return 0;
  cmark_node *last_child = cmark_node_last_child(node);

  if (last_child == NULL || cmark_node_get_type(last_child) != CMARK_NODE_TEXT)
    return 0;

  const char *lit = cmark_node_get_literal(last_child);
  if (strchr(lit, '{') == NULL)
    return 0;

  size_t lit_len = strlen(lit);
  char stack_id[256];
  char *id = lit_len < sizeof(stack_id) ? stack_id : malloc(lit_len + 1);
  if (id == NULL)
    return 0;

  int pos = parse_heading_bracketed_span_id(lit, id);
  if (pos == -1) {
    if (id != stack_id)
      free(id);
    return 0;
  }

  HeadingInfo *info = mem->calloc(1, sizeof(*info));
  if (info == NULL) {
    if (id != stack_id)
      free(id);
    return 0;
  }
  info->anchor = mem_strdup(mem, id);
  if (id != stack_id)
    free(id);
  if (info->anchor == NULL) {
    mem->free(info);
    return 0;
  }
  cmark_node_set_user_data(node, info);
  cmark_node_set_user_data_free_func(node, free_heading_info);

  if (pos == 0) {
    cmark_node_unlink(last_child);
    cmark_node_free(last_child);
  } else {
    char stack_lit[256];
    char *new_lit =
        (size_t)pos < sizeof(stack_lit) ? stack_lit : malloc((size_t)pos + 1);
    if (new_lit != NULL) {
      memcpy(new_lit, lit, (size_t)pos);
      new_lit[pos] = '\0';
      cmark_node_set_literal(last_child, new_lit);
      if (new_lit != stack_lit)
        free(new_lit);
    }
  }

  return 1;
}

static char *format_heading_enter(const char *level, const char *multipage_url,
                                  const char *escaped_anchor) {
  size_t size =
      2 * strlen(escaped_anchor) + strlen(multipage_url) + strlen(level) + 32;
  char *on_enter = malloc(size);
  if (on_enter == NULL)
    return NULL;
  snprintf(on_enter, size, "<h%s id='%s'><a href='%s#%s'>", level,
           escaped_anchor, multipage_url, escaped_anchor);
  return on_enter;
}

typedef struct {
  cmark_node *node;
  int level;
  char *escaped_anchor;
} AnchoredHeadingRef;

typedef struct {
  AnchoredHeadingRef *items;
  size_t used;
  size_t cap;
} AnchoredHeadingVec;

static int push_anchored_heading(AnchoredHeadingVec *v, cmark_node *node,
                                 int level, char *escaped_anchor) {
  if (v->used == v->cap) {
    size_t next_cap = v->cap == 0 ? 16 : v->cap * 2;
    AnchoredHeadingRef *next = realloc(v->items, next_cap * sizeof(*next));
    if (next == NULL)
      return -1;
    v->items = next;
    v->cap = next_cap;
  }
  v->items[v->used].node = node;
  v->items[v->used].level = level;
  v->items[v->used].escaped_anchor = escaped_anchor;
  v->used++;
  return 0;
}

static int replace_header_for_html(const char *multipage_url, cmark_node *node,
                                   cmark_mem *mem, AnchoredHeadingVec *track) {
  if (cmark_node_get_type(node) != CMARK_NODE_HEADER)
    return 0;

  HeadingInfo *info = cmark_node_get_user_data(node);
  if (info == NULL)
    return 0;

  int hlevel = cmark_node_get_heading_level(node);
  char level[16];
  snprintf(level, sizeof(level), "%d", hlevel);

  char *escaped_anchor = html_escape_string(info->anchor);
  if (escaped_anchor == NULL)
    return 0;

  char *on_enter = format_heading_enter(level, multipage_url, escaped_anchor);
  if (on_enter == NULL) {
    free(escaped_anchor);
    return 0;
  }

  char on_exit[24];
  snprintf(on_exit, sizeof(on_exit), "</a></h%s>", level);

  cmark_node *new_node = cmark_node_new_with_mem(CMARK_NODE_CUSTOM_BLOCK, mem);
  if (new_node == NULL) {
    free(on_enter);
    free(escaped_anchor);
    return 0;
  }
  cmark_node_set_on_enter(new_node, on_enter);
  cmark_node_set_on_exit(new_node, on_exit);
  free(on_enter);

  cmark_node_replace_with_children(node, new_node);
  cmark_node_free(node);

  if (track != NULL) {
    if (push_anchored_heading(track, new_node, hlevel, escaped_anchor) != 0)
      free(escaped_anchor);
  } else {
    free(escaped_anchor);
  }
  return 1;
}

int replace_headers_with_attributes_for_html(char *multipage_url,
                                             cmark_node *node) {
  return replace_header_for_html(multipage_url, node,
                                 cmark_get_default_mem_allocator(), NULL);
}

/* ---- Bracketed span rewriting ---- */

typedef struct {
  cmark_node *open_node;
  cmark_node *close_node;
  char *id;
} SpanRef;

typedef struct {
  SpanRef *items;
  size_t used;
  size_t cap;
} SpanVec;

static void push_span_ref(SpanVec *v, cmark_node *open_node,
                          cmark_node *close_node, const char *id,
                          cmark_mem *mem) {
  if (v == NULL)
    return;
  if (v->used == v->cap) {
    size_t next_cap = v->cap == 0 ? 16 : v->cap * 2;
    SpanRef *next = realloc(v->items, next_cap * sizeof(*next));
    if (next == NULL)
      return;
    v->items = next;
    v->cap = next_cap;
  }
  v->items[v->used].open_node = open_node;
  v->items[v->used].close_node = close_node;
  v->items[v->used].id = mem_strdup(mem, id);
  v->used++;
}

static void wrap_span_with_mem(cmark_node *node, const char *id, int markdown,
                               cmark_mem *mem, SpanVec *track) {
  cmark_node_type type =
      markdown ? CMARK_NODE_CUSTOM_INLINE : CMARK_NODE_HTML_INLINE;
  cmark_node *open = cmark_node_new_with_mem(type, mem);
  cmark_node *close = cmark_node_new_with_mem(type, mem);
  if (open == NULL || close == NULL) {
    cmark_node_free(open);
    cmark_node_free(close);
    return;
  }

  if (markdown) {
    size_t id_len = strlen(id);
    char stack_marker[256];
    char *marker =
        id_len + 8 < sizeof(stack_marker) ? stack_marker : malloc(id_len + 8);
    if (marker != NULL) {
      snprintf(marker, id_len + 8, "]{#%s}", id);
      cmark_node_set_on_enter(open, "[");
      cmark_node_set_on_enter(close, marker);
      if (marker != stack_marker)
        free(marker);
    }
  } else {
    char *escaped = html_escape_string(id);
    if (escaped != NULL) {
      size_t esc_len = strlen(escaped);
      char stack_marker[256];
      char *marker = esc_len + 16 < sizeof(stack_marker) ? stack_marker
                                                         : malloc(esc_len + 16);
      if (marker != NULL) {
        snprintf(marker, esc_len + 16, "<span id='%s'>", escaped);
        cmark_node_set_literal(open, marker);
        cmark_node_set_literal(close, "</span>");
        if (marker != stack_marker)
          free(marker);
      }
      free(escaped);
    }
  }
  cmark_node_insert_before(node, open);
  cmark_node_insert_after(node, close);
  push_span_ref(track, open, close, id, mem);
}

static cmark_node *replace_link_bracket_with_span(cmark_node *node,
                                                  int markdown, cmark_mem *mem,
                                                  SpanVec *track) {
  const char *lit = cmark_node_get_literal(node);
  if (strchr(lit, '[') == NULL)
    return NULL;

  size_t lit_len = strlen(lit);
  char stack_id[256];
  char stack_text[256];
  char *id = lit_len < sizeof(stack_id) ? stack_id : malloc(lit_len + 1);
  char *span_text =
      lit_len < sizeof(stack_text) ? stack_text : malloc(lit_len + 1);
  if (id == NULL || span_text == NULL) {
    if (id != stack_id)
      free(id);
    if (span_text != stack_text)
      free(span_text);
    return NULL;
  }

  int pos = parse_link_bracketed_span_id(lit, span_text, id);
  if (pos == -1) {
    if (id != stack_id)
      free(id);
    if (span_text != stack_text)
      free(span_text);
    return NULL;
  }

  size_t span_text_len = strlen(span_text);
  size_t id_len = strlen(id);

  if (pos > 0) {
    char stack_l[256];
    char *new_l =
        (size_t)pos < sizeof(stack_l) ? stack_l : malloc((size_t)pos + 1);
    if (new_l != NULL) {
      memcpy(new_l, lit, (size_t)pos);
      new_l[pos] = '\0';
      cmark_node *left_node = cmark_node_new_with_mem(CMARK_NODE_TEXT, mem);
      if (left_node != NULL) {
        cmark_node_set_literal(left_node, new_l);
        cmark_node_insert_before(node, left_node);
      }
      if (new_l != stack_l)
        free(new_l);
    }
  }

  cmark_node *content_node = cmark_node_new_with_mem(CMARK_NODE_TEXT, mem);
  if (content_node != NULL) {
    cmark_node_set_literal(content_node, span_text);
    cmark_node_insert_before(node, content_node);
    wrap_span_with_mem(content_node, id, markdown, mem, track);
  }

  size_t end_of_span = (size_t)pos + 1 + span_text_len + 3 + id_len + 1;
  cmark_node *tail_node = NULL;
  if (end_of_span < lit_len) {
    tail_node = cmark_node_new_with_mem(CMARK_NODE_TEXT, mem);
    if (tail_node != NULL) {
      cmark_node_set_literal(tail_node, lit + end_of_span);
      cmark_node_insert_before(node, tail_node);
    }
  }

  cmark_node *resume = tail_node != NULL ? tail_node : cmark_node_next(node);
  cmark_node_unlink(node);
  cmark_node_free(node);
  if (id != stack_id)
    free(id);
  if (span_text != stack_text)
    free(span_text);
  /* Return the node to continue scanning (tail_node if present, else next
   * sibling, or a sentinel? Caller checks return != NULL or uses prev/parent).
   */
  return resume != NULL ? resume : (cmark_node *)(intptr_t)1;
}

static int replace_code_link_bracket_with_span(cmark_node *node, int markdown,
                                               cmark_mem *mem, SpanVec *track) {
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
  char stack_id[256];
  char *id =
      anchor_length < sizeof(stack_id) ? stack_id : malloc(anchor_length + 1);
  if (id == NULL)
    return 0;
  memcpy(id, right + 3, anchor_length);
  id[anchor_length] = '\0';

  char stack_left[256];
  char *new_left =
      left_length <= sizeof(stack_left) ? stack_left : malloc(left_length);
  size_t right_rest_len = strlen(anchor_end + 1);
  char stack_right[256];
  char *new_right = right_rest_len < sizeof(stack_right)
                        ? stack_right
                        : malloc(right_rest_len + 1);
  if (new_left == NULL || new_right == NULL) {
    if (id != stack_id)
      free(id);
    if (new_left != stack_left)
      free(new_left);
    if (new_right != stack_right)
      free(new_right);
    return 0;
  }
  memcpy(new_left, left, left_length - 1);
  new_left[left_length - 1] = '\0';
  memcpy(new_right, anchor_end + 1, right_rest_len + 1);
  cmark_node_set_literal(previous, new_left);
  cmark_node_set_literal(next, new_right);

  wrap_span_with_mem(node, id, markdown, mem, track);
  if (id != stack_id)
    free(id);
  if (new_left != stack_left)
    free(new_left);
  if (new_right != stack_right)
    free(new_right);
  return 1;
}

static void cmark_rewrite_spans_internal(cmark_node *document, int markdown,
                                         cmark_mem *mem, SpanVec *track) {
  cmark_iter *iter = cmark_iter_new(document);
  cmark_event_type event = cmark_iter_next(iter);
  while (event != CMARK_EVENT_DONE) {
    if (event == CMARK_EVENT_ENTER) {
      cmark_node *node = cmark_iter_get_node(iter);
      cmark_node *parent = cmark_node_parent(node);
      if (replace_code_link_bracket_with_span(node, markdown, mem, track)) {
        /* The code node survives; advance past its newly inserted close span.
         */
        cmark_iter_reset(iter, node, CMARK_EVENT_ENTER);
      } else if (cmark_node_get_type(node) == CMARK_NODE_TEXT) {
        cmark_node *resume =
            replace_link_bracket_with_span(node, markdown, mem, track);
        if (resume != NULL) {
          if (resume == (cmark_node *)(intptr_t)1) {
            cmark_iter_reset(iter, parent, CMARK_EVENT_EXIT);
          } else {
            cmark_iter_reset(iter, resume, CMARK_EVENT_ENTER);
            event = CMARK_EVENT_ENTER;
            continue;
          }
        }
      }
    }
    event = cmark_iter_next(iter);
  }
  cmark_iter_free(iter);
}

void cmark_rewrite_spans(cmark_node *document, int markdown) {
  cmark_rewrite_spans_internal(document, markdown,
                               cmark_get_default_mem_allocator(), NULL);
}

/* ---- Admonitions ---- */

typedef struct {
  cmark_node *node;
  char *escaped_type;
  char *escaped_anchor;
} AnchoredAdmonitionRef;

typedef struct {
  AnchoredAdmonitionRef *items;
  size_t used;
  size_t cap;
} AnchoredAdmonitionVec;

static int push_anchored_admonition(AnchoredAdmonitionVec *v, cmark_node *node,
                                    char *escaped_type, char *escaped_anchor) {
  if (v->used == v->cap) {
    size_t next_cap = v->cap == 0 ? 8 : v->cap * 2;
    AnchoredAdmonitionRef *next = realloc(v->items, next_cap * sizeof(*next));
    if (next == NULL)
      return -1;
    v->items = next;
    v->cap = next_cap;
  }
  v->items[v->used].node = node;
  v->items[v->used].escaped_type = escaped_type;
  v->items[v->used].escaped_anchor = escaped_anchor;
  v->used++;
  return 0;
}

static char *format_admonition_start(const char *escaped_type,
                                     const char *escaped_anchor,
                                     const char *multipage_url) {
  char first_upper = (char)toupper((unsigned char)escaped_type[0]);
  if (escaped_anchor == NULL || escaped_anchor[0] == '\0') {
    const char *fmt = "<div class='admonition %s'><h3 class='title'>%c%s</h3>";
    size_t len = strlen(fmt) + strlen(escaped_type) * 2 + 1;
    char *start = malloc(len);
    if (start != NULL)
      snprintf(start, len, fmt, escaped_type, first_upper, escaped_type + 1);
    return start;
  }
  const char *fmt = "<div class='admonition %s'>"
                    "<h3 id='%s' class='title'>"
                    "<a href='%s#%s'>%c%s</a></h3>";
  size_t len = strlen(fmt) + strlen(multipage_url) +
               strlen(escaped_anchor) * 2 + strlen(escaped_type) * 2 + 1;
  char *start = malloc(len);
  if (start != NULL)
    snprintf(start, len, fmt, escaped_type, escaped_anchor, multipage_url,
             escaped_anchor, first_upper, escaped_type + 1);
  return start;
}

static int replace_admonition_start_internal(const char *multipage_url,
                                             cmark_node *node, cmark_mem *mem,
                                             AnchoredAdmonitionVec *track) {
  cmark_node *parent = cmark_node_parent(node);
  if (parent == NULL || cmark_node_get_type(parent) != CMARK_NODE_PARAGRAPH)
    return 0;

  const char *lit = cmark_node_get_literal(node);
  const char *start_token = lit;
  while (*start_token == ' ')
    start_token++;
  if (strncmp(start_token, ":::", 3) != 0)
    return 0;

  size_t lit_len = strlen(lit);
  char stack_type[128];
  char stack_anchor[128];
  char *admonition_type =
      lit_len < sizeof(stack_type) ? stack_type : malloc(lit_len + 1);
  char *admonition_anchor =
      lit_len < sizeof(stack_anchor) ? stack_anchor : malloc(lit_len + 1);
  if (admonition_type == NULL || admonition_anchor == NULL) {
    if (admonition_type != stack_type)
      free(admonition_type);
    if (admonition_anchor != stack_anchor)
      free(admonition_anchor);
    return 0;
  }
  admonition_type[0] = '\0';
  admonition_anchor[0] = '\0';

  int pos = parse_admonition_start(lit, admonition_type, admonition_anchor);
  if (pos == -1 || admonition_type[0] == '\0') {
    if (pos != -1 && admonition_type[0] == '\0')
      printf("no admonition type specified\n");
    if (admonition_type != stack_type)
      free(admonition_type);
    if (admonition_anchor != stack_anchor)
      free(admonition_anchor);
    return 0;
  }

  char *escaped_type = html_escape_string(admonition_type);
  char *escaped_anchor = admonition_anchor[0] != '\0'
                             ? html_escape_string(admonition_anchor)
                             : NULL;
  if (admonition_type != stack_type)
    free(admonition_type);
  if (admonition_anchor != stack_anchor)
    free(admonition_anchor);
  if (escaped_type == NULL) {
    free(escaped_anchor);
    return 0;
  }

  char *start =
      format_admonition_start(escaped_type, escaped_anchor, multipage_url);
  if (start == NULL) {
    free(escaped_type);
    free(escaped_anchor);
    return 0;
  }

  cmark_node *new_node = cmark_node_new_with_mem(CMARK_NODE_HTML_BLOCK, mem);
  if (new_node == NULL) {
    free(start);
    free(escaped_type);
    free(escaped_anchor);
    return 0;
  }
  cmark_node_set_literal(new_node, start);
  free(start);
  cmark_node_insert_before(parent, new_node);
  cmark_node_unlink(node);
  cmark_node_free(node);

  if (track != NULL && escaped_anchor != NULL) {
    if (push_anchored_admonition(track, new_node, escaped_type,
                                 escaped_anchor) != 0) {
      free(escaped_type);
      free(escaped_anchor);
    }
  } else {
    free(escaped_type);
    free(escaped_anchor);
  }
  return 1;
}

static int replace_admonition_end_internal(cmark_node *node, cmark_mem *mem) {
  cmark_node *parent = cmark_node_parent(node);
  if (parent == NULL || cmark_node_get_type(parent) != CMARK_NODE_PARAGRAPH)
    return 0;

  const char *lit = cmark_node_get_literal(node);
  if (parse_admonition_end(lit) == -1)
    return 0;

  cmark_node *new_node = cmark_node_new_with_mem(CMARK_NODE_HTML_BLOCK, mem);
  if (new_node == NULL)
    return 0;
  cmark_node_set_literal(new_node, "</div>");
  cmark_node_insert_after(parent, new_node);
  cmark_node_unlink(node);
  cmark_node_free(node);
  return 1;
}

static int is_block_container(cmark_node_type type) {
  return type == CMARK_NODE_DOCUMENT || type == CMARK_NODE_BLOCK_QUOTE ||
         type == CMARK_NODE_LIST || type == CMARK_NODE_ITEM;
}

static void rewrite_admonitions_in(cmark_node *container,
                                   const char *multipage_url, cmark_mem *mem,
                                   AnchoredAdmonitionVec *track) {
  for (cmark_node *block = cmark_node_first_child(container); block != NULL;) {
    cmark_node *next_block = cmark_node_next(block);
    cmark_node_type btype = cmark_node_get_type(block);
    if (btype == CMARK_NODE_PARAGRAPH) {
      for (cmark_node *child = cmark_node_first_child(block); child != NULL;) {
        cmark_node *next_child = cmark_node_next(child);
        if (cmark_node_get_type(child) == CMARK_NODE_TEXT) {
          if (!replace_admonition_start_internal(multipage_url, child, mem,
                                                 track))
            replace_admonition_end_internal(child, mem);
        }
        child = next_child;
      }
      if (cmark_node_first_child(block) == NULL) {
        cmark_node_unlink(block);
        cmark_node_free(block);
      }
    } else if (is_block_container(btype)) {
      rewrite_admonitions_in(block, multipage_url, mem, track);
    }
    block = next_block;
  }
}

/* ---- Definition lists ---- */

int mmdoc_render_cmark_options = CMARK_OPT_DEFAULT | CMARK_OPT_UNSAFE;

static const char dd_continuation_marker = 0;

static cmark_node *new_html_block(const char *literal, cmark_mem *mem) {
  cmark_node *node = cmark_node_new_with_mem(CMARK_NODE_HTML_BLOCK, mem);
  if (node != NULL)
    cmark_node_set_literal(node, literal);
  return node;
}

static int is_indented_code_block(cmark_node *node) {
  if (node == NULL || cmark_node_get_type(node) != CMARK_NODE_CODE_BLOCK)
    return 0;
  int len = 0, offset = 0;
  char ch = 0;
  return !cmark_node_get_fenced(node, &len, &offset, &ch);
}

static int is_dd_continuation_wrapper(cmark_node *node) {
  return node != NULL && cmark_node_get_type(node) == CMARK_NODE_CUSTOM_BLOCK &&
         cmark_node_get_user_data(node) == (void *)&dd_continuation_marker;
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
  if (literal[i] != ' ' && literal[i] != '\t')
    return -1;
  while (literal[i] == ' ' || literal[i] == '\t')
    i++;
  return literal[i] == '\0' && cmark_node_next(node) != NULL ? (int)i : -1;
}

static int paragraph_definition_position(cmark_node *node) {
  if (node == NULL || cmark_node_get_type(node) != CMARK_NODE_PARAGRAPH)
    return -1;
  cmark_node *child = cmark_node_first_child(node);
  return definition_position_for_text_node(child);
}

static int paragraph_has_compact_definition(cmark_node *node) {
  if (node == NULL || cmark_node_get_type(node) != CMARK_NODE_PARAGRAPH)
    return 0;
  for (cmark_node *child = cmark_node_first_child(node); child != NULL;
       child = cmark_node_next(child)) {
    if (cmark_node_get_type(child) == CMARK_NODE_SOFTBREAK) {
      cmark_node *next = cmark_node_next(child);
      if (definition_position_for_text_node(next) != -1)
        return 1;
    }
  }
  return 0;
}

static void expand_definition_continuations(cmark_node *container,
                                            cmark_mem *mem, int depth);

static cmark_node *expand_indented_continuation(cmark_node *code_block,
                                                cmark_mem *mem, int depth) {
  const char *lit = cmark_node_get_literal(code_block);
  if (lit == NULL)
    return code_block;
  size_t len = strlen(lit);

  cmark_parser *sub_parser =
      cmark_parser_new_with_mem(mmdoc_render_cmark_options, mem);
  if (sub_parser == NULL)
    return code_block;
  cmark_syntax_extension *table_extension = mmdoc_table_extension();
  if (table_extension != NULL)
    cmark_parser_attach_syntax_extension(sub_parser, table_extension);
  if (len > 0)
    cmark_parser_feed(sub_parser, lit, len);
  cmark_node *sub_doc = cmark_parser_finish(sub_parser);
  if (sub_doc == NULL) {
    cmark_parser_free(sub_parser);
    return code_block;
  }

  cmark_node *wrapper = cmark_node_new_with_mem(CMARK_NODE_CUSTOM_BLOCK, mem);
  if (wrapper == NULL) {
    cmark_node_free(sub_doc);
    cmark_parser_free(sub_parser);
    return code_block;
  }
  cmark_node_set_on_enter(wrapper, "");
  cmark_node_set_on_exit(wrapper, "");
  cmark_node_set_user_data(wrapper, (void *)&dd_continuation_marker);

  cmark_node *child;
  while ((child = cmark_node_first_child(sub_doc)) != NULL) {
    cmark_node_unlink(child);
    cmark_node_append_child(wrapper, child);
  }
  cmark_node_replace(code_block, wrapper);
  cmark_node_free(code_block);
  cmark_node_free(sub_doc);
  cmark_parser_free(sub_parser);

  if (depth < 16)
    expand_definition_continuations(wrapper, mem, depth + 1);
  return wrapper;
}

static cmark_node *expand_dd_continuations(cmark_node *first_cont, int dd_col,
                                           cmark_mem *mem, int depth) {
  cmark_node *cont = first_cont;
  while (cont != NULL) {
    if (is_indented_code_block(cont)) {
      cmark_node *wrapper = expand_indented_continuation(cont, mem, depth);
      cont = cmark_node_next(wrapper);
    } else if (cmark_node_get_start_column(cont) > dd_col) {
      if (is_block_container(cmark_node_get_type(cont)) && depth < 16)
        expand_definition_continuations(cont, mem, depth + 1);
      cont = cmark_node_next(cont);
    } else {
      break;
    }
  }
  return cont;
}

static void expand_definition_continuations(cmark_node *container,
                                            cmark_mem *mem, int depth) {
  cmark_node *cursor = cmark_node_first_child(container);
  while (cursor != NULL) {
    cmark_node_type ctype = cmark_node_get_type(cursor);
    if (ctype == CMARK_NODE_PARAGRAPH) {
      if (paragraph_has_compact_definition(cursor)) {
        int term_col = cmark_node_get_start_column(cursor);
        if (term_col <= 0)
          term_col = 1;
        cmark_node *after = expand_dd_continuations(cmark_node_next(cursor),
                                                    term_col, mem, depth);
        while (paragraph_definition_position(after) != -1) {
          int col = cmark_node_get_start_column(after);
          int dd_col = col > 0 ? col : term_col;
          after = expand_dd_continuations(cmark_node_next(after), dd_col, mem,
                                          depth);
        }
        cursor = after;
        continue;
      }
      cmark_node *next = cmark_node_next(cursor);
      if (paragraph_definition_position(next) != -1) {
        int term_col = cmark_node_get_start_column(cursor);
        if (term_col <= 0)
          term_col = 1;
        cmark_node *def = next;
        while (paragraph_definition_position(def) != -1) {
          int col = cmark_node_get_start_column(def);
          int dd_col = col > 0 ? col : term_col;
          def =
              expand_dd_continuations(cmark_node_next(def), dd_col, mem, depth);
        }
        cursor = def;
        continue;
      }
    } else if (is_block_container(ctype) && depth < 16) {
      expand_definition_continuations(cursor, mem, depth + 1);
    }
    cursor = cmark_node_next(cursor);
  }
}

static void split_compact_definition_paragraph(cmark_node *paragraph,
                                               cmark_mem *mem) {
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

    cmark_node *definition = cmark_node_new_with_mem(CMARK_NODE_PARAGRAPH, mem);
    if (definition == NULL)
      return;
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

static void normalize_compact_definition_lists(cmark_node *container,
                                               cmark_mem *mem) {
  for (cmark_node *child = cmark_node_first_child(container); child != NULL;) {
    cmark_node *next = cmark_node_next(child);
    cmark_node_type type = cmark_node_get_type(child);
    if (type == CMARK_NODE_PARAGRAPH)
      split_compact_definition_paragraph(child, mem);
    else if (is_block_container(type))
      normalize_compact_definition_lists(child, mem);
    child = next;
  }
}

static void wrap_block(cmark_node *node, const char *start, const char *end,
                       cmark_mem *mem) {
  cmark_node *start_node = new_html_block(start, mem);
  cmark_node *end_node = new_html_block(end, mem);
  if (start_node != NULL)
    cmark_node_insert_before(node, start_node);
  if (end_node != NULL)
    cmark_node_insert_after(node, end_node);
}

static void rewrite_definition_lists(cmark_node *document, cmark_mem *mem);

static void rewrite_definition_lists_in(cmark_node *container, cmark_mem *mem) {
  cmark_node *cursor = cmark_node_first_child(container);
  while (cursor != NULL) {
    cmark_node *first_definition = cmark_node_next(cursor);
    cmark_node_type ctype = cmark_node_get_type(cursor);
    if (ctype != CMARK_NODE_PARAGRAPH ||
        paragraph_definition_position(first_definition) == -1) {
      if (is_block_container(ctype))
        rewrite_definition_lists_in(cursor, mem);
      cursor = cmark_node_next(cursor);
      continue;
    }

    cmark_node *open = new_html_block("<dl>", mem);
    if (open != NULL)
      cmark_node_insert_before(cursor, open);
    for (;;) {
      int term_col = cmark_node_get_start_column(cursor);
      if (term_col <= 0)
        term_col = 1;
      cmark_node *definition = cmark_node_next(cursor);
      wrap_block(cursor, "<dt>", "</dt>", mem);
      cursor = definition;

      while (paragraph_definition_position(cursor) != -1) {
        int col = cmark_node_get_start_column(cursor);
        int dd_col = col > 0 ? col : term_col;
        cmark_node *first_in_dd = cursor;
        cmark_node *last_in_dd = cursor;
        cmark_node *next = cmark_node_next(cursor);
        cmark_node *first_child = cmark_node_first_child(cursor);
        const char *literal = cmark_node_get_literal(first_child);
        int position = definition_position_for_text_node(first_child);
        const char *trimmed = literal + position;
        size_t trimmed_len = strlen(trimmed);
        char stack_copy[256];
        char *copy = trimmed_len < sizeof(stack_copy) ? stack_copy
                                                      : malloc(trimmed_len + 1);
        if (copy != NULL) {
          memcpy(copy, trimmed, trimmed_len + 1);
          cmark_node_set_literal(first_child, copy);
          if (copy != stack_copy)
            free(copy);
        }

        while (next != NULL) {
          if (is_dd_continuation_wrapper(next)) {
            cmark_node *after_wrap = cmark_node_next(next);
            rewrite_definition_lists(next, mem);
            cmark_node *child;
            while ((child = cmark_node_first_child(next)) != NULL) {
              cmark_node_unlink(child);
              cmark_node_insert_before(next, child);
              last_in_dd = child;
            }
            cmark_node_unlink(next);
            cmark_node_free(next);
            next = after_wrap;
          } else if (cmark_node_get_start_column(next) > dd_col) {
            if (is_block_container(cmark_node_get_type(next)))
              rewrite_definition_lists_in(next, mem);
            last_in_dd = next;
            next = cmark_node_next(next);
          } else {
            break;
          }
        }

        cmark_node *dd_open = new_html_block("<dd>", mem);
        cmark_node *dd_close = new_html_block("</dd>", mem);
        if (dd_open != NULL)
          cmark_node_insert_before(first_in_dd, dd_open);
        if (dd_close != NULL)
          cmark_node_insert_after(last_in_dd, dd_close);
        cursor = next;
      }

      if (cursor == NULL ||
          cmark_node_get_type(cursor) != CMARK_NODE_PARAGRAPH ||
          paragraph_definition_position(cmark_node_next(cursor)) == -1)
        break;
    }

    cmark_node *close = new_html_block("</dl>", mem);
    if (close != NULL) {
      if (cursor == NULL)
        cmark_node_append_child(container, close);
      else
        cmark_node_insert_before(cursor, close);
    }
  }
}

static void rewrite_definition_lists(cmark_node *document, cmark_mem *mem) {
  normalize_compact_definition_lists(document, mem);
  rewrite_definition_lists_in(document, mem);
}

/* ---- Link rewriting ---- */

static void replace_link_with_mem(cmark_node *node, const char *input_file_path,
                                  AnchorLocationArray *anchor_locations,
                                  render_type render_type,
                                  const char *multipage_url, cmark_mem *mem) {
  const char *url = cmark_node_get_url(node);
  if (url == NULL || url[0] != '#')
    return;

  if (render_type == RENDER_TYPE_SINGLE) {
    if (anchor_locations->used == 0)
      return;
    AnchorLocation *anchor_location =
        find_anchor_location(anchor_locations, url);
    if (anchor_location != NULL && anchor_location->title[0] != '\0' &&
        cmark_node_first_child(node) == NULL) {
      cmark_node *new_child = cmark_node_new_with_mem(CMARK_NODE_TEXT, mem);
      if (new_child != NULL) {
        cmark_node_set_literal(new_child, anchor_location->title);
        cmark_node_prepend_child(node, new_child);
      }
    }
    return;
  }

  AnchorLocation *anchor_location = find_anchor_location(anchor_locations, url);
  if (anchor_location == NULL) {
    printf("Anchor \"%s\" referenced in %s not found.\n", url, input_file_path);
    if (render_type == RENDER_TYPE_MULTIPAGE) {
      size_t ulen = strlen(multipage_url) + strlen(url) + 1;
      char *new_url = malloc(ulen);
      if (new_url != NULL) {
        snprintf(new_url, ulen, "%s%s", multipage_url, url);
        cmark_node_set_url(node, new_url);
        free(new_url);
      }
    }
    return;
  }
  if (render_type == RENDER_TYPE_MULTIPAGE) {
    size_t ulen = strlen(anchor_location->multipage_url) + strlen(url) + 1;
    char *new_url = malloc(ulen);
    if (new_url != NULL) {
      snprintf(new_url, ulen, "%s%s", anchor_location->multipage_url, url);
      cmark_node_set_url(node, new_url);
      free(new_url);
    }
  }
  if (anchor_location->title[0] != '\0' &&
      cmark_node_first_child(node) == NULL) {
    cmark_node *new_child = cmark_node_new_with_mem(CMARK_NODE_TEXT, mem);
    if (new_child != NULL) {
      cmark_node_set_literal(new_child, anchor_location->title);
      cmark_node_prepend_child(node, new_child);
    }
  }
}

/* ---- Fast AST plaintext extraction & JSON serialization ---- */

static int extract_plaintext(cmark_node *document, MmdocBuf *out) {
  cmark_iter *iter = cmark_iter_new(document);
  if (iter == NULL)
    return -1;
  cmark_event_type event;
  int failed = 0;

  while (!failed && (event = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
    if (event != CMARK_EVENT_ENTER)
      continue;
    cmark_node *node = cmark_iter_get_node(iter);
    cmark_node_type type = cmark_node_get_type(node);
    switch (type) {
    case CMARK_NODE_TEXT:
    case CMARK_NODE_CODE: {
      const char *lit = cmark_node_get_literal(node);
      if (lit != NULL)
        failed = mmdoc_buf_append_str(out, lit) != 0;
      break;
    }
    case CMARK_NODE_CODE_BLOCK: {
      if (out->len > 0 && out->data[out->len - 1] != '\n')
        failed = mmdoc_buf_append(out, "\n\n", 2) != 0;
      if (!failed) {
        const char *lit = cmark_node_get_literal(node);
        if (lit != NULL)
          failed = mmdoc_buf_append_str(out, lit) != 0;
      }
      break;
    }
    case CMARK_NODE_SOFTBREAK:
    case CMARK_NODE_LINEBREAK:
      if (out->len > 0 && out->data[out->len - 1] != '\n' &&
          out->data[out->len - 1] != ' ')
        failed = mmdoc_buf_append_char(out, ' ') != 0;
      break;
    case CMARK_NODE_HEADING:
    case CMARK_NODE_PARAGRAPH:
      if (out->len > 0 && out->data[out->len - 1] != '\n')
        failed = mmdoc_buf_append(out, "\n\n", 2) != 0;
      break;
    case CMARK_NODE_ITEM:
      if (out->len > 0 && out->data[out->len - 1] != '\n')
        failed = mmdoc_buf_append_char(out, '\n') != 0;
      break;
    default: {
      const char *ts = cmark_node_get_type_string(node);
      if (strcmp(ts, "table_row") == 0) {
        if (out->len > 0 && out->data[out->len - 1] != '\n')
          failed = mmdoc_buf_append_char(out, '\n') != 0;
      } else if (strcmp(ts, "table_cell") == 0) {
        if (out->len > 0 && out->data[out->len - 1] != '\n' &&
            out->data[out->len - 1] != ' ')
          failed = mmdoc_buf_append_char(out, ' ') != 0;
      }
      break;
    }
    }
  }
  cmark_iter_free(iter);
  return failed ? -1 : 0;
}

static int append_json_string(MmdocBuf *out, const char *str, size_t len) {
  static const char hex[] = "0123456789abcdef";
  if (mmdoc_buf_append_char(out, '"') != 0)
    return -1;

  size_t run_start = 0;
  for (size_t i = 0; i < len; i++) {
    unsigned char c = (unsigned char)str[i];
    const char *esc = NULL;
    char unicode_buf[7];
    size_t esc_len = 2;

    if (c == '"')
      esc = "\\\"";
    else if (c == '\\')
      esc = "\\\\";
    else if (c == '\b')
      esc = "\\b";
    else if (c == '\f')
      esc = "\\f";
    else if (c == '\n')
      esc = "\\n";
    else if (c == '\r')
      esc = "\\r";
    else if (c == '\t')
      esc = "\\t";
    else if (c < 0x20) {
      unicode_buf[0] = '\\';
      unicode_buf[1] = 'u';
      unicode_buf[2] = '0';
      unicode_buf[3] = '0';
      unicode_buf[4] = hex[c >> 4];
      unicode_buf[5] = hex[c & 0x0f];
      unicode_buf[6] = '\0';
      esc = unicode_buf;
      esc_len = 6;
    }

    if (esc != NULL) {
      if (i > run_start &&
          mmdoc_buf_append(out, str + run_start, i - run_start) != 0)
        return -1;
      if (mmdoc_buf_append(out, esc, esc_len) != 0)
        return -1;
      run_start = i + 1;
    }
  }
  if (len > run_start &&
      mmdoc_buf_append(out, str + run_start, len - run_start) != 0)
    return -1;
  return mmdoc_buf_append_char(out, '"');
}

static int append_search_json(MmdocBuf *out, const char *url, const char *title,
                              const char *text, size_t text_len) {
  if (mmdoc_buf_append_str(out, "{ \"url\": ") != 0 ||
      append_json_string(out, url, strlen(url)) != 0 ||
      mmdoc_buf_append_str(out, ", \"title\": ") != 0 ||
      append_json_string(out, title, strlen(title)) != 0 ||
      mmdoc_buf_append_str(out, ", \"text\": ") != 0 ||
      append_json_string(out, text, text_len) != 0 ||
      mmdoc_buf_append_str(out, " }") != 0)
    return -1;
  return 0;
}

void insert_search_index(FILE *search_index_path, const char *text,
                         const char *title, const char *multipage_url) {
  MmdocBuf json;
  mmdoc_buf_init(&json,
                 strlen(text) + strlen(title) + strlen(multipage_url) + 64);
  if (append_search_json(&json, multipage_url, title, text, strlen(text)) ==
      0) {
    fwrite(json.data, 1, json.len, search_index_path);
    fputs("\n,", search_index_path);
  }
  mmdoc_buf_free(&json);
}

/* ---- Syntax rewriting ---- */

static int cmark_rewrite_syntax_with_mem(cmark_node *document, cmark_mem *mem) {
  cmark_iter *iter = cmark_iter_new(document);
  int has_code_block = 0;

  cmark_event_type event;
  while ((event = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
    cmark_node *node = cmark_iter_get_node(iter);
    if (event == CMARK_EVENT_ENTER) {
      if (cmark_node_get_type(node) == CMARK_NODE_CODE_BLOCK)
        has_code_block = 1;
    } else if (event == CMARK_EVENT_EXIT) {
      replace_header_attributes(node, mem);
    }
  }
  cmark_iter_free(iter);
  return has_code_block;
}

int cmark_rewrite_syntax(cmark_node *document) {
  return cmark_rewrite_syntax_with_mem(document,
                                       cmark_get_default_mem_allocator());
}

static void cmark_rewrite_links_and_admonitions(
    cmark_node *document, cmark_mem *mem, const char *input_file_path,
    render_type render_type, const char *multipage_url,
    AnchorLocationArray *anchor_locations) {
  rewrite_admonitions_in(document, multipage_url, mem, NULL);

  cmark_iter *iter = cmark_iter_new(document);
  cmark_event_type event;
  while ((event = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
    if (event != CMARK_EVENT_ENTER)
      continue;
    cmark_node *node = cmark_iter_get_node(iter);
    if (cmark_node_get_type(node) == CMARK_NODE_LINK)
      replace_link_with_mem(node, input_file_path, anchor_locations,
                            render_type, multipage_url, mem);
  }
  cmark_iter_free(iter);
}

static void cmark_rewrite_html_with_mem(const char *multipage_url,
                                        cmark_node *document, cmark_mem *mem,
                                        AnchoredHeadingVec *track) {
  cmark_iter *iter = cmark_iter_new(document);
  cmark_event_type event;
  while ((event = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
    if (event != CMARK_EVENT_EXIT)
      continue;
    cmark_node *node = cmark_iter_get_node(iter);
    replace_header_for_html(multipage_url, node, mem, track);
  }
  cmark_iter_free(iter);
}

/* ---- Document parsing ---- */

cmark_node *mmdoc_parse_cmark_buffer(const char *data, size_t len,
                                     cmark_parser *parser) {
  cmark_syntax_extension *table_extension = mmdoc_table_extension();
  if (table_extension != NULL)
    cmark_parser_attach_syntax_extension(parser, table_extension);
  if (len > 0)
    cmark_parser_feed(parser, data, len);
  cmark_node *doc = cmark_parser_finish(parser);
  if (doc != NULL)
    expand_definition_continuations(doc, mmdoc_arena_current_cmark_mem(), 0);
  return doc;
}

cmark_node *mmdoc_render_cmark_document(char *file_path, cmark_parser *parser) {
  FILE *file = fopen(file_path, "rb");
  if (file == NULL)
    return NULL;

  cmark_syntax_extension *table_extension = mmdoc_table_extension();
  if (table_extension != NULL)
    cmark_parser_attach_syntax_extension(parser, table_extension);

  struct stat st;
  int failed = 0;
  if (fstat(fileno(file), &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0) {
    size_t file_size = (size_t)st.st_size;
    char stack_buf[65536];
    char *buf = file_size <= sizeof(stack_buf) ? stack_buf : malloc(file_size);
    if (buf != NULL) {
      size_t total = 0;
      while (total < file_size) {
        size_t n = fread(buf + total, 1, file_size - total, file);
        if (n == 0)
          break;
        total += n;
      }
      failed = ferror(file);
      if (!failed && total > 0)
        cmark_parser_feed(parser, buf, total);
      if (buf != stack_buf)
        free(buf);
    } else {
      failed = 1;
    }
  } else {
    char buffer[65536];
    size_t bytes;
    while ((bytes = fread(buffer, 1, sizeof(buffer), file)) > 0)
      cmark_parser_feed(parser, buffer, bytes);
    failed = ferror(file);
  }

  if (fclose(file) != 0)
    failed = 1;
  if (failed)
    return NULL;
  cmark_node *doc = cmark_parser_finish(parser);
  if (doc != NULL)
    expand_definition_continuations(doc, mmdoc_arena_current_cmark_mem(), 0);
  return doc;
}

/* ---- Metadata extraction (anchors + title) ---- */

static int collect_anchors_from_text(Array *anchors, const char *text,
                                     const char *file_path) {
  if (strchr(text, '{') == NULL)
    return 0;
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

static char *mmdoc_render_get_heading_title(cmark_node *heading) {
  cmark_node *last_child = cmark_node_last_child(heading);
  if (last_child == NULL || cmark_node_get_type(last_child) != CMARK_NODE_TEXT)
    return NULL;

  const char *last_literal = cmark_node_get_literal(last_child);
  if (strchr(last_literal, '{') == NULL)
    return NULL;

  size_t last_len = strlen(last_literal);
  char stack_id[256];
  char *id = last_len < sizeof(stack_id) ? stack_id : malloc(last_len + 1);
  if (id == NULL)
    return NULL;
  int attribute_position = parse_heading_bracketed_span_id(last_literal, id);
  if (id != stack_id)
    free(id);
  if (attribute_position == -1)
    return NULL;

  MmdocBuf title_buf;
  mmdoc_buf_init(&title_buf, 128);
  cmark_iter *iter = cmark_iter_new(heading);
  cmark_event_type event;
  int failed = 0;
  while (!failed && (event = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
    if (event != CMARK_EVENT_ENTER)
      continue;
    cmark_node *node = cmark_iter_get_node(iter);
    cmark_node_type type = cmark_node_get_type(node);
    if (type != CMARK_NODE_TEXT && type != CMARK_NODE_CODE)
      continue;
    const char *literal = cmark_node_get_literal(node);
    size_t literal_length =
        node == last_child ? (size_t)attribute_position : strlen(literal);
    failed = mmdoc_buf_append(&title_buf, literal, literal_length) != 0;
  }
  cmark_iter_free(iter);
  if (failed) {
    mmdoc_buf_free(&title_buf);
    return NULL;
  }
  if (title_buf.data == NULL)
    return calloc(1, 1);
  return title_buf.data;
}

int mmdoc_extract_metadata_from_doc(cmark_node *document, const char *file_path,
                                    Array *anchors, char **title) {
  if (title != NULL)
    *title = NULL;
  int failed = 0;
  cmark_iter *iter = cmark_iter_new(document);
  cmark_event_type event;
  while (!failed && (event = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
    if (event != CMARK_EVENT_ENTER)
      continue;
    cmark_node *node = cmark_iter_get_node(iter);
    cmark_node_type type = cmark_node_get_type(node);
    if (title != NULL && *title == NULL && type == CMARK_NODE_HEADER)
      *title = mmdoc_render_get_heading_title(node);
    if (anchors == NULL && title != NULL && *title != NULL)
      break;
    if (anchors != NULL && type == CMARK_NODE_TEXT)
      failed = collect_anchors_from_text(anchors, cmark_node_get_literal(node),
                                         file_path) != 0;
  }
  cmark_iter_free(iter);
  if (title != NULL) {
    if (failed) {
      free(*title);
      *title = NULL;
    } else if (*title == NULL) {
      *title = calloc(1, 1);
      if (*title == NULL)
        failed = 1;
    }
  }
  return failed ? -1 : 0;
}

int mmdoc_render_collect_metadata(char *file_path, Array *anchors,
                                  char **title) {
  if (title != NULL)
    *title = NULL;
  MmdocArena arena;
  mmdoc_arena_init(&arena, 65536);
  MmdocArena *prev_arena = mmdoc_arena_bind(&arena);
  cmark_mem *mem = mmdoc_arena_cmark_mem();

  cmark_parser *parser =
      cmark_parser_new_with_mem(mmdoc_render_cmark_options, mem);
  cmark_node *document =
      parser != NULL ? mmdoc_render_cmark_document(file_path, parser) : NULL;
  int result = -1;
  if (document != NULL)
    result =
        mmdoc_extract_metadata_from_doc(document, file_path, anchors, title);

  mmdoc_arena_bind(prev_arena);
  mmdoc_arena_destroy(&arena);
  return result;
}

int mmdoc_render_collect_anchors(char *file_path, Array *anchors) {
  return mmdoc_render_collect_metadata(file_path, anchors, NULL);
}

char *mmdoc_render_get_title_from_file(char *file_path) {
  char *title = NULL;
  if (mmdoc_render_collect_metadata(file_path, NULL, &title) != 0)
    return NULL;
  return title;
}

/* ---- Standalone single-file render (used by unit tests & TOC) ---- */

int mmdoc_render_part(char *file_path, FILE *output_file,
                      render_type render_type, AnchorLocation *anchor_location,
                      AnchorLocationArray anchor_locations, char *multipage_url,
                      FILE *search_index_path) {
  MmdocArena arena;
  mmdoc_arena_init(&arena, 65536);
  MmdocArena *prev_arena = mmdoc_arena_bind(&arena);
  cmark_mem *mem = mmdoc_arena_cmark_mem();

  cmark_parser *parser =
      cmark_parser_new_with_mem(mmdoc_render_cmark_options, mem);
  cmark_node *document =
      parser != NULL ? mmdoc_render_cmark_document(file_path, parser) : NULL;

  if (document == NULL) {
    mmdoc_arena_bind(prev_arena);
    mmdoc_arena_destroy(&arena);
    return -1;
  }

  int has_code_block = cmark_rewrite_syntax_with_mem(document, mem);
  cmark_rewrite_spans_internal(document, 0, mem, NULL);
  rewrite_definition_lists(document, mem);
  cmark_rewrite_links_and_admonitions(document, mem, file_path, render_type,
                                      multipage_url, &anchor_locations);

  int failed = 0;
  if (render_type != RENDER_TYPE_MAN) {
    if (search_index_path != NULL) {
      MmdocBuf plain;
      mmdoc_buf_init(&plain, 1024);
      if (extract_plaintext(document, &plain) == 0) {
        insert_search_index(search_index_path,
                            plain.data != NULL ? plain.data : "",
                            anchor_location->title, multipage_url);
      } else {
        failed = 1;
      }
      mmdoc_buf_free(&plain);
    }

    cmark_rewrite_html_with_mem(multipage_url, document, mem, NULL);

    if (!failed) {
      char *result = cmark_render_html_with_mem(
          document, mmdoc_render_cmark_options,
          cmark_parser_get_syntax_extensions(parser), mem);
      if (result == NULL || fputs(result, output_file) == EOF)
        failed = 1;
    }
  } else {
    char *result = cmark_render_man_with_mem(
        document, mmdoc_render_cmark_options, 240, mem);
    if (result == NULL || fputs(result, output_file) == EOF)
      failed = 1;
  }

  mmdoc_arena_bind(prev_arena);
  mmdoc_arena_destroy(&arena);

  if (failed || ferror(output_file) ||
      (search_index_path != NULL && ferror(search_index_path)))
    return -1;
  return has_code_block;
}

/* ---- Single-AST multi-output chapter renderer ---- */

static int has_relative_path(const char *url) {
  if (*url == '\0' || strchr("/?#", *url))
    return 0;
  if (!isalpha((unsigned char)*url))
    return 1;
  const char *p = url + 1;
  while (isalnum((unsigned char)*p) || (*p && strchr("+-.", *p)))
    p++;
  return *p != ':';
}

static char *md_chapter_url(const char *base, const AnchorLocation *chapter,
                            const char *fragment, cmark_mem *mem) {
  const char *dir = chapter->multipage_url;
  if (strcmp(dir, "./") == 0)
    dir = "";
  size_t len =
      strlen(base) + strlen(dir) + sizeof("index.md") + strlen(fragment);
  char *url = mem->calloc(1, len);
  if (url == NULL)
    return NULL;
  char *out = stpcpy(url, base);
  out = stpcpy(out, dir);
  out = stpcpy(out, "index.md");
  strcpy(out, fragment);
  return url;
}

static int url_is_simple_for_placeholder(const char *url) {
  if (url == NULL || url[0] == '\0')
    return 0;
  for (const unsigned char *p = (const unsigned char *)url; *p != '\0'; p++) {
    unsigned char c = *p;
    if (c <= 0x20 || c >= 0x7f)
      return 0;
    if (c == '(' || c == ')' || c == '<' || c == '>' || c == '\\' || c == '"' ||
        c == '\'')
      return 0;
  }
  return 1;
}

typedef struct {
  cmark_node *node;
  const char *orig_url;
  const char *full_url;
  const char *chap_url;
} UrlSub;

typedef struct {
  cmark_node *node;
  const char *orig_lit;
  cmark_node *marker;
} AdmonMarkerEdit;

typedef struct {
  cmark_node *node;
  const char *orig_url;
  AnchorLocation *target;
} InternalLinkRef;

static int expand_url_placeholders(const char *rendered, size_t rendered_len,
                                   const UrlSub *subs, size_t sub_count,
                                   MmdocBuf *full_out, MmdocBuf *chap_out) {
  const char *cursor = rendered;
  const char *end = rendered + rendered_len;
  size_t matched = 0;

  while (cursor < end) {
    const char *hit = strstr(cursor, "mmdocurl");
    if (hit == NULL)
      break;
    const char *num = hit + 8;
    if (!isdigit((unsigned char)*num))
      return -1;
    size_t idx = 0;
    while (isdigit((unsigned char)*num)) {
      idx = idx * 10 + (size_t)(*num - '0');
      num++;
    }
    if (strncmp(num, "end", 3) != 0 || idx != matched || idx >= sub_count)
      return -1;
    const char *after = num + 3;

    size_t prefix_len = (size_t)(hit - cursor);
    if (mmdoc_buf_append(full_out, cursor, prefix_len) != 0 ||
        mmdoc_buf_append_str(full_out, subs[idx].full_url) != 0 ||
        mmdoc_buf_append(chap_out, cursor, prefix_len) != 0 ||
        mmdoc_buf_append_str(chap_out, subs[idx].chap_url) != 0)
      return -1;

    cursor = after;
    matched++;
  }

  if (matched != sub_count)
    return -1;

  size_t tail_len = (size_t)(end - cursor);
  if (mmdoc_buf_append(full_out, cursor, tail_len) != 0 ||
      mmdoc_buf_append(chap_out, cursor, tail_len) != 0)
    return -1;
  return 0;
}

int mmdoc_render_chapter_from_doc(cmark_node *document, cmark_parser *parser,
                                  cmark_mem *mem, const AnchorLocation *chapter,
                                  AnchorLocationArray *anchors,
                                  AnchorLocationArray *chapters_by_source,
                                  ChapterOutputs *out) {
  memset(out, 0, sizeof(*out));
  int result = -1;

  SpanVec spans = {0};
  cmark_node **heading_markers = NULL;
  size_t heading_markers_used = 0, heading_markers_cap = 0;
  AdmonMarkerEdit *admon_edits = NULL;
  size_t admon_edits_used = 0, admon_edits_cap = 0;
  cmark_node **added_labels = NULL;
  size_t added_labels_used = 0, added_labels_cap = 0;
  UrlSub *url_subs = NULL;
  size_t url_subs_used = 0, url_subs_cap = 0;
  InternalLinkRef *internal_links = NULL;
  size_t internal_links_used = 0, internal_links_cap = 0;
  AnchoredHeadingVec anchored_headings = {0};
  AnchoredAdmonitionVec anchored_admonitions = {0};

  /* 1. Extract heading IDs and detect code blocks once. */
  out->has_code_block = cmark_rewrite_syntax_with_mem(document, mem);

  /* 2. Rewrite bracketed spans once (starting in Markdown form). */
  cmark_rewrite_spans_internal(document, 1, mem, &spans);

  /* 3. Prepare document for Markdown outputs (llms-full.txt & index.md). */
  int all_simple_urls = 1;
  cmark_iter *iter = cmark_iter_new(document);
  if (iter == NULL)
    goto cleanup;
  cmark_event_type event;
  while ((event = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
    if (event != CMARK_EVENT_ENTER)
      continue;
    cmark_node *node = cmark_iter_get_node(iter);
    cmark_node_type type = cmark_node_get_type(node);

    if (type == CMARK_NODE_HEADING) {
      HeadingInfo *info = cmark_node_get_user_data(node);
      if (info != NULL) {
        size_t alen = strlen(info->anchor);
        char stack_attr[256];
        char *attr =
            alen + 8 < sizeof(stack_attr) ? stack_attr : malloc(alen + 8);
        if (attr == NULL) {
          cmark_iter_free(iter);
          goto cleanup;
        }
        snprintf(attr, alen + 8, " {#%s}", info->anchor);
        cmark_node *marker =
            cmark_node_new_with_mem(CMARK_NODE_CUSTOM_INLINE, mem);
        if (marker != NULL) {
          cmark_node_set_on_enter(marker, attr);
          cmark_node_append_child(node, marker);
          if (heading_markers_used == heading_markers_cap) {
            size_t nc = heading_markers_cap == 0 ? 16 : heading_markers_cap * 2;
            cmark_node **tmp = realloc(heading_markers, nc * sizeof(*tmp));
            if (tmp == NULL) {
              if (attr != stack_attr)
                free(attr);
              cmark_iter_free(iter);
              goto cleanup;
            }
            heading_markers = tmp;
            heading_markers_cap = nc;
          }
          heading_markers[heading_markers_used++] = marker;
        }
        if (attr != stack_attr)
          free(attr);
      }
    } else if (type == CMARK_NODE_TEXT) {
      const char *literal = cmark_node_get_literal(node);
      const char *start = literal + strspn(literal, " ");
      if (strncmp(start, ":::", 3) == 0) {
        const char *attrs = start + 3 + strspn(start + 3, " ");
        const char *end = strchr(attrs, '}');
        if (*attrs == '{' && end != NULL && memchr(attrs, '#', end - attrs)) {
          char *orig_copy = mem_strdup(mem, literal);
          char *prefix = mem_strndup(mem, literal, (size_t)(end + 1 - literal));
          cmark_node *marker =
              cmark_node_new_with_mem(CMARK_NODE_CUSTOM_INLINE, mem);
          if (orig_copy == NULL || prefix == NULL || marker == NULL) {
            cmark_iter_free(iter);
            goto cleanup;
          }
          cmark_node_set_on_enter(marker, prefix);
          cmark_node_insert_before(node, marker);
          cmark_node_set_literal(node, end + 1);
          if (admon_edits_used == admon_edits_cap) {
            size_t nc = admon_edits_cap == 0 ? 8 : admon_edits_cap * 2;
            AdmonMarkerEdit *tmp = realloc(admon_edits, nc * sizeof(*tmp));
            if (tmp == NULL) {
              cmark_iter_free(iter);
              goto cleanup;
            }
            admon_edits = tmp;
            admon_edits_cap = nc;
          }
          admon_edits[admon_edits_used].node = node;
          admon_edits[admon_edits_used].orig_lit = orig_copy;
          admon_edits[admon_edits_used].marker = marker;
          admon_edits_used++;
        }
      }
    } else if (type == CMARK_NODE_LINK || type == CMARK_NODE_IMAGE) {
      const char *url = cmark_node_get_url(node);
      if (url == NULL)
        continue;
      const char *orig_url = NULL;
      const char *chap_url = NULL;

      if (*url == '#' && type == CMARK_NODE_LINK) {
        AnchorLocation *target = find_anchor_location(anchors, url);
        if (target != NULL) {
          AnchorLocation *page =
              find_anchor_location(chapters_by_source, target->file_path);
          if (page != NULL) {
            if (cmark_node_first_child(node) == NULL) {
              cmark_node *label = cmark_node_new_with_mem(CMARK_NODE_TEXT, mem);
              if (label != NULL) {
                cmark_node_set_literal(label,
                                       *page->title ? page->title : url + 1);
                cmark_node_append_child(node, label);
                if (added_labels_used == added_labels_cap) {
                  size_t nc = added_labels_cap == 0 ? 16 : added_labels_cap * 2;
                  cmark_node **tmp = realloc(added_labels, nc * sizeof(*tmp));
                  if (tmp == NULL) {
                    cmark_iter_free(iter);
                    goto cleanup;
                  }
                  added_labels = tmp;
                  added_labels_cap = nc;
                }
                added_labels[added_labels_used++] = label;
              }
            }
            if (strcmp(page->file_path, chapter->file_path) != 0) {
              orig_url = mem_strdup(mem, url);
              chap_url =
                  md_chapter_url(chapter->multipage_base_href, page, url, mem);
            }
          }
        }
      } else if (*chapter->multipage_base_href && has_relative_path(url)) {
        orig_url = mem_strdup(mem, url);
        size_t len = strlen(chapter->multipage_base_href) + strlen(url) + 1;
        char *rebased = mem->calloc(1, len);
        if (rebased != NULL) {
          snprintf(rebased, len, "%s%s", chapter->multipage_base_href, url);
          chap_url = rebased;
        }
      }

      if (chap_url != NULL && orig_url != NULL) {
        if (url_subs_used == url_subs_cap) {
          size_t nc = url_subs_cap == 0 ? 16 : url_subs_cap * 2;
          UrlSub *tmp = realloc(url_subs, nc * sizeof(*tmp));
          if (tmp == NULL) {
            cmark_iter_free(iter);
            goto cleanup;
          }
          url_subs = tmp;
          url_subs_cap = nc;
        }
        url_subs[url_subs_used].node = node;
        url_subs[url_subs_used].orig_url = orig_url;
        url_subs[url_subs_used].full_url = orig_url;
        url_subs[url_subs_used].chap_url = chap_url;
        url_subs_used++;
        if (!url_is_simple_for_placeholder(orig_url) ||
            !url_is_simple_for_placeholder(chap_url))
          all_simple_urls = 0;
      }
    }
  }
  cmark_iter_free(iter);

  /* Render CommonMark (single pass via placeholders when safe). */
  if (url_subs_used == 0) {
    char *md =
        cmark_render_commonmark_with_mem(document, CMARK_OPT_DEFAULT, 0, mem);
    if (md == NULL)
      goto cleanup;
    size_t md_len = strlen(md);
    if (mmdoc_buf_append(&out->full_md, md, md_len) != 0 ||
        mmdoc_buf_append(&out->chapter_md, md, md_len) != 0)
      goto cleanup;
  } else {
    int expanded = 0;
    if (all_simple_urls) {
      for (size_t i = 0; i < url_subs_used; i++) {
        char token[40];
        snprintf(token, sizeof(token), "mmdocurl%zuend", i);
        cmark_node_set_url(url_subs[i].node, token);
      }
      char *md =
          cmark_render_commonmark_with_mem(document, CMARK_OPT_DEFAULT, 0, mem);
      if (md == NULL)
        goto cleanup;
      size_t md_len = strlen(md);
      mmdoc_buf_init(&out->full_md, md_len + 256);
      mmdoc_buf_init(&out->chapter_md, md_len + 512);
      if (expand_url_placeholders(md, md_len, url_subs, url_subs_used,
                                  &out->full_md, &out->chapter_md) == 0) {
        expanded = 1;
      } else {
        mmdoc_buf_free(&out->full_md);
        mmdoc_buf_free(&out->chapter_md);
      }
    }
    if (!expanded) {
      for (size_t i = 0; i < url_subs_used; i++)
        cmark_node_set_url(url_subs[i].node, url_subs[i].full_url);
      char *full_md =
          cmark_render_commonmark_with_mem(document, CMARK_OPT_DEFAULT, 0, mem);
      if (full_md == NULL || mmdoc_buf_append_str(&out->full_md, full_md) != 0)
        goto cleanup;

      for (size_t i = 0; i < url_subs_used; i++)
        cmark_node_set_url(url_subs[i].node, url_subs[i].chap_url);
      char *chap_md =
          cmark_render_commonmark_with_mem(document, CMARK_OPT_DEFAULT, 0, mem);
      if (chap_md == NULL ||
          mmdoc_buf_append_str(&out->chapter_md, chap_md) != 0)
        goto cleanup;
    }
  }

  /* 4. Undo Markdown-only edits and convert span markers to HTML. */
  for (size_t i = 0; i < heading_markers_used; i++)
    cmark_node_unlink(heading_markers[i]);
  for (size_t i = 0; i < admon_edits_used; i++) {
    cmark_node_unlink(admon_edits[i].marker);
    cmark_node_set_literal(admon_edits[i].node, admon_edits[i].orig_lit);
  }
  for (size_t i = 0; i < added_labels_used; i++)
    cmark_node_unlink(added_labels[i]);
  for (size_t i = 0; i < url_subs_used; i++)
    cmark_node_set_url(url_subs[i].node, url_subs[i].orig_url);

  for (size_t i = 0; i < spans.used; i++) {
    cmark_node *open_html =
        cmark_node_new_with_mem(CMARK_NODE_HTML_INLINE, mem);
    cmark_node *close_html =
        cmark_node_new_with_mem(CMARK_NODE_HTML_INLINE, mem);
    char *escaped = html_escape_string(spans.items[i].id);
    if (open_html == NULL || close_html == NULL || escaped == NULL) {
      free(escaped);
      goto cleanup;
    }
    size_t esc_len = strlen(escaped);
    char stack_marker[256];
    char *marker = esc_len + 16 < sizeof(stack_marker) ? stack_marker
                                                       : malloc(esc_len + 16);
    if (marker == NULL) {
      free(escaped);
      goto cleanup;
    }
    snprintf(marker, esc_len + 16, "<span id='%s'>", escaped);
    cmark_node_set_literal(open_html, marker);
    cmark_node_set_literal(close_html, "</span>");
    if (marker != stack_marker)
      free(marker);
    free(escaped);
    cmark_node_replace(spans.items[i].open_node, open_html);
    cmark_node_replace(spans.items[i].close_node, close_html);
  }

  /* 5. Shared HTML rewrites: definition lists, admonitions, internal links. */
  rewrite_definition_lists(document, mem);
  rewrite_admonitions_in(document, "", mem, &anchored_admonitions);

  iter = cmark_iter_new(document);
  if (iter == NULL)
    goto cleanup;
  while ((event = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
    if (event != CMARK_EVENT_ENTER)
      continue;
    cmark_node *node = cmark_iter_get_node(iter);
    if (cmark_node_get_type(node) != CMARK_NODE_LINK)
      continue;
    const char *url = cmark_node_get_url(node);
    if (url == NULL || url[0] != '#')
      continue;
    AnchorLocation *target = find_anchor_location(anchors, url);
    if (target != NULL && target->title[0] != '\0' &&
        cmark_node_first_child(node) == NULL) {
      cmark_node *new_child = cmark_node_new_with_mem(CMARK_NODE_TEXT, mem);
      if (new_child != NULL) {
        cmark_node_set_literal(new_child, target->title);
        cmark_node_prepend_child(node, new_child);
      }
    }
    if (internal_links_used == internal_links_cap) {
      size_t nc = internal_links_cap == 0 ? 16 : internal_links_cap * 2;
      InternalLinkRef *tmp = realloc(internal_links, nc * sizeof(*tmp));
      if (tmp == NULL) {
        cmark_iter_free(iter);
        goto cleanup;
      }
      internal_links = tmp;
      internal_links_cap = nc;
    }
    internal_links[internal_links_used].node = node;
    internal_links[internal_links_used].orig_url = mem_strdup(mem, url);
    internal_links[internal_links_used].target = target;
    internal_links_used++;
  }
  cmark_iter_free(iter);

  /* 6. Fast search index plaintext + JSON BEFORE replacing headings. */
  MmdocBuf plain;
  mmdoc_buf_init(&plain, 1024);
  if (extract_plaintext(document, &plain) != 0 ||
      append_search_json(&out->search_json, chapter->multipage_url,
                         chapter->title, plain.data != NULL ? plain.data : "",
                         plain.len) != 0) {
    mmdoc_buf_free(&plain);
    goto cleanup;
  }
  mmdoc_buf_free(&plain);

  /* 7. Render Single-page HTML (headings with href='#id'). */
  cmark_rewrite_html_with_mem("", document, mem, &anchored_headings);
  cmark_llist *extensions = cmark_parser_get_syntax_extensions(parser);
  char *single_html = cmark_render_html_with_mem(
      document, mmdoc_render_cmark_options, extensions, mem);
  if (single_html == NULL ||
      mmdoc_buf_append_str(&out->single_html, single_html) != 0)
    goto cleanup;

  /* 8. Patch only internal links, anchored headings, and anchored admonitions
   * for Multi-page HTML, then render Multi-page HTML. */
  for (size_t i = 0; i < internal_links_used; i++) {
    InternalLinkRef *ref = &internal_links[i];
    if (ref->orig_url == NULL)
      continue;
    if (ref->target == NULL) {
      size_t wlen = strlen(ref->orig_url) + strlen(chapter->file_path) + 64;
      char *wbuf = malloc(wlen);
      if (wbuf != NULL) {
        int n =
            snprintf(wbuf, wlen, "Anchor \"%s\" referenced in %s not found.\n",
                     ref->orig_url, chapter->file_path);
        if (n > 0)
          mmdoc_buf_append(&out->warnings, wbuf, (size_t)n);
        free(wbuf);
      }
      size_t ulen = strlen(chapter->multipage_url) + strlen(ref->orig_url) + 1;
      char *new_url = malloc(ulen);
      if (new_url == NULL)
        goto cleanup;
      snprintf(new_url, ulen, "%s%s", chapter->multipage_url, ref->orig_url);
      cmark_node_set_url(ref->node, new_url);
      free(new_url);
    } else {
      size_t ulen =
          strlen(ref->target->multipage_url) + strlen(ref->orig_url) + 1;
      char *new_url = malloc(ulen);
      if (new_url == NULL)
        goto cleanup;
      snprintf(new_url, ulen, "%s%s", ref->target->multipage_url,
               ref->orig_url);
      cmark_node_set_url(ref->node, new_url);
      free(new_url);
    }
  }

  for (size_t i = 0; i < anchored_headings.used; i++) {
    AnchoredHeadingRef *h = &anchored_headings.items[i];
    char level[16];
    snprintf(level, sizeof(level), "%d", h->level);
    char *on_enter =
        format_heading_enter(level, chapter->multipage_url, h->escaped_anchor);
    if (on_enter == NULL)
      goto cleanup;
    cmark_node_set_on_enter(h->node, on_enter);
    free(on_enter);
  }

  for (size_t i = 0; i < anchored_admonitions.used; i++) {
    AnchoredAdmonitionRef *a = &anchored_admonitions.items[i];
    char *start = format_admonition_start(a->escaped_type, a->escaped_anchor,
                                          chapter->multipage_url);
    if (start == NULL)
      goto cleanup;
    cmark_node_set_literal(a->node, start);
    free(start);
  }

  char *multi_html = cmark_render_html_with_mem(
      document, mmdoc_render_cmark_options, extensions, mem);
  if (multi_html == NULL ||
      mmdoc_buf_append_str(&out->multi_html, multi_html) != 0)
    goto cleanup;

  result = 0;

cleanup:
  free(spans.items);
  free(heading_markers);
  free(admon_edits);
  free(added_labels);
  free(url_subs);
  free(internal_links);
  for (size_t i = 0; i < anchored_headings.used; i++)
    free(anchored_headings.items[i].escaped_anchor);
  free(anchored_headings.items);
  for (size_t i = 0; i < anchored_admonitions.used; i++) {
    free(anchored_admonitions.items[i].escaped_type);
    free(anchored_admonitions.items[i].escaped_anchor);
  }
  free(anchored_admonitions.items);
  if (result != 0)
    chapter_outputs_free(out);
  return result;
}
