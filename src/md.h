/* SPDX-License-Identifier: CC0-1.0 */
#ifndef MMDOC_MD_H
#define MMDOC_MD_H

#include "types.h"
#include <stddef.h>
#include <stdint.h>

typedef enum : uint8_t {
  MD_NONE = 0,
  MD_DOCUMENT,
  MD_BLOCK_QUOTE,
  MD_LIST,
  MD_ITEM,
  MD_ADMONITION,
  MD_DEFLIST,
  MD_DT,
  MD_DD,
  MD_TABLE,
  MD_TABLE_HEAD,
  MD_TABLE_BODY,
  MD_TABLE_ROW,
  MD_TABLE_CELL,
  MD_PARAGRAPH,
  MD_HEADING,
  MD_CODE_BLOCK,
  MD_HTML_BLOCK,
  MD_THEMATIC_BREAK,
  /* Inlines */
  MD_TEXT,
  MD_CODE,
  MD_SOFTBREAK,
  MD_LINEBREAK,
  MD_EMPH,
  MD_STRONG,
  MD_LINK,
  MD_IMAGE,
  MD_HTML_INLINE,
  MD_SPAN,
} MdNodeType;

enum : uint8_t {
  MD_F_TEXT_POOL = 1 << 0,
  MD_F_AUX_POOL = 1 << 1,
  MD_F_FENCED = 1 << 2,
  MD_F_ORDERED = 1 << 3,
  MD_F_TIGHT = 1 << 4,
  MD_F_HAS_EXTRA = 1 << 5,
  MD_F_TABLE_TH = 1 << 6,
  MD_F_AUTOLINK = 1 << 7,
};

/* Admonition tight-boundary bits stored in MdNode.info for MD_ADMONITION */
enum : uint8_t {
  MD_ADMON_TIGHT_OPEN = 1 << 0,
  MD_ADMON_TIGHT_CLOSE = 1 << 1,
};

enum : uint8_t {
  MD_ALIGN_NONE = 0,
  MD_ALIGN_LEFT = 1,
  MD_ALIGN_CENTER = 2,
  MD_ALIGN_RIGHT = 3,
};

/* Compact 32-byte flat AST node (2 nodes per 64-byte cache line). */
typedef struct {
  uint32_t first_child;
  uint32_t last_child;
  uint32_t next_sibling;
  uint32_t text_off;
  uint32_t text_len;
  uint32_t aux_off;
  uint32_t aux_len;
  MdNodeType type;
  uint8_t flags;
  uint8_t info; /* heading level 1..6, list delim, table align, admon flags */
  uint8_t col;  /* 1-based start column (saturated at 255) */
} MdNode;

static_assert(sizeof(MdNode) == 32);

typedef struct {
  uint32_t node_id;
  uint32_t str_off;
  uint32_t str_len;
  int32_t num;
  bool str_in_pool;
} MdExtra;

typedef struct {
  uint32_t label_off;
  uint32_t label_len;
  uint32_t url_off;
  uint32_t url_len;
  uint32_t title_off;
  uint32_t title_len;
  bool url_in_pool;
  bool title_in_pool;
  uint64_t hash;
} MdRefDef;

typedef struct {
  char *src;
  size_t src_len;
  bool owns_src;

  char *pool;
  size_t pool_len;
  size_t pool_cap;

  MdNode *nodes;
  uint32_t nodes_len;
  uint32_t nodes_cap;

  MdExtra *extras;
  uint32_t extras_len;
  uint32_t extras_cap;

  MdRefDef *refs;
  uint32_t refs_cap;
  uint32_t refs_used;

  bool has_code_block;
  bool has_deflist;
  bool anchor_overflow;
  char *title;
  Array anchors;
} MdDoc;

static inline const char *md_text_ptr(const MdDoc *doc, const MdNode *n) {
  if (n->text_len == 0)
    return "";
  return (n->flags & MD_F_TEXT_POOL) ? (doc->pool + n->text_off)
                                     : (doc->src + n->text_off);
}

static inline const char *md_aux_ptr(const MdDoc *doc, const MdNode *n) {
  if (n->aux_len == 0)
    return "";
  return (n->flags & MD_F_AUX_POOL) ? (doc->pool + n->aux_off)
                                    : (doc->src + n->aux_off);
}

const MdExtra *md_find_extra(const MdDoc *doc, uint32_t node_id);

/* Parse Markdown buffer or file into a flat-array zero-copy MdDoc.
 * Collects anchors, first anchored heading title, and has_code_block in-pass.
 * Returns 0 on success, -1 on I/O, allocation, or anchor-length error. */
[[nodiscard]] int md_parse_buffer(MdDoc *doc, const char *data, size_t len,
                                  const char *file_path, bool collect_meta);
[[nodiscard]] int md_parse_file(MdDoc *doc, const char *file_path,
                                bool collect_meta);
void md_doc_free(MdDoc *doc);

#endif /* MMDOC_MD_H */
