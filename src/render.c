/* SPDX-License-Identifier: CC0-1.0 */
#include "render.h"
#include "html.h"
#include "md.h"
#include "types.h"
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/* ---- Fast LUT HTML & URL escaping ---- */

static const unsigned char html_esc_lut[256] = {
    ['&'] = 1,
    ['<'] = 2,
    ['>'] = 3,
    ['"'] = 4,
};

static const unsigned char html_attr_sq_lut[256] = {
    ['&'] = 1, ['<'] = 2, ['>'] = 3, ['"'] = 4, ['\''] = 5,
};

static int buf_append_html_escaped(MmdocBuf *out, const char *s, size_t len,
                                   int esc_sq) {
  const unsigned char *lut = esc_sq ? html_attr_sq_lut : html_esc_lut;
  size_t run = 0;
  for (size_t i = 0; i < len; i++) {
    unsigned char k = lut[(unsigned char)s[i]];
    if (__builtin_expect(k != 0, 0)) {
      if (i > run && buf_append(out, s + run, i - run) != 0)
        return -1;
      switch (k) {
      case 1:
        if (buf_append(out, "&amp;", 5) != 0)
          return -1;
        break;
      case 2:
        if (buf_append(out, "&lt;", 4) != 0)
          return -1;
        break;
      case 3:
        if (buf_append(out, "&gt;", 4) != 0)
          return -1;
        break;
      case 4:
        if (buf_append(out, "&quot;", 6) != 0)
          return -1;
        break;
      case 5:
        if (buf_append(out, "&#39;", 5) != 0)
          return -1;
        break;
      }
      run = i + 1;
    }
  }
  if (len > run && buf_append(out, s + run, len - run) != 0)
    return -1;
  return 0;
}

static const unsigned char href_safe_lut[256] = {
    ['!'] = 1, ['#'] = 1, ['$'] = 1, ['%'] = 1, ['&'] = 2, ['\''] = 1,
    ['('] = 1, [')'] = 1, ['*'] = 1, ['+'] = 1, [','] = 1, ['-'] = 1,
    ['.'] = 1, ['/'] = 1, ['0'] = 1, ['1'] = 1, ['2'] = 1, ['3'] = 1,
    ['4'] = 1, ['5'] = 1, ['6'] = 1, ['7'] = 1, ['8'] = 1, ['9'] = 1,
    [':'] = 1, [';'] = 1, ['='] = 1, ['?'] = 1, ['@'] = 1, ['A'] = 1,
    ['B'] = 1, ['C'] = 1, ['D'] = 1, ['E'] = 1, ['F'] = 1, ['G'] = 1,
    ['H'] = 1, ['I'] = 1, ['J'] = 1, ['K'] = 1, ['L'] = 1, ['M'] = 1,
    ['N'] = 1, ['O'] = 1, ['P'] = 1, ['Q'] = 1, ['R'] = 1, ['S'] = 1,
    ['T'] = 1, ['U'] = 1, ['V'] = 1, ['W'] = 1, ['X'] = 1, ['Y'] = 1,
    ['Z'] = 1, ['_'] = 1, ['a'] = 1, ['b'] = 1, ['c'] = 1, ['d'] = 1,
    ['e'] = 1, ['f'] = 1, ['g'] = 1, ['h'] = 1, ['i'] = 1, ['j'] = 1,
    ['k'] = 1, ['l'] = 1, ['m'] = 1, ['n'] = 1, ['o'] = 1, ['p'] = 1,
    ['q'] = 1, ['r'] = 1, ['s'] = 1, ['t'] = 1, ['u'] = 1, ['v'] = 1,
    ['w'] = 1, ['x'] = 1, ['y'] = 1, ['z'] = 1, ['~'] = 1,
};

static int buf_append_href(MmdocBuf *out, const char *s, size_t len) {
  static const char hex[] = "0123456789ABCDEF";
  size_t run = 0;
  for (size_t i = 0; i < len; i++) {
    unsigned char c = (unsigned char)s[i];
    unsigned char k = href_safe_lut[c];
    if (__builtin_expect(k != 1, 0)) {
      if (i > run && buf_append(out, s + run, i - run) != 0)
        return -1;
      if (k == 2) {
        if (buf_append(out, "&amp;", 5) != 0)
          return -1;
      } else {
        char pct[3] = {'%', hex[c >> 4], hex[c & 0x0F]};
        if (buf_append(out, pct, 3) != 0)
          return -1;
      }
      run = i + 1;
    }
  }
  if (len > run && buf_append(out, s + run, len - run) != 0)
    return -1;
  return 0;
}

/* ---- Search JSON serializer ---- */

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
        char u[6] = {'\\', 'u', '0', '0', hex[c >> 4], hex[c & 0x0f]};
        if (buf_append(out, u, 6) != 0)
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
  if (buf_append(out, "{ \"url\": ", 9) != 0 ||
      append_json_string(out, url, url_len) != 0 ||
      buf_append(out, ", \"title\": ", 11) != 0 ||
      append_json_string(out, title, title_len) != 0 ||
      buf_append(out, ", \"text\": ", 10) != 0 ||
      append_json_string(out, text, text_len) != 0 ||
      buf_append(out, " }", 2) != 0)
    return -1;
  return 0;
}

/* ---- Fused Single-Pass HTML + Dual-Prefix Patch + Plaintext Renderer ---- */

typedef struct {
  size_t offset;
  const char *prefix;
  size_t prefix_len;
} HtmlPatch;

typedef struct {
  const MdDoc *doc;
  MmdocBuf *html;
  MmdocBuf *plain;
  MmdocBuf *warnings;
  const char *input_file_path;
  const AnchorLocation *chapter;
  AnchorLocationArray *anchors;
  const char *fixed_multi_prefix; /* non-NULL in standalone MULTIPAGE render */
  const char *heading_prefix;     /* heading/admonition anchor prefix */
  int dual_mode;
  int print_warnings_stdout;
  HtmlPatch *patches;
  size_t patches_used;
  size_t patches_cap;
  size_t patches_extra_bytes;
  int failed;
} HtmlWriter;

static void html_push_patch(HtmlWriter *w, size_t offset, const char *prefix) {
  if (!w->dual_mode || prefix == NULL || prefix[0] == '\0')
    return;
  if (w->patches_used == w->patches_cap) {
    size_t nc = w->patches_cap == 0 ? 32 : w->patches_cap * 2;
    HtmlPatch *tmp = realloc(w->patches, nc * sizeof(*tmp));
    if (tmp == NULL) {
      w->failed = 1;
      return;
    }
    w->patches = tmp;
    w->patches_cap = nc;
  }
  size_t len = strlen(prefix);
  w->patches[w->patches_used++] = (HtmlPatch){offset, prefix, len};
  w->patches_extra_bytes += len;
}

static void render_alt_text(const MdDoc *doc, uint32_t node_id, MmdocBuf *out,
                            int *failed) {
  for (uint32_t c = doc->nodes[node_id].first_child; !*failed && c != 0;
       c = doc->nodes[c].next_sibling) {
    const MdNode *n = &doc->nodes[c];
    if (n->type == MD_TEXT || n->type == MD_CODE) {
      if (buf_append_html_escaped(out, md_text_ptr(doc, n), n->text_len, 0) !=
          0)
        *failed = 1;
    } else if (n->type == MD_SOFTBREAK || n->type == MD_LINEBREAK) {
      if (buf_append_char(out, '\n') != 0)
        *failed = 1;
    } else if (n->first_child != 0) {
      render_alt_text(doc, c, out, failed);
    }
  }
}

static void html_render_inlines(HtmlWriter *w, uint32_t parent);

static void html_render_inline(HtmlWriter *w, uint32_t id) {
  if (w->failed)
    return;
  const MdDoc *doc = w->doc;
  const MdNode *n = &doc->nodes[id];
  switch ((MdNodeType)n->type) {
  case MD_TEXT: {
    const char *p = md_text_ptr(doc, n);
    if (buf_append_html_escaped(w->html, p, n->text_len, 0) != 0) {
      w->failed = 1;
      return;
    }
    if (w->plain != NULL && n->text_len > 0 &&
        buf_append(w->plain, p, n->text_len) != 0)
      w->failed = 1;
    break;
  }
  case MD_CODE: {
    const char *p = md_text_ptr(doc, n);
    if (buf_append(w->html, "<code>", 6) != 0 ||
        buf_append_html_escaped(w->html, p, n->text_len, 0) != 0 ||
        buf_append(w->html, "</code>", 7) != 0) {
      w->failed = 1;
      return;
    }
    if (w->plain != NULL && n->text_len > 0 &&
        buf_append(w->plain, p, n->text_len) != 0)
      w->failed = 1;
    break;
  }
  case MD_SOFTBREAK:
    if (buf_append_char(w->html, '\n') != 0) {
      w->failed = 1;
      return;
    }
    if (w->plain != NULL && w->plain->len > 0 &&
        w->plain->data[w->plain->len - 1] != '\n' &&
        w->plain->data[w->plain->len - 1] != ' ' &&
        buf_append_char(w->plain, ' ') != 0)
      w->failed = 1;
    break;
  case MD_LINEBREAK:
    if (buf_append(w->html, "<br />\n", 7) != 0) {
      w->failed = 1;
      return;
    }
    if (w->plain != NULL && w->plain->len > 0 &&
        w->plain->data[w->plain->len - 1] != '\n' &&
        w->plain->data[w->plain->len - 1] != ' ' &&
        buf_append_char(w->plain, ' ') != 0)
      w->failed = 1;
    break;
  case MD_EMPH:
    if (buf_append(w->html, "<em>", 4) != 0) {
      w->failed = 1;
      return;
    }
    html_render_inlines(w, id);
    if (!w->failed && buf_append(w->html, "</em>", 5) != 0)
      w->failed = 1;
    break;
  case MD_STRONG:
    if (buf_append(w->html, "<strong>", 8) != 0) {
      w->failed = 1;
      return;
    }
    html_render_inlines(w, id);
    if (!w->failed && buf_append(w->html, "</strong>", 9) != 0)
      w->failed = 1;
    break;
  case MD_HTML_INLINE: {
    const char *p = md_text_ptr(doc, n);
    if (buf_append(w->html, p, n->text_len) != 0)
      w->failed = 1;
    break;
  }
  case MD_SPAN: {
    const char *anc = md_aux_ptr(doc, n);
    if (buf_append(w->html, "<span id='", 10) != 0 ||
        buf_append_html_escaped(w->html, anc, n->aux_len, 1) != 0 ||
        buf_append(w->html, "'>", 2) != 0) {
      w->failed = 1;
      return;
    }
    html_render_inlines(w, id);
    if (!w->failed && buf_append(w->html, "</span>", 7) != 0)
      w->failed = 1;
    break;
  }
  case MD_LINK: {
    const char *url = md_aux_ptr(doc, n);
    size_t ulen = n->aux_len;
    const char *auto_title = NULL;
    const char *multi_prefix = NULL;

    if (ulen > 0 && url[0] == '#' &&
        (w->dual_mode || w->fixed_multi_prefix != NULL ||
         (w->anchors != NULL && w->anchors->used > 0))) {
      char stack_key[256];
      char *key = ulen < sizeof(stack_key) ? stack_key : malloc(ulen + 1);
      if (key != NULL) {
        memcpy(key, url, ulen);
        key[ulen] = '\0';
        AnchorLocation *target = (w->anchors != NULL && w->anchors->used > 0)
                                     ? find_anchor_location(w->anchors, key)
                                     : NULL;
        if (target != NULL) {
          if (n->first_child == 0 && target->title[0] != '\0')
            auto_title = target->title;
          multi_prefix = target->multipage_url;
        } else if (w->dual_mode || w->fixed_multi_prefix != NULL) {
          const char *fp =
              w->chapter != NULL ? w->chapter->file_path : w->input_file_path;
          if (w->warnings != NULL && fp != NULL) {
            size_t wl = ulen + strlen(fp) + 64;
            char *wb = malloc(wl);
            if (wb != NULL) {
              int wn = snprintf(wb, wl,
                                "Anchor \"%s\" referenced in %s not found.\n",
                                key, fp);
              if (wn > 0)
                buf_append(w->warnings, wb, (size_t)wn);
              free(wb);
            }
          } else if (w->print_warnings_stdout && fp != NULL) {
            printf("Anchor \"%s\" referenced in %s not found.\n", key, fp);
          }
          multi_prefix =
              w->chapter ? w->chapter->multipage_url : w->fixed_multi_prefix;
        }
        if (key != stack_key)
          free(key);
      }
    }

    if (buf_append(w->html, "<a href=\"", 9) != 0) {
      w->failed = 1;
      return;
    }
    if (multi_prefix != NULL) {
      if (w->dual_mode) {
        html_push_patch(w, w->html->len, multi_prefix);
      } else if (w->fixed_multi_prefix != NULL &&
                 buf_append_str(w->html, multi_prefix) != 0) {
        w->failed = 1;
        return;
      }
    }
    if (buf_append_href(w->html, url, ulen) != 0 ||
        buf_append_char(w->html, '"') != 0) {
      w->failed = 1;
      return;
    }
    const MdExtra *ex = md_find_extra(doc, id);
    if (ex != NULL && ex->str_len > 0) {
      const char *ts = ex->str_in_pool ? (doc->pool + ex->str_off)
                                       : (doc->src + ex->str_off);
      if (buf_append(w->html, " title=\"", 8) != 0 ||
          buf_append_html_escaped(w->html, ts, ex->str_len, 0) != 0 ||
          buf_append_char(w->html, '"') != 0) {
        w->failed = 1;
        return;
      }
    }
    if (buf_append_char(w->html, '>') != 0) {
      w->failed = 1;
      return;
    }
    if (auto_title != NULL) {
      size_t tl = strlen(auto_title);
      if (buf_append_html_escaped(w->html, auto_title, tl, 0) != 0) {
        w->failed = 1;
        return;
      }
      if (w->plain != NULL && buf_append(w->plain, auto_title, tl) != 0) {
        w->failed = 1;
        return;
      }
    } else {
      html_render_inlines(w, id);
    }
    if (!w->failed && buf_append(w->html, "</a>", 4) != 0)
      w->failed = 1;
    break;
  }
  case MD_IMAGE: {
    const char *url = md_aux_ptr(doc, n);
    if (buf_append(w->html, "<img src=\"", 10) != 0 ||
        buf_append_href(w->html, url, n->aux_len) != 0 ||
        buf_append(w->html, "\" alt=\"", 7) != 0) {
      w->failed = 1;
      return;
    }
    render_alt_text(doc, id, w->html, &w->failed);
    if (w->failed)
      return;
    if (buf_append_char(w->html, '"') != 0) {
      w->failed = 1;
      return;
    }
    const MdExtra *ex = md_find_extra(doc, id);
    if (ex != NULL && ex->str_len > 0) {
      const char *ts = ex->str_in_pool ? (doc->pool + ex->str_off)
                                       : (doc->src + ex->str_off);
      if (buf_append(w->html, " title=\"", 8) != 0 ||
          buf_append_html_escaped(w->html, ts, ex->str_len, 0) != 0 ||
          buf_append_char(w->html, '"') != 0) {
        w->failed = 1;
        return;
      }
    }
    if (buf_append(w->html, " />", 3) != 0)
      w->failed = 1;
    break;
  }
  default:
    break;
  }
}

static void html_render_inlines(HtmlWriter *w, uint32_t parent) {
  for (uint32_t c = w->doc->nodes[parent].first_child; !w->failed && c != 0;
       c = w->doc->nodes[c].next_sibling) {
    html_render_inline(w, c);
  }
}

static void html_render_blocks(HtmlWriter *w, uint32_t parent, int tight);

static void html_render_block_ex(HtmlWriter *w, uint32_t id, int tight,
                                 int para_nl_open, int para_nl_close) {
  if (w->failed)
    return;
  const MdDoc *doc = w->doc;
  const MdNode *n = &doc->nodes[id];
  switch ((MdNodeType)n->type) {
  case MD_PARAGRAPH:
    if (w->plain != NULL && w->plain->len > 0 &&
        w->plain->data[w->plain->len - 1] != '\n' &&
        buf_append(w->plain, "\n\n", 2) != 0) {
      w->failed = 1;
      return;
    }
    if (!tight) {
      if (buf_append(w->html, para_nl_open ? "<p>\n" : "<p>",
                     para_nl_open ? 4 : 3) != 0) {
        w->failed = 1;
        return;
      }
    }
    html_render_inlines(w, id);
    if (!w->failed && !tight) {
      if (buf_append(w->html, para_nl_close ? "\n</p>\n" : "</p>\n",
                     para_nl_close ? 6 : 5) != 0)
        w->failed = 1;
    }
    break;

  case MD_HEADING: {
    if (w->plain != NULL && w->plain->len > 0 &&
        w->plain->data[w->plain->len - 1] != '\n' &&
        buf_append(w->plain, "\n\n", 2) != 0) {
      w->failed = 1;
      return;
    }
    char lvl = (char)('0' + (n->info >= 1 && n->info <= 6 ? n->info : 1));
    if (n->aux_len > 0) {
      const char *anc = md_aux_ptr(doc, n);
      char open[4] = {'<', 'h', lvl, ' '};
      if (buf_append(w->html, open, 4) != 0 ||
          buf_append(w->html, "id='", 4) != 0 ||
          buf_append_html_escaped(w->html, anc, n->aux_len, 1) != 0 ||
          buf_append(w->html, "'><a href='", 11) != 0) {
        w->failed = 1;
        return;
      }
      if (w->dual_mode && w->chapter != NULL) {
        html_push_patch(w, w->html->len, w->chapter->multipage_url);
      } else if (w->heading_prefix != NULL &&
                 buf_append_str(w->html, w->heading_prefix) != 0) {
        w->failed = 1;
        return;
      }
      if (buf_append_char(w->html, '#') != 0 ||
          buf_append_html_escaped(w->html, anc, n->aux_len, 1) != 0 ||
          buf_append(w->html, "'>\n", 3) != 0) {
        w->failed = 1;
        return;
      }
      html_render_inlines(w, id);
      char close[11] = {'\n', '<', '/', 'a', '>', '<',
                        '/',  'h', lvl, '>', '\n'};
      if (!w->failed && buf_append(w->html, close, 11) != 0)
        w->failed = 1;
    } else {
      char open[4] = {'<', 'h', lvl, '>'};
      if (buf_append(w->html, open, 4) != 0) {
        w->failed = 1;
        return;
      }
      html_render_inlines(w, id);
      char close[6] = {'<', '/', 'h', lvl, '>', '\n'};
      if (!w->failed && buf_append(w->html, close, 6) != 0)
        w->failed = 1;
    }
    break;
  }

  case MD_CODE_BLOCK: {
    const char *p = md_text_ptr(doc, n);
    if (w->plain != NULL) {
      if (w->plain->len > 0 && w->plain->data[w->plain->len - 1] != '\n' &&
          buf_append(w->plain, "\n\n", 2) != 0) {
        w->failed = 1;
        return;
      }
      if (n->text_len > 0 && buf_append(w->plain, p, n->text_len) != 0) {
        w->failed = 1;
        return;
      }
    }
    if (n->aux_len > 0) {
      const char *info = md_aux_ptr(doc, n);
      size_t lang_len = 0;
      while (lang_len < n->aux_len && info[lang_len] != ' ' &&
             info[lang_len] != '\t')
        lang_len++;
      if (lang_len > 0) {
        if (buf_append(w->html, "<pre><code class=\"language-", 27) != 0 ||
            buf_append_html_escaped(w->html, info, lang_len, 0) != 0 ||
            buf_append(w->html, "\">", 2) != 0) {
          w->failed = 1;
          return;
        }
      } else if (buf_append(w->html, "<pre><code>", 11) != 0) {
        w->failed = 1;
        return;
      }
    } else if (buf_append(w->html, "<pre><code>", 11) != 0) {
      w->failed = 1;
      return;
    }
    if (buf_append_html_escaped(w->html, p, n->text_len, 0) != 0 ||
        buf_append(w->html, "</code></pre>\n", 14) != 0)
      w->failed = 1;
    break;
  }

  case MD_HTML_BLOCK: {
    const char *p = md_text_ptr(doc, n);
    if (buf_append(w->html, p, n->text_len) != 0)
      w->failed = 1;
    break;
  }

  case MD_THEMATIC_BREAK:
    if (buf_append(w->html, "<hr />\n", 7) != 0)
      w->failed = 1;
    break;

  case MD_BLOCK_QUOTE:
    if (buf_append(w->html, "<blockquote>\n", 13) != 0) {
      w->failed = 1;
      return;
    }
    html_render_blocks(w, id, 0);
    if (!w->failed && buf_append(w->html, "</blockquote>\n", 14) != 0)
      w->failed = 1;
    break;

  case MD_LIST: {
    int ordered = (n->flags & MD_F_ORDERED) != 0;
    int list_tight = (n->flags & MD_F_TIGHT) != 0;
    if (ordered) {
      const MdExtra *ex = md_find_extra(doc, id);
      if (ex != NULL && ex->num != 1) {
        char ob[32];
        int ol = snprintf(ob, sizeof(ob), "<ol start=\"%d\">\n", ex->num);
        if (buf_append(w->html, ob, (size_t)ol) != 0) {
          w->failed = 1;
          return;
        }
      } else if (buf_append(w->html, "<ol>\n", 5) != 0) {
        w->failed = 1;
        return;
      }
    } else if (buf_append(w->html, "<ul>\n", 5) != 0) {
      w->failed = 1;
      return;
    }
    for (uint32_t item = n->first_child; !w->failed && item != 0;
         item = doc->nodes[item].next_sibling) {
      if (w->plain != NULL && w->plain->len > 0 &&
          w->plain->data[w->plain->len - 1] != '\n' &&
          buf_append_char(w->plain, '\n') != 0) {
        w->failed = 1;
        return;
      }
      if (buf_append(w->html, "<li>", 4) != 0) {
        w->failed = 1;
        return;
      }
      uint32_t fc = doc->nodes[item].first_child;
      if (fc != 0 && !(list_tight && doc->nodes[fc].type == MD_PARAGRAPH)) {
        if (buf_append_char(w->html, '\n') != 0) {
          w->failed = 1;
          return;
        }
      }
      for (uint32_t ic = fc; !w->failed && ic != 0;
           ic = doc->nodes[ic].next_sibling) {
        html_render_block_ex(w, ic, list_tight, 0, 0);
        if (!w->failed && list_tight && doc->nodes[ic].type == MD_PARAGRAPH &&
            doc->nodes[ic].next_sibling != 0) {
          if (buf_append_char(w->html, '\n') != 0)
            w->failed = 1;
        }
      }
      if (!w->failed && buf_append(w->html, "</li>\n", 6) != 0)
        w->failed = 1;
    }
    if (!w->failed &&
        buf_append(w->html, ordered ? "</ol>\n" : "</ul>\n", 6) != 0)
      w->failed = 1;
    break;
  }

  case MD_ADMONITION: {
    const char *ts = md_aux_ptr(doc, n);
    size_t tl = n->aux_len;
    const MdExtra *ex = md_find_extra(doc, id);
    if (buf_append(w->html, "<div class='admonition ", 23) != 0 ||
        buf_append_html_escaped(w->html, ts, tl, 1) != 0 ||
        buf_append(w->html, "'>", 2) != 0) {
      w->failed = 1;
      return;
    }
    if (ex != NULL && ex->str_len > 0) {
      const char *anc = ex->str_in_pool ? (doc->pool + ex->str_off)
                                        : (doc->src + ex->str_off);
      if (buf_append(w->html, "<h3 id='", 8) != 0 ||
          buf_append_html_escaped(w->html, anc, ex->str_len, 1) != 0 ||
          buf_append(w->html, "' class='title'><a href='", 25) != 0) {
        w->failed = 1;
        return;
      }
      if (w->dual_mode && w->chapter != NULL) {
        html_push_patch(w, w->html->len, w->chapter->multipage_url);
      } else if (w->heading_prefix != NULL &&
                 buf_append_str(w->html, w->heading_prefix) != 0) {
        w->failed = 1;
        return;
      }
      if (buf_append_char(w->html, '#') != 0 ||
          buf_append_html_escaped(w->html, anc, ex->str_len, 1) != 0 ||
          buf_append(w->html, "'>", 2) != 0) {
        w->failed = 1;
        return;
      }
      if (tl > 0) {
        char cap = (char)toupper((unsigned char)ts[0]);
        if (buf_append_char(w->html, cap) != 0 ||
            buf_append_html_escaped(w->html, ts + 1, tl - 1, 1) != 0) {
          w->failed = 1;
          return;
        }
      }
      if (buf_append(w->html, "</a></h3>\n", 10) != 0) {
        w->failed = 1;
        return;
      }
    } else {
      if (buf_append(w->html, "<h3 class='title'>", 18) != 0) {
        w->failed = 1;
        return;
      }
      if (tl > 0) {
        char cap = (char)toupper((unsigned char)ts[0]);
        if (buf_append_char(w->html, cap) != 0 ||
            buf_append_html_escaped(w->html, ts + 1, tl - 1, 1) != 0) {
          w->failed = 1;
          return;
        }
      }
      if (buf_append(w->html, "</h3>\n", 6) != 0) {
        w->failed = 1;
        return;
      }
    }
    int tight_open = (n->info & MD_ADMON_TIGHT_OPEN) != 0;
    int tight_close = (n->info & MD_ADMON_TIGHT_CLOSE) != 0;
    for (uint32_t c = n->first_child; !w->failed && c != 0;
         c = doc->nodes[c].next_sibling) {
      int nl_o = (c == n->first_child && tight_open &&
                  doc->nodes[c].type == MD_PARAGRAPH);
      int nl_c = (c == n->last_child && tight_close &&
                  doc->nodes[c].type == MD_PARAGRAPH);
      html_render_block_ex(w, c, 0, nl_o, nl_c);
    }
    if (!w->failed && buf_append(w->html, "</div>\n", 7) != 0)
      w->failed = 1;
    break;
  }

  case MD_DEFLIST:
    if (buf_append(w->html, "<dl>\n", 5) != 0) {
      w->failed = 1;
      return;
    }
    for (uint32_t c = n->first_child; !w->failed && c != 0;
         c = doc->nodes[c].next_sibling) {
      if (doc->nodes[c].type == MD_DT) {
        if (buf_append(w->html, "<dt>\n", 5) != 0) {
          w->failed = 1;
          return;
        }
        html_render_blocks(w, c, 0);
        if (!w->failed && buf_append(w->html, "</dt>\n", 6) != 0)
          w->failed = 1;
      } else if (doc->nodes[c].type == MD_DD) {
        if (buf_append(w->html, "<dd>\n", 5) != 0) {
          w->failed = 1;
          return;
        }
        html_render_blocks(w, c, 0);
        if (!w->failed && buf_append(w->html, "</dd>\n", 6) != 0)
          w->failed = 1;
      }
    }
    if (!w->failed && buf_append(w->html, "</dl>\n", 6) != 0)
      w->failed = 1;
    break;

  case MD_TABLE:
    if (buf_append(w->html, "<table>\n", 8) != 0) {
      w->failed = 1;
      return;
    }
    for (uint32_t sec = n->first_child; !w->failed && sec != 0;
         sec = doc->nodes[sec].next_sibling) {
      int is_head = doc->nodes[sec].type == MD_TABLE_HEAD;
      if (buf_append(w->html, is_head ? "<thead>\n" : "<tbody>\n", 8) != 0) {
        w->failed = 1;
        return;
      }
      for (uint32_t row = doc->nodes[sec].first_child; !w->failed && row != 0;
           row = doc->nodes[row].next_sibling) {
        if (w->plain != NULL && w->plain->len > 0 &&
            w->plain->data[w->plain->len - 1] != '\n' &&
            buf_append_char(w->plain, '\n') != 0) {
          w->failed = 1;
          return;
        }
        if (buf_append(w->html, "<tr>\n", 5) != 0) {
          w->failed = 1;
          return;
        }
        for (uint32_t cell = doc->nodes[row].first_child;
             !w->failed && cell != 0; cell = doc->nodes[cell].next_sibling) {
          if (w->plain != NULL && w->plain->len > 0 &&
              w->plain->data[w->plain->len - 1] != '\n' &&
              w->plain->data[w->plain->len - 1] != ' ' &&
              buf_append_char(w->plain, ' ') != 0) {
            w->failed = 1;
            return;
          }
          const MdNode *cn = &doc->nodes[cell];
          const char *tag = (cn->flags & MD_F_TABLE_TH) ? "th" : "td";
          const char *align_attr =
              cn->info == MD_ALIGN_LEFT     ? " align=\"left\""
              : cn->info == MD_ALIGN_CENTER ? " align=\"center\""
              : cn->info == MD_ALIGN_RIGHT  ? " align=\"right\""
                                            : "";
          if (buf_append_char(w->html, '<') != 0 ||
              buf_append(w->html, tag, 2) != 0 ||
              (align_attr[0] && buf_append_str(w->html, align_attr) != 0) ||
              buf_append_char(w->html, '>') != 0) {
            w->failed = 1;
            return;
          }
          html_render_inlines(w, cell);
          if (!w->failed && (buf_append(w->html, "</", 2) != 0 ||
                             buf_append(w->html, tag, 2) != 0 ||
                             buf_append(w->html, ">\n", 2) != 0))
            w->failed = 1;
        }
        if (!w->failed && buf_append(w->html, "</tr>\n", 6) != 0)
          w->failed = 1;
      }
      if (!w->failed &&
          buf_append(w->html, is_head ? "</thead>\n" : "</tbody>\n", 9) != 0)
        w->failed = 1;
    }
    if (!w->failed && buf_append(w->html, "</table>\n", 9) != 0)
      w->failed = 1;
    break;

  default:
    break;
  }
}

static void html_render_blocks(HtmlWriter *w, uint32_t parent, int tight) {
  for (uint32_t c = w->doc->nodes[parent].first_child; !w->failed && c != 0;
       c = w->doc->nodes[c].next_sibling) {
    html_render_block_ex(w, c, tight, 0, 0);
  }
}

/* ---- Direct Dual CommonMark Emitter (llms-full.txt & chapter/index.md) ----
 */

static int has_relative_path(const char *url, size_t len) {
  if (len == 0 || url[0] == '/' || url[0] == '?' || url[0] == '#')
    return 0;
  if (!isalpha((unsigned char)url[0]))
    return 1;
  size_t i = 1;
  while (i < len && (isalnum((unsigned char)url[i]) || url[i] == '+' ||
                     url[i] == '-' || url[i] == '.'))
    i++;
  return i >= len || url[i] != ':';
}

static int url_needs_angle_brackets(const char *url, size_t len) {
  for (size_t i = 0; i < len; i++) {
    unsigned char c = (unsigned char)url[i];
    if (c <= 0x20 || c == '(' || c == ')' || c == '<' || c == '>')
      return 1;
  }
  return 0;
}

static void md_format_url(MmdocBuf *dst, const char *url, size_t len,
                          int *failed) {
  if (!url_needs_angle_brackets(url, len)) {
    if (buf_append(dst, url, len) != 0)
      *failed = 1;
    return;
  }
  if (buf_append_char(dst, '<') != 0) {
    *failed = 1;
    return;
  }
  for (size_t i = 0; i < len; i++) {
    if (url[i] == '<' || url[i] == '>' || url[i] == '\\') {
      char pair[2] = {'\\', url[i]};
      if (buf_append(dst, pair, 2) != 0) {
        *failed = 1;
        return;
      }
    } else if (buf_append_char(dst, url[i]) != 0) {
      *failed = 1;
      return;
    }
  }
  if (buf_append_char(dst, '>') != 0)
    *failed = 1;
}

typedef struct {
  size_t offset;
  size_t full_len;
  char *chap_str;
  size_t chap_len;
} MdUrlPatch;

typedef struct {
  const MdDoc *doc;
  MmdocBuf *out;
  MmdocBuf prefix;
  const AnchorLocation *chapter;
  AnchorLocationArray *anchors;
  AnchorLocationArray *chapters_by_source;
  MdUrlPatch *patches;
  size_t patches_used;
  size_t patches_cap;
  int in_table_cell;
  int failed;
} MdWriter;

static const unsigned char md_esc[256] = {
    ['\\'] = 1, ['*'] = 1, ['_'] = 1, ['`'] = 1,
    ['['] = 1,  [']'] = 1, ['<'] = 1, ['>'] = 1,
};
static const unsigned char md_esc_cell[256] = {
    ['\\'] = 1, ['*'] = 1, ['_'] = 1, ['`'] = 1, ['['] = 1,
    [']'] = 1,  ['<'] = 1, ['>'] = 1, ['|'] = 1,
};

static void md_emit_escaped(MdWriter *w, const char *s, size_t len) {
  const unsigned char *tbl = w->in_table_cell ? md_esc_cell : md_esc;
  size_t run = 0;
  for (size_t i = 0; i < len; i++) {
    if (__builtin_expect(tbl[(unsigned char)s[i]], 0)) {
      if (i > run && buf_append(w->out, s + run, i - run) != 0) {
        w->failed = 1;
        return;
      }
      char pair[2] = {'\\', s[i]};
      if (buf_append(w->out, pair, 2) != 0) {
        w->failed = 1;
        return;
      }
      run = i + 1;
    }
  }
  if (len > run && buf_append(w->out, s + run, len - run) != 0)
    w->failed = 1;
}

static void md_nl(MdWriter *w) {
  if (buf_append_char(w->out, '\n') != 0 ||
      (w->prefix.len > 0 &&
       buf_append(w->out, w->prefix.data, w->prefix.len) != 0))
    w->failed = 1;
}

static void md_blank(MdWriter *w) {
  if (w->prefix.len == 0) {
    if (buf_append(w->out, "\n\n", 2) != 0)
      w->failed = 1;
    return;
  }
  size_t tr = w->prefix.len;
  while (tr > 0 && w->prefix.data[tr - 1] == ' ')
    tr--;
  if (buf_append_char(w->out, '\n') != 0 ||
      (tr > 0 && buf_append(w->out, w->prefix.data, tr) != 0) ||
      buf_append_char(w->out, '\n') != 0 ||
      buf_append(w->out, w->prefix.data, w->prefix.len) != 0)
    w->failed = 1;
}

static void md_push_patch(MdWriter *w, size_t off, size_t flen, char *cstr,
                          size_t clen) {
  if (w->patches_used == w->patches_cap) {
    size_t nc = w->patches_cap == 0 ? 16 : w->patches_cap * 2;
    MdUrlPatch *tmp = realloc(w->patches, nc * sizeof(*tmp));
    if (tmp == NULL) {
      free(cstr);
      w->failed = 1;
      return;
    }
    w->patches = tmp;
    w->patches_cap = nc;
  }
  w->patches[w->patches_used++] = (MdUrlPatch){off, flen, cstr, clen};
}

static void md_render_inlines(MdWriter *w, uint32_t parent);

static void md_render_inline(MdWriter *w, uint32_t id) {
  if (w->failed)
    return;
  const MdDoc *doc = w->doc;
  const MdNode *n = &doc->nodes[id];
  switch ((MdNodeType)n->type) {
  case MD_TEXT:
    md_emit_escaped(w, md_text_ptr(doc, n), n->text_len);
    break;
  case MD_SOFTBREAK:
    if (w->in_table_cell) {
      if (buf_append_char(w->out, ' ') != 0)
        w->failed = 1;
    } else {
      md_nl(w);
    }
    break;
  case MD_LINEBREAK:
    if (w->in_table_cell) {
      if (buf_append(w->out, "<br>", 4) != 0)
        w->failed = 1;
    } else {
      if (buf_append(w->out, "  ", 2) != 0) {
        w->failed = 1;
        return;
      }
      md_nl(w);
    }
    break;
  case MD_CODE: {
    const char *p = md_text_ptr(doc, n);
    size_t len = n->text_len;
    size_t max_bt = 0;
    for (size_t i = 0; i < len;) {
      if (p[i] == '`') {
        size_t r = 0;
        while (i < len && p[i] == '`') {
          r++;
          i++;
        }
        if (r > max_bt)
          max_bt = r;
      } else {
        i++;
      }
    }
    size_t ticks = max_bt + 1;
    int pad = len > 0 && (p[0] == '`' || p[len - 1] == '`' ||
                          (p[0] == ' ' && p[len - 1] == ' '));
    for (size_t k = 0; k < ticks; k++)
      buf_append_char(w->out, '`');
    if (pad)
      buf_append_char(w->out, ' ');
    if (len > 0)
      buf_append(w->out, p, len);
    if (pad)
      buf_append_char(w->out, ' ');
    for (size_t k = 0; k < ticks; k++)
      buf_append_char(w->out, '`');
    break;
  }
  case MD_EMPH:
    buf_append_char(w->out, '*');
    md_render_inlines(w, id);
    buf_append_char(w->out, '*');
    break;
  case MD_STRONG:
    buf_append(w->out, "**", 2);
    md_render_inlines(w, id);
    buf_append(w->out, "**", 2);
    break;
  case MD_HTML_INLINE:
    buf_append(w->out, md_text_ptr(doc, n), n->text_len);
    break;
  case MD_SPAN:
    buf_append_char(w->out, '[');
    md_render_inlines(w, id);
    buf_append(w->out, "]{#", 3);
    buf_append(w->out, md_aux_ptr(doc, n), n->aux_len);
    buf_append_char(w->out, '}');
    break;
  case MD_LINK:
  case MD_IMAGE: {
    const char *url = md_aux_ptr(doc, n);
    size_t ulen = n->aux_len;
    const char *auto_label = NULL;
    char *chap_url = NULL;
    size_t chap_len = 0;

    if (n->type == MD_LINK && ulen > 0 && url[0] == '#' && w->anchors != NULL) {
      char skey[256];
      char *key = ulen < sizeof(skey) ? skey : malloc(ulen + 1);
      if (key != NULL) {
        memcpy(key, url, ulen);
        key[ulen] = '\0';
        AnchorLocation *target = find_anchor_location(w->anchors, key);
        if (target != NULL) {
          AnchorLocation *page =
              find_anchor_location(w->chapters_by_source, target->file_path);
          if (page != NULL) {
            if (n->first_child == 0)
              auto_label = *page->title ? page->title : key + 1;
            if (w->chapter != NULL &&
                strcmp(page->file_path, w->chapter->file_path) != 0) {
              const char *base = w->chapter->multipage_base_href;
              const char *dir = strcmp(page->multipage_url, "./") == 0
                                    ? ""
                                    : page->multipage_url;
              size_t bl = strlen(base);
              size_t dl = strlen(dir);
              chap_url = malloc(bl + dl + 8 + ulen + 1);
              if (chap_url != NULL) {
                char *q = chap_url;
                memcpy(q, base, bl);
                q += bl;
                memcpy(q, dir, dl);
                q += dl;
                memcpy(q, "index.md", 8);
                q += 8;
                memcpy(q, url, ulen);
                q += ulen;
                *q = '\0';
                chap_len = (size_t)(q - chap_url);
              }
            }
          }
        }
        if (key != skey)
          free(key);
      }
    } else if (w->chapter != NULL && *w->chapter->multipage_base_href &&
               has_relative_path(url, ulen)) {
      size_t bl = strlen(w->chapter->multipage_base_href);
      chap_url = malloc(bl + ulen + 1);
      if (chap_url != NULL) {
        memcpy(chap_url, w->chapter->multipage_base_href, bl);
        memcpy(chap_url + bl, url, ulen);
        chap_url[bl + ulen] = '\0';
        chap_len = bl + ulen;
      }
    }

    if (n->type == MD_IMAGE)
      buf_append_char(w->out, '!');
    buf_append_char(w->out, '[');
    if (auto_label != NULL)
      md_emit_escaped(w, auto_label, strlen(auto_label));
    else
      md_render_inlines(w, id);
    buf_append(w->out, "](", 2);
    size_t uoff = w->out->len;
    md_format_url(w->out, url, ulen, &w->failed);
    size_t flen = w->out->len - uoff;
    if (chap_url != NULL) {
      if (!url_needs_angle_brackets(chap_url, chap_len)) {
        md_push_patch(w, uoff, flen, chap_url, chap_len);
      } else {
        MmdocBuf tmp = {0};
        md_format_url(&tmp, chap_url, chap_len, &w->failed);
        free(chap_url);
        if (!w->failed && tmp.data != NULL)
          md_push_patch(w, uoff, flen, tmp.data, tmp.len);
        else
          mmdoc_buf_free(&tmp);
      }
    }
    const MdExtra *ex = md_find_extra(doc, id);
    if (!w->failed && ex != NULL && ex->str_len > 0) {
      const char *ts = ex->str_in_pool ? (doc->pool + ex->str_off)
                                       : (doc->src + ex->str_off);
      buf_append(w->out, " \"", 2);
      for (size_t k = 0; k < ex->str_len; k++) {
        if (ts[k] == '"' || ts[k] == '\\') {
          char pair[2] = {'\\', ts[k]};
          buf_append(w->out, pair, 2);
        } else {
          buf_append_char(w->out, ts[k]);
        }
      }
      buf_append_char(w->out, '"');
    }
    buf_append_char(w->out, ')');
    break;
  }
  default:
    break;
  }
}

static void md_render_inlines(MdWriter *w, uint32_t parent) {
  for (uint32_t c = w->doc->nodes[parent].first_child; !w->failed && c != 0;
       c = w->doc->nodes[c].next_sibling) {
    md_render_inline(w, c);
  }
}

static void md_render_blocks(MdWriter *w, uint32_t parent, int tight);

static void md_render_block(MdWriter *w, uint32_t id) {
  if (w->failed)
    return;
  const MdDoc *doc = w->doc;
  const MdNode *n = &doc->nodes[id];
  switch ((MdNodeType)n->type) {
  case MD_HEADING: {
    int lvl = n->info >= 1 && n->info <= 6 ? n->info : 1;
    char hashes[8] = "###### ";
    buf_append(w->out, hashes + (6 - lvl), (size_t)lvl + 1);
    md_render_inlines(w, id);
    if (n->aux_len > 0) {
      buf_append(w->out, " {#", 3);
      buf_append(w->out, md_aux_ptr(doc, n), n->aux_len);
      buf_append_char(w->out, '}');
    }
    break;
  }
  case MD_PARAGRAPH:
    md_render_inlines(w, id);
    break;
  case MD_THEMATIC_BREAK:
    buf_append(w->out, "---", 3);
    break;
  case MD_CODE_BLOCK: {
    const char *p = md_text_ptr(doc, n);
    size_t len = n->text_len;
    if ((n->flags & MD_F_FENCED) || n->aux_len > 0) {
      size_t max_bt = 0;
      for (size_t i = 0; i < len;) {
        if (p[i] == '`') {
          size_t r = 0;
          while (i < len && p[i] == '`') {
            r++;
            i++;
          }
          if (r > max_bt)
            max_bt = r;
        } else {
          i++;
        }
      }
      size_t fence_len = max_bt >= 3 ? max_bt + 1 : 3;
      for (size_t k = 0; k < fence_len; k++)
        buf_append_char(w->out, '`');
      if (n->aux_len > 0) {
        buf_append_char(w->out, ' ');
        buf_append(w->out, md_aux_ptr(doc, n), n->aux_len);
      }
      buf_append_char(w->out, '\n');
      if (w->prefix.len == 0) {
        buf_append(w->out, p, len);
        if (len > 0 && p[len - 1] != '\n')
          buf_append_char(w->out, '\n');
      } else {
        size_t pos = 0;
        while (pos < len) {
          const char *nl = memchr(p + pos, '\n', len - pos);
          size_t ll = nl ? (size_t)(nl - (p + pos) + 1) : (len - pos);
          buf_append(w->out, w->prefix.data, w->prefix.len);
          buf_append(w->out, p + pos, ll);
          if (!nl) {
            buf_append_char(w->out, '\n');
            break;
          }
          pos = (size_t)(nl - p) + 1;
        }
        buf_append(w->out, w->prefix.data, w->prefix.len);
      }
      for (size_t k = 0; k < fence_len; k++)
        buf_append_char(w->out, '`');
    } else {
      size_t end_len = len;
      while (end_len > 0 && p[end_len - 1] == '\n')
        end_len--;
      size_t pos = 0;
      int first = 1;
      while (pos <= end_len) {
        const char *nl = memchr(p + pos, '\n', end_len - pos);
        size_t ll = nl ? (size_t)(nl - (p + pos)) : (end_len - pos);
        if (!first)
          md_nl(w);
        first = 0;
        if (ll > 0) {
          buf_append(w->out, "    ", 4);
          buf_append(w->out, p + pos, ll);
        }
        if (!nl)
          break;
        pos = (size_t)(nl - p) + 1;
      }
    }
    break;
  }
  case MD_HTML_BLOCK: {
    const char *p = md_text_ptr(doc, n);
    size_t len = n->text_len;
    while (len > 0 && p[len - 1] == '\n')
      len--;
    if (w->prefix.len == 0) {
      if (len > 0)
        buf_append(w->out, p, len);
    } else {
      size_t pos = 0;
      int first = 1;
      while (pos <= len) {
        const char *nl = memchr(p + pos, '\n', len - pos);
        size_t ll = nl ? (size_t)(nl - (p + pos)) : (len - pos);
        if (!first)
          md_nl(w);
        first = 0;
        if (ll > 0)
          buf_append(w->out, p + pos, ll);
        if (!nl)
          break;
        pos = (size_t)(nl - p) + 1;
      }
    }
    break;
  }
  case MD_BLOCK_QUOTE: {
    size_t saved = w->prefix.len;
    buf_append(w->out, "> ", 2);
    buf_append(&w->prefix, "> ", 2);
    md_render_blocks(w, id, 0);
    w->prefix.len = saved;
    break;
  }
  case MD_ADMONITION: {
    const MdExtra *ex = md_find_extra(doc, id);
    if (ex != NULL && ex->str_len > 0) {
      const char *anc = ex->str_in_pool ? (doc->pool + ex->str_off)
                                        : (doc->src + ex->str_off);
      buf_append(w->out, "::: {.", 6);
      buf_append(w->out, md_aux_ptr(doc, n), n->aux_len);
      buf_append(w->out, " #", 2);
      buf_append(w->out, anc, ex->str_len);
      buf_append_char(w->out, '}');
    } else {
      buf_append(w->out, "::: ", 4);
      buf_append(w->out, md_aux_ptr(doc, n), n->aux_len);
    }
    md_nl(w);
    md_render_blocks(w, id, 0);
    md_nl(w);
    buf_append(w->out, ":::", 3);
    break;
  }
  case MD_DEFLIST: {
    int prev_was_dt = 0;
    for (uint32_t c = n->first_child; !w->failed && c != 0;
         c = doc->nodes[c].next_sibling) {
      if (c != n->first_child) {
        if (prev_was_dt && doc->nodes[c].type == MD_DD)
          md_nl(w);
        else
          md_blank(w);
      }
      if (doc->nodes[c].type == MD_DT) {
        md_render_blocks(w, c, 0);
        prev_was_dt = 1;
      } else if (doc->nodes[c].type == MD_DD) {
        buf_append(w->out, ": ", 2);
        md_render_blocks(w, c, 0);
        prev_was_dt = 0;
      }
    }
    break;
  }
  case MD_LIST: {
    int ordered = (n->flags & MD_F_ORDERED) != 0;
    int tight = (n->flags & MD_F_TIGHT) != 0;
    const MdExtra *ex = md_find_extra(doc, id);
    int num = (ex != NULL) ? ex->num : 1;
    char dc = n->info == ')' ? ')' : '.';
    int first = 1;
    for (uint32_t item = n->first_child; !w->failed && item != 0;
         item = doc->nodes[item].next_sibling) {
      if (!first) {
        if (tight)
          md_nl(w);
        else
          md_blank(w);
      }
      first = 0;
      char m[24];
      size_t ml = ordered ? (size_t)snprintf(m, sizeof(m), "%d%c ", num++, dc)
                          : (memcpy(m, "- ", 2), 2);
      buf_append(w->out, m, ml);
      size_t saved = w->prefix.len;
      for (size_t k = 0; k < ml; k++)
        buf_append_char(&w->prefix, ' ');
      md_render_blocks(w, item, tight);
      w->prefix.len = saved;
    }
    break;
  }
  case MD_TABLE: {
    int ridx = 0;
    for (uint32_t sec = n->first_child; !w->failed && sec != 0;
         sec = doc->nodes[sec].next_sibling) {
      for (uint32_t row = doc->nodes[sec].first_child; !w->failed && row != 0;
           row = doc->nodes[row].next_sibling) {
        if (ridx > 0)
          md_nl(w);
        buf_append_char(w->out, '|');
        size_t cols = 0;
        w->in_table_cell = 1;
        for (uint32_t cell = doc->nodes[row].first_child;
             !w->failed && cell != 0; cell = doc->nodes[cell].next_sibling) {
          buf_append_char(w->out, ' ');
          md_render_inlines(w, cell);
          buf_append(w->out, " |", 2);
          cols++;
        }
        w->in_table_cell = 0;
        if (ridx == 0 && cols > 0) {
          md_nl(w);
          buf_append_char(w->out, '|');
          for (size_t k = 0; k < cols; k++)
            buf_append(w->out, " --- |", 6);
        }
        ridx++;
      }
    }
    break;
  }
  default:
    break;
  }
}

static void md_render_blocks(MdWriter *w, uint32_t parent, int tight) {
  int first = 1;
  for (uint32_t c = w->doc->nodes[parent].first_child; !w->failed && c != 0;
       c = w->doc->nodes[c].next_sibling) {
    if (!first) {
      if (tight)
        md_nl(w);
      else
        md_blank(w);
    }
    first = 0;
    md_render_block(w, c);
  }
}

int mmdoc_render_markdown_dual(const MdDoc *doc, const AnchorLocation *chapter,
                               AnchorLocationArray *anchors,
                               AnchorLocationArray *chapters_by_source,
                               MmdocBuf *full_out, MmdocBuf *chap_out) {
  mmdoc_buf_init(full_out, doc->src_len + (doc->src_len >> 3) + 256);
  MdWriter mw = {
      .doc = doc,
      .out = full_out,
      .chapter = chapter,
      .anchors = anchors,
      .chapters_by_source = chapters_by_source,
  };
  md_render_blocks(&mw, 1, 0);
  if (!mw.failed && full_out->len > 0)
    buf_append_char(full_out, '\n');

  if (!mw.failed && mw.patches_used > 0) {
    mmdoc_buf_init(chap_out, full_out->len + mw.patches_used * 32);
    size_t pos = 0;
    for (size_t i = 0; i < mw.patches_used; i++) {
      MdUrlPatch *pt = &mw.patches[i];
      if (pt->offset > pos &&
          buf_append(chap_out, full_out->data + pos, pt->offset - pos) != 0) {
        mw.failed = 1;
        break;
      }
      if (pt->chap_len > 0 &&
          buf_append(chap_out, pt->chap_str, pt->chap_len) != 0) {
        mw.failed = 1;
        break;
      }
      pos = pt->offset + pt->full_len;
    }
    if (!mw.failed && pos < full_out->len &&
        buf_append(chap_out, full_out->data + pos, full_out->len - pos) != 0)
      mw.failed = 1;
  }

  for (size_t i = 0; i < mw.patches_used; i++)
    free(mw.patches[i].chap_str);
  free(mw.patches);
  mmdoc_buf_free(&mw.prefix);
  return mw.failed ? -1 : 0;
}

/* ---- Minimal roff man renderer (for RENDER_TYPE_MAN) ---- */

static void man_render_inlines(const MdDoc *doc, uint32_t parent, MmdocBuf *out,
                               int *failed) {
  for (uint32_t c = doc->nodes[parent].first_child; !*failed && c != 0;
       c = doc->nodes[c].next_sibling) {
    const MdNode *n = &doc->nodes[c];
    if (n->type == MD_TEXT || n->type == MD_CODE) {
      const char *p = md_text_ptr(doc, n);
      for (size_t i = 0; i < n->text_len; i++) {
        if (p[i] == '\\')
          buf_append(out, "\\e", 2);
        else
          buf_append_char(out, p[i]);
      }
    } else if (n->type == MD_SOFTBREAK || n->type == MD_LINEBREAK) {
      buf_append_char(out, '\n');
    } else if (n->first_child != 0) {
      man_render_inlines(doc, c, out, failed);
    }
  }
}

static void man_render_blocks(const MdDoc *doc, uint32_t parent, MmdocBuf *out,
                              int *failed) {
  for (uint32_t c = doc->nodes[parent].first_child; !*failed && c != 0;
       c = doc->nodes[c].next_sibling) {
    const MdNode *n = &doc->nodes[c];
    if (n->type == MD_HEADING) {
      buf_append(out, n->info <= 1 ? ".SH " : ".SS ", 4);
      man_render_inlines(doc, c, out, failed);
      buf_append_char(out, '\n');
    } else if (n->type == MD_PARAGRAPH) {
      buf_append(out, ".PP\n", 4);
      man_render_inlines(doc, c, out, failed);
      buf_append_char(out, '\n');
    } else if (n->type == MD_CODE_BLOCK) {
      buf_append(out, ".nf\n", 4);
      buf_append(out, md_text_ptr(doc, n), n->text_len);
      buf_append(out, "\n.fi\n", 5);
    } else if (n->first_child != 0) {
      man_render_blocks(doc, c, out, failed);
    }
  }
}

/* ---- Chapter renderer ---- */

int mmdoc_render_chapter_from_mddoc(const MdDoc *doc,
                                    const AnchorLocation *chapter,
                                    AnchorLocationArray *anchors,
                                    AnchorLocationArray *chapters_by_source,
                                    MmdocBuf *scratch_plain,
                                    ChapterOutputs *out) {
  memset(out, 0, sizeof(*out));
  out->has_code_block = doc->has_code_block;

  size_t est_html = doc->src_len + (doc->src_len >> 1) + 256;
  mmdoc_buf_init(&out->single_html, est_html);
  MmdocBuf local_plain = {0};
  MmdocBuf *plain = scratch_plain != NULL ? scratch_plain : &local_plain;
  if (plain->cap == 0)
    mmdoc_buf_init(plain, doc->src_len + 256);
  else
    plain->len = 0;

  HtmlWriter hw = {
      .doc = doc,
      .html = &out->single_html,
      .plain = plain,
      .warnings = &out->warnings,
      .chapter = chapter,
      .anchors = anchors,
      .dual_mode = 1,
  };
  html_render_blocks(&hw, 1, 0);

  if (!hw.failed && hw.patches_used > 0) {
    mmdoc_buf_init(&out->multi_html,
                   out->single_html.len + hw.patches_extra_bytes + 16);
    size_t pos = 0;
    for (size_t i = 0; i < hw.patches_used; i++) {
      HtmlPatch *pt = &hw.patches[i];
      if (pt->offset > pos &&
          buf_append(&out->multi_html, out->single_html.data + pos,
                     pt->offset - pos) != 0) {
        hw.failed = 1;
        break;
      }
      if (buf_append(&out->multi_html, pt->prefix, pt->prefix_len) != 0) {
        hw.failed = 1;
        break;
      }
      pos = pt->offset;
    }
    if (!hw.failed && pos < out->single_html.len &&
        buf_append(&out->multi_html, out->single_html.data + pos,
                   out->single_html.len - pos) != 0)
      hw.failed = 1;
  }
  free(hw.patches);

  if (!hw.failed &&
      append_search_json(&out->search_json, chapter->multipage_url,
                         chapter->title, plain->data ? plain->data : "",
                         plain->len) != 0)
    hw.failed = 1;
  mmdoc_buf_free(&local_plain);
  if (hw.failed) {
    chapter_outputs_free(out);
    return -1;
  }

  if (mmdoc_render_markdown_dual(doc, chapter, anchors, chapters_by_source,
                                 &out->full_md, &out->chapter_md) != 0) {
    chapter_outputs_free(out);
    return -1;
  }
  return 0;
}

/* ---- Standalone metadata & single-file render (used by unit tests & TOC) ----
 */

int mmdoc_render_collect_metadata(char *file_path, Array *anchors,
                                  char **title) {
  if (title != NULL)
    *title = NULL;
  MdDoc doc;
  if (md_parse_file(&doc, file_path, 1) != 0)
    return -1;
  if (anchors != NULL) {
    for (size_t i = 0; i < doc.anchors.used; i++)
      insert_array(anchors, doc.anchors.array[i]);
  }
  if (title != NULL) {
    *title = doc.title;
    doc.title = NULL;
  }
  md_doc_free(&doc);
  return 0;
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

int mmdoc_render_part(char *file_path, FILE *output_file,
                      render_type render_type, AnchorLocation *anchor_location,
                      AnchorLocationArray anchor_locations, char *multipage_url,
                      FILE *search_index_path) {
  MdDoc doc;
  if (md_parse_file(&doc, file_path, 0) != 0)
    return -1;

  int failed = 0;
  int has_code = doc.has_code_block;

  if (render_type == RENDER_TYPE_MAN) {
    MmdocBuf man;
    mmdoc_buf_init(&man, doc.src_len + 256);
    man_render_blocks(&doc, 1, &man, &failed);
    if (!failed && man.len > 0 &&
        fwrite(man.data, 1, man.len, output_file) != man.len)
      failed = 1;
    mmdoc_buf_free(&man);
  } else {
    MmdocBuf html;
    MmdocBuf plain = {0};
    mmdoc_buf_init(&html, doc.src_len * 2 + 256);
    if (search_index_path != NULL)
      mmdoc_buf_init(&plain, doc.src_len + 256);
    HtmlWriter hw = {
        .doc = &doc,
        .html = &html,
        .plain = search_index_path != NULL ? &plain : NULL,
        .input_file_path = file_path,
        .anchors = &anchor_locations,
        .fixed_multi_prefix =
            render_type == RENDER_TYPE_MULTIPAGE ? multipage_url : NULL,
        .heading_prefix = (multipage_url != NULL && multipage_url[0] != '\0')
                              ? multipage_url
                              : NULL,
        .dual_mode = 0,
        .print_warnings_stdout = 1,
    };
    html_render_blocks(&hw, 1, 0);
    failed = hw.failed;

    if (!failed && search_index_path != NULL && anchor_location != NULL) {
      MmdocBuf json;
      mmdoc_buf_init(&json, plain.len + 128);
      if (append_search_json(&json, multipage_url, anchor_location->title,
                             plain.data ? plain.data : "", plain.len) != 0 ||
          fwrite(json.data, 1, json.len, search_index_path) != json.len ||
          fputs("\n,", search_index_path) == EOF)
        failed = 1;
      mmdoc_buf_free(&json);
    }

    if (!failed && html.len > 0 &&
        fwrite(html.data, 1, html.len, output_file) != html.len)
      failed = 1;

    mmdoc_buf_free(&plain);
    mmdoc_buf_free(&html);
  }

  md_doc_free(&doc);
  if (failed || ferror(output_file) ||
      (search_index_path != NULL && ferror(search_index_path)))
    return -1;
  return has_code;
}
