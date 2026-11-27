/* SPDX-License-Identifier: CC0-1.0 */
#include "md.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* ---- Portable helpers (zero GNU-only dependencies) ---- */

static inline int is_ascii_punct(unsigned char c) {
  return (c >= '!' && c <= '/') || (c >= ':' && c <= '@') ||
         (c >= '[' && c <= '`') || (c >= '{' && c <= '~');
}

static inline int is_ws(unsigned char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' ||
         c == '\v';
}

static const char *md_memmem(const char *hay, size_t hlen, const char *needle,
                             size_t nlen) {
  if (nlen == 0)
    return hay;
  if (hlen < nlen)
    return NULL;
  const char *p = hay;
  const char *end = hay + hlen - nlen + 1;
  while (p < end) {
    const char *hit = memchr(p, needle[0], (size_t)(end - p));
    if (hit == NULL)
      return NULL;
    if (memcmp(hit, needle, nlen) == 0)
      return hit;
    p = hit + 1;
  }
  return NULL;
}

static int md_strncasecmp(const char *a, const char *b, size_t n) {
  for (size_t i = 0; i < n; i++) {
    unsigned char ca = (unsigned char)a[i];
    unsigned char cb = (unsigned char)b[i];
    if (ca >= 'A' && ca <= 'Z')
      ca = (unsigned char)(ca - 'A' + 'a');
    if (cb >= 'A' && cb <= 'Z')
      cb = (unsigned char)(cb - 'A' + 'a');
    if (ca != cb)
      return (int)ca - (int)cb;
  }
  return 0;
}

/* ---- Bump allocators ---- */

static uint32_t pool_alloc(MdDoc *doc, size_t len) {
  size_t need = doc->pool_len + len + 1;
  if (need > doc->pool_cap) {
    size_t nc = doc->pool_cap == 0 ? 1024 : doc->pool_cap * 2;
    while (nc < need)
      nc *= 2;
    char *next = realloc(doc->pool, nc);
    if (next == NULL)
      return UINT32_MAX;
    doc->pool = next;
    doc->pool_cap = nc;
  }
  uint32_t off = (uint32_t)doc->pool_len;
  doc->pool_len += len + 1;
  doc->pool[off + len] = '\0';
  return off;
}

static uint32_t pool_copy(MdDoc *doc, const char *s, size_t len) {
  int from_pool =
      (doc->pool != NULL && s >= doc->pool && s < doc->pool + doc->pool_cap);
  size_t rel = from_pool ? (size_t)(s - doc->pool) : 0;
  uint32_t off = pool_alloc(doc, len);
  if (off == UINT32_MAX)
    return UINT32_MAX;
  if (from_pool)
    s = doc->pool + rel;
  if (len > 0)
    memcpy(doc->pool + off, s, len);
  return off;
}

static uint32_t node_new(MdDoc *doc, MdNodeType type) {
  if (doc->nodes_len == doc->nodes_cap) {
    uint32_t nc = doc->nodes_cap == 0 ? 128 : doc->nodes_cap * 2;
    MdNode *next = realloc(doc->nodes, (size_t)nc * sizeof(*next));
    if (next == NULL)
      return 0;
    doc->nodes = next;
    doc->nodes_cap = nc;
  }
  uint32_t id = doc->nodes_len++;
  memset(&doc->nodes[id], 0, sizeof(MdNode));
  doc->nodes[id].type = (uint8_t)type;
  return id;
}

static inline void node_append(MdDoc *doc, uint32_t parent, uint32_t child) {
  MdNode *p = &doc->nodes[parent];
  doc->nodes[child].next_sibling = 0;
  if (p->last_child == 0) {
    p->first_child = child;
    p->last_child = child;
  } else {
    doc->nodes[p->last_child].next_sibling = child;
    p->last_child = child;
  }
}

static int add_extra(MdDoc *doc, uint32_t node_id, uint32_t str_off,
                     uint32_t str_len, uint8_t str_in_pool, int32_t num) {
  if (doc->extras_len == doc->extras_cap) {
    uint32_t nc = doc->extras_cap == 0 ? 16 : doc->extras_cap * 2;
    MdExtra *next = realloc(doc->extras, (size_t)nc * sizeof(*next));
    if (next == NULL)
      return -1;
    doc->extras = next;
    doc->extras_cap = nc;
  }
  MdExtra *e = &doc->extras[doc->extras_len++];
  e->node_id = node_id;
  e->str_off = str_off;
  e->str_len = str_len;
  e->str_in_pool = str_in_pool;
  e->num = num;
  doc->nodes[node_id].flags |= MD_F_HAS_EXTRA;
  return 0;
}

const MdExtra *md_find_extra(const MdDoc *doc, uint32_t node_id) {
  if (!(doc->nodes[node_id].flags & MD_F_HAS_EXTRA))
    return NULL;
  for (uint32_t i = 0; i < doc->extras_len; i++) {
    if (doc->extras[i].node_id == node_id)
      return &doc->extras[i];
  }
  return NULL;
}

static int record_anchor(MdDoc *doc, const char *hash_anchor, size_t len,
                         const char *file_path) {
  if (len <= 1)
    return 0;
  if (len >= 1024) {
    fprintf(stderr, "Anchor in %s exceeds 1023 bytes.\n",
            file_path ? file_path : "<input>");
    doc->anchor_overflow = 1;
    return -1;
  }
  if (doc->anchors.used == doc->anchors.size) {
    size_t ns = doc->anchors.size == 0 ? 16 : doc->anchors.size * 2;
    char **next = realloc(doc->anchors.array, ns * sizeof(*next));
    if (next == NULL)
      return -1;
    doc->anchors.array = next;
    doc->anchors.size = ns;
  }
  char *copy = malloc(len + 1);
  if (copy == NULL)
    return -1;
  memcpy(copy, hash_anchor, len);
  copy[len] = '\0';
  doc->anchors.array[doc->anchors.used++] = copy;
  return 0;
}

/* ---- Link reference definition table ---- */

static uint64_t fnv1a_norm(const char *s, size_t len) {
  uint64_t h = 1469598103934665603ULL;
  size_t i = 0;
  while (i < len && is_ws((unsigned char)s[i]))
    i++;
  size_t end = len;
  while (end > i && is_ws((unsigned char)s[end - 1]))
    end--;
  int in_ws = 0;
  for (; i < end; i++) {
    unsigned char c = (unsigned char)s[i];
    if (is_ws(c)) {
      if (!in_ws) {
        h ^= ' ';
        h *= 1099511628211ULL;
        in_ws = 1;
      }
    } else {
      if (c >= 'A' && c <= 'Z')
        c = (unsigned char)(c - 'A' + 'a');
      h ^= c;
      h *= 1099511628211ULL;
      in_ws = 0;
    }
  }
  return h;
}

static uint32_t normalize_label_to_pool(MdDoc *doc, const char *s, size_t len,
                                        uint32_t *out_len) {
  size_t i = 0;
  while (i < len && is_ws((unsigned char)s[i]))
    i++;
  size_t end = len;
  while (end > i && is_ws((unsigned char)s[end - 1]))
    end--;
  if (i >= end) {
    *out_len = 0;
    return 0;
  }
  uint32_t off = pool_alloc(doc, end - i);
  if (off == UINT32_MAX)
    return UINT32_MAX;
  char *dst = doc->pool + off;
  size_t w = 0;
  int in_ws = 0;
  for (; i < end; i++) {
    unsigned char c = (unsigned char)s[i];
    if (is_ws(c)) {
      if (!in_ws) {
        dst[w++] = ' ';
        in_ws = 1;
      }
    } else {
      if (c >= 'A' && c <= 'Z')
        c = (unsigned char)(c - 'A' + 'a');
      dst[w++] = (char)c;
      in_ws = 0;
    }
  }
  dst[w] = '\0';
  *out_len = (uint32_t)w;
  return off;
}

static int ref_label_eq_norm(const char *norm, size_t nlen, const char *raw,
                             size_t rlen) {
  size_t i = 0;
  while (i < rlen && is_ws((unsigned char)raw[i]))
    i++;
  size_t end = rlen;
  while (end > i && is_ws((unsigned char)raw[end - 1]))
    end--;
  size_t ni = 0;
  int in_ws = 0;
  for (; i < end; i++) {
    unsigned char c = (unsigned char)raw[i];
    if (is_ws(c)) {
      if (!in_ws) {
        if (ni >= nlen || norm[ni++] != ' ')
          return 0;
        in_ws = 1;
      }
    } else {
      if (c >= 'A' && c <= 'Z')
        c = (unsigned char)(c - 'A' + 'a');
      if (ni >= nlen || (unsigned char)norm[ni++] != c)
        return 0;
      in_ws = 0;
    }
  }
  return ni == nlen;
}

static int ref_table_grow(MdDoc *doc) {
  uint32_t nc = doc->refs_cap == 0 ? 32 : doc->refs_cap * 2;
  MdRefDef *nt = calloc(nc, sizeof(*nt));
  if (nt == NULL)
    return -1;
  uint32_t mask = nc - 1;
  for (uint32_t i = 0; i < doc->refs_cap; i++) {
    if (doc->refs[i].label_len == 0)
      continue;
    uint32_t slot = (uint32_t)(doc->refs[i].hash & mask);
    while (nt[slot].label_len != 0)
      slot = (slot + 1) & mask;
    nt[slot] = doc->refs[i];
  }
  free(doc->refs);
  doc->refs = nt;
  doc->refs_cap = nc;
  return 0;
}

static void ref_table_insert(MdDoc *doc, const char *label, size_t label_len,
                             uint32_t url_off, uint32_t url_len,
                             uint8_t url_in_pool, uint32_t title_off,
                             uint32_t title_len, uint8_t title_in_pool) {
  if (doc->refs_cap == 0 || (doc->refs_used + 1) * 2 >= doc->refs_cap) {
    if (ref_table_grow(doc) != 0)
      return;
  }
  uint64_t h = fnv1a_norm(label, label_len);
  uint32_t mask = doc->refs_cap - 1;
  uint32_t slot = (uint32_t)(h & mask);
  while (doc->refs[slot].label_len != 0) {
    if (doc->refs[slot].hash == h &&
        ref_label_eq_norm(doc->pool + doc->refs[slot].label_off,
                          doc->refs[slot].label_len, label, label_len)) {
      return; /* First definition wins per CommonMark. */
    }
    slot = (slot + 1) & mask;
  }
  uint32_t norm_len = 0;
  uint32_t norm_off = normalize_label_to_pool(doc, label, label_len, &norm_len);
  if (norm_len == 0 || norm_off == UINT32_MAX)
    return;
  doc->refs[slot].label_off = norm_off;
  doc->refs[slot].label_len = norm_len;
  doc->refs[slot].url_off = url_off;
  doc->refs[slot].url_len = url_len;
  doc->refs[slot].url_in_pool = url_in_pool;
  doc->refs[slot].title_off = title_off;
  doc->refs[slot].title_len = title_len;
  doc->refs[slot].title_in_pool = title_in_pool;
  doc->refs[slot].hash = h;
  doc->refs_used++;
}

static const MdRefDef *ref_table_find(const MdDoc *doc, const char *label,
                                      size_t label_len) {
  if (doc->refs_used == 0 || label_len == 0 || label_len > 999)
    return NULL;
  uint64_t h = fnv1a_norm(label, label_len);
  uint32_t mask = doc->refs_cap - 1;
  uint32_t slot = (uint32_t)(h & mask);
  while (doc->refs[slot].label_len != 0) {
    if (doc->refs[slot].hash == h &&
        ref_label_eq_norm(doc->pool + doc->refs[slot].label_off,
                          doc->refs[slot].label_len, label, label_len)) {
      return &doc->refs[slot];
    }
    slot = (slot + 1) & mask;
  }
  return NULL;
}

/* ---- UTF-8 & HTML5 named/numeric entity decoding ---- */

static size_t encode_utf8(uint32_t cp, char out[4]) {
  if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
    cp = 0xFFFD;
  if (cp < 0x80) {
    out[0] = (char)cp;
    return 1;
  }
  if (cp < 0x800) {
    out[0] = (char)(0xC0 | (cp >> 6));
    out[1] = (char)(0x80 | (cp & 0x3F));
    return 2;
  }
  if (cp < 0x10000) {
    out[0] = (char)(0xE0 | (cp >> 12));
    out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[2] = (char)(0x80 | (cp & 0x3F));
    return 3;
  }
  out[0] = (char)(0xF0 | (cp >> 18));
  out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
  out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
  out[3] = (char)(0x80 | (cp & 0x3F));
  return 4;
}

typedef struct {
  const char *name;
  const char *utf8;
} NamedEntity;

/* Sorted alphabetically by name for O(log N) binary search. */
static const NamedEntity named_entities[] = {
    {"AMP", "&"},
    {"AElig", "\xC3\x86"},
    {"COPY", "\xC2\xA9"},
    {"GT", ">"},
    {"LT", "<"},
    {"QUOT", "\""},
    {"REG", "\xC2\xAE"},
    {"aelig", "\xC3\xA6"},
    {"amp", "&"},
    {"apos", "'"},
    {"bdquo", "\xE2\x80\x9E"},
    {"bull", "\xE2\x80\xA2"},
    {"cent", "\xC2\xA2"},
    {"check", "\xE2\x9C\x93"},
    {"commat", "@"},
    {"copy", "\xC2\xA9"},
    {"cross", "\xE2\x9C\x97"},
    {"curren", "\xC2\xA4"},
    {"dagger", "\xE2\x80\xA0"},
    {"darr", "\xE2\x86\x93"},
    {"deg", "\xC2\xB0"},
    {"divide", "\xC3\xB7"},
    {"emsp", "\xE2\x80\x83"},
    {"ensp", "\xE2\x80\x82"},
    {"euro", "\xE2\x82\xAC"},
    {"excl", "!"},
    {"frac12", "\xC2\xBD"},
    {"frac14", "\xC2\xBC"},
    {"frac34", "\xC2\xBE"},
    {"ge", "\xE2\x89\xA5"},
    {"gt", ">"},
    {"harr", "\xE2\x86\x94"},
    {"hellip", "\xE2\x80\xA6"},
    {"iexcl", "\xC2\xA1"},
    {"infin", "\xE2\x88\x9E"},
    {"iquest", "\xC2\xBF"},
    {"laquo", "\xC2\xAB"},
    {"larr", "\xE2\x86\x90"},
    {"ldquo", "\xE2\x80\x9C"},
    {"le", "\xE2\x89\xA4"},
    {"lowbar", "_"},
    {"lsaquo", "\xE2\x80\xB9"},
    {"lsquo", "\xE2\x80\x98"},
    {"lt", "<"},
    {"mdash", "\xE2\x80\x94"},
    {"micro", "\xC2\xB5"},
    {"middot", "\xC2\xB7"},
    {"minus", "\xE2\x88\x92"},
    {"nbsp", "\xC2\xA0"},
    {"ndash", "\xE2\x80\x93"},
    {"ne", "\xE2\x89\xA0"},
    {"not", "\xC2\xAC"},
    {"num", "#"},
    {"para", "\xC2\xB6"},
    {"permil", "\xE2\x80\xB0"},
    {"plusmn", "\xC2\xB1"},
    {"pound", "\xC2\xA3"},
    {"prime", "\xE2\x80\xB2"},
    {"quot", "\""},
    {"raquo", "\xC2\xBB"},
    {"rarr", "\xE2\x86\x92"},
    {"rdquo", "\xE2\x80\x9D"},
    {"reg", "\xC2\xAE"},
    {"rsaquo", "\xE2\x80\xBA"},
    {"rsquo", "\xE2\x80\x99"},
    {"sbquo", "\xE2\x80\x9A"},
    {"sect", "\xC2\xA7"},
    {"shy", "\xC2\xAD"},
    {"sup1", "\xC2\xB9"},
    {"sup2", "\xC2\xB2"},
    {"sup3", "\xC2\xB3"},
    {"szlig", "\xC3\x9F"},
    {"thinsp", "\xE2\x80\x89"},
    {"times", "\xC3\x97"},
    {"trade", "\xE2\x84\xA2"},
    {"uarr", "\xE2\x86\x91"},
    {"verbar", "|"},
    {"yen", "\xC2\xA5"},
    {"zwj", "\xE2\x80\x8D"},
    {"zwnj", "\xE2\x80\x8C"},
};

static size_t decode_entity(const char *s, size_t len, char out[8],
                            size_t *out_len) {
  if (len < 3 || s[0] != '&')
    return 0;
  if (s[1] == '#') {
    size_t i = 2;
    uint32_t cp = 0;
    if (i < len && (s[i] == 'x' || s[i] == 'X')) {
      i++;
      size_t digits = 0;
      while (i < len && isxdigit((unsigned char)s[i]) && digits < 7) {
        unsigned char c = (unsigned char)s[i++];
        uint32_t v = (c >= '0' && c <= '9')   ? (uint32_t)(c - '0')
                     : (c >= 'a' && c <= 'f') ? (uint32_t)(c - 'a' + 10)
                                              : (uint32_t)(c - 'A' + 10);
        cp = (cp << 4) | v;
        digits++;
      }
      if (digits == 0 || digits > 6 || i >= len || s[i] != ';')
        return 0;
      *out_len = encode_utf8(cp, out);
      return i + 1;
    } else {
      size_t digits = 0;
      while (i < len && isdigit((unsigned char)s[i]) && digits < 8) {
        cp = cp * 10 + (uint32_t)(s[i++] - '0');
        digits++;
      }
      if (digits == 0 || digits > 7 || i >= len || s[i] != ';')
        return 0;
      *out_len = encode_utf8(cp, out);
      return i + 1;
    }
  }
  size_t i = 1;
  while (i < len && i < 32 && isalnum((unsigned char)s[i]))
    i++;
  if (i <= 1 || i >= len || s[i] != ';')
    return 0;
  size_t nlen = i - 1;
  size_t lo = 0;
  size_t hi = sizeof(named_entities) / sizeof(named_entities[0]);
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    const char *en = named_entities[mid].name;
    size_t el = strlen(en);
    size_t ml = nlen < el ? nlen : el;
    int cmp = memcmp(s + 1, en, ml);
    if (cmp == 0) {
      if (nlen == el) {
        size_t ulen = strlen(named_entities[mid].utf8);
        memcpy(out, named_entities[mid].utf8, ulen);
        *out_len = ulen;
        return i + 1;
      }
      cmp = nlen < el ? -1 : 1;
    }
    if (cmp < 0)
      hi = mid;
    else
      lo = mid + 1;
  }
  return 0;
}

static uint32_t unescape_slice(MdDoc *doc, const char *s, size_t len,
                               uint32_t *out_len, uint8_t *in_pool) {
  int needs = 0;
  for (size_t i = 0; i < len; i++) {
    if (s[i] == '\\' || s[i] == '&') {
      needs = 1;
      break;
    }
  }
  if (!needs) {
    *out_len = (uint32_t)len;
    if (s >= doc->src && s <= doc->src + doc->src_len) {
      *in_pool = 0;
      return (uint32_t)(s - doc->src);
    }
    *in_pool = 1;
    return pool_copy(doc, s, len);
  }
  int from_pool =
      (doc->pool != NULL && s >= doc->pool && s < doc->pool + doc->pool_cap);
  size_t rel = from_pool ? (size_t)(s - doc->pool) : 0;
  uint32_t off = pool_alloc(doc, len);
  if (off == UINT32_MAX) {
    *out_len = 0;
    *in_pool = 1;
    return 0;
  }
  if (from_pool)
    s = doc->pool + rel;
  char *dst = doc->pool + off;
  size_t w = 0;
  for (size_t i = 0; i < len;) {
    if (s[i] == '\\' && i + 1 < len &&
        is_ascii_punct((unsigned char)s[i + 1])) {
      dst[w++] = s[i + 1];
      i += 2;
    } else if (s[i] == '&') {
      char ubuf[8];
      size_t ulen = 0;
      size_t consumed = decode_entity(s + i, len - i, ubuf, &ulen);
      if (consumed > 0) {
        memcpy(dst + w, ubuf, ulen);
        w += ulen;
        i += consumed;
      } else {
        dst[w++] = s[i++];
      }
    } else {
      dst[w++] = s[i++];
    }
  }
  dst[w] = '\0';
  *out_len = (uint32_t)w;
  *in_pool = 1;
  return off;
}

/* ---- Inline Parser (strictly O(n) with bracket & delimiter stacks) ---- */

static const unsigned char inline_special[256] = {
    ['\n'] = 1, ['\r'] = 1, ['`'] = 1, ['*'] = 1,  ['_'] = 1, ['['] = 1,
    [']'] = 1,  ['!'] = 1,  ['<'] = 1, ['\\'] = 1, ['&'] = 1,
};

static const char *
parse_inline_link_dest(MdDoc *doc, const char *p, const char *end,
                       uint32_t *url_off, uint32_t *url_len,
                       uint8_t *url_in_pool, uint32_t *title_off,
                       uint32_t *title_len, uint8_t *title_in_pool) {
  *url_off = 0;
  *url_len = 0;
  *url_in_pool = 0;
  *title_off = 0;
  *title_len = 0;
  *title_in_pool = 0;

  while (p < end && is_ws((unsigned char)*p))
    p++;
  if (p >= end)
    return NULL;

  const char *u_start = p;
  const char *u_end = p;
  if (*p == ')') {
    return p + 1;
  } else if (*p == '<') {
    p++;
    u_start = p;
    while (p < end && *p != '>' && *p != '\n' && *p != '\r') {
      if (*p == '\\' && p + 1 < end)
        p += 2;
      else if (*p == '<')
        return NULL;
      else
        p++;
    }
    if (p >= end || *p != '>')
      return NULL;
    u_end = p;
    p++;
  } else {
    int parens = 0;
    u_start = p;
    while (p < end && !is_ws((unsigned char)*p)) {
      if (*p == '\\' && p + 1 < end && is_ascii_punct((unsigned char)p[1])) {
        p += 2;
      } else if (*p == '(') {
        parens++;
        if (parens > 32)
          return NULL;
        p++;
      } else if (*p == ')') {
        if (parens == 0)
          break;
        parens--;
        p++;
      } else if ((unsigned char)*p < 0x20 || *p == 0x7f) {
        return NULL;
      } else {
        p++;
      }
    }
    if (parens != 0)
      return NULL;
    u_end = p;
  }

  const char *ws_start = p;
  while (p < end && is_ws((unsigned char)*p))
    p++;
  const char *t_start = NULL, *t_end = NULL;
  if (p < end && *p != ')') {
    if (p == ws_start)
      return NULL;
    char quote = *p;
    char close_q = (quote == '"')    ? '"'
                   : (quote == '\'') ? '\''
                   : (quote == '(')  ? ')'
                                     : 0;
    if (close_q == 0)
      return NULL;
    p++;
    t_start = p;
    while (p < end && *p != close_q) {
      if (*p == '\\' && p + 1 < end)
        p += 2;
      else
        p++;
    }
    if (p >= end || *p != close_q)
      return NULL;
    t_end = p;
    p++;
    while (p < end && is_ws((unsigned char)*p))
      p++;
  }
  if (p >= end || *p != ')')
    return NULL;

  *url_off = unescape_slice(doc, u_start, (size_t)(u_end - u_start), url_len,
                            url_in_pool);
  if (t_start != NULL) {
    *title_off = unescape_slice(doc, t_start, (size_t)(t_end - t_start),
                                title_len, title_in_pool);
  }
  return p + 1;
}

static size_t match_inline_html(const char *s, size_t len) {
  if (len < 3 || s[0] != '<')
    return 0;
  if (len >= 5 && memcmp(s, "<!--", 4) == 0) {
    if (s[4] == '>')
      return 5;
    if (len >= 6 && s[4] == '-' && s[5] == '>')
      return 6;
    if (len >= 7) {
      const char *cl = md_memmem(s + 4, len - 4, "-->", 3);
      if (cl != NULL)
        return (size_t)(cl + 3 - s);
    }
    return 0;
  }
  if (len >= 4 && s[1] == '?') {
    const char *cl = md_memmem(s + 2, len - 2, "?>", 2);
    if (cl != NULL)
      return (size_t)(cl + 2 - s);
    return 0;
  }
  if (len >= 12 && memcmp(s, "<![CDATA[", 9) == 0) {
    const char *cl = md_memmem(s + 9, len - 9, "]]>", 3);
    if (cl != NULL)
      return (size_t)(cl + 3 - s);
    return 0;
  }
  if (s[1] == '/') {
    size_t i = 2;
    if (i >= len || !isalpha((unsigned char)s[i]))
      return 0;
    while (i < len && (isalnum((unsigned char)s[i]) || s[i] == '-'))
      i++;
    while (i < len && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n'))
      i++;
    return (i < len && s[i] == '>') ? i + 1 : 0;
  }
  if (isalpha((unsigned char)s[1])) {
    size_t i = 2;
    while (i < len && (isalnum((unsigned char)s[i]) || s[i] == '-'))
      i++;
    while (i < len) {
      if (s[i] == '>')
        return i + 1;
      if (s[i] == '/' && i + 1 < len && s[i + 1] == '>')
        return i + 2;
      if (!is_ws((unsigned char)s[i]))
        return 0;
      while (i < len && is_ws((unsigned char)s[i]))
        i++;
      if (i < len &&
          (s[i] == '>' || (s[i] == '/' && i + 1 < len && s[i + 1] == '>')))
        continue;
      if (i >= len ||
          !(isalpha((unsigned char)s[i]) || s[i] == '_' || s[i] == ':'))
        return 0;
      while (i < len && (isalnum((unsigned char)s[i]) || s[i] == '_' ||
                         s[i] == '.' || s[i] == ':' || s[i] == '-'))
        i++;
      size_t sp = i;
      while (sp < len && is_ws((unsigned char)s[sp]))
        sp++;
      if (sp < len && s[sp] == '=') {
        i = sp + 1;
        while (i < len && is_ws((unsigned char)s[i]))
          i++;
        if (i >= len)
          return 0;
        if (s[i] == '"' || s[i] == '\'') {
          char q = s[i++];
          while (i < len && s[i] != q)
            i++;
          if (i >= len)
            return 0;
          i++;
        } else {
          size_t vs = i;
          while (i < len && !is_ws((unsigned char)s[i]) && s[i] != '"' &&
                 s[i] != '\'' && s[i] != '=' && s[i] != '<' && s[i] != '>' &&
                 s[i] != '`')
            i++;
          if (i == vs)
            return 0;
        }
      }
    }
  }
  return 0;
}

static size_t match_autolink(const char *s, size_t len, int *is_email) {
  *is_email = 0;
  if (len < 4 || s[0] != '<')
    return 0;
  const char *gt = memchr(s + 1, '>', len - 1);
  if (gt == NULL)
    return 0;
  size_t inner = (size_t)(gt - (s + 1));
  if (inner < 3)
    return 0;
  for (size_t i = 0; i < inner; i++) {
    unsigned char c = (unsigned char)s[1 + i];
    if (c <= 0x20 || c == '<' || c == '>')
      return 0;
  }
  if (isalpha((unsigned char)s[1])) {
    size_t i = 1;
    while (i < inner && i < 32 &&
           (isalnum((unsigned char)s[1 + i]) || s[1 + i] == '+' ||
            s[1 + i] == '.' || s[1 + i] == '-'))
      i++;
    if (i >= 2 && i < inner && s[1 + i] == ':')
      return inner + 2;
  }
  const char *at = memchr(s + 1, '@', inner);
  if (at != NULL && at > s + 1 && at < gt - 1) {
    const char *dot = memchr(at + 1, '.', (size_t)(gt - (at + 1)));
    if (dot != NULL && dot > at + 1 && dot < gt - 1) {
      *is_email = 1;
      return inner + 2;
    }
  }
  return 0;
}

static uint32_t append_text_node(MdDoc *doc, uint32_t parent, const char *s,
                                 size_t len, int in_pool) {
  if (len == 0)
    return 0;
  uint32_t last = doc->nodes[parent].last_child;
  if (last != 0 && doc->nodes[last].type == MD_TEXT &&
      doc->nodes[last].info == 0) {
    MdNode *ln = &doc->nodes[last];
    int last_pool = (ln->flags & MD_F_TEXT_POOL) != 0;
    if (!last_pool && !in_pool && s == doc->src + ln->text_off + ln->text_len) {
      ln->text_len += (uint32_t)len;
      return last;
    }
    if (last_pool && in_pool && s == doc->pool + ln->text_off + ln->text_len) {
      ln->text_len += (uint32_t)len;
      return last;
    }
  }
  uint32_t id = node_new(doc, MD_TEXT);
  if (id == 0)
    return 0;
  if (in_pool) {
    doc->nodes[id].text_off = (uint32_t)(s - doc->pool);
    doc->nodes[id].flags |= MD_F_TEXT_POOL;
  } else if (s >= doc->src && s <= doc->src + doc->src_len) {
    doc->nodes[id].text_off = (uint32_t)(s - doc->src);
  } else {
    uint32_t off = pool_copy(doc, s, len);
    if (off == UINT32_MAX)
      return 0;
    doc->nodes[id].text_off = off;
    doc->nodes[id].flags |= MD_F_TEXT_POOL;
  }
  doc->nodes[id].text_len = (uint32_t)len;
  node_append(doc, parent, id);
  return id;
}

static const char *match_brace_id(const char *p, const char *end,
                                  const char **id_start, size_t *id_len) {
  if (p + 3 >= end || p[0] != '{' || p[1] != '#')
    return NULL;
  const char *q = p + 2;
  while (q < end && *q != '}' && !is_ws((unsigned char)*q) && *q != '[')
    q++;
  if (q >= end || *q != '}' || q == p + 2)
    return NULL;
  *id_start = p + 2;
  *id_len = (size_t)(q - (p + 2));
  return q + 1;
}

static void collect_braced_anchors_in_text(MdDoc *doc, const char *s,
                                           size_t len, const char *file_path) {
  const char *end = s + len;
  const char *p = memchr(s, '{', len);
  while (p != NULL && p < end) {
    const char *close = memchr(p + 1, '}', (size_t)(end - (p + 1)));
    if (close == NULL)
      break;
    const char *hash = memchr(p + 1, '#', (size_t)(close - (p + 1)));
    if (hash != NULL) {
      const char *ae = hash + 1;
      while (ae < close && !is_ws((unsigned char)*ae))
        ae++;
      if (ae > hash + 1)
        record_anchor(doc, hash, (size_t)(ae - hash), file_path);
    }
    p = memchr(close + 1, '{', (size_t)(end - (close + 1)));
  }
}

typedef struct {
  uint32_t node_id;
  char ch;
  uint16_t count;
  uint16_t orig_count;
  uint8_t can_open;
  uint8_t can_close;
} EmphDelim;

typedef struct {
  uint32_t node_id;      /* literal '[' or '![' MD_TEXT node in parent */
  size_t delim_bottom;   /* delimiter stack index when opener was pushed */
  const char *label_pos; /* pointer right after '[' for ref-link lookup */
  uint8_t is_img;
  uint8_t active;
} BracketOpener;

static void wrap_node_range(MdDoc *doc, uint32_t parent, uint32_t prev_of_first,
                            uint32_t first, uint32_t last, uint32_t wrapper) {
  uint32_t after_last = doc->nodes[last].next_sibling;
  doc->nodes[wrapper].first_child = first;
  doc->nodes[wrapper].last_child = last;
  doc->nodes[last].next_sibling = 0;
  doc->nodes[wrapper].next_sibling = after_last;
  if (prev_of_first == 0)
    doc->nodes[parent].first_child = wrapper;
  else
    doc->nodes[prev_of_first].next_sibling = wrapper;
  if (doc->nodes[parent].last_child == last)
    doc->nodes[parent].last_child = wrapper;
}

/* Strictly O(n) CommonMark emphasis resolution from stack_bottom..ndelims. */
static void process_emph_delims(MdDoc *doc, uint32_t parent, EmphDelim *delims,
                                size_t *ndelims_ptr, size_t stack_bottom,
                                uint32_t start_cleanup_after) {
  size_t ndelims = *ndelims_ptr;
  if (ndelims <= stack_bottom + 1) {
    *ndelims_ptr = stack_bottom;
    return;
  }

  /* CommonMark Appendix A openers_bottom[char_idx][orig_len%3][can_open] */
  size_t openers_bottom[2][3][2];
  for (int a = 0; a < 2; a++)
    for (int b = 0; b < 3; b++)
      for (int c = 0; c < 2; c++)
        openers_bottom[a][b][c] = stack_bottom;

  for (size_t ci = stack_bottom + 1; ci < ndelims; ci++) {
    if (!delims[ci].can_close || delims[ci].count == 0)
      continue;
    int ch_idx = delims[ci].ch == '_' ? 1 : 0;
    int mod3 = (int)(delims[ci].orig_count % 3);
    int c_open = delims[ci].can_open ? 1 : 0;

    while (delims[ci].count > 0) {
      size_t bot = openers_bottom[ch_idx][mod3][c_open];
      size_t oi = ci;
      int found = 0;
      while (oi > bot) {
        oi--;
        if (!delims[oi].can_open || delims[oi].count == 0 ||
            delims[oi].ch != delims[ci].ch)
          continue;
        if ((delims[oi].can_close || delims[ci].can_open) &&
            (delims[ci].orig_count % 3 != 0) &&
            ((delims[oi].orig_count + delims[ci].orig_count) % 3 == 0))
          continue;
        found = 1;
        break;
      }
      if (!found) {
        openers_bottom[ch_idx][mod3][c_open] = ci;
        if (!delims[ci].can_open)
          delims[ci].count = 0;
        break;
      }
      uint16_t use = (delims[oi].count >= 2 && delims[ci].count >= 2) ? 2 : 1;
      uint32_t onode = delims[oi].node_id;
      uint32_t cnode = delims[ci].node_id;
      doc->nodes[onode].text_len -= use;
      doc->nodes[cnode].text_off += use;
      doc->nodes[cnode].text_len -= use;
      delims[oi].count -= use;
      delims[ci].count -= use;

      uint32_t first_in = doc->nodes[onode].next_sibling;
      if (first_in != 0 && first_in != cnode) {
        uint32_t last_in = first_in;
        while (doc->nodes[last_in].next_sibling != cnode &&
               doc->nodes[last_in].next_sibling != 0) {
          last_in = doc->nodes[last_in].next_sibling;
        }
        if (doc->nodes[last_in].next_sibling == cnode) {
          uint32_t wrap = node_new(doc, use == 2 ? MD_STRONG : MD_EMPH);
          if (wrap != 0)
            wrap_node_range(doc, parent, onode, first_in, last_in, wrap);
        }
      }
      for (size_t k = oi + 1; k < ci; k++)
        delims[k].count = 0;
    }
  }

  /* Unlink emptied delimiter nodes strictly after start_cleanup_after */
  uint32_t prev = start_cleanup_after;
  uint32_t cur = start_cleanup_after == 0
                     ? doc->nodes[parent].first_child
                     : doc->nodes[start_cleanup_after].next_sibling;
  while (cur != 0) {
    uint32_t nxt = doc->nodes[cur].next_sibling;
    if (doc->nodes[cur].type == MD_TEXT && doc->nodes[cur].text_len == 0) {
      if (prev == 0)
        doc->nodes[parent].first_child = nxt;
      else
        doc->nodes[prev].next_sibling = nxt;
      if (doc->nodes[parent].last_child == cur)
        doc->nodes[parent].last_child = prev;
    } else {
      prev = cur;
    }
    cur = nxt;
  }
  *ndelims_ptr = stack_bottom;
}

static void parse_inlines_range(MdDoc *doc, uint32_t parent, const char *start,
                                const char *end, const char *file_path,
                                int collect_meta) {
  EmphDelim stack_delims[64];
  EmphDelim *delims = stack_delims;
  size_t ndelims = 0, cap_delims = 64;

  BracketOpener stack_brackets[32];
  BracketOpener *brackets = stack_brackets;
  size_t nbrackets = 0, cap_brackets = 32;

  /* Memoize backtick run lengths (1..63) that have no matching closer ahead. */
  uint8_t failed_bt[64];
  memset(failed_bt, 0, sizeof(failed_bt));

  const char *p = start;
  while (p < end) {
    const char *run = p;
    while (p < end && !inline_special[(unsigned char)*p])
      p++;
    if (p > run) {
      append_text_node(doc, parent, run, (size_t)(p - run), 0);
      if (collect_meta)
        collect_braced_anchors_in_text(doc, run, (size_t)(p - run), file_path);
    }
    if (p >= end)
      break;

    char c = *p;
    if (c == '\r') {
      p++;
      continue;
    }
    if (c == '\n') {
      int hard = 0;
      uint32_t last = doc->nodes[parent].last_child;
      if (last != 0 && doc->nodes[last].type == MD_TEXT &&
          doc->nodes[last].info == 0) {
        MdNode *ln = &doc->nodes[last];
        const char *tp = md_text_ptr(doc, ln);
        size_t sp = 0;
        while (sp < ln->text_len && tp[ln->text_len - 1 - sp] == ' ')
          sp++;
        if (sp >= 2)
          hard = 1;
        while (sp < ln->text_len && (tp[ln->text_len - 1 - sp] == ' ' ||
                                     tp[ln->text_len - 1 - sp] == '\t'))
          sp++;
        ln->text_len -= (uint32_t)sp;
      }
      p++;
      while (p < end && *p == ' ')
        p++;
      uint32_t br = node_new(doc, hard ? MD_LINEBREAK : MD_SOFTBREAK);
      if (br != 0)
        node_append(doc, parent, br);
      continue;
    }

    if (c == '\\') {
      if (p + 1 < end && p[1] == '\n') {
        uint32_t br = node_new(doc, MD_LINEBREAK);
        if (br != 0)
          node_append(doc, parent, br);
        p += 2;
        while (p < end && *p == ' ')
          p++;
        continue;
      }
      if (p + 1 < end && is_ascii_punct((unsigned char)p[1])) {
        append_text_node(doc, parent, p + 1, 1, 0);
        p += 2;
        continue;
      }
      append_text_node(doc, parent, p, 1, 0);
      p++;
      continue;
    }

    if (c == '`') {
      const char *tick_start = p;
      size_t ticks = 0;
      while (p < end && *p == '`') {
        ticks++;
        p++;
      }
      if (ticks < 64 && failed_bt[ticks]) {
        append_text_node(doc, parent, tick_start, ticks, 0);
        continue;
      }
      const char *cs = p;
      const char *q = p;
      const char *ce = NULL;
      while (q < end) {
        q = memchr(q, '`', (size_t)(end - q));
        if (q == NULL)
          break;
        const char *cand_end = q;
        size_t ct = 0;
        while (q < end && *q == '`') {
          ct++;
          q++;
        }
        if (ct == ticks) {
          ce = cand_end;
          p = q;
          break;
        }
      }
      if (ce == NULL) {
        if (ticks < 64)
          failed_bt[ticks] = 1;
        append_text_node(doc, parent, tick_start, ticks, 0);
        continue;
      }
      size_t clen = (size_t)(ce - cs);
      int has_nl =
          memchr(cs, '\n', clen) != NULL || memchr(cs, '\r', clen) != NULL;
      uint32_t cid = node_new(doc, MD_CODE);
      if (cid != 0) {
        if (!has_nl) {
          if (clen >= 2 && cs[0] == ' ' && cs[clen - 1] == ' ') {
            int all_sp = 1;
            for (size_t k = 0; k < clen; k++) {
              if (cs[k] != ' ') {
                all_sp = 0;
                break;
              }
            }
            if (!all_sp) {
              cs++;
              clen -= 2;
            }
          }
          if (cs >= doc->src && cs <= doc->src + doc->src_len) {
            doc->nodes[cid].text_off = (uint32_t)(cs - doc->src);
          } else {
            doc->nodes[cid].text_off = pool_copy(doc, cs, clen);
            doc->nodes[cid].flags |= MD_F_TEXT_POOL;
          }
          doc->nodes[cid].text_len = (uint32_t)clen;
        } else {
          uint32_t off = pool_alloc(doc, clen);
          if (off != UINT32_MAX) {
            char *dst = doc->pool + off;
            size_t w = 0;
            for (size_t k = 0; k < clen; k++) {
              if (cs[k] == '\r')
                continue;
              dst[w++] = (cs[k] == '\n') ? ' ' : cs[k];
            }
            size_t s0 = 0;
            if (w >= 2 && dst[0] == ' ' && dst[w - 1] == ' ') {
              int all_sp = 1;
              for (size_t k = 0; k < w; k++) {
                if (dst[k] != ' ') {
                  all_sp = 0;
                  break;
                }
              }
              if (!all_sp) {
                s0 = 1;
                w -= 2;
              }
            }
            doc->nodes[cid].text_off = off + (uint32_t)s0;
            doc->nodes[cid].text_len = (uint32_t)w;
            doc->nodes[cid].flags |= MD_F_TEXT_POOL;
          }
        }
        node_append(doc, parent, cid);
      }
      continue;
    }

    if (c == '&') {
      char ubuf[8];
      size_t ulen = 0;
      size_t consumed = decode_entity(p, (size_t)(end - p), ubuf, &ulen);
      if (consumed > 0) {
        uint32_t off = pool_copy(doc, ubuf, ulen);
        if (off != UINT32_MAX)
          append_text_node(doc, parent, doc->pool + off, ulen, 1);
        p += consumed;
        continue;
      }
      append_text_node(doc, parent, p, 1, 0);
      p++;
      continue;
    }

    if (c == '<') {
      int is_email = 0;
      size_t alen = match_autolink(p, (size_t)(end - p), &is_email);
      if (alen > 0) {
        uint32_t lid = node_new(doc, MD_LINK);
        if (lid != 0) {
          const char *inner = p + 1;
          size_t ilen = alen - 2;
          doc->nodes[lid].flags |= MD_F_AUTOLINK;
          if (is_email) {
            uint32_t uoff = pool_alloc(doc, 7 + ilen);
            if (uoff != UINT32_MAX) {
              memcpy(doc->pool + uoff, "mailto:", 7);
              memcpy(doc->pool + uoff + 7, inner, ilen);
              doc->nodes[lid].aux_off = uoff;
              doc->nodes[lid].aux_len = (uint32_t)(7 + ilen);
              doc->nodes[lid].flags |= MD_F_AUX_POOL;
            }
          } else if (inner >= doc->src && inner <= doc->src + doc->src_len) {
            doc->nodes[lid].aux_off = (uint32_t)(inner - doc->src);
            doc->nodes[lid].aux_len = (uint32_t)ilen;
          } else {
            doc->nodes[lid].aux_off = pool_copy(doc, inner, ilen);
            doc->nodes[lid].aux_len = (uint32_t)ilen;
            doc->nodes[lid].flags |= MD_F_AUX_POOL;
          }
          append_text_node(doc, lid, inner, ilen, 0);
          node_append(doc, parent, lid);
        }
        p += alen;
        continue;
      }
      size_t hlen = match_inline_html(p, (size_t)(end - p));
      if (hlen > 0) {
        uint32_t hid = node_new(doc, MD_HTML_INLINE);
        if (hid != 0) {
          if (p >= doc->src && p <= doc->src + doc->src_len) {
            doc->nodes[hid].text_off = (uint32_t)(p - doc->src);
          } else {
            doc->nodes[hid].text_off = pool_copy(doc, p, hlen);
            doc->nodes[hid].flags |= MD_F_TEXT_POOL;
          }
          doc->nodes[hid].text_len = (uint32_t)hlen;
          node_append(doc, parent, hid);
        }
        p += hlen;
        continue;
      }
      append_text_node(doc, parent, p, 1, 0);
      p++;
      continue;
    }

    if (c == '*' || c == '_') {
      const char *dstart = p;
      while (p < end && *p == c)
        p++;
      size_t cnt = (size_t)(p - dstart);
      unsigned char before = (dstart > start) ? (unsigned char)dstart[-1] : ' ';
      unsigned char after = (p < end) ? (unsigned char)*p : ' ';
      int b_ws = is_ws(before);
      int a_ws = is_ws(after);
      int b_punct = is_ascii_punct(before);
      int a_punct = is_ascii_punct(after);
      int left_flank = !a_ws && (!a_punct || b_ws || b_punct);
      int right_flank = !b_ws && (!b_punct || a_ws || a_punct);
      int can_open =
          (c == '*') ? left_flank : (left_flank && (!right_flank || b_punct));
      int can_close =
          (c == '*') ? right_flank : (right_flank && (!left_flank || a_punct));
      uint32_t tid = node_new(doc, MD_TEXT);
      if (tid != 0) {
        doc->nodes[tid].info = 1; /* delimiter run: do not coalesce */
        if (dstart >= doc->src && dstart <= doc->src + doc->src_len) {
          doc->nodes[tid].text_off = (uint32_t)(dstart - doc->src);
        } else {
          doc->nodes[tid].text_off = pool_copy(doc, dstart, cnt);
          doc->nodes[tid].flags |= MD_F_TEXT_POOL;
        }
        doc->nodes[tid].text_len = (uint32_t)cnt;
        node_append(doc, parent, tid);
        if (can_open || can_close) {
          if (ndelims == cap_delims) {
            size_t nc = cap_delims * 2;
            EmphDelim *nd = (delims == stack_delims)
                                ? malloc(nc * sizeof(*nd))
                                : realloc(delims, nc * sizeof(*nd));
            if (nd != NULL) {
              if (delims == stack_delims)
                memcpy(nd, stack_delims, ndelims * sizeof(*nd));
              delims = nd;
              cap_delims = nc;
            }
          }
          if (ndelims < cap_delims) {
            delims[ndelims++] = (EmphDelim){
                .node_id = tid,
                .ch = c,
                .count = (uint16_t)cnt,
                .orig_count = (uint16_t)cnt,
                .can_open = (uint8_t)can_open,
                .can_close = (uint8_t)can_close,
            };
          }
        }
      }
      continue;
    }

    /* Push '[' or '![' onto bracket opener stack (strictly O(1)!) */
    if (c == '[' || (c == '!' && p + 1 < end && p[1] == '[')) {
      int is_img = (c == '!');
      size_t mlen = is_img ? 2 : 1;
      uint32_t tid = node_new(doc, MD_TEXT);
      if (tid != 0) {
        doc->nodes[tid].info = 1; /* bracket marker: do not coalesce */
        if (p >= doc->src && p <= doc->src + doc->src_len) {
          doc->nodes[tid].text_off = (uint32_t)(p - doc->src);
        } else {
          doc->nodes[tid].text_off = pool_copy(doc, p, mlen);
          doc->nodes[tid].flags |= MD_F_TEXT_POOL;
        }
        doc->nodes[tid].text_len = (uint32_t)mlen;
        node_append(doc, parent, tid);
        if (nbrackets == cap_brackets) {
          size_t nc = cap_brackets * 2;
          BracketOpener *nb = (brackets == stack_brackets)
                                  ? malloc(nc * sizeof(*nb))
                                  : realloc(brackets, nc * sizeof(*nb));
          if (nb != NULL) {
            if (brackets == stack_brackets)
              memcpy(nb, stack_brackets, nbrackets * sizeof(*nb));
            brackets = nb;
            cap_brackets = nc;
          }
        }
        if (nbrackets < cap_brackets) {
          brackets[nbrackets++] = (BracketOpener){
              .node_id = tid,
              .delim_bottom = ndelims,
              .label_pos = p + mlen,
              .is_img = (uint8_t)is_img,
              .active = 1,
          };
        }
      }
      p += mlen;
      continue;
    }

    /* Close ']' using top of bracket opener stack (strictly O(1)!) */
    if (c == ']' && nbrackets > 0) {
      BracketOpener op = brackets[--nbrackets];
      if (!op.active) {
        append_text_node(doc, parent, p, 1, 0);
        p++;
        continue;
      }

      const char *after = p + 1;
      MdNodeType matched_type = MD_NONE;
      uint32_t aux_off = 0, aux_len = 0;
      uint8_t aux_in_pool = 0;
      uint32_t title_off = 0, title_len = 0;
      uint8_t title_in_pool = 0;
      const char *next_p = NULL;
      const char *span_id_s = NULL;
      size_t span_id_l = 0;

      /* 1. Native mmdoc bracketed span: [content]{#id} */
      if (!op.is_img && after < end && *after == '{') {
        const char *ab = match_brace_id(after, end, &span_id_s, &span_id_l);
        if (ab != NULL) {
          matched_type = MD_SPAN;
          aux_off =
              unescape_slice(doc, span_id_s, span_id_l, &aux_len, &aux_in_pool);
          next_p = ab;
        }
      }

      /* 2. Inline link or image: [content](url "title") */
      if (matched_type == MD_NONE && after < end && *after == '(') {
        const char *alp = parse_inline_link_dest(
            doc, after + 1, end, &aux_off, &aux_len, &aux_in_pool, &title_off,
            &title_len, &title_in_pool);
        if (alp != NULL) {
          matched_type = op.is_img ? MD_IMAGE : MD_LINK;
          next_p = alp;
        }
      }

      /* 3. Reference link or image: [text][ref], [ref][], [ref] */
      if (matched_type == MD_NONE && doc->refs_used > 0) {
        const char *ref_s = op.label_pos;
        size_t ref_l = (size_t)(p - op.label_pos);
        const char *after_ref = after;
        if (after < end && *after == '[') {
          const char *q = after + 1;
          while (q < end && *q != ']' && *q != '[') {
            if (*q == '\\' && q + 1 < end)
              q += 2;
            else
              q++;
          }
          if (q < end && *q == ']') {
            if (q > after + 1) {
              ref_s = after + 1;
              ref_l = (size_t)(q - (after + 1));
            }
            after_ref = q + 1;
          }
        }
        const MdRefDef *rd = ref_table_find(doc, ref_s, ref_l);
        if (rd != NULL) {
          matched_type = op.is_img ? MD_IMAGE : MD_LINK;
          aux_off = rd->url_off;
          aux_len = rd->url_len;
          aux_in_pool = rd->url_in_pool;
          title_off = rd->title_off;
          title_len = rd->title_len;
          title_in_pool = rd->title_in_pool;
          next_p = after_ref;
        }
      }

      if (matched_type != MD_NONE) {
        /* Resolve emphasis strictly inside the bracketed children first */
        process_emph_delims(doc, parent, delims, &ndelims, op.delim_bottom,
                            op.node_id);

        /* Mutate op.node_id in place into the wrapper (O(1), zero unlink!) */
        uint32_t first_in = doc->nodes[op.node_id].next_sibling;
        uint32_t last_in = first_in ? doc->nodes[parent].last_child : 0;
        MdNode *wn = &doc->nodes[op.node_id];
        memset(wn, 0, sizeof(*wn));
        wn->type = (uint8_t)matched_type;
        wn->first_child = first_in;
        wn->last_child = last_in;
        wn->next_sibling = 0;
        wn->aux_off = aux_off;
        wn->aux_len = aux_len;
        if (aux_in_pool)
          wn->flags |= MD_F_AUX_POOL;
        doc->nodes[parent].last_child = op.node_id;

        if (title_len > 0)
          add_extra(doc, op.node_id, title_off, title_len, title_in_pool, 0);

        if (matched_type == MD_SPAN && collect_meta && aux_len > 0) {
          char stack_a[256];
          char *ab =
              aux_len + 2 <= sizeof(stack_a) ? stack_a : malloc(aux_len + 2);
          if (ab != NULL) {
            ab[0] = '#';
            memcpy(ab + 1,
                   aux_in_pool ? (doc->pool + aux_off) : (doc->src + aux_off),
                   aux_len);
            ab[aux_len + 1] = '\0';
            record_anchor(doc, ab, aux_len + 1, file_path);
            if (ab != stack_a)
              free(ab);
          }
        }

        /* Links cannot contain links: deactivate earlier non-image openers */
        if (matched_type == MD_LINK) {
          for (size_t k = 0; k < nbrackets; k++) {
            if (!brackets[k].is_img)
              brackets[k].active = 0;
          }
        }
        p = next_p;
        continue;
      }

      append_text_node(doc, parent, p, 1, 0);
      p++;
      continue;
    }

    append_text_node(doc, parent, p, 1, 0);
    p++;
  }

  process_emph_delims(doc, parent, delims, &ndelims, 0, 0);
  if (delims != stack_delims)
    free(delims);
  if (brackets != stack_brackets)
    free(brackets);
}

/* ---- Heading attribute extraction & title collection ---- */

static void extract_plain_title_rec(const MdDoc *doc, uint32_t node_id,
                                    char **buf, size_t *len, size_t *cap) {
  for (uint32_t c = doc->nodes[node_id].first_child; c != 0;
       c = doc->nodes[c].next_sibling) {
    const MdNode *n = &doc->nodes[c];
    if (n->type == MD_TEXT || n->type == MD_CODE) {
      if (n->text_len > 0) {
        if (*len + n->text_len + 1 > *cap) {
          size_t nc = *cap == 0 ? 128 : *cap * 2;
          while (nc < *len + n->text_len + 1)
            nc *= 2;
          char *nb = realloc(*buf, nc);
          if (nb == NULL)
            return;
          *buf = nb;
          *cap = nc;
        }
        memcpy(*buf + *len, md_text_ptr(doc, n), n->text_len);
        *len += n->text_len;
        (*buf)[*len] = '\0';
      }
    } else if (n->first_child != 0) {
      extract_plain_title_rec(doc, c, buf, len, cap);
    }
  }
}

static void set_heading_anchor(MdDoc *doc, uint32_t hid, const char *id_s,
                               size_t id_l, const char *file_path,
                               int collect_meta) {
  uint32_t ulen = 0;
  uint8_t uip = 0;
  uint32_t uoff = unescape_slice(doc, id_s, id_l, &ulen, &uip);
  doc->nodes[hid].aux_off = uoff;
  doc->nodes[hid].aux_len = ulen;
  if (uip)
    doc->nodes[hid].flags |= MD_F_AUX_POOL;
  if (collect_meta && ulen > 0) {
    char stack_a[256];
    char *ab = ulen + 2 <= sizeof(stack_a) ? stack_a : malloc(ulen + 2);
    if (ab != NULL) {
      ab[0] = '#';
      memcpy(ab + 1, uip ? (doc->pool + uoff) : (doc->src + uoff), ulen);
      ab[ulen + 1] = '\0';
      record_anchor(doc, ab, ulen + 1, file_path);
      if (ab != stack_a)
        free(ab);
    }
  }
}

static size_t strip_heading_id(const char *s, size_t len, const char **id_s,
                               size_t *id_l) {
  *id_s = NULL;
  *id_l = 0;
  if (len < 4 || s[len - 1] != '}')
    return len;
  size_t i = len - 1;
  while (i > 0 && s[i - 1] != '{' && !is_ws((unsigned char)s[i - 1]))
    i--;
  if (i == 0 || s[i - 1] != '{' || s[i] != '#' || i + 1 >= len - 1)
    return len;
  size_t brace = i - 1;
  if (brace == 0 || s[brace - 1] != ' ')
    return len;
  size_t trim = brace;
  while (trim > 0 && s[trim - 1] == ' ')
    trim--;
  *id_s = s + i + 1;
  *id_l = (len - 1) - (i + 1);
  return trim;
}

/* ---- Block Helpers ---- */

static int is_blank_line(const char *s, size_t len) {
  for (size_t i = 0; i < len; i++) {
    if (s[i] != ' ' && s[i] != '\t' && s[i] != '\r')
      return 0;
  }
  return 1;
}

static size_t count_indent(const char *s, size_t len, size_t *bytes_out) {
  size_t col = 0, i = 0;
  while (i < len) {
    if (s[i] == ' ') {
      col++;
      i++;
    } else if (s[i] == '\t') {
      col += 4 - (col % 4);
      i++;
    } else {
      break;
    }
  }
  if (bytes_out)
    *bytes_out = i;
  return col;
}

static int is_thematic_break(const char *s, size_t len) {
  if (len < 3)
    return 0;
  char ch = s[0];
  if (ch != '-' && ch != '*' && ch != '_')
    return 0;
  size_t cnt = 0;
  for (size_t i = 0; i < len; i++) {
    if (s[i] == ch)
      cnt++;
    else if (s[i] != ' ' && s[i] != '\t' && s[i] != '\r')
      return 0;
  }
  return cnt >= 3;
}

static int is_setext_underline(const char *s, size_t len) {
  if (len == 0 || (s[0] != '=' && s[0] != '-'))
    return 0;
  char ch = s[0];
  size_t i = 0;
  while (i < len && s[i] == ch)
    i++;
  while (i < len && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r'))
    i++;
  if (i != len)
    return 0;
  return ch == '=' ? 1 : 2;
}

static int parse_atx_heading(const char *s, size_t len, int *level_out,
                             const char **txt_s, size_t *txt_l) {
  if (len == 0 || s[0] != '#')
    return 0;
  int lvl = 0;
  while (lvl < (int)len && lvl < 7 && s[lvl] == '#')
    lvl++;
  if (lvl < 1 || lvl > 6)
    return 0;
  size_t i = (size_t)lvl;
  if (i < len && s[i] != ' ' && s[i] != '\t' && s[i] != '\r')
    return 0;
  while (i < len && (s[i] == ' ' || s[i] == '\t'))
    i++;
  size_t e = len;
  while (e > i && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r'))
    e--;
  size_t h = e;
  while (h > i && s[h - 1] == '#')
    h--;
  if (h < e && (h == i || s[h - 1] == ' ' || s[h - 1] == '\t')) {
    e = h;
    while (e > i && (s[e - 1] == ' ' || s[e - 1] == '\t'))
      e--;
  }
  *level_out = lvl;
  *txt_s = s + i;
  *txt_l = e - i;
  return 1;
}

static int parse_fence_open(const char *s, size_t len, char *fch, size_t *flen,
                            const char **info_s, size_t *info_l) {
  if (len < 3 || (s[0] != '`' && s[0] != '~'))
    return 0;
  char ch = s[0];
  size_t n = 0;
  while (n < len && s[n] == ch)
    n++;
  if (n < 3)
    return 0;
  size_t i = n;
  while (i < len && (s[i] == ' ' || s[i] == '\t'))
    i++;
  size_t e = len;
  while (e > i && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r'))
    e--;
  if (ch == '`' && memchr(s + i, '`', e - i) != NULL)
    return 0;
  *fch = ch;
  *flen = n;
  *info_s = s + i;
  *info_l = e - i;
  return 1;
}

static int is_fence_close(const char *s, size_t len, char fch, size_t flen) {
  size_t n = 0;
  while (n < len && s[n] == fch)
    n++;
  if (n < flen)
    return 0;
  for (size_t i = n; i < len; i++) {
    if (s[i] != ' ' && s[i] != '\t' && s[i] != '\r')
      return 0;
  }
  return 1;
}

/* Pandoc fenced divs / admonitions: :::+, optional {.type #anchor} or type */
static int parse_admon_open(const char *s, size_t len, size_t *colons_out,
                            const char **type_s, size_t *type_l,
                            const char **anc_s, size_t *anc_l) {
  size_t colons = 0;
  while (colons < len && s[colons] == ':')
    colons++;
  if (colons < 3 || colons >= len)
    return 0;
  size_t i = colons;
  while (i < len && (s[i] == ' ' || s[i] == '\t'))
    i++;
  if (i >= len || s[i] == '\r')
    return 0;
  int braced = 0;
  if (s[i] == '{') {
    braced = 1;
    i++;
    while (i < len && (s[i] == ' ' || s[i] == '\t' || s[i] == '.'))
      i++;
  }
  if (i >= len || s[i] == '}')
    return 0;
  size_t ts = i;
  while (i < len && s[i] != ' ' && s[i] != '}' && s[i] != '\r' && s[i] != '\t')
    i++;
  if (i == ts)
    return 0;
  *colons_out = colons;
  *type_s = s + ts;
  *type_l = i - ts;
  *anc_s = NULL;
  *anc_l = 0;
  while (i < len && (s[i] == ' ' || s[i] == '\t'))
    i++;
  if (i < len && s[i] == '#') {
    i++;
    size_t as = i;
    while (i < len && s[i] != ' ' && s[i] != '}' && s[i] != '\r' &&
           s[i] != '\t')
      i++;
    if (i > as) {
      *anc_s = s + as;
      *anc_l = i - as;
    }
  }
  if (braced) {
    while (i < len && s[i] != '}')
      i++;
    if (i >= len)
      return 0;
  }
  return 1;
}

static size_t count_admon_close_colons(const char *s, size_t len) {
  size_t i = 0;
  while (i < len && s[i] == ':')
    i++;
  if (i < 3)
    return 0;
  size_t j = i;
  while (j < len && (s[j] == ' ' || s[j] == '\t' || s[j] == '\r'))
    j++;
  return j == len ? i : 0;
}

static int parse_list_marker(const char *s, size_t len, size_t indent_col,
                             int *ordered, char *delim, int *start_num,
                             size_t *marker_cols, size_t *marker_bytes) {
  if (len == 0)
    return 0;
  if (s[0] == '-' || s[0] == '*' || s[0] == '+') {
    if (len == 1 || s[1] == ' ' || s[1] == '\t' || s[1] == '\r') {
      *ordered = 0;
      *delim = s[0];
      *start_num = 1;
      size_t i = 1;
      size_t sp = 0;
      while (i < len && (s[i] == ' ' || s[i] == '\t')) {
        size_t col = indent_col + 1 + sp;
        sp += (s[i] == '\t') ? (4 - (col % 4)) : 1;
        i++;
      }
      if (i == len || s[i] == '\r') {
        *marker_cols = 2;
        *marker_bytes = i;
      } else if (sp > 4) {
        *marker_cols = 2;
        *marker_bytes = 2;
      } else {
        *marker_cols = 1 + sp;
        *marker_bytes = i;
      }
      return 1;
    }
    return 0;
  }
  if (isdigit((unsigned char)s[0])) {
    size_t d = 0;
    int num = 0;
    while (d < len && d < 9 && isdigit((unsigned char)s[d])) {
      num = num * 10 + (s[d] - '0');
      d++;
    }
    if (d < len && (s[d] == '.' || s[d] == ')') &&
        (d + 1 == len || s[d + 1] == ' ' || s[d + 1] == '\t' ||
         s[d + 1] == '\r')) {
      *ordered = 1;
      *delim = s[d];
      *start_num = num;
      size_t i = d + 1;
      size_t sp = 0;
      while (i < len && (s[i] == ' ' || s[i] == '\t')) {
        size_t col = indent_col + d + 1 + sp;
        sp += (s[i] == '\t') ? (4 - (col % 4)) : 1;
        i++;
      }
      if (i == len || s[i] == '\r') {
        *marker_cols = d + 2;
        *marker_bytes = i;
      } else if (sp > 4) {
        *marker_cols = d + 2;
        *marker_bytes = d + 2;
      } else {
        *marker_cols = d + 1 + sp;
        *marker_bytes = i;
      }
      return 1;
    }
  }
  return 0;
}

static int detect_html_block_type(const char *s, size_t len) {
  if (len < 3 || s[0] != '<')
    return 0;
  if (len >= 4 && memcmp(s, "<!--", 4) == 0)
    return 2;
  if (s[1] == '?')
    return 3;
  if (len >= 9 && memcmp(s, "<![CDATA[", 9) == 0)
    return 5;
  if (s[1] == '!' && len >= 3 && isupper((unsigned char)s[2]))
    return 4;

  size_t i = (s[1] == '/') ? 2 : 1;
  size_t ts = i;
  while (i < len && isalpha((unsigned char)s[i]))
    i++;
  size_t tl = i - ts;
  if (tl == 0)
    return 0;
  char tag[16];
  if (tl < sizeof(tag)) {
    for (size_t k = 0; k < tl; k++)
      tag[k] = (char)tolower((unsigned char)s[ts + k]);
    tag[tl] = '\0';
    int boundary =
        (i == len || s[i] == ' ' || s[i] == '\t' || s[i] == '>' ||
         s[i] == '\r' || (s[i] == '/' && i + 1 < len && s[i + 1] == '>'));
    if (boundary) {
      if (s[1] != '/' &&
          (strcmp(tag, "pre") == 0 || strcmp(tag, "script") == 0 ||
           strcmp(tag, "style") == 0 || strcmp(tag, "textarea") == 0))
        return 1;
      static const char *const type6[] = {
          "address",    "article",  "aside",   "base",     "basefont",
          "blockquote", "body",     "caption", "center",   "col",
          "colgroup",   "dd",       "details", "dialog",   "dir",
          "div",        "dl",       "dt",      "fieldset", "figcaption",
          "figure",     "footer",   "form",    "frame",    "frameset",
          "h1",         "h2",       "h3",      "h4",       "h5",
          "h6",         "head",     "header",  "hr",       "html",
          "iframe",     "legend",   "li",      "link",     "main",
          "menu",       "menuitem", "nav",     "noframes", "ol",
          "optgroup",   "option",   "p",       "param",    "search",
          "section",    "summary",  "table",   "tbody",    "td",
          "tfoot",      "th",       "thead",   "title",    "tr",
          "track",      "ul",       NULL};
      for (size_t k = 0; type6[k] != NULL; k++) {
        if (strcmp(tag, type6[k]) == 0)
          return 6;
      }
    }
  }
  size_t hlen = match_inline_html(s, len);
  if (hlen > 0) {
    size_t j = hlen;
    while (j < len && (s[j] == ' ' || s[j] == '\t' || s[j] == '\r'))
      j++;
    if (j == len)
      return 7;
  }
  return 0;
}

static int html_block_ends_on_line(int htype, const char *s, size_t len) {
  if (htype == 2)
    return md_memmem(s, len, "-->", 3) != NULL;
  if (htype == 3)
    return md_memmem(s, len, "?>", 2) != NULL;
  if (htype == 4)
    return memchr(s, '>', len) != NULL;
  if (htype == 5)
    return md_memmem(s, len, "]]>", 3) != NULL;
  if (htype == 1) {
    for (size_t i = 0; i + 5 < len; i++) {
      if (s[i] == '<' && s[i + 1] == '/') {
        if (i + 6 <= len && md_strncasecmp(s + i, "</pre>", 6) == 0)
          return 1;
        if (i + 9 <= len && md_strncasecmp(s + i, "</script>", 9) == 0)
          return 1;
        if (i + 8 <= len && md_strncasecmp(s + i, "</style>", 8) == 0)
          return 1;
        if (i + 11 <= len && md_strncasecmp(s + i, "</textarea>", 11) == 0)
          return 1;
      }
    }
  }
  return 0;
}

/* ---- GFM Pipe Table helpers ---- */

static int parse_table_delim_row(const char *s, size_t len, uint8_t *aligns,
                                 size_t max_cols, size_t *ncols_out) {
  if (memchr(s, '-', len) == NULL)
    return 0;
  size_t i = 0;
  while (i < len && (s[i] == ' ' || s[i] == '\t'))
    i++;
  size_t e = len;
  while (e > i && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r'))
    e--;
  if (i >= e || memchr(s + i, '|', e - i) == NULL)
    return 0;
  if (s[i] == '-' && i + 1 < e && (s[i + 1] == ' ' || s[i + 1] == '\t'))
    return 0;
  if (s[i] == '|')
    i++;
  if (e > i && s[e - 1] == '|')
    e--;
  size_t cols = 0;
  while (i <= e) {
    const char *bar = memchr(s + i, '|', e - i);
    size_t ce = bar ? (size_t)(bar - s) : e;
    size_t cs = i;
    while (cs < ce && (s[cs] == ' ' || s[cs] == '\t'))
      cs++;
    while (ce > cs && (s[ce - 1] == ' ' || s[ce - 1] == '\t'))
      ce--;
    if (cs >= ce)
      return 0;
    int left = 0, right = 0;
    if (s[cs] == ':') {
      left = 1;
      cs++;
    }
    if (ce > cs && s[ce - 1] == ':') {
      right = 1;
      ce--;
    }
    if (cs >= ce)
      return 0;
    for (size_t k = cs; k < ce; k++) {
      if (s[k] != '-')
        return 0;
    }
    if (cols < max_cols) {
      aligns[cols] = (left && right) ? MD_ALIGN_CENTER
                     : left          ? MD_ALIGN_LEFT
                     : right         ? MD_ALIGN_RIGHT
                                     : MD_ALIGN_NONE;
    }
    cols++;
    if (!bar)
      break;
    i = (size_t)(bar - s) + 1;
  }
  if (cols == 0)
    return 0;
  *ncols_out = cols;
  return 1;
}

static void set_table_cell_text(MdDoc *doc, uint32_t cell, const char *s,
                                size_t len) {
  if (len == 0)
    return;
  int has_esc_pipe = 0;
  for (size_t k = 0; k + 1 < len; k++) {
    if (s[k] == '\\' && s[k + 1] == '|') {
      has_esc_pipe = 1;
      break;
    }
  }
  if (!has_esc_pipe) {
    if (s >= doc->src && s <= doc->src + doc->src_len) {
      doc->nodes[cell].text_off = (uint32_t)(s - doc->src);
    } else {
      doc->nodes[cell].text_off = pool_copy(doc, s, len);
      doc->nodes[cell].flags |= MD_F_TEXT_POOL;
    }
    doc->nodes[cell].text_len = (uint32_t)len;
    return;
  }
  int from_pool =
      (doc->pool != NULL && s >= doc->pool && s < doc->pool + doc->pool_cap);
  size_t rel = from_pool ? (size_t)(s - doc->pool) : 0;
  uint32_t off = pool_alloc(doc, len);
  if (off == UINT32_MAX)
    return;
  if (from_pool)
    s = doc->pool + rel;
  char *dst = doc->pool + off;
  size_t w = 0;
  for (size_t k = 0; k < len;) {
    if (s[k] == '\\' && k + 1 < len && s[k + 1] == '|') {
      dst[w++] = '|';
      k += 2;
    } else {
      dst[w++] = s[k++];
    }
  }
  dst[w] = '\0';
  doc->nodes[cell].text_off = off;
  doc->nodes[cell].text_len = (uint32_t)w;
  doc->nodes[cell].flags |= MD_F_TEXT_POOL;
}

static size_t split_table_cells(const char *s, size_t len, const char **starts,
                                size_t *lens, size_t max_cols) {
  size_t i = 0;
  while (i < len && (s[i] == ' ' || s[i] == '\t'))
    i++;
  size_t e = len;
  while (e > i && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r'))
    e--;
  if (i < e && s[i] == '|')
    i++;
  if (e > i && s[e - 1] == '|' && !(e >= i + 2 && s[e - 2] == '\\'))
    e--;
  size_t cols = 0;
  while (i <= e && cols < max_cols) {
    size_t cs = i;
    size_t p = i;
    while (p < e) {
      if (s[p] == '\\' && p + 1 < e) {
        p += 2;
      } else if (s[p] == '|') {
        break;
      } else {
        p++;
      }
    }
    size_t ce = p;
    while (cs < ce && (s[cs] == ' ' || s[cs] == '\t'))
      cs++;
    while (ce > cs && (s[ce - 1] == ' ' || s[ce - 1] == '\t'))
      ce--;
    starts[cols] = s + cs;
    lens[cols] = ce - cs;
    cols++;
    if (p >= e)
      break;
    i = p + 1;
  }
  return cols;
}

/* ---- Link reference definitions at start of paragraph ---- */

static size_t try_consume_ref_defs(MdDoc *doc, const char *s, size_t len) {
  size_t pos = 0;
  while (pos < len) {
    size_t i = pos;
    while (i < len && s[i] == ' ' && i - pos < 3)
      i++;
    if (i >= len || s[i] != '[')
      break;
    i++;
    size_t ls = i;
    while (i < len && s[i] != ']') {
      if (s[i] == '\\' && i + 1 < len)
        i += 2;
      else if (s[i] == '[')
        goto done;
      else
        i++;
    }
    if (i >= len || s[i] != ']' || i == ls || i + 1 >= len || s[i + 1] != ':')
      break;
    size_t ll = i - ls;
    i += 2;
    while (i < len && (s[i] == ' ' || s[i] == '\t'))
      i++;
    if (i < len && s[i] == '\n') {
      i++;
      while (i < len && (s[i] == ' ' || s[i] == '\t'))
        i++;
    }
    if (i >= len)
      break;
    size_t us = i, ue = i;
    if (s[i] == '<') {
      i++;
      us = i;
      while (i < len && s[i] != '>' && s[i] != '\n')
        i++;
      if (i >= len || s[i] != '>')
        break;
      ue = i;
      i++;
    } else {
      us = i;
      while (i < len && !is_ws((unsigned char)s[i]))
        i++;
      ue = i;
    }
    if (ue == us)
      break;
    size_t after_url = i;
    while (i < len && (s[i] == ' ' || s[i] == '\t'))
      i++;
    size_t ts = 0, te = 0;
    size_t line_end = after_url;
    if (i < len && (s[i] == '"' || s[i] == '\'' || s[i] == '(')) {
      char q = s[i];
      char cq = (q == '(') ? ')' : q;
      size_t j = i + 1;
      size_t cand_ts = j;
      while (j < len && s[j] != cq && s[j] != '\n') {
        if (s[j] == '\\' && j + 1 < len)
          j += 2;
        else
          j++;
      }
      if (j < len && s[j] == cq) {
        size_t k = j + 1;
        while (k < len && (s[k] == ' ' || s[k] == '\t' || s[k] == '\r'))
          k++;
        if (k == len || s[k] == '\n') {
          ts = cand_ts;
          te = j;
          line_end = (k < len && s[k] == '\n') ? k + 1 : k;
        }
      }
    }
    if (line_end == after_url) {
      size_t k = after_url;
      while (k < len && (s[k] == ' ' || s[k] == '\t' || s[k] == '\r'))
        k++;
      if (k != len && s[k] != '\n')
        break;
      line_end = (k < len && s[k] == '\n') ? k + 1 : k;
    }
    uint32_t uoff = 0, ulen = 0, toff = 0, tlen = 0;
    uint8_t uip = 0, tip = 0;
    uoff = unescape_slice(doc, s + us, ue - us, &ulen, &uip);
    if (te > ts)
      toff = unescape_slice(doc, s + ts, te - ts, &tlen, &tip);
    ref_table_insert(doc, s + ls, ll, uoff, ulen, uip, toff, tlen, tip);
    pos = line_end;
  }
done:
  return pos;
}

/* ---- Block Parser ---- */

typedef struct {
  char *data;
  size_t len;
  size_t cap;
} ScratchBuf;

static void sbuf_append(ScratchBuf *b, const char *s, size_t n) {
  if (n == 0)
    return;
  if (b->len + n + 1 > b->cap) {
    size_t nc = b->cap == 0 ? 512 : b->cap * 2;
    while (nc < b->len + n + 1)
      nc *= 2;
    char *nd = realloc(b->data, nc);
    if (nd == NULL)
      return;
    b->data = nd;
    b->cap = nc;
  }
  memcpy(b->data + b->len, s, n);
  b->len += n;
  b->data[b->len] = '\0';
}

static void sbuf_append_char(ScratchBuf *b, char c) { sbuf_append(b, &c, 1); }

typedef struct {
  const char *s;
  size_t len;
  uint8_t lazy;
} LineView;

static void parse_blocks(MdDoc *doc, uint32_t parent, const LineView *lines,
                         size_t nlines, uint8_t base_col, const char *file_path,
                         int collect_meta, int depth,
                         int *had_direct_blank_out);

static int line_breaks_lazy(LineView lv) {
  size_t b = 0;
  size_t ind = count_indent(lv.s, lv.len, &b);
  if (ind >= 4 || b >= lv.len)
    return 0;
  const char *t = lv.s + b;
  size_t tl = lv.len - b;
  int hl = 0, ord = 0, snum = 1;
  char fc = 0, ldel = 0;
  size_t fl = 0, il = 0, hsl = 0, mc = 0, mb = 0, ac = 0, atl = 0, aal = 0;
  const char *hs = NULL, *is = NULL, *ats = NULL, *aas = NULL;
  if (t[0] == '>' || is_thematic_break(t, tl) ||
      parse_atx_heading(t, tl, &hl, &hs, &hsl) ||
      parse_fence_open(t, tl, &fc, &fl, &is, &il) ||
      parse_admon_open(t, tl, &ac, &ats, &atl, &aas, &aal) ||
      count_admon_close_colons(t, tl) >= 3 ||
      detect_html_block_type(t, tl) > 0 ||
      parse_list_marker(t, tl, ind, &ord, &ldel, &snum, &mc, &mb))
    return 1;
  return 0;
}

static int line_can_lazy_continue(LineView lv, int depth) {
  if (depth > 16 || is_blank_line(lv.s, lv.len))
    return 0;
  size_t b = 0;
  size_t ind = count_indent(lv.s, lv.len, &b);
  if (ind >= 4 || b >= lv.len)
    return 0;
  const char *t = lv.s + b;
  size_t tl = lv.len - b;
  int hl = 0, ord = 0, snum = 1;
  char fc = 0, ldel = 0;
  size_t fl = 0, il = 0, hsl = 0, mc = 0, mb = 0, ac = 0, atl = 0, aal = 0;
  const char *hs = NULL, *is = NULL, *ats = NULL, *aas = NULL;
  if (is_thematic_break(t, tl) || parse_atx_heading(t, tl, &hl, &hs, &hsl) ||
      parse_fence_open(t, tl, &fc, &fl, &is, &il) ||
      parse_admon_open(t, tl, &ac, &ats, &atl, &aas, &aal) ||
      count_admon_close_colons(t, tl) >= 3 || detect_html_block_type(t, tl) > 0)
    return 0;
  if (t[0] == '>') {
    size_t skip = 1;
    if (skip < tl && t[skip] == ' ')
      skip++;
    return line_can_lazy_continue((LineView){t + skip, tl - skip, 0},
                                  depth + 1);
  }
  if (parse_list_marker(t, tl, ind, &ord, &ldel, &snum, &mc, &mb)) {
    return line_can_lazy_continue((LineView){t + mb, tl - mb, 0}, depth + 1);
  }
  return 1;
}

static LineView strip_columns_alloc(LineView lv, size_t cols, char ***allocs,
                                    size_t *an, size_t *acap) {
  size_t i = 0, c = 0;
  while (i < lv.len && c < cols) {
    if (lv.s[i] == ' ') {
      c++;
      i++;
    } else if (lv.s[i] == '\t') {
      size_t adv = 4 - (c % 4);
      if (c + adv > cols) {
        size_t rem_sp = c + adv - cols;
        size_t rest_len = lv.len - (i + 1);
        char *eb = malloc(rem_sp + rest_len + 1);
        if (eb != NULL) {
          memset(eb, ' ', rem_sp);
          if (rest_len > 0)
            memcpy(eb + rem_sp, lv.s + i + 1, rest_len);
          eb[rem_sp + rest_len] = '\0';
          if (*an == *acap) {
            *acap = *acap == 0 ? 8 : *acap * 2;
            *allocs = realloc(*allocs, *acap * sizeof(**allocs));
          }
          (*allocs)[(*an)++] = eb;
          return (LineView){eb, rem_sp + rest_len, 0};
        }
        break;
      }
      c += adv;
      i++;
    } else {
      break;
    }
  }
  return (LineView){lv.s + i, lv.len - i, 0};
}

static LineView strip_columns(LineView lv, size_t cols) {
  size_t i = 0, c = 0;
  while (i < lv.len && c < cols) {
    if (lv.s[i] == ' ') {
      c++;
      i++;
    } else if (lv.s[i] == '\t') {
      size_t adv = 4 - (c % 4);
      if (c + adv > cols)
        break;
      c += adv;
      i++;
    } else {
      break;
    }
  }
  return (LineView){lv.s + i, lv.len - i, 0};
}

static uint32_t finalize_paragraph(MdDoc *doc, uint32_t parent,
                                   const char *raw_s, size_t raw_len,
                                   int raw_in_src, uint8_t col) {
  if (raw_len == 0)
    return 0;
  size_t consumed = try_consume_ref_defs(doc, raw_s, raw_len);
  if (consumed >= raw_len)
    return 0;
  raw_s += consumed;
  raw_len -= consumed;
  while (raw_len > 0 &&
         (raw_s[0] == ' ' || raw_s[0] == '\t' || raw_s[0] == '\n')) {
    raw_s++;
    raw_len--;
  }
  while (raw_len > 0 &&
         (raw_s[raw_len - 1] == ' ' || raw_s[raw_len - 1] == '\t' ||
          raw_s[raw_len - 1] == '\n' || raw_s[raw_len - 1] == '\r'))
    raw_len--;
  if (raw_len == 0)
    return 0;

  uint32_t pid = node_new(doc, MD_PARAGRAPH);
  if (pid == 0)
    return 0;
  doc->nodes[pid].col = col;
  if (raw_in_src && raw_s >= doc->src && raw_s <= doc->src + doc->src_len) {
    doc->nodes[pid].text_off = (uint32_t)(raw_s - doc->src);
  } else {
    doc->nodes[pid].text_off = pool_copy(doc, raw_s, raw_len);
    doc->nodes[pid].flags |= MD_F_TEXT_POOL;
  }
  doc->nodes[pid].text_len = (uint32_t)raw_len;
  node_append(doc, parent, pid);
  return pid;
}

static void parse_blocks(MdDoc *doc, uint32_t parent, const LineView *lines,
                         size_t nlines, uint8_t base_col, const char *file_path,
                         int collect_meta, int depth,
                         int *had_direct_blank_out) {
  if (had_direct_blank_out != NULL)
    *had_direct_blank_out = 0;
  if (depth > 32)
    return;
  ScratchBuf pbuf = {0};
  const char *p_src_start = NULL;
  const char *p_src_end = NULL;
  int p_contiguous = 0;
  uint8_t p_col = base_col;
  int saw_blank_after_child = 0;

  size_t i = 0;
  while (i < nlines) {
    LineView lv = lines[i];
    if (is_blank_line(lv.s, lv.len)) {
      if (pbuf.len > 0 || p_contiguous) {
        if (p_contiguous)
          finalize_paragraph(doc, parent, p_src_start,
                             (size_t)(p_src_end - p_src_start), 1, p_col);
        else
          finalize_paragraph(doc, parent, pbuf.data, pbuf.len, 0, p_col);
        pbuf.len = 0;
        p_contiguous = 0;
      }
      if (doc->nodes[parent].first_child != 0)
        saw_blank_after_child = 1;
      i++;
      continue;
    }

    if (saw_blank_after_child) {
      if (had_direct_blank_out != NULL)
        *had_direct_blank_out = 1;
      saw_blank_after_child = 0;
    }

    size_t ind_bytes = 0;
    size_t indent = count_indent(lv.s, lv.len, &ind_bytes);
    const char *trimmed = lv.s + ind_bytes;
    size_t tlen = lv.len - ind_bytes;
    uint8_t cur_col = (uint8_t)(base_col + (indent < 200 ? indent : 200));

    int in_para = (pbuf.len > 0 || p_contiguous);

    /* 1. Indented code block (>= 4 spaces, only outside paragraph) */
    if (!in_para && indent >= 4) {
      ScratchBuf cbuf = {0};
      size_t j = i;
      size_t last_nonblank = i;
      while (j < nlines) {
        if (is_blank_line(lines[j].s, lines[j].len)) {
          sbuf_append_char(&cbuf, '\n');
          j++;
          continue;
        }
        size_t jb = 0;
        size_t jind = count_indent(lines[j].s, lines[j].len, &jb);
        if (jind < 4)
          break;
        LineView sub = strip_columns(lines[j], 4);
        sbuf_append(&cbuf, sub.s, sub.len);
        sbuf_append_char(&cbuf, '\n');
        last_nonblank = j + 1;
        j++;
      }
      while (cbuf.len > 1 && cbuf.data[cbuf.len - 2] == '\n')
        cbuf.len--;
      uint32_t cid = node_new(doc, MD_CODE_BLOCK);
      if (cid != 0) {
        doc->nodes[cid].col = cur_col;
        doc->nodes[cid].text_off =
            pool_copy(doc, cbuf.data ? cbuf.data : "", cbuf.len);
        doc->nodes[cid].text_len = (uint32_t)cbuf.len;
        doc->nodes[cid].flags |= MD_F_TEXT_POOL;
        doc->has_code_block = 1;
        node_append(doc, parent, cid);
      }
      free(cbuf.data);
      i = last_nonblank;
      continue;
    }

    /* 2. Setext heading underline (when in paragraph; never on lazy line) */
    if (!lv.lazy && in_para && indent < 4) {
      int slevel = is_setext_underline(trimmed, tlen);
      if (slevel > 0) {
        const char *ps = p_contiguous ? p_src_start : pbuf.data;
        size_t pl = p_contiguous ? (size_t)(p_src_end - p_src_start) : pbuf.len;
        size_t consumed = try_consume_ref_defs(doc, ps, pl);
        if (consumed < pl) {
          ps += consumed;
          pl -= consumed;
          while (pl > 0 && is_ws((unsigned char)ps[0])) {
            ps++;
            pl--;
          }
          while (pl > 0 && is_ws((unsigned char)ps[pl - 1]))
            pl--;
          const char *id_s = NULL;
          size_t id_l = 0;
          pl = strip_heading_id(ps, pl, &id_s, &id_l);
          uint32_t hid = node_new(doc, MD_HEADING);
          if (hid != 0) {
            doc->nodes[hid].info = (uint8_t)slevel;
            doc->nodes[hid].col = p_col;
            if (p_contiguous && ps >= doc->src &&
                ps <= doc->src + doc->src_len) {
              doc->nodes[hid].text_off = (uint32_t)(ps - doc->src);
            } else {
              doc->nodes[hid].text_off = pool_copy(doc, ps, pl);
              doc->nodes[hid].flags |= MD_F_TEXT_POOL;
            }
            doc->nodes[hid].text_len = (uint32_t)pl;
            if (id_s != NULL)
              set_heading_anchor(doc, hid, id_s, id_l, file_path, collect_meta);
            node_append(doc, parent, hid);
          }
          pbuf.len = 0;
          p_contiguous = 0;
          i++;
          continue;
        }
      }
    }

    /* 3. GFM Pipe Table (check last line of open paragraph against delim row;
     * never on a lazy continuation line) */
    if (!lv.lazy && in_para && indent < 4) {
      uint8_t aligns[64];
      size_t ncols = 0;
      if (parse_table_delim_row(trimmed, tlen, aligns, 64, &ncols)) {
        const char *ps = p_contiguous ? p_src_start : pbuf.data;
        size_t pl = p_contiguous ? (size_t)(p_src_end - p_src_start) : pbuf.len;
        /* Isolate last line of paragraph as candidate header row */
        const char *hdr_s = ps;
        size_t hdr_l = pl;
        const char *last_nl = NULL;
        for (size_t k = pl; k > 0; k--) {
          if (ps[k - 1] == '\n') {
            last_nl = ps + k - 1;
            break;
          }
        }
        if (last_nl != NULL) {
          hdr_s = last_nl + 1;
          hdr_l = pl - (size_t)(hdr_s - ps);
        }
        if (memchr(hdr_s, '|', hdr_l) != NULL) {
          const char *hstarts[64];
          size_t hlens[64];
          size_t hcols = split_table_cells(hdr_s, hdr_l, hstarts, hlens, 64);
          if (hcols == ncols) {
            /* Emit any earlier lines before the table header as a paragraph */
            if (last_nl != NULL) {
              finalize_paragraph(doc, parent, ps, (size_t)(last_nl - ps),
                                 p_contiguous, p_col);
            }
            uint32_t tid = node_new(doc, MD_TABLE);
            uint32_t thead = node_new(doc, MD_TABLE_HEAD);
            uint32_t hrow = node_new(doc, MD_TABLE_ROW);
            if (tid && thead && hrow) {
              doc->nodes[tid].col = p_col;
              node_append(doc, parent, tid);
              node_append(doc, tid, thead);
              node_append(doc, thead, hrow);
              for (size_t c = 0; c < ncols; c++) {
                uint32_t cell = node_new(doc, MD_TABLE_CELL);
                if (cell) {
                  doc->nodes[cell].flags |= MD_F_TABLE_TH;
                  doc->nodes[cell].info = aligns[c];
                  set_table_cell_text(doc, cell, hstarts[c], hlens[c]);
                  node_append(doc, hrow, cell);
                }
              }
              pbuf.len = 0;
              p_contiguous = 0;
              i++;
              uint32_t tbody = 0;
              while (i < nlines && !is_blank_line(lines[i].s, lines[i].len)) {
                size_t rb = 0;
                size_t rind = count_indent(lines[i].s, lines[i].len, &rb);
                const char *rs = lines[i].s + rb;
                size_t rl = lines[i].len - rb;
                if (rind >= 4 || line_breaks_lazy(lines[i]))
                  break;
                if (tbody == 0) {
                  tbody = node_new(doc, MD_TABLE_BODY);
                  if (tbody)
                    node_append(doc, tid, tbody);
                }
                uint32_t brow = node_new(doc, MD_TABLE_ROW);
                if (brow && tbody) {
                  node_append(doc, tbody, brow);
                  const char *cstarts[64];
                  size_t clens[64];
                  size_t cc = split_table_cells(rs, rl, cstarts, clens, 64);
                  for (size_t c = 0; c < ncols; c++) {
                    uint32_t cell = node_new(doc, MD_TABLE_CELL);
                    if (cell) {
                      doc->nodes[cell].info = aligns[c];
                      if (c < cc && clens[c] > 0)
                        set_table_cell_text(doc, cell, cstarts[c], clens[c]);
                      node_append(doc, brow, cell);
                    }
                  }
                }
                i++;
              }
              continue;
            }
          }
        }
      }
    }

    /* 4. Block-interrupting constructs (when indent < 4) */
    if (indent < 4) {
      /* 4a. ATX Heading */
      int hlevel = 0;
      const char *hs = NULL;
      size_t hl = 0;
      if (parse_atx_heading(trimmed, tlen, &hlevel, &hs, &hl)) {
        if (in_para) {
          if (p_contiguous)
            finalize_paragraph(doc, parent, p_src_start,
                               (size_t)(p_src_end - p_src_start), 1, p_col);
          else
            finalize_paragraph(doc, parent, pbuf.data, pbuf.len, 0, p_col);
          pbuf.len = 0;
          p_contiguous = 0;
        }
        const char *id_s = NULL;
        size_t id_l = 0;
        hl = strip_heading_id(hs, hl, &id_s, &id_l);
        uint32_t hid = node_new(doc, MD_HEADING);
        if (hid != 0) {
          doc->nodes[hid].info = (uint8_t)hlevel;
          doc->nodes[hid].col = cur_col;
          if (hs >= doc->src && hs <= doc->src + doc->src_len) {
            doc->nodes[hid].text_off = (uint32_t)(hs - doc->src);
          } else {
            doc->nodes[hid].text_off = pool_copy(doc, hs, hl);
            doc->nodes[hid].flags |= MD_F_TEXT_POOL;
          }
          doc->nodes[hid].text_len = (uint32_t)hl;
          if (id_s != NULL)
            set_heading_anchor(doc, hid, id_s, id_l, file_path, collect_meta);
          node_append(doc, parent, hid);
        }
        i++;
        continue;
      }

      /* 4b. Fenced Code Block */
      char fch = 0;
      size_t flen = 0;
      const char *info_s = NULL;
      size_t info_l = 0;
      if (parse_fence_open(trimmed, tlen, &fch, &flen, &info_s, &info_l)) {
        if (in_para) {
          if (p_contiguous)
            finalize_paragraph(doc, parent, p_src_start,
                               (size_t)(p_src_end - p_src_start), 1, p_col);
          else
            finalize_paragraph(doc, parent, pbuf.data, pbuf.len, 0, p_col);
          pbuf.len = 0;
          p_contiguous = 0;
        }
        size_t j = i + 1;
        int can_zero_copy =
            (indent == 0 && j < nlines && lines[j].s >= doc->src &&
             lines[j].s <= doc->src + doc->src_len);
        const char *zc_start = can_zero_copy ? lines[j].s : NULL;
        const char *zc_end = zc_start;
        ScratchBuf cbuf = {0};
        while (j < nlines) {
          size_t jb = 0;
          size_t jind = count_indent(lines[j].s, lines[j].len, &jb);
          if (jind < 4 &&
              is_fence_close(lines[j].s + jb, lines[j].len - jb, fch, flen)) {
            j++;
            break;
          }
          if (can_zero_copy && lines[j].s == zc_end) {
            const char *le = lines[j].s + lines[j].len;
            if (le < doc->src + doc->src_len && *le == '\n') {
              zc_end = le + 1;
            } else {
              sbuf_append(&cbuf, zc_start, (size_t)(zc_end - zc_start));
              can_zero_copy = 0;
              sbuf_append(&cbuf, lines[j].s, lines[j].len);
              sbuf_append_char(&cbuf, '\n');
            }
          } else {
            if (can_zero_copy) {
              sbuf_append(&cbuf, zc_start, (size_t)(zc_end - zc_start));
              can_zero_copy = 0;
            }
            LineView sub =
                indent > 0 ? strip_columns(lines[j], indent) : lines[j];
            sbuf_append(&cbuf, sub.s, sub.len);
            sbuf_append_char(&cbuf, '\n');
          }
          j++;
        }
        uint32_t cid = node_new(doc, MD_CODE_BLOCK);
        if (cid != 0) {
          doc->nodes[cid].col = cur_col;
          doc->nodes[cid].flags |= MD_F_FENCED;
          doc->has_code_block = 1;
          if (can_zero_copy && zc_end > zc_start) {
            doc->nodes[cid].text_off = (uint32_t)(zc_start - doc->src);
            doc->nodes[cid].text_len = (uint32_t)(zc_end - zc_start);
          } else if (cbuf.len > 0) {
            doc->nodes[cid].text_off = pool_copy(doc, cbuf.data, cbuf.len);
            doc->nodes[cid].text_len = (uint32_t)cbuf.len;
            doc->nodes[cid].flags |= MD_F_TEXT_POOL;
          }
          if (info_l > 0) {
            uint32_t il = 0;
            uint8_t ip = 0;
            uint32_t io = unescape_slice(doc, info_s, info_l, &il, &ip);
            doc->nodes[cid].aux_off = io;
            doc->nodes[cid].aux_len = il;
            if (ip)
              doc->nodes[cid].flags |= MD_F_AUX_POOL;
          }
          node_append(doc, parent, cid);
        }
        free(cbuf.data);
        i = j;
        continue;
      }

      /* 4c. Native mmdoc Admonition: :::+ type / :::+ {.type #anchor} */
      size_t open_colons = 0;
      const char *atype_s = NULL, *aanc_s = NULL;
      size_t atype_l = 0, aanc_l = 0;
      if (parse_admon_open(trimmed, tlen, &open_colons, &atype_s, &atype_l,
                           &aanc_s, &aanc_l)) {
        size_t j = i + 1;
        int nested = 0;
        int found_close = 0;
        while (j < nlines) {
          size_t jb = 0;
          size_t jind = count_indent(lines[j].s, lines[j].len, &jb);
          const char *jt = lines[j].s + jb;
          size_t jtl = lines[j].len - jb;
          if (jind < 4) {
            size_t nc = 0;
            const char *dt, *da;
            size_t dtl, dal;
            if (parse_admon_open(jt, jtl, &nc, &dt, &dtl, &da, &dal)) {
              nested++;
            } else {
              size_t cc = count_admon_close_colons(jt, jtl);
              if (cc >= 3) {
                if (nested == 0 && cc >= open_colons) {
                  found_close = 1;
                  break;
                }
                if (nested > 0)
                  nested--;
              }
            }
          }
          j++;
        }
        if (found_close) {
          if (in_para) {
            if (p_contiguous)
              finalize_paragraph(doc, parent, p_src_start,
                                 (size_t)(p_src_end - p_src_start), 1, p_col);
            else
              finalize_paragraph(doc, parent, pbuf.data, pbuf.len, 0, p_col);
            pbuf.len = 0;
            p_contiguous = 0;
          }
          uint32_t aid = node_new(doc, MD_ADMONITION);
          if (aid != 0) {
            doc->nodes[aid].col = cur_col;
            if (j > i + 1) {
              if (!is_blank_line(lines[i + 1].s, lines[i + 1].len))
                doc->nodes[aid].info |= MD_ADMON_TIGHT_OPEN;
              if (!is_blank_line(lines[j - 1].s, lines[j - 1].len))
                doc->nodes[aid].info |= MD_ADMON_TIGHT_CLOSE;
            }
            if (atype_s >= doc->src && atype_s <= doc->src + doc->src_len) {
              doc->nodes[aid].aux_off = (uint32_t)(atype_s - doc->src);
            } else {
              doc->nodes[aid].aux_off = pool_copy(doc, atype_s, atype_l);
              doc->nodes[aid].flags |= MD_F_AUX_POOL;
            }
            doc->nodes[aid].aux_len = (uint32_t)atype_l;
            if (aanc_s != NULL && aanc_l > 0) {
              uint32_t ao;
              uint8_t aip = 0;
              if (aanc_s >= doc->src && aanc_s <= doc->src + doc->src_len) {
                ao = (uint32_t)(aanc_s - doc->src);
              } else {
                ao = pool_copy(doc, aanc_s, aanc_l);
                aip = 1;
              }
              add_extra(doc, aid, ao, (uint32_t)aanc_l, aip, 0);
              if (collect_meta)
                record_anchor(doc, aanc_s - 1, aanc_l + 1, file_path);
            }
            node_append(doc, parent, aid);
            if (j > i + 1) {
              parse_blocks(doc, aid, lines + i + 1, j - (i + 1), cur_col,
                           file_path, collect_meta, depth + 1, NULL);
            }
          }
          i = j + 1;
          continue;
        }
      }

      /* 4d. Thematic break */
      if (is_thematic_break(trimmed, tlen)) {
        if (in_para) {
          if (p_contiguous)
            finalize_paragraph(doc, parent, p_src_start,
                               (size_t)(p_src_end - p_src_start), 1, p_col);
          else
            finalize_paragraph(doc, parent, pbuf.data, pbuf.len, 0, p_col);
          pbuf.len = 0;
          p_contiguous = 0;
        }
        uint32_t hr = node_new(doc, MD_THEMATIC_BREAK);
        if (hr != 0) {
          doc->nodes[hr].col = cur_col;
          node_append(doc, parent, hr);
        }
        i++;
        continue;
      }

      /* 4e. Blockquote */
      if (trimmed[0] == '>') {
        if (in_para) {
          if (p_contiguous)
            finalize_paragraph(doc, parent, p_src_start,
                               (size_t)(p_src_end - p_src_start), 1, p_col);
          else
            finalize_paragraph(doc, parent, pbuf.data, pbuf.len, 0, p_col);
          pbuf.len = 0;
          p_contiguous = 0;
        }
        LineView *qlines = NULL;
        size_t qn = 0, qcap = 0;
        char **qallocs = NULL;
        size_t qan = 0, qacap = 0;
        size_t j = i;
        int last_was_para = 0;
        int in_q_fence = 0;
        char q_fch = 0;
        size_t q_flen = 0;
        while (j < nlines) {
          size_t jb = 0;
          size_t jind = count_indent(lines[j].s, lines[j].len, &jb);
          LineView ql;
          if (jind < 4 && jb < lines[j].len && lines[j].s[jb] == '>') {
            size_t skip = jb + 1;
            if (skip < lines[j].len && lines[j].s[skip] == ' ') {
              skip++;
              ql = (LineView){lines[j].s + skip, lines[j].len - skip, 0};
            } else if (skip < lines[j].len && lines[j].s[skip] == '\t') {
              size_t tab_w = 4 - ((jind + 1) % 4);
              size_t rem_sp = tab_w > 0 ? tab_w - 1 : 0;
              size_t rest_len = lines[j].len - (skip + 1);
              if (rem_sp == 0) {
                ql = (LineView){lines[j].s + skip + 1, rest_len, 0};
              } else {
                char *eb = malloc(rem_sp + rest_len + 1);
                if (eb != NULL) {
                  memset(eb, ' ', rem_sp);
                  if (rest_len > 0)
                    memcpy(eb + rem_sp, lines[j].s + skip + 1, rest_len);
                  eb[rem_sp + rest_len] = '\0';
                  if (qan == qacap) {
                    qacap = qacap == 0 ? 8 : qacap * 2;
                    qallocs = realloc(qallocs, qacap * sizeof(*qallocs));
                  }
                  qallocs[qan++] = eb;
                  ql = (LineView){eb, rem_sp + rest_len, 0};
                } else {
                  ql = (LineView){lines[j].s + skip + 1, rest_len, 0};
                }
              }
            } else {
              ql = (LineView){lines[j].s + skip, lines[j].len - skip, 0};
            }
            if (is_blank_line(ql.s, ql.len)) {
              last_was_para = 0;
            } else {
              size_t qb = 0;
              size_t qind = count_indent(ql.s, ql.len, &qb);
              const char *qt = ql.s + qb;
              size_t qtl = ql.len - qb;
              if (in_q_fence) {
                if (qind < 4 && is_fence_close(qt, qtl, q_fch, q_flen))
                  in_q_fence = 0;
                last_was_para = 0;
              } else {
                const char *qis = NULL;
                size_t qil = 0;
                if (qind < 4 &&
                    parse_fence_open(qt, qtl, &q_fch, &q_flen, &qis, &qil)) {
                  in_q_fence = 1;
                  last_was_para = 0;
                } else {
                  last_was_para = line_can_lazy_continue(ql, 0);
                }
              }
            }
          } else if (last_was_para && !in_q_fence &&
                     !is_blank_line(lines[j].s, lines[j].len) &&
                     !line_breaks_lazy(lines[j])) {
            ql = (LineView){lines[j].s, lines[j].len, 1};
          } else {
            break;
          }
          if (qn == qcap) {
            qcap = qcap == 0 ? 16 : qcap * 2;
            qlines = realloc(qlines, qcap * sizeof(*qlines));
          }
          qlines[qn++] = ql;
          j++;
        }
        uint32_t bq = node_new(doc, MD_BLOCK_QUOTE);
        if (bq != 0) {
          doc->nodes[bq].col = cur_col;
          node_append(doc, parent, bq);
          parse_blocks(doc, bq, qlines, qn, cur_col, file_path, collect_meta,
                       depth + 1, NULL);
        }
        free(qlines);
        for (size_t qa = 0; qa < qan; qa++)
          free(qallocs[qa]);
        free(qallocs);
        i = j;
        continue;
      }

      /* 4f. HTML Block */
      int htype = detect_html_block_type(trimmed, tlen);
      if (htype > 0 && (htype <= 6 || !in_para)) {
        if (in_para) {
          if (p_contiguous)
            finalize_paragraph(doc, parent, p_src_start,
                               (size_t)(p_src_end - p_src_start), 1, p_col);
          else
            finalize_paragraph(doc, parent, pbuf.data, pbuf.len, 0, p_col);
          pbuf.len = 0;
          p_contiguous = 0;
        }
        ScratchBuf hbuf = {0};
        size_t j = i;
        while (j < nlines) {
          if (htype >= 6 && is_blank_line(lines[j].s, lines[j].len))
            break;
          sbuf_append(&hbuf, lines[j].s, lines[j].len);
          sbuf_append_char(&hbuf, '\n');
          if (htype <= 5 &&
              html_block_ends_on_line(htype, lines[j].s, lines[j].len)) {
            j++;
            break;
          }
          j++;
        }
        uint32_t hb = node_new(doc, MD_HTML_BLOCK);
        if (hb != 0) {
          doc->nodes[hb].col = cur_col;
          doc->nodes[hb].text_off =
              pool_copy(doc, hbuf.data ? hbuf.data : "", hbuf.len);
          doc->nodes[hb].text_len = (uint32_t)hbuf.len;
          doc->nodes[hb].flags |= MD_F_TEXT_POOL;
          node_append(doc, parent, hb);
        }
        free(hbuf.data);
        i = j;
        continue;
      }

      /* 4g. List (bullet or ordered) */
      int ordered = 0, start_num = 1;
      char ldelim = 0;
      size_t mcols = 0, mbytes = 0;
      if (parse_list_marker(trimmed, tlen, indent, &ordered, &ldelim,
                            &start_num, &mcols, &mbytes)) {
        if (!in_para || ((!ordered || start_num == 1) && mbytes < tlen)) {
          if (in_para) {
            if (p_contiguous)
              finalize_paragraph(doc, parent, p_src_start,
                                 (size_t)(p_src_end - p_src_start), 1, p_col);
            else
              finalize_paragraph(doc, parent, pbuf.data, pbuf.len, 0, p_col);
            pbuf.len = 0;
            p_contiguous = 0;
          }
          uint32_t lid = node_new(doc, MD_LIST);
          if (lid != 0) {
            doc->nodes[lid].col = cur_col;
            doc->nodes[lid].info = (uint8_t)ldelim;
            if (ordered)
              doc->nodes[lid].flags |= MD_F_ORDERED;
            if (ordered && start_num != 1)
              add_extra(doc, lid, 0, 0, 0, start_num);
            node_append(doc, parent, lid);

            int tight = 1;
            int saw_blank_between = 0;
            size_t list_indent = indent;
            size_t j = i;
            LineView *ilines = NULL;
            size_t icap = 0;

            while (j < nlines) {
              if (is_blank_line(lines[j].s, lines[j].len)) {
                size_t k = j;
                while (k < nlines && is_blank_line(lines[k].s, lines[k].len))
                  k++;
                if (k >= nlines) {
                  j = k;
                  break;
                }
                size_t kb = 0;
                size_t kind = count_indent(lines[k].s, lines[k].len, &kb);
                int kord = 0, ksnum = 1;
                char kdel = 0;
                size_t kmc = 0, kmb = 0;
                if (kind < list_indent + 4 &&
                    parse_list_marker(lines[k].s + kb, lines[k].len - kb, kind,
                                      &kord, &kdel, &ksnum, &kmc, &kmb) &&
                    kord == ordered && kdel == ldelim) {
                  saw_blank_between = 1;
                  j = k;
                } else {
                  break;
                }
              }
              size_t jb = 0;
              size_t jind = count_indent(lines[j].s, lines[j].len, &jb);
              int jord = 0, jsnum = 1;
              char jdel = 0;
              size_t jmc = 0, jmb = 0;
              if (jind >= list_indent + 4 ||
                  !parse_list_marker(lines[j].s + jb, lines[j].len - jb, jind,
                                     &jord, &jdel, &jsnum, &jmc, &jmb) ||
                  jord != ordered || jdel != ldelim)
                break;

              if (saw_blank_between)
                tight = 0;
              size_t content_col = jind + jmc;
              size_t in = 0;
              if (icap == 0) {
                icap = 16;
                ilines = malloc(icap * sizeof(*ilines));
              }
              LineView first_lv =
                  (LineView){lines[j].s + jb + jmb, lines[j].len - jb - jmb, 0};
              int has_content = !is_blank_line(first_lv.s, first_lv.len);
              int last_was_para = line_can_lazy_continue(first_lv, 0);
              ilines[in++] = first_lv;
              j++;
              int item_had_blank = 0;
              char **lallocs = NULL;
              size_t lan = 0, lacap = 0;
              while (j < nlines) {
                if (is_blank_line(lines[j].s, lines[j].len)) {
                  if (!has_content)
                    break;
                  size_t k = j;
                  while (k < nlines && is_blank_line(lines[k].s, lines[k].len))
                    k++;
                  if (k >= nlines)
                    break;
                  size_t kb = 0;
                  size_t kind = count_indent(lines[k].s, lines[k].len, &kb);
                  if (kind >= content_col) {
                    item_had_blank = 1;
                    last_was_para = 0;
                    while (j < k) {
                      if (in == icap) {
                        icap *= 2;
                        ilines = realloc(ilines, icap * sizeof(*ilines));
                      }
                      ilines[in++] = (LineView){"", 0, 0};
                      j++;
                    }
                    continue;
                  }
                  break;
                }
                size_t nb = 0;
                size_t nind = count_indent(lines[j].s, lines[j].len, &nb);
                if (nind >= content_col) {
                  has_content = 1;
                  if (in == icap) {
                    icap *= 2;
                    ilines = realloc(ilines, icap * sizeof(*ilines));
                  }
                  LineView sl = strip_columns_alloc(lines[j], content_col,
                                                    &lallocs, &lan, &lacap);
                  last_was_para = line_can_lazy_continue(sl, 0);
                  ilines[in++] = sl;
                  j++;
                } else {
                  if (!has_content || !last_was_para || item_had_blank ||
                      line_breaks_lazy(lines[j]))
                    break;
                  const char *nt = lines[j].s + nb;
                  size_t ntl = lines[j].len - nb;
                  if (in == icap) {
                    icap *= 2;
                    ilines = realloc(ilines, icap * sizeof(*ilines));
                  }
                  ilines[in++] = (LineView){nt, ntl, 1};
                  j++;
                }
              }
              uint32_t item = node_new(doc, MD_ITEM);
              if (item != 0) {
                doc->nodes[item].col =
                    (uint8_t)(base_col +
                              (content_col < 200 ? content_col : 200));
                node_append(doc, lid, item);
                int item_direct_blank = 0;
                parse_blocks(doc, item, ilines, in, doc->nodes[item].col,
                             file_path, collect_meta, depth + 1,
                             &item_direct_blank);
                if (item_direct_blank)
                  tight = 0;
              }
              for (size_t la = 0; la < lan; la++)
                free(lallocs[la]);
              free(lallocs);
            }
            free(ilines);
            if (tight)
              doc->nodes[lid].flags |= MD_F_TIGHT;
            i = j;
            continue;
          }
        }
      }
    }

    /* 5. Detect potential definition list line ": " */
    if (indent < 4 && tlen >= 2 && trimmed[0] == ':' &&
        (trimmed[1] == ' ' || trimmed[1] == '\t')) {
      doc->has_deflist = 1;
    }

    /* 6. Accumulate paragraph line (zero-copy when contiguous in src) */
    if (!in_para) {
      p_col = cur_col;
      if (trimmed >= doc->src && trimmed <= doc->src + doc->src_len) {
        p_src_start = trimmed;
        p_src_end = trimmed + tlen;
        p_contiguous = 1;
      } else {
        sbuf_append(&pbuf, trimmed, tlen);
      }
    } else if (p_contiguous && trimmed == p_src_end + 1 &&
               p_src_end[0] == '\n') {
      p_src_end = trimmed + tlen;
    } else {
      if (p_contiguous) {
        sbuf_append(&pbuf, p_src_start, (size_t)(p_src_end - p_src_start));
        p_contiguous = 0;
      }
      sbuf_append_char(&pbuf, '\n');
      sbuf_append(&pbuf, trimmed, tlen);
    }
    i++;
  }

  if (pbuf.len > 0 || p_contiguous) {
    if (p_contiguous)
      finalize_paragraph(doc, parent, p_src_start,
                         (size_t)(p_src_end - p_src_start), 1, p_col);
    else
      finalize_paragraph(doc, parent, pbuf.data, pbuf.len, 0, p_col);
  }
  free(pbuf.data);
}

/* ---- Definition List Grouping (runs only when doc->has_deflist != 0) ---- */

static int line_def_offset(const char *s, size_t len) {
  size_t i = 0;
  while (i < len && s[i] == ' ')
    i++;
  if (i >= len || s[i] != ':')
    return -1;
  i++;
  if (i >= len || (s[i] != ' ' && s[i] != '\t'))
    return -1;
  while (i < len && (s[i] == ' ' || s[i] == '\t'))
    i++;
  return (int)i;
}

static void split_compact_deflist_paras(MdDoc *doc, uint32_t container) {
  uint32_t cur = doc->nodes[container].first_child;
  while (cur != 0) {
    uint32_t next = doc->nodes[cur].next_sibling;
    MdNodeType t = (MdNodeType)doc->nodes[cur].type;
    if (t == MD_PARAGRAPH && doc->nodes[cur].text_len > 0) {
      const char *s = md_text_ptr(doc, &doc->nodes[cur]);
      size_t len = doc->nodes[cur].text_len;
      int in_pool = (doc->nodes[cur].flags & MD_F_TEXT_POOL) != 0;
      const char *nl = memchr(s, '\n', len);
      uint32_t split_target = cur;
      while (nl != NULL) {
        size_t rem = len - (size_t)(nl + 1 - s);
        const char *next_nl = memchr(nl + 1, '\n', rem);
        size_t line_len = next_nl ? (size_t)(next_nl - (nl + 1)) : rem;
        if (line_def_offset(nl + 1, line_len) != -1) {
          size_t left_len = (size_t)(nl - s);
          doc->nodes[split_target].text_len = (uint32_t)left_len;
          uint32_t dp = node_new(doc, MD_PARAGRAPH);
          if (dp == 0)
            break;
          doc->nodes[dp].col = doc->nodes[split_target].col;
          doc->nodes[dp].text_off =
              doc->nodes[split_target].text_off + (uint32_t)left_len + 1;
          doc->nodes[dp].text_len = (uint32_t)rem;
          if (in_pool)
            doc->nodes[dp].flags |= MD_F_TEXT_POOL;
          doc->nodes[dp].next_sibling = doc->nodes[split_target].next_sibling;
          doc->nodes[split_target].next_sibling = dp;
          if (doc->nodes[container].last_child == split_target)
            doc->nodes[container].last_child = dp;
          split_target = dp;
          s = md_text_ptr(doc, &doc->nodes[dp]);
          len = doc->nodes[dp].text_len;
          nl = memchr(s, '\n', len);
          continue;
        }
        nl = next_nl;
      }
    } else if (t == MD_BLOCK_QUOTE || t == MD_LIST || t == MD_ITEM ||
               t == MD_ADMONITION || t == MD_DD) {
      split_compact_deflist_paras(doc, cur);
    }
    cur = next;
  }
}

static void group_deflists(MdDoc *doc, uint32_t container,
                           const char *file_path, int collect_meta, int depth) {
  if (depth > 16)
    return;
  split_compact_deflist_paras(doc, container);

  uint32_t prev = 0;
  uint32_t cur = doc->nodes[container].first_child;
  while (cur != 0) {
    uint32_t nxt = doc->nodes[cur].next_sibling;
    MdNodeType ct = (MdNodeType)doc->nodes[cur].type;
    int next_is_def = 0;
    if (ct == MD_PARAGRAPH && nxt != 0 &&
        doc->nodes[nxt].type == MD_PARAGRAPH) {
      const char *ns = md_text_ptr(doc, &doc->nodes[nxt]);
      size_t nl = doc->nodes[nxt].text_len;
      const char *eol = memchr(ns, '\n', nl);
      if (line_def_offset(ns, eol ? (size_t)(eol - ns) : nl) != -1)
        next_is_def = 1;
    }
    if (!next_is_def) {
      if (ct == MD_BLOCK_QUOTE || ct == MD_LIST || ct == MD_ITEM ||
          ct == MD_ADMONITION)
        group_deflists(doc, cur, file_path, collect_meta, depth + 1);
      prev = cur;
      cur = nxt;
      continue;
    }

    uint32_t dl = node_new(doc, MD_DEFLIST);
    if (dl == 0)
      return;
    if (prev == 0)
      doc->nodes[container].first_child = dl;
    else
      doc->nodes[prev].next_sibling = dl;

    uint32_t after = cur;
    for (;;) {
      uint32_t term = after;
      uint8_t term_col = doc->nodes[term].col;
      uint32_t def = doc->nodes[term].next_sibling;
      uint32_t dt = node_new(doc, MD_DT);
      if (dt == 0)
        break;
      node_append(doc, dl, dt);
      node_append(doc, dt, term);
      after = def;

      while (after != 0 && doc->nodes[after].type == MD_PARAGRAPH) {
        const char *ds = md_text_ptr(doc, &doc->nodes[after]);
        size_t dlen = doc->nodes[after].text_len;
        const char *eol = memchr(ds, '\n', dlen);
        int doff = line_def_offset(ds, eol ? (size_t)(eol - ds) : dlen);
        if (doff == -1)
          break;
        uint8_t dd_col =
            doc->nodes[after].col > 0 ? doc->nodes[after].col : term_col;
        doc->nodes[after].text_off += (uint32_t)doff;
        doc->nodes[after].text_len -= (uint32_t)doff;

        uint32_t dd = node_new(doc, MD_DD);
        if (dd == 0)
          break;
        node_append(doc, dl, dd);
        uint32_t cont = doc->nodes[after].next_sibling;
        node_append(doc, dd, after);

        int absorbed_extra = 0;
        while (cont != 0) {
          uint32_t cont_next = doc->nodes[cont].next_sibling;
          MdNodeType kt = (MdNodeType)doc->nodes[cont].type;
          if (kt == MD_CODE_BLOCK && !(doc->nodes[cont].flags & MD_F_FENCED)) {
            const char *ks_raw = md_text_ptr(doc, &doc->nodes[cont]);
            size_t kl = doc->nodes[cont].text_len;
            char *kcopy = malloc(kl + 1);
            if (kcopy != NULL) {
              memcpy(kcopy, ks_raw, kl);
              kcopy[kl] = '\0';
              LineView *sub_lines = NULL;
              size_t sn = 0, scap = 0;
              size_t pos = 0;
              while (pos < kl) {
                const char *nl2 = memchr(kcopy + pos, '\n', kl - pos);
                size_t ll2 = nl2 ? (size_t)(nl2 - (kcopy + pos)) : (kl - pos);
                if (sn == scap) {
                  scap = scap == 0 ? 16 : scap * 2;
                  sub_lines = realloc(sub_lines, scap * sizeof(*sub_lines));
                }
                sub_lines[sn++] = (LineView){kcopy + pos, ll2, 0};
                pos = nl2 ? (size_t)(nl2 - kcopy) + 1 : kl;
              }
              parse_blocks(doc, dd, sub_lines, sn, dd_col, file_path,
                           collect_meta, depth + 1, NULL);
              free(sub_lines);
              free(kcopy);
            }
            absorbed_extra = 1;
            cont = cont_next;
          } else if (doc->nodes[cont].col > dd_col) {
            node_append(doc, dd, cont);
            absorbed_extra = 1;
            cont = cont_next;
          } else {
            break;
          }
        }
        /* Recursively group nested definition lists inside <dd> */
        if (absorbed_extra)
          group_deflists(doc, dd, file_path, collect_meta, depth + 1);
        after = cont;
      }

      if (after == 0 || doc->nodes[after].type != MD_PARAGRAPH)
        break;
      uint32_t anext = doc->nodes[after].next_sibling;
      if (anext == 0 || doc->nodes[anext].type != MD_PARAGRAPH)
        break;
      const char *ns = md_text_ptr(doc, &doc->nodes[anext]);
      size_t nl = doc->nodes[anext].text_len;
      const char *eol = memchr(ns, '\n', nl);
      if (line_def_offset(ns, eol ? (size_t)(eol - ns) : nl) == -1)
        break;
    }

    doc->nodes[dl].next_sibling = after;
    if (after == 0)
      doc->nodes[container].last_child = dl;
    prev = dl;
    cur = after;
  }
}

/* ---- Phase 2: Parse inlines for all leaf blocks ---- */

static void parse_all_inlines(MdDoc *doc, const char *file_path,
                              int collect_meta) {
  uint32_t block_count = doc->nodes_len;
  for (uint32_t id = 1; id < block_count; id++) {
    MdNodeType t = (MdNodeType)doc->nodes[id].type;
    if (t != MD_PARAGRAPH && t != MD_HEADING && t != MD_TABLE_CELL)
      continue;
    if (doc->nodes[id].text_len == 0)
      continue;
    const char *s = md_text_ptr(doc, &doc->nodes[id]);
    size_t len = doc->nodes[id].text_len;
    char *tmp_copy = NULL;
    if (doc->nodes[id].flags & MD_F_TEXT_POOL) {
      tmp_copy = malloc(len + 1);
      if (tmp_copy == NULL)
        continue;
      memcpy(tmp_copy, s, len);
      tmp_copy[len] = '\0';
      s = tmp_copy;
    }
    parse_inlines_range(doc, id, s, s + len, file_path, collect_meta);
    free(tmp_copy);

    if (collect_meta && t == MD_HEADING && doc->nodes[id].aux_len > 0 &&
        doc->title == NULL) {
      char *tbuf = NULL;
      size_t tlen = 0, tcap = 0;
      extract_plain_title_rec(doc, id, &tbuf, &tlen, &tcap);
      doc->title = tbuf != NULL ? tbuf : calloc(1, 1);
    }
  }
  if (collect_meta && doc->title == NULL)
    doc->title = calloc(1, 1);
}

/* ---- Public API ---- */

int md_parse_buffer(MdDoc *doc, const char *data, size_t len,
                    const char *file_path, int collect_meta) {
  memset(doc, 0, sizeof(*doc));
  doc->src = (char *)data;
  doc->src_len = len;
  doc->owns_src = 0;

  uint32_t init_nodes = (uint32_t)(len / 16) + 64;
  if (init_nodes < 128)
    init_nodes = 128;
  doc->nodes = malloc((size_t)init_nodes * sizeof(MdNode));
  if (doc->nodes == NULL)
    return -1;
  doc->nodes_cap = init_nodes;
  doc->nodes_len = 1;
  memset(&doc->nodes[0], 0, sizeof(MdNode));

  size_t init_pool = len / 4 + 256;
  doc->pool = malloc(init_pool);
  if (doc->pool == NULL) {
    md_doc_free(doc);
    return -1;
  }
  doc->pool_cap = init_pool;
  doc->pool_len = 0;

  if (collect_meta)
    init_array(&doc->anchors, len > 65536 ? 256 : 16);

  uint32_t root = node_new(doc, MD_DOCUMENT);
  if (root == 0) {
    md_doc_free(doc);
    return -1;
  }

  size_t lcap = len / 32 + 32;
  LineView *lines = malloc(lcap * sizeof(*lines));
  if (lines == NULL) {
    md_doc_free(doc);
    return -1;
  }
  size_t nlines = 0;
  size_t pos = 0;
  while (pos < len) {
    const char *nl = memchr(data + pos, '\n', len - pos);
    size_t ll = nl ? (size_t)(nl - (data + pos)) : (len - pos);
    if (ll > 0 && data[pos + ll - 1] == '\r')
      ll--;
    if (nlines == lcap) {
      lcap *= 2;
      LineView *nlv = realloc(lines, lcap * sizeof(*nlv));
      if (nlv == NULL) {
        free(lines);
        md_doc_free(doc);
        return -1;
      }
      lines = nlv;
    }
    lines[nlines++] = (LineView){data + pos, ll, 0};
    pos = nl ? (size_t)(nl - data) + 1 : len;
  }

  parse_blocks(doc, root, lines, nlines, 1, file_path, collect_meta, 0, NULL);
  free(lines);

  if (doc->has_deflist)
    group_deflists(doc, root, file_path, collect_meta, 0);

  parse_all_inlines(doc, file_path, collect_meta);
  if (doc->anchor_overflow) {
    md_doc_free(doc);
    return -1;
  }
  return 0;
}

int md_parse_file(MdDoc *doc, const char *file_path, int collect_meta) {
  FILE *f = fopen(file_path, "rb");
  if (f == NULL)
    return -1;
  struct stat st;
  char *buf = NULL;
  size_t total = 0;
  if (fstat(fileno(f), &st) == 0 && S_ISREG(st.st_mode) && st.st_size >= 0) {
    size_t sz = (size_t)st.st_size;
    buf = malloc(sz + 1);
    if (buf == NULL) {
      fclose(f);
      return -1;
    }
    while (total < sz) {
      size_t n = fread(buf + total, 1, sz - total, f);
      if (n == 0)
        break;
      total += n;
    }
    buf[total] = '\0';
  } else {
    size_t cap = 4096;
    buf = malloc(cap);
    if (buf == NULL) {
      fclose(f);
      return -1;
    }
    for (;;) {
      if (total + 4096 + 1 > cap) {
        cap *= 2;
        char *nb = realloc(buf, cap);
        if (nb == NULL) {
          free(buf);
          fclose(f);
          return -1;
        }
        buf = nb;
      }
      size_t n = fread(buf + total, 1, 4096, f);
      total += n;
      if (n < 4096)
        break;
    }
    buf[total] = '\0';
  }
  int err = ferror(f);
  if (fclose(f) != 0 || err) {
    free(buf);
    return -1;
  }
  int rc = md_parse_buffer(doc, buf, total, file_path, collect_meta);
  if (rc != 0) {
    free(buf);
    return -1;
  }
  doc->owns_src = 1;
  return 0;
}

void md_doc_free(MdDoc *doc) {
  if (doc->owns_src)
    free(doc->src);
  free(doc->pool);
  free(doc->nodes);
  free(doc->extras);
  free(doc->refs);
  free(doc->title);
  free_array(&doc->anchors);
  memset(doc, 0, sizeof(*doc));
}
