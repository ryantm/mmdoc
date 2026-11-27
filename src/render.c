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

static inline int buf_append(MmdocBuf *buf, const char *data, size_t len) {
  if (__builtin_expect(buf->len + len >= buf->cap, 0)) {
    if (mmdoc_buf_reserve(buf, len) != 0)
      return -1;
  }
  if (len > 0)
    memcpy(buf->data + buf->len, data, len);
  buf->len += len;
  buf->data[buf->len] = '\0';
  return 0;
}

static inline int buf_append_str(MmdocBuf *buf, const char *str) {
  return buf_append(buf, str, strlen(str));
}

static inline int buf_append_char(MmdocBuf *buf, char c) {
  if (__builtin_expect(buf->len + 1 >= buf->cap, 0)) {
    if (mmdoc_buf_reserve(buf, 1) != 0)
      return -1;
  }
  buf->data[buf->len++] = c;
  buf->data[buf->len] = '\0';
  return 0;
}

int mmdoc_buf_append(MmdocBuf *buf, const char *data, size_t len) {
  return buf_append(buf, data, len);
}

int mmdoc_buf_append_str(MmdocBuf *buf, const char *str) {
  return buf_append_str(buf, str);
}

int mmdoc_buf_append_char(MmdocBuf *buf, char c) {
  return buf_append_char(buf, c);
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
  const char *raw_anchor;
} AnchoredHeadingRef;

typedef struct {
  AnchoredHeadingRef *items;
  size_t used;
  size_t cap;
} AnchoredHeadingVec;

static int push_anchored_heading(AnchoredHeadingVec *v, cmark_node *node,
                                 int level, const char *raw_anchor) {
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
  v->items[v->used].raw_anchor = raw_anchor;
  v->used++;
  return 0;
}

static int html_needs_escape(const char *s) {
  for (const unsigned char *p = (const unsigned char *)s; *p != '\0'; p++) {
    if (*p == '&' || *p == '<' || *p == '>' || *p == '\'' || *p == '"')
      return 1;
  }
  return 0;
}

static cmark_node *replace_header_for_html_node(const char *multipage_url,
                                                cmark_node *node,
                                                cmark_mem *mem,
                                                AnchoredHeadingVec *track) {
  if (cmark_node_get_type(node) != CMARK_NODE_HEADER)
    return NULL;

  HeadingInfo *info = cmark_node_get_user_data(node);
  if (info == NULL)
    return NULL;

  int hlevel = cmark_node_get_heading_level(node);
  char level[16];
  snprintf(level, sizeof(level), "%d", hlevel);

  const char *esc_ptr = info->anchor;
  char *escaped_alloc = NULL;
  if (html_needs_escape(info->anchor)) {
    escaped_alloc = html_escape_string(info->anchor);
    if (escaped_alloc == NULL)
      return NULL;
    esc_ptr = escaped_alloc;
  }

  size_t need =
      2 * strlen(esc_ptr) + strlen(multipage_url) + strlen(level) + 32;
  char stack_enter[512];
  char *on_enter = need <= sizeof(stack_enter) ? stack_enter : malloc(need);
  if (on_enter == NULL) {
    free(escaped_alloc);
    return NULL;
  }
  snprintf(on_enter, need, "<h%s id='%s'><a href='%s#%s'>", level, esc_ptr,
           multipage_url, esc_ptr);

  char on_exit[24];
  snprintf(on_exit, sizeof(on_exit), "</a></h%s>", level);

  cmark_node *new_node = cmark_node_new_with_mem(CMARK_NODE_CUSTOM_BLOCK, mem);
  if (new_node == NULL) {
    if (on_enter != stack_enter)
      free(on_enter);
    free(escaped_alloc);
    return NULL;
  }
  cmark_node_set_on_enter(new_node, on_enter);
  cmark_node_set_on_exit(new_node, on_exit);
  if (on_enter != stack_enter)
    free(on_enter);

  cmark_node_replace_with_children(node, new_node);
  const char *raw_anchor = info->anchor;
  if (track != NULL) {
    /* Transfer HeadingInfo ownership to new_node so raw_anchor stays valid
     * under both arena and default allocators without extra strdup/free. */
    cmark_node_set_user_data(new_node, info);
    cmark_node_set_user_data_free_func(new_node, free_heading_info);
    cmark_node_set_user_data(node, NULL);
    cmark_node_set_user_data_free_func(node, NULL);
  }
  cmark_node_free(node);
  free(escaped_alloc);

  if (track != NULL)
    push_anchored_heading(track, new_node, hlevel, raw_anchor);
  return new_node;
}

static int replace_header_for_html(const char *multipage_url, cmark_node *node,
                                   cmark_mem *mem, AnchoredHeadingVec *track) {
  return replace_header_for_html_node(multipage_url, node, mem, track) != NULL;
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
          const char *lit = cmark_node_get_literal(child);
          if (lit != NULL && (lit[0] == ':' || lit[0] == ' ')) {
            if (!replace_admonition_start_internal(multipage_url, child, mem,
                                                   track))
              replace_admonition_end_internal(child, mem);
          }
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

static inline int plaintext_visit_node(cmark_node *node, cmark_node_type type,
                                       MmdocBuf *out) {
  switch (type) {
  case CMARK_NODE_TEXT:
  case CMARK_NODE_CODE: {
    const char *lit = cmark_node_get_literal(node);
    if (lit != NULL)
      return buf_append_str(out, lit);
    return 0;
  }
  case CMARK_NODE_CODE_BLOCK: {
    if (out->len > 0 && out->data[out->len - 1] != '\n' &&
        buf_append(out, "\n\n", 2) != 0)
      return -1;
    const char *lit = cmark_node_get_literal(node);
    if (lit != NULL)
      return buf_append_str(out, lit);
    return 0;
  }
  case CMARK_NODE_SOFTBREAK:
  case CMARK_NODE_LINEBREAK:
    if (out->len > 0 && out->data[out->len - 1] != '\n' &&
        out->data[out->len - 1] != ' ')
      return buf_append_char(out, ' ');
    return 0;
  case CMARK_NODE_HEADING:
  case CMARK_NODE_PARAGRAPH:
    if (out->len > 0 && out->data[out->len - 1] != '\n')
      return buf_append(out, "\n\n", 2);
    return 0;
  case CMARK_NODE_ITEM:
    if (out->len > 0 && out->data[out->len - 1] != '\n')
      return buf_append_char(out, '\n');
    return 0;
  case CMARK_NODE_DOCUMENT:
  case CMARK_NODE_BLOCK_QUOTE:
  case CMARK_NODE_LIST:
  case CMARK_NODE_HTML_BLOCK:
  case CMARK_NODE_CUSTOM_BLOCK:
  case CMARK_NODE_THEMATIC_BREAK:
  case CMARK_NODE_HTML_INLINE:
  case CMARK_NODE_CUSTOM_INLINE:
  case CMARK_NODE_EMPH:
  case CMARK_NODE_STRONG:
  case CMARK_NODE_LINK:
  case CMARK_NODE_IMAGE:
    return 0;
  default: {
    const char *ts = cmark_node_get_type_string(node);
    if (strcmp(ts, "table_row") == 0) {
      if (out->len > 0 && out->data[out->len - 1] != '\n')
        return buf_append_char(out, '\n');
    } else if (strcmp(ts, "table_cell") == 0) {
      if (out->len > 0 && out->data[out->len - 1] != '\n' &&
          out->data[out->len - 1] != ' ')
        return buf_append_char(out, ' ');
    }
    return 0;
  }
  }
}

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
    failed = plaintext_visit_node(node, cmark_node_get_type(node), out) != 0;
  }
  cmark_iter_free(iter);
  return failed ? -1 : 0;
}

static const unsigned char json_escape_class[256] = {
    1, 1, 1, 1, 1, 1, 1, 1, 'b', 't', 'n', 1, 'f', 'r', 1, 1,   1,    1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1,   1,   1,   1, 1,   0,   0, '"', 0,    0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0,   0,   0,   0, 0,   0,   0, 0,   0,    0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0,   0,   0,   0, 0,   0,   0, 0,   0,    0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0,   0,   0,   0, 0,   0,   0, 0,   '\\',
};

static int append_json_string(MmdocBuf *out, const char *str, size_t len) {
  static const char hex[] = "0123456789abcdef";
  if (mmdoc_buf_reserve(out, len + 2) != 0)
    return -1;
  out->data[out->len++] = '"';

  size_t run_start = 0;
  for (size_t i = 0; i < len; i++) {
    unsigned char c = (unsigned char)str[i];
    unsigned char esc_code = json_escape_class[c];
    if (__builtin_expect(esc_code != 0, 0)) {
      if (i > run_start && buf_append(out, str + run_start, i - run_start) != 0)
        return -1;
      if (esc_code == 1) {
        char unicode_buf[6] = {'\\', 'u', '0', '0', hex[c >> 4], hex[c & 0x0f]};
        if (buf_append(out, unicode_buf, 6) != 0)
          return -1;
      } else {
        char pair[2] = {'\\', (char)esc_code};
        if (buf_append(out, pair, 2) != 0)
          return -1;
      }
      run_start = i + 1;
    }
  }
  if (len > run_start && buf_append(out, str + run_start, len - run_start) != 0)
    return -1;
  return buf_append_char(out, '"');
}

static int append_search_json(MmdocBuf *out, const char *url, const char *title,
                              const char *text, size_t text_len) {
  size_t url_len = strlen(url);
  size_t title_len = strlen(title);
  if (mmdoc_buf_reserve(out, url_len + title_len + text_len + (text_len >> 4) +
                                 64) != 0)
    return -1;
  if (mmdoc_buf_append(out, "{ \"url\": ", 9) != 0 ||
      append_json_string(out, url, url_len) != 0 ||
      mmdoc_buf_append(out, ", \"title\": ", 11) != 0 ||
      append_json_string(out, title, title_len) != 0 ||
      mmdoc_buf_append(out, ", \"text\": ", 10) != 0 ||
      append_json_string(out, text, text_len) != 0 ||
      mmdoc_buf_append(out, " }", 2) != 0)
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

static unsigned int scan_syntax_hints(const char *data, size_t len) {
  unsigned int hints = 0;
  const char *end = data + len;

  for (const char *p = data; p < end;) {
    p = memchr(p, ']', (size_t)(end - p));
    if (p == NULL)
      break;
    if (p + 2 < end && p[1] == '{' && p[2] == '#') {
      hints |= MMDOC_HINT_SPAN;
      break;
    }
    p++;
  }

  for (const char *p = data; p < end;) {
    p = memchr(p, ':', (size_t)(end - p));
    if (p == NULL)
      break;
    if (!(hints & MMDOC_HINT_ADMON) && p + 2 < end && p[1] == ':' &&
        p[2] == ':') {
      hints |= MMDOC_HINT_ADMON;
    } else if (!(hints & MMDOC_HINT_DEFLIST) && p + 1 < end &&
               (p[1] == ' ' || p[1] == '\t')) {
      const char *q = p;
      while (q > data && (q[-1] == ' ' || q[-1] == '\t' || q[-1] == '>'))
        q--;
      if (q == data || q[-1] == '\n' || q[-1] == '\r')
        hints |= MMDOC_HINT_DEFLIST;
    }
    if ((hints & (MMDOC_HINT_ADMON | MMDOC_HINT_DEFLIST)) ==
        (MMDOC_HINT_ADMON | MMDOC_HINT_DEFLIST))
      break;
    p++;
  }
  return hints;
}

cmark_node *mmdoc_parse_cmark_buffer(const char *data, size_t len,
                                     cmark_parser *parser) {
  cmark_syntax_extension *table_extension = mmdoc_table_extension();
  if (table_extension != NULL)
    cmark_parser_attach_syntax_extension(parser, table_extension);
  unsigned int hints = len > 0 ? scan_syntax_hints(data, len) : 0;
  if (len > 0)
    cmark_parser_feed(parser, data, len);
  cmark_node *doc = cmark_parser_finish(parser);
  if (doc != NULL && (hints & MMDOC_HINT_DEFLIST))
    expand_definition_continuations(doc, mmdoc_arena_current_cmark_mem(), 0);
  return doc;
}

cmark_node *mmdoc_render_cmark_document_ex(char *file_path,
                                           cmark_parser *parser,
                                           unsigned int *syntax_hints_out) {
  FILE *file = fopen(file_path, "rb");
  if (file == NULL)
    return NULL;

  cmark_syntax_extension *table_extension = mmdoc_table_extension();
  if (table_extension != NULL)
    cmark_parser_attach_syntax_extension(parser, table_extension);

  struct stat st;
  int failed = 0;
  unsigned int hints = MMDOC_HINT_ALL;
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
      if (!failed && total > 0) {
        hints = scan_syntax_hints(buf, total);
        cmark_parser_feed(parser, buf, total);
      } else if (!failed) {
        hints = 0;
      }
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
  if (syntax_hints_out != NULL)
    *syntax_hints_out = hints;
  cmark_node *doc = cmark_parser_finish(parser);
  if (doc != NULL && (hints & MMDOC_HINT_DEFLIST))
    expand_definition_continuations(doc, mmdoc_arena_current_cmark_mem(), 0);
  return doc;
}

cmark_node *mmdoc_render_cmark_document(char *file_path, cmark_parser *parser) {
  return mmdoc_render_cmark_document_ex(file_path, parser, NULL);
}

/* ---- Metadata extraction (anchors + title) ---- */

static int insert_anchor_with_mem(Array *a, const char *s, size_t len,
                                  cmark_mem *mem) {
  if (a->used == a->size) {
    size_t next_size = a->size == 0 ? 16 : a->size * 2;
    char **next = realloc(a->array, next_size * sizeof(*a->array));
    if (next == NULL)
      return -1;
    a->array = next;
    a->size = next_size;
  }
  char *copy = mem != NULL ? mem->calloc(1, len + 1) : malloc(len + 1);
  if (copy == NULL)
    return -1;
  memcpy(copy, s, len);
  copy[len] = '\0';
  a->array[a->used++] = copy;
  return 0;
}

static int collect_anchors_from_text_mem(Array *anchors, const char *text,
                                         const char *file_path,
                                         cmark_mem *mem) {
  const char *p = strchr(text, '{');
  if (p == NULL)
    return 0;
  for (; *p != '\0'; p++) {
    if (*p != '{')
      continue;
    const char *close = strchr(p + 1, '}');
    if (close == NULL)
      break;

    const char *hash = memchr(p + 1, '#', (size_t)(close - (p + 1)));
    if (hash == NULL) {
      p = close;
      continue;
    }

    const char *end = hash + 1;
    while (end < close && !isspace((unsigned char)*end))
      end++;
    size_t anchor_length = (size_t)(end - hash);
    if (anchor_length <= 1) {
      p = close;
      continue;
    }
    if (anchor_length >= 1024) {
      fprintf(stderr, "Anchor in %s exceeds 1023 bytes.\n", file_path);
      return -1;
    }
    if (insert_anchor_with_mem(anchors, hash, anchor_length, mem) != 0)
      return -1;
    p = close;
  }
  return 0;
}

static int collect_anchors_from_text(Array *anchors, const char *text,
                                     const char *file_path) {
  return collect_anchors_from_text_mem(anchors, text, file_path, NULL);
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

int mmdoc_prepare_parsed_doc(cmark_node *document, cmark_mem *mem,
                             const char *file_path, Array *anchors,
                             char **title, int *has_code_block_out) {
  if (title != NULL)
    *title = NULL;
  if (has_code_block_out != NULL)
    *has_code_block_out = 0;
  int failed = 0;
  cmark_iter *iter = cmark_iter_new(document);
  if (iter == NULL)
    return -1;
  cmark_event_type event;
  while (!failed && (event = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
    cmark_node *node = cmark_iter_get_node(iter);
    cmark_node_type type = cmark_node_get_type(node);
    if (event == CMARK_EVENT_ENTER) {
      if (type == CMARK_NODE_CODE_BLOCK) {
        if (has_code_block_out != NULL)
          *has_code_block_out = 1;
      } else if (title != NULL && *title == NULL && type == CMARK_NODE_HEADER) {
        *title = mmdoc_render_get_heading_title(node);
      } else if (anchors != NULL && type == CMARK_NODE_TEXT) {
        failed =
            collect_anchors_from_text_mem(anchors, cmark_node_get_literal(node),
                                          file_path, mem) != 0;
      }
    } else if (event == CMARK_EVENT_EXIT && type == CMARK_NODE_HEADER) {
      replace_header_attributes(node, mem);
    }
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

/* ---- Fast dual-output CommonMark emitter ---- */

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

typedef struct {
  size_t offset;
  size_t full_len;
  const char *chap_str;
  size_t chap_len;
} MdUrlPatch;

typedef struct {
  MmdocBuf *out;
  MmdocBuf prefix;
  const AnchorLocation *chapter;
  AnchorLocationArray *anchors;
  AnchorLocationArray *chapters_by_source;
  cmark_mem *mem;
  MdUrlPatch *patches;
  size_t patches_used;
  size_t patches_cap;
  int in_table_cell;
  int failed;
} MdWriter;

static const unsigned char md_text_escape[256] = {
    ['\\'] = 1, ['*'] = 1, ['_'] = 1, ['`'] = 1,
    ['['] = 1,  [']'] = 1, ['<'] = 1, ['>'] = 1,
};

static const unsigned char md_text_escape_cell[256] = {
    ['\\'] = 1, ['*'] = 1, ['_'] = 1, ['`'] = 1, ['['] = 1,
    [']'] = 1,  ['<'] = 1, ['>'] = 1, ['|'] = 1,
};

static int md_push_patch(MdWriter *w, size_t offset, size_t full_len,
                         const char *chap_str, size_t chap_len) {
  if (w->patches_used == w->patches_cap) {
    size_t nc = w->patches_cap == 0 ? 16 : w->patches_cap * 2;
    MdUrlPatch *tmp = realloc(w->patches, nc * sizeof(*tmp));
    if (tmp == NULL) {
      w->failed = 1;
      return -1;
    }
    w->patches = tmp;
    w->patches_cap = nc;
  }
  w->patches[w->patches_used].offset = offset;
  w->patches[w->patches_used].full_len = full_len;
  w->patches[w->patches_used].chap_str = chap_str;
  w->patches[w->patches_used].chap_len = chap_len;
  w->patches_used++;
  return 0;
}

static void md_emit_escaped_text(MdWriter *w, const char *text) {
  if (w->failed || text == NULL)
    return;
  const char *p = text;
  const char *run = text;
  const unsigned char *table =
      w->in_table_cell ? md_text_escape_cell : md_text_escape;
  while (*p != '\0') {
    unsigned char c = (unsigned char)*p;
    if (__builtin_expect(table[c], 0)) {
      if (p > run && buf_append(w->out, run, (size_t)(p - run)) != 0) {
        w->failed = 1;
        return;
      }
      char pair[2] = {'\\', (char)c};
      if (buf_append(w->out, pair, 2) != 0) {
        w->failed = 1;
        return;
      }
      run = p + 1;
    }
    p++;
  }
  if (p > run && buf_append(w->out, run, (size_t)(p - run)) != 0)
    w->failed = 1;
}

static void md_emit_newline_with_prefix(MdWriter *w) {
  if (w->failed)
    return;
  if (buf_append_char(w->out, '\n') != 0) {
    w->failed = 1;
    return;
  }
  if (w->prefix.len > 0 &&
      buf_append(w->out, w->prefix.data, w->prefix.len) != 0)
    w->failed = 1;
}

static void md_emit_blank_line(MdWriter *w) {
  if (w->failed)
    return;
  if (w->prefix.len == 0) {
    if (buf_append(w->out, "\n\n", 2) != 0)
      w->failed = 1;
    return;
  }
  size_t trimmed = w->prefix.len;
  while (trimmed > 0 && w->prefix.data[trimmed - 1] == ' ')
    trimmed--;
  if (buf_append_char(w->out, '\n') != 0 ||
      (trimmed > 0 && buf_append(w->out, w->prefix.data, trimmed) != 0) ||
      buf_append_char(w->out, '\n') != 0 ||
      buf_append(w->out, w->prefix.data, w->prefix.len) != 0)
    w->failed = 1;
}

static int url_needs_angle_brackets(const char *url) {
  if (url[0] == '\0')
    return 0;
  for (const unsigned char *p = (const unsigned char *)url; *p != '\0'; p++) {
    if (*p <= 0x20 || *p == '(' || *p == ')' || *p == '<' || *p == '>')
      return 1;
  }
  return 0;
}

static void md_format_url(MmdocBuf *dst, const char *url, int *failed) {
  if (!url_needs_angle_brackets(url)) {
    if (buf_append_str(dst, url) != 0)
      *failed = 1;
    return;
  }
  if (buf_append_char(dst, '<') != 0) {
    *failed = 1;
    return;
  }
  for (const char *p = url; *p != '\0'; p++) {
    if (*p == '<' || *p == '>' || *p == '\\') {
      char pair[2] = {'\\', *p};
      if (buf_append(dst, pair, 2) != 0) {
        *failed = 1;
        return;
      }
    } else if (buf_append_char(dst, *p) != 0) {
      *failed = 1;
      return;
    }
  }
  if (buf_append_char(dst, '>') != 0)
    *failed = 1;
}

static void md_render_inlines(MdWriter *w, cmark_node *parent);

static void md_render_inline(MdWriter *w, cmark_node *node) {
  if (w->failed)
    return;
  cmark_node_type type = cmark_node_get_type(node);
  switch (type) {
  case CMARK_NODE_TEXT:
    md_emit_escaped_text(w, cmark_node_get_literal(node));
    break;
  case CMARK_NODE_SOFTBREAK:
    if (w->in_table_cell) {
      if (buf_append_char(w->out, ' ') != 0)
        w->failed = 1;
    } else {
      md_emit_newline_with_prefix(w);
    }
    break;
  case CMARK_NODE_LINEBREAK:
    if (w->in_table_cell) {
      if (buf_append(w->out, "<br>", 4) != 0)
        w->failed = 1;
    } else {
      if (buf_append(w->out, "  ", 2) != 0) {
        w->failed = 1;
        return;
      }
      md_emit_newline_with_prefix(w);
    }
    break;
  case CMARK_NODE_CODE: {
    const char *lit = cmark_node_get_literal(node);
    if (lit == NULL)
      lit = "";
    size_t len = strlen(lit);
    size_t max_bt = 0;
    for (const char *p = lit; *p != '\0';) {
      if (*p == '`') {
        size_t run = 0;
        while (*p == '`') {
          run++;
          p++;
        }
        if (run > max_bt)
          max_bt = run;
      } else {
        p++;
      }
    }
    size_t ticks = max_bt + 1;
    int pad = len > 0 && (lit[0] == '`' || lit[len - 1] == '`' ||
                          (lit[0] == ' ' && lit[len - 1] == ' '));
    for (size_t i = 0; i < ticks; i++)
      if (buf_append_char(w->out, '`') != 0) {
        w->failed = 1;
        return;
      }
    if (pad && buf_append_char(w->out, ' ') != 0) {
      w->failed = 1;
      return;
    }
    if (len > 0 && buf_append(w->out, lit, len) != 0) {
      w->failed = 1;
      return;
    }
    if (pad && buf_append_char(w->out, ' ') != 0) {
      w->failed = 1;
      return;
    }
    for (size_t i = 0; i < ticks; i++)
      if (buf_append_char(w->out, '`') != 0) {
        w->failed = 1;
        return;
      }
    break;
  }
  case CMARK_NODE_EMPH:
    if (buf_append_char(w->out, '*') != 0) {
      w->failed = 1;
      return;
    }
    md_render_inlines(w, node);
    if (!w->failed && buf_append_char(w->out, '*') != 0)
      w->failed = 1;
    break;
  case CMARK_NODE_STRONG:
    if (buf_append(w->out, "**", 2) != 0) {
      w->failed = 1;
      return;
    }
    md_render_inlines(w, node);
    if (!w->failed && buf_append(w->out, "**", 2) != 0)
      w->failed = 1;
    break;
  case CMARK_NODE_HTML_INLINE: {
    const char *lit = cmark_node_get_literal(node);
    if (lit != NULL && buf_append_str(w->out, lit) != 0)
      w->failed = 1;
    break;
  }
  case CMARK_NODE_CUSTOM_INLINE: {
    const char *on_enter = cmark_node_get_on_enter(node);
    const char *on_exit = cmark_node_get_on_exit(node);
    if (on_enter != NULL && on_enter[0] != '\0' &&
        buf_append_str(w->out, on_enter) != 0) {
      w->failed = 1;
      return;
    }
    md_render_inlines(w, node);
    if (!w->failed && on_exit != NULL && on_exit[0] != '\0' &&
        buf_append_str(w->out, on_exit) != 0)
      w->failed = 1;
    break;
  }
  case CMARK_NODE_LINK:
  case CMARK_NODE_IMAGE: {
    const char *url = cmark_node_get_url(node);
    if (url == NULL)
      url = "";
    const char *title = cmark_node_get_title(node);
    const char *auto_label = NULL;
    const char *chap_url = NULL;

    if (*url == '#' && type == CMARK_NODE_LINK) {
      AnchorLocation *target = find_anchor_location(w->anchors, url);
      if (target != NULL) {
        AnchorLocation *page =
            find_anchor_location(w->chapters_by_source, target->file_path);
        if (page != NULL) {
          if (cmark_node_first_child(node) == NULL)
            auto_label = *page->title ? page->title : url + 1;
          if (strcmp(page->file_path, w->chapter->file_path) != 0)
            chap_url = md_chapter_url(w->chapter->multipage_base_href, page,
                                      url, w->mem);
        }
      }
    } else if (*w->chapter->multipage_base_href && has_relative_path(url)) {
      size_t blen = strlen(w->chapter->multipage_base_href);
      size_t ulen = strlen(url);
      char *rebased = w->mem->calloc(1, blen + ulen + 1);
      if (rebased != NULL) {
        memcpy(rebased, w->chapter->multipage_base_href, blen);
        memcpy(rebased + blen, url, ulen + 1);
        chap_url = rebased;
      }
    }

    if (type == CMARK_NODE_IMAGE && buf_append_char(w->out, '!') != 0) {
      w->failed = 1;
      return;
    }
    if (buf_append_char(w->out, '[') != 0) {
      w->failed = 1;
      return;
    }
    if (auto_label != NULL)
      md_emit_escaped_text(w, auto_label);
    else
      md_render_inlines(w, node);
    if (w->failed || buf_append(w->out, "](", 2) != 0) {
      w->failed = 1;
      return;
    }

    size_t url_off = w->out->len;
    md_format_url(w->out, url, &w->failed);
    if (w->failed)
      return;
    size_t full_url_len = w->out->len - url_off;

    if (chap_url != NULL) {
      if (!url_needs_angle_brackets(chap_url)) {
        md_push_patch(w, url_off, full_url_len, chap_url, strlen(chap_url));
      } else {
        MmdocBuf tmp = {0};
        md_format_url(&tmp, chap_url, &w->failed);
        if (!w->failed && tmp.data != NULL) {
          char *copy = mem_strndup(w->mem, tmp.data, tmp.len);
          if (copy != NULL)
            md_push_patch(w, url_off, full_url_len, copy, tmp.len);
          else
            w->failed = 1;
        }
        mmdoc_buf_free(&tmp);
      }
      if (w->failed)
        return;
    }

    if (title != NULL && title[0] != '\0') {
      if (buf_append(w->out, " \"", 2) != 0) {
        w->failed = 1;
        return;
      }
      for (const char *p = title; *p != '\0'; p++) {
        if (*p == '"' || *p == '\\') {
          char pair[2] = {'\\', *p};
          if (buf_append(w->out, pair, 2) != 0) {
            w->failed = 1;
            return;
          }
        } else if (buf_append_char(w->out, *p) != 0) {
          w->failed = 1;
          return;
        }
      }
      if (buf_append_char(w->out, '"') != 0) {
        w->failed = 1;
        return;
      }
    }
    if (buf_append_char(w->out, ')') != 0)
      w->failed = 1;
    break;
  }
  default:
    md_render_inlines(w, node);
    break;
  }
}

static void md_render_inlines(MdWriter *w, cmark_node *parent) {
  for (cmark_node *child = cmark_node_first_child(parent);
       !w->failed && child != NULL; child = cmark_node_next(child)) {
    md_render_inline(w, child);
  }
}

static void md_render_blocks(MdWriter *w, cmark_node *container, int tight);

static void md_render_block(MdWriter *w, cmark_node *node) {
  if (w->failed)
    return;
  cmark_node_type type = cmark_node_get_type(node);
  switch (type) {
  case CMARK_NODE_HEADING: {
    int level = cmark_node_get_heading_level(node);
    if (level < 1)
      level = 1;
    if (level > 6)
      level = 6;
    char hashes[8] = "###### ";
    if (buf_append(w->out, hashes + (6 - level), (size_t)level + 1) != 0) {
      w->failed = 1;
      return;
    }
    md_render_inlines(w, node);
    HeadingInfo *info = cmark_node_get_user_data(node);
    if (!w->failed && info != NULL && info->anchor != NULL) {
      if (buf_append(w->out, " {#", 3) != 0 ||
          buf_append_str(w->out, info->anchor) != 0 ||
          buf_append_char(w->out, '}') != 0)
        w->failed = 1;
    }
    break;
  }
  case CMARK_NODE_PARAGRAPH:
    md_render_inlines(w, node);
    break;
  case CMARK_NODE_THEMATIC_BREAK:
    if (buf_append(w->out, "---", 3) != 0)
      w->failed = 1;
    break;
  case CMARK_NODE_CODE_BLOCK: {
    const char *lit = cmark_node_get_literal(node);
    if (lit == NULL)
      lit = "";
    size_t len = strlen(lit);
    int flen = 0, foff = 0;
    char fch = 0;
    int fenced = cmark_node_get_fenced(node, &flen, &foff, &fch);
    const char *info = cmark_node_get_fence_info(node);

    if (fenced || (info != NULL && info[0] != '\0')) {
      size_t max_bt = 0;
      const char *p = lit;
      const char *end = lit + len;
      while (p < end) {
        p = memchr(p, '`', (size_t)(end - p));
        if (p == NULL)
          break;
        size_t run = 0;
        while (p < end && *p == '`') {
          run++;
          p++;
        }
        if (run > max_bt)
          max_bt = run;
      }
      size_t fence_len = max_bt >= 3 ? max_bt + 1 : 3;
      for (size_t i = 0; i < fence_len; i++)
        if (buf_append_char(w->out, '`') != 0) {
          w->failed = 1;
          return;
        }
      if (info != NULL && info[0] != '\0') {
        if (buf_append_char(w->out, ' ') != 0 ||
            buf_append_str(w->out, info) != 0) {
          w->failed = 1;
          return;
        }
      }
      if (buf_append_char(w->out, '\n') != 0) {
        w->failed = 1;
        return;
      }
      if (w->prefix.len == 0) {
        if (len > 0 && buf_append(w->out, lit, len) != 0) {
          w->failed = 1;
          return;
        }
        if (len > 0 && lit[len - 1] != '\n' &&
            buf_append_char(w->out, '\n') != 0) {
          w->failed = 1;
          return;
        }
      } else {
        const char *line = lit;
        while (line < end) {
          const char *nl = memchr(line, '\n', (size_t)(end - line));
          size_t llen =
              nl != NULL ? (size_t)(nl - line + 1) : (size_t)(end - line);
          if (buf_append(w->out, w->prefix.data, w->prefix.len) != 0 ||
              buf_append(w->out, line, llen) != 0) {
            w->failed = 1;
            return;
          }
          if (nl == NULL) {
            if (buf_append_char(w->out, '\n') != 0) {
              w->failed = 1;
              return;
            }
            break;
          }
          line = nl + 1;
        }
        if (buf_append(w->out, w->prefix.data, w->prefix.len) != 0) {
          w->failed = 1;
          return;
        }
      }
      for (size_t i = 0; i < fence_len; i++)
        if (buf_append_char(w->out, '`') != 0) {
          w->failed = 1;
          return;
        }
    } else {
      const char *line = lit;
      const char *end = lit + len;
      while (end > line && end[-1] == '\n')
        end--;
      int first = 1;
      while (line <= end) {
        const char *nl = memchr(line, '\n', (size_t)(end - line));
        size_t llen = nl != NULL ? (size_t)(nl - line) : (size_t)(end - line);
        if (!first)
          md_emit_newline_with_prefix(w);
        first = 0;
        if (llen > 0) {
          if (buf_append(w->out, "    ", 4) != 0 ||
              buf_append(w->out, line, llen) != 0) {
            w->failed = 1;
            return;
          }
        }
        if (nl == NULL)
          break;
        line = nl + 1;
      }
    }
    break;
  }
  case CMARK_NODE_HTML_BLOCK: {
    const char *lit = cmark_node_get_literal(node);
    if (lit == NULL)
      break;
    size_t len = strlen(lit);
    while (len > 0 && lit[len - 1] == '\n')
      len--;
    if (w->prefix.len == 0) {
      if (len > 0 && buf_append(w->out, lit, len) != 0)
        w->failed = 1;
    } else {
      const char *line = lit;
      const char *end = lit + len;
      int first = 1;
      while (line <= end) {
        const char *nl = memchr(line, '\n', (size_t)(end - line));
        size_t llen = nl != NULL ? (size_t)(nl - line) : (size_t)(end - line);
        if (!first)
          md_emit_newline_with_prefix(w);
        first = 0;
        if (llen > 0 && buf_append(w->out, line, llen) != 0) {
          w->failed = 1;
          return;
        }
        if (nl == NULL)
          break;
        line = nl + 1;
      }
    }
    break;
  }
  case CMARK_NODE_BLOCK_QUOTE: {
    size_t saved = w->prefix.len;
    if (buf_append(w->out, "> ", 2) != 0 ||
        buf_append(&w->prefix, "> ", 2) != 0) {
      w->failed = 1;
      return;
    }
    md_render_blocks(w, node, 0);
    w->prefix.len = saved;
    if (w->prefix.data != NULL)
      w->prefix.data[saved] = '\0';
    break;
  }
  case CMARK_NODE_LIST: {
    cmark_list_type ltype = cmark_node_get_list_type(node);
    int tight = cmark_node_get_list_tight(node);
    int num = cmark_node_get_list_start(node);
    cmark_delim_type delim = cmark_node_get_list_delim(node);
    char delim_ch = delim == CMARK_PAREN_DELIM ? ')' : '.';
    int first = 1;
    for (cmark_node *item = cmark_node_first_child(node);
         !w->failed && item != NULL; item = cmark_node_next(item)) {
      if (!first) {
        if (tight)
          md_emit_newline_with_prefix(w);
        else
          md_emit_blank_line(w);
      }
      first = 0;
      char marker[32];
      size_t mlen;
      if (ltype == CMARK_ORDERED_LIST) {
        mlen =
            (size_t)snprintf(marker, sizeof(marker), "%d%c ", num++, delim_ch);
      } else {
        marker[0] = '-';
        marker[1] = ' ';
        marker[2] = '\0';
        mlen = 2;
      }
      if (buf_append(w->out, marker, mlen) != 0) {
        w->failed = 1;
        return;
      }
      size_t saved = w->prefix.len;
      for (size_t i = 0; i < mlen; i++) {
        if (buf_append_char(&w->prefix, ' ') != 0) {
          w->failed = 1;
          return;
        }
      }
      md_render_blocks(w, item, tight);
      w->prefix.len = saved;
      if (w->prefix.data != NULL)
        w->prefix.data[saved] = '\0';
    }
    break;
  }
  case CMARK_NODE_CUSTOM_BLOCK: {
    const char *on_enter = cmark_node_get_on_enter(node);
    const char *on_exit = cmark_node_get_on_exit(node);
    if (on_enter != NULL && on_enter[0] != '\0' &&
        buf_append_str(w->out, on_enter) != 0) {
      w->failed = 1;
      return;
    }
    md_render_blocks(w, node, 0);
    if (!w->failed && on_exit != NULL && on_exit[0] != '\0' &&
        buf_append_str(w->out, on_exit) != 0)
      w->failed = 1;
    break;
  }
  default: {
    const char *ts = cmark_node_get_type_string(node);
    if (strcmp(ts, "table") == 0) {
      int row_idx = 0;
      for (cmark_node *row = cmark_node_first_child(node);
           !w->failed && row != NULL; row = cmark_node_next(row)) {
        if (row_idx > 0)
          md_emit_newline_with_prefix(w);
        size_t cols = 0;
        if (buf_append_char(w->out, '|') != 0) {
          w->failed = 1;
          return;
        }
        w->in_table_cell = 1;
        for (cmark_node *cell = cmark_node_first_child(row);
             !w->failed && cell != NULL; cell = cmark_node_next(cell)) {
          if (buf_append_char(w->out, ' ') != 0) {
            w->failed = 1;
            break;
          }
          md_render_inlines(w, cell);
          if (!w->failed && buf_append(w->out, " |", 2) != 0)
            w->failed = 1;
          cols++;
        }
        w->in_table_cell = 0;
        if (!w->failed && row_idx == 0 && cols > 0) {
          md_emit_newline_with_prefix(w);
          if (buf_append_char(w->out, '|') != 0) {
            w->failed = 1;
            return;
          }
          for (size_t c = 0; c < cols; c++) {
            if (buf_append(w->out, " --- |", 6) != 0) {
              w->failed = 1;
              return;
            }
          }
        }
        row_idx++;
      }
    }
    break;
  }
  }
}

static void md_render_blocks(MdWriter *w, cmark_node *container, int tight) {
  int first = 1;
  for (cmark_node *child = cmark_node_first_child(container);
       !w->failed && child != NULL; child = cmark_node_next(child)) {
    if (cmark_node_get_type(child) == CMARK_NODE_CUSTOM_BLOCK &&
        cmark_node_first_child(child) == NULL)
      continue;
    if (!first) {
      if (tight)
        md_emit_newline_with_prefix(w);
      else
        md_emit_blank_line(w);
    }
    first = 0;
    md_render_block(w, child);
  }
}

static int render_commonmark_dual(cmark_node *document, cmark_mem *mem,
                                  const AnchorLocation *chapter,
                                  AnchorLocationArray *anchors,
                                  AnchorLocationArray *chapters_by_source,
                                  size_t file_size_hint, MmdocBuf *full_out,
                                  MmdocBuf *chap_out) {
  MdWriter w = {
      .out = full_out,
      .prefix = {0},
      .chapter = chapter,
      .anchors = anchors,
      .chapters_by_source = chapters_by_source,
      .mem = mem,
      .patches = NULL,
      .patches_used = 0,
      .patches_cap = 0,
      .in_table_cell = 0,
      .failed = 0,
  };
  size_t init_cap =
      file_size_hint > 0 ? file_size_hint + (file_size_hint >> 3) + 256 : 4096;
  mmdoc_buf_init(full_out, init_cap);
  md_render_blocks(&w, document, 0);
  if (!w.failed && full_out->len > 0 && buf_append_char(full_out, '\n') != 0)
    w.failed = 1;

  if (!w.failed && w.patches_used > 0) {
    size_t est = full_out->len + w.patches_used * 32;
    mmdoc_buf_init(chap_out, est);
    size_t pos = 0;
    for (size_t i = 0; i < w.patches_used; i++) {
      MdUrlPatch *pt = &w.patches[i];
      if (pt->offset > pos &&
          buf_append(chap_out, full_out->data + pos, pt->offset - pos) != 0) {
        w.failed = 1;
        break;
      }
      if (pt->chap_len > 0 &&
          buf_append(chap_out, pt->chap_str, pt->chap_len) != 0) {
        w.failed = 1;
        break;
      }
      pos = pt->offset + pt->full_len;
    }
    if (!w.failed && pos < full_out->len &&
        buf_append(chap_out, full_out->data + pos, full_out->len - pos) != 0)
      w.failed = 1;
  }

  mmdoc_buf_free(&w.prefix);
  free(w.patches);
  return w.failed ? -1 : 0;
}

/* ---- Single-AST multi-output chapter renderer ---- */

typedef struct {
  cmark_node *node;
  const char *orig_url;
  AnchorLocation *target;
} InternalLinkRef;

typedef struct {
  const char *multi_prefix;
  size_t multi_prefix_len;
} HtmlPrefixSub;

typedef struct {
  HtmlPrefixSub *items;
  size_t used;
  size_t cap;
  size_t extra_bytes;
} HtmlPrefixSubVec;

static int push_html_prefix_sub(HtmlPrefixSubVec *v, const char *multi_prefix,
                                size_t *out_idx) {
  if (v->used == v->cap) {
    size_t nc = v->cap == 0 ? 32 : v->cap * 2;
    HtmlPrefixSub *tmp = realloc(v->items, nc * sizeof(*tmp));
    if (tmp == NULL)
      return -1;
    v->items = tmp;
    v->cap = nc;
  }
  size_t len = strlen(multi_prefix);
  *out_idx = v->used;
  v->items[v->used].multi_prefix = multi_prefix;
  v->items[v->used].multi_prefix_len = len;
  v->used++;
  v->extra_bytes += len;
  return 0;
}

static int expand_html_placeholders(const char *rendered, size_t rendered_len,
                                    const HtmlPrefixSubVec *subs,
                                    MmdocBuf *single_out, MmdocBuf *multi_out) {
  unsigned char stack_seen[256];
  unsigned char *seen =
      subs->used <= sizeof(stack_seen) ? stack_seen : calloc(subs->used, 1);
  if (seen == NULL)
    return -1;
  if (seen == stack_seen)
    memset(seen, 0, subs->used);

  mmdoc_buf_init(single_out, rendered_len + 16);
  mmdoc_buf_init(multi_out, rendered_len + subs->extra_bytes + 16);

  const char *cursor = rendered;
  const char *end = rendered + rendered_len;
  size_t matched = 0;
  int failed = 0;

  while (cursor < end) {
    const char *hit = strstr(cursor, "mmdochref");
    if (hit == NULL)
      break;
    const char *num = hit + 9;
    if (!isdigit((unsigned char)*num)) {
      failed = 1;
      break;
    }
    size_t idx = 0;
    while (isdigit((unsigned char)*num)) {
      idx = idx * 10 + (size_t)(*num - '0');
      num++;
    }
    if (strncmp(num, "end", 3) != 0 || idx >= subs->used || seen[idx]) {
      failed = 1;
      break;
    }
    seen[idx] = 1;
    const char *after = num + 3;

    size_t chunk_len = (size_t)(hit - cursor);
    if (chunk_len > 0 && (buf_append(single_out, cursor, chunk_len) != 0 ||
                          buf_append(multi_out, cursor, chunk_len) != 0)) {
      failed = 1;
      break;
    }
    if (subs->items[idx].multi_prefix_len > 0 &&
        buf_append(multi_out, subs->items[idx].multi_prefix,
                   subs->items[idx].multi_prefix_len) != 0) {
      failed = 1;
      break;
    }
    cursor = after;
    matched++;
  }

  if (!failed && matched != subs->used)
    failed = 1;

  if (!failed && cursor < end) {
    size_t tail_len = (size_t)(end - cursor);
    if (buf_append(single_out, cursor, tail_len) != 0 ||
        buf_append(multi_out, cursor, tail_len) != 0)
      failed = 1;
  }

  if (seen != stack_seen)
    free(seen);
  if (failed) {
    mmdoc_buf_free(single_out);
    mmdoc_buf_free(multi_out);
    return -1;
  }
  return 0;
}

int mmdoc_render_chapter_from_doc_ex(
    cmark_node *document, cmark_parser *parser, cmark_mem *mem,
    const AnchorLocation *chapter, AnchorLocationArray *anchors,
    AnchorLocationArray *chapters_by_source, unsigned int syntax_hints,
    int already_prepared, int pre_has_code_block, size_t file_size_hint,
    MmdocBuf *scratch_plain, ChapterOutputs *out) {
  memset(out, 0, sizeof(*out));
  int result = -1;

  SpanVec spans = {0};
  InternalLinkRef *internal_links = NULL;
  size_t internal_links_used = 0, internal_links_cap = 0;
  AnchoredHeadingVec anchored_headings = {0};
  AnchoredAdmonitionVec anchored_admonitions = {0};
  HtmlPrefixSubVec html_subs = {0};

  /* 1. Pass 1: if already prepared in Stage A, only rewrite spans when hinted;
   * otherwise fuse heading IDs, code block detection, and spans in one walk. */
  if (already_prepared) {
    out->has_code_block = pre_has_code_block;
    if (syntax_hints & MMDOC_HINT_SPAN)
      cmark_rewrite_spans_internal(document, 1, mem, &spans);
  } else {
    int scan_spans = (syntax_hints & MMDOC_HINT_SPAN) != 0;
    cmark_iter *iter = cmark_iter_new(document);
    if (iter == NULL)
      goto cleanup;
    cmark_event_type event = cmark_iter_next(iter);
    while (event != CMARK_EVENT_DONE) {
      cmark_node *node = cmark_iter_get_node(iter);
      cmark_node_type type = cmark_node_get_type(node);
      if (event == CMARK_EVENT_ENTER) {
        if (type == CMARK_NODE_CODE_BLOCK) {
          out->has_code_block = 1;
        } else if (scan_spans) {
          cmark_node *parent = cmark_node_parent(node);
          if (type == CMARK_NODE_CODE &&
              replace_code_link_bracket_with_span(node, 1, mem, &spans)) {
            cmark_iter_reset(iter, node, CMARK_EVENT_ENTER);
          } else if (type == CMARK_NODE_TEXT) {
            cmark_node *resume =
                replace_link_bracket_with_span(node, 1, mem, &spans);
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
      } else if (event == CMARK_EVENT_EXIT && type == CMARK_NODE_HEADER) {
        replace_header_attributes(node, mem);
      }
      event = cmark_iter_next(iter);
    }
    cmark_iter_free(iter);
  }

  /* 2. Fast direct AST -> dual CommonMark emission (llms-full.txt &
   * chapter/index.md) with zero AST mutation or undo pass. */
  if (render_commonmark_dual(document, mem, chapter, anchors,
                             chapters_by_source, file_size_hint, &out->full_md,
                             &out->chapter_md) != 0)
    goto cleanup;

  /* 3. Convert span markers from Markdown to HTML. */
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

  /* 4. Conditional definition lists and admonitions. */
  if (syntax_hints & MMDOC_HINT_DEFLIST)
    rewrite_definition_lists(document, mem);
  if (syntax_hints & MMDOC_HINT_ADMON) {
    rewrite_admonitions_in(document, "", mem, &anchored_admonitions);
    for (size_t i = 0; i < anchored_admonitions.used; i++) {
      AnchoredAdmonitionRef *a = &anchored_admonitions.items[i];
      size_t sub_idx = 0;
      if (push_html_prefix_sub(&html_subs, chapter->multipage_url, &sub_idx) !=
          0)
        goto cleanup;
      char token[40];
      snprintf(token, sizeof(token), "mmdochref%zuend", sub_idx);
      char *start =
          format_admonition_start(a->escaped_type, a->escaped_anchor, token);
      if (start == NULL)
        goto cleanup;
      cmark_node_set_literal(a->node, start);
      free(start);
    }
  }

  /* 5. Fused Pass 3: resolve internal links, extract search plaintext, and
   * rewrite anchored headings using single-pass placeholder prefixes. */
  MmdocBuf local_plain = {0};
  MmdocBuf *plain = scratch_plain != NULL ? scratch_plain : &local_plain;
  if (plain->cap == 0)
    mmdoc_buf_init(plain, file_size_hint > 0 ? file_size_hint + 256 : 1024);
  else
    plain->len = 0;
  {
    cmark_iter *iter = cmark_iter_new(document);
    if (iter == NULL) {
      mmdoc_buf_free(&local_plain);
      goto cleanup;
    }
    cmark_event_type event;
    int plain_failed = 0;
    while (!plain_failed &&
           (event = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
      cmark_node *node = cmark_iter_get_node(iter);
      cmark_node_type type = cmark_node_get_type(node);
      if (event == CMARK_EVENT_ENTER) {
        if (type == CMARK_NODE_LINK) {
          const char *url = cmark_node_get_url(node);
          if (url != NULL && url[0] == '#') {
            AnchorLocation *target = find_anchor_location(anchors, url);
            if (target != NULL && target->title[0] != '\0' &&
                cmark_node_first_child(node) == NULL) {
              cmark_node *new_child =
                  cmark_node_new_with_mem(CMARK_NODE_TEXT, mem);
              if (new_child != NULL) {
                cmark_node_set_literal(new_child, target->title);
                cmark_node_prepend_child(node, new_child);
                if (buf_append_str(plain, target->title) != 0) {
                  plain_failed = 1;
                  break;
                }
              }
            }
            if (target == NULL) {
              size_t wlen = strlen(url) + strlen(chapter->file_path) + 64;
              char *wbuf = malloc(wlen);
              if (wbuf != NULL) {
                int n = snprintf(wbuf, wlen,
                                 "Anchor \"%s\" referenced in %s not found.\n",
                                 url, chapter->file_path);
                if (n > 0)
                  buf_append(&out->warnings, wbuf, (size_t)n);
                free(wbuf);
              }
            }
            if (internal_links_used == internal_links_cap) {
              size_t nc = internal_links_cap == 0 ? 16 : internal_links_cap * 2;
              InternalLinkRef *tmp = realloc(internal_links, nc * sizeof(*tmp));
              if (tmp == NULL) {
                plain_failed = 1;
                break;
              }
              internal_links = tmp;
              internal_links_cap = nc;
            }
            char *orig_copy = mem_strdup(mem, url);
            internal_links[internal_links_used].node = node;
            internal_links[internal_links_used].orig_url = orig_copy;
            internal_links[internal_links_used].target = target;
            internal_links_used++;

            const char *multi_prefix =
                target != NULL ? target->multipage_url : chapter->multipage_url;
            size_t sub_idx = 0;
            if (push_html_prefix_sub(&html_subs, multi_prefix, &sub_idx) != 0) {
              plain_failed = 1;
              break;
            }
            size_t ulen = strlen(url);
            char stack_tok[256];
            char *tok_url =
                ulen + 40 < sizeof(stack_tok) ? stack_tok : malloc(ulen + 40);
            if (tok_url == NULL) {
              plain_failed = 1;
              break;
            }
            snprintf(tok_url, ulen + 40, "mmdochref%zuend%s", sub_idx, url);
            cmark_node_set_url(node, tok_url);
            if (tok_url != stack_tok)
              free(tok_url);
          }
        } else {
          if (plaintext_visit_node(node, type, plain) != 0)
            plain_failed = 1;
        }
      } else if (event == CMARK_EVENT_EXIT && type == CMARK_NODE_HEADER) {
        HeadingInfo *info = cmark_node_get_user_data(node);
        if (info != NULL) {
          size_t sub_idx = 0;
          if (push_html_prefix_sub(&html_subs, chapter->multipage_url,
                                   &sub_idx) != 0) {
            plain_failed = 1;
            break;
          }
          char token[40];
          snprintf(token, sizeof(token), "mmdochref%zuend", sub_idx);
          cmark_node *new_node = replace_header_for_html_node(
              token, node, mem, &anchored_headings);
          if (new_node != NULL)
            cmark_iter_reset(iter, new_node, CMARK_EVENT_EXIT);
        }
      }
    }
    cmark_iter_free(iter);
    if (plain_failed ||
        append_search_json(
            &out->search_json, chapter->multipage_url, chapter->title,
            plain->data != NULL ? plain->data : "", plain->len) != 0) {
      mmdoc_buf_free(&local_plain);
      goto cleanup;
    }
  }
  mmdoc_buf_free(&local_plain);

  /* 6. Single-pass HTML render + dual expansion into single_html & multi_html
   * (with automatic 2-pass fallback if a literal collision ever occurs). */
  cmark_llist *extensions = cmark_parser_get_syntax_extensions(parser);
  char *rendered_html = cmark_render_html_with_mem(
      document, mmdoc_render_cmark_options, extensions, mem);
  if (rendered_html == NULL)
    goto cleanup;
  size_t rendered_len = strlen(rendered_html);

  if (html_subs.used == 0) {
    if (buf_append(&out->single_html, rendered_html, rendered_len) != 0)
      goto cleanup;
  } else if (expand_html_placeholders(rendered_html, rendered_len, &html_subs,
                                      &out->single_html,
                                      &out->multi_html) != 0) {
    /* Fallback: render single-page and multi-page HTML separately. */
    for (size_t i = 0; i < internal_links_used; i++) {
      if (internal_links[i].orig_url != NULL)
        cmark_node_set_url(internal_links[i].node, internal_links[i].orig_url);
    }
    for (size_t i = 0; i < anchored_headings.used; i++) {
      AnchoredHeadingRef *h = &anchored_headings.items[i];
      char level[16];
      snprintf(level, sizeof(level), "%d", h->level);
      char *esc = html_escape_string(h->raw_anchor);
      if (esc == NULL)
        goto cleanup;
      char *on_enter = format_heading_enter(level, "", esc);
      free(esc);
      if (on_enter == NULL)
        goto cleanup;
      cmark_node_set_on_enter(h->node, on_enter);
      free(on_enter);
    }
    for (size_t i = 0; i < anchored_admonitions.used; i++) {
      AnchoredAdmonitionRef *a = &anchored_admonitions.items[i];
      char *start =
          format_admonition_start(a->escaped_type, a->escaped_anchor, "");
      if (start == NULL)
        goto cleanup;
      cmark_node_set_literal(a->node, start);
      free(start);
    }
    char *single_html = cmark_render_html_with_mem(
        document, mmdoc_render_cmark_options, extensions, mem);
    if (single_html == NULL ||
        buf_append_str(&out->single_html, single_html) != 0)
      goto cleanup;

    for (size_t i = 0; i < internal_links_used; i++) {
      InternalLinkRef *ref = &internal_links[i];
      if (ref->orig_url == NULL)
        continue;
      const char *pfx = ref->target != NULL ? ref->target->multipage_url
                                            : chapter->multipage_url;
      size_t ulen = strlen(pfx) + strlen(ref->orig_url) + 1;
      char *new_url = malloc(ulen);
      if (new_url == NULL)
        goto cleanup;
      snprintf(new_url, ulen, "%s%s", pfx, ref->orig_url);
      cmark_node_set_url(ref->node, new_url);
      free(new_url);
    }
    for (size_t i = 0; i < anchored_headings.used; i++) {
      AnchoredHeadingRef *h = &anchored_headings.items[i];
      char level[16];
      snprintf(level, sizeof(level), "%d", h->level);
      char *esc = html_escape_string(h->raw_anchor);
      if (esc == NULL)
        goto cleanup;
      char *on_enter = format_heading_enter(level, chapter->multipage_url, esc);
      free(esc);
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
    if (multi_html == NULL || buf_append_str(&out->multi_html, multi_html) != 0)
      goto cleanup;
  }

  result = 0;

cleanup:
  free(spans.items);
  free(internal_links);
  free(html_subs.items);
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

int mmdoc_render_chapter_from_doc(cmark_node *document, cmark_parser *parser,
                                  cmark_mem *mem, const AnchorLocation *chapter,
                                  AnchorLocationArray *anchors,
                                  AnchorLocationArray *chapters_by_source,
                                  ChapterOutputs *out) {
  return mmdoc_render_chapter_from_doc_ex(document, parser, mem, chapter,
                                          anchors, chapters_by_source,
                                          MMDOC_HINT_ALL, 0, 0, 0, NULL, out);
}
