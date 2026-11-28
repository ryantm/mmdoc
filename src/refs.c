/* SPDX-License-Identifier: CC0-1.0 */
#include "refs.h"
#include "md.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>

static constexpr size_t MAX_ANCHOR_REF_LEN = 1024;

static int collect_links_rec(const MdDoc *doc, uint32_t node_id, Array *md_refs,
                             const char *path) {
  for (uint32_t c = doc->nodes[node_id].first_child; c != 0;
       c = doc->nodes[c].next_sibling) {
    const MdNode *n = &doc->nodes[c];
    if (n->type == MD_LINK && n->aux_len > 1) {
      const char *url = md_aux_ptr(doc, n);
      if (url[0] == '#') {
        if (n->aux_len >= MAX_ANCHOR_REF_LEN) {
          fprintf(stderr, "Anchor reference in %s exceeds %zu bytes.\n", path,
                  MAX_ANCHOR_REF_LEN - 1);
          return -1;
        }
        char buf[MAX_ANCHOR_REF_LEN];
        memcpy(buf, url, n->aux_len);
        buf[n->aux_len] = '\0';
        insert_array(md_refs, buf);
      }
    }
    if (n->first_child != 0 && collect_links_rec(doc, c, md_refs, path) != 0)
      return -1;
  }
  return 0;
}

int mmdoc_refs(Array *md_refs, char *path) {
  init_array(md_refs, 64);
  MdDoc doc;
  if (md_parse_file(&doc, path, false) != 0) {
    fprintf(stderr, "Error opening %s: %s\n", path, strerror(errno));
    return 1;
  }
  bool failed = collect_links_rec(&doc, 1, md_refs, path) != 0;
  md_doc_free(&doc);
  if (failed)
    return 1;
  if (md_refs->used == 0) {
    printf("Error %s didn't reference any anchor.\n", path);
    return 1;
  }
  return 0;
}
