/* SPDX-License-Identifier: CC0-1.0 */
#include "refs.h"
#include "arena.h"
#include "parse.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>

int mmdoc_refs(Array *md_refs, char *path) {
  init_array(md_refs, 64);

  MmdocArena arena;
  mmdoc_arena_init(&arena, 65536);
  MmdocArena *prev_arena = mmdoc_arena_bind(&arena);
  cmark_mem *mem = mmdoc_arena_cmark_mem();

  cmark_parser *parser = cmark_parser_new_with_mem(CMARK_OPT_DEFAULT, mem);
  if (parser == NULL) {
    mmdoc_arena_bind(prev_arena);
    mmdoc_arena_destroy(&arena);
    return 1;
  }
  cmark_node *document = mmdoc_render_cmark_document(path, parser);
  if (document == NULL) {
    fprintf(stderr, "Error opening %s: %s\n", path, strerror(errno));
    mmdoc_arena_bind(prev_arena);
    mmdoc_arena_destroy(&arena);
    return 1;
  }

  int failed = 0;
  cmark_iter *iter = cmark_iter_new(document);
  cmark_event_type event;
  while ((event = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
    if (event != CMARK_EVENT_ENTER)
      continue;
    cmark_node *node = cmark_iter_get_node(iter);
    if (cmark_node_get_type(node) != CMARK_NODE_LINK)
      continue;
    const char *url = cmark_node_get_url(node);
    if (url == NULL || url[0] != '#' || url[1] == '\0')
      continue;
    size_t len = strlen(url);
    if (len >= 1024) {
      fprintf(stderr, "Anchor reference in %s exceeds %zu bytes.\n", path,
              (size_t)1023);
      failed = 1;
      break;
    }
    insert_array(md_refs, (char *)url);
  }
  cmark_iter_free(iter);

  mmdoc_arena_bind(prev_arena);
  mmdoc_arena_destroy(&arena);

  if (failed)
    return 1;

  if (md_refs->used == 0) {
    printf("Error %s didn't reference any anchor.\n", path);
    return 1;
  }

  return 0;
}
