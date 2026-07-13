/* SPDX-License-Identifier: CC0-1.0 */
#include "refs.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>

enum ref_parse_state { NONE, L, REF };

int mmdoc_refs(Array *md_refs, char *path) {
  char ref[1024];
  size_t refpos = 0;
  init_array(md_refs, 500);

  FILE *file = fopen(path, "r");
  if (file == NULL) {
    fprintf(stderr, "Error opening %s: %s\n", path, strerror(errno));
    return 1;
  }
  int c;
  enum ref_parse_state state = NONE;

  while (1) {
    c = fgetc(file);
    if (c == EOF)
      break;
    if (state == NONE && c == '(') {
      state = L;
      continue;
    }
    if (state == L && c == '#') {
      state = REF;
      ref[refpos] = c;
      refpos += 1;
      continue;
    }
    if (state == REF && (c == '\n' || c == '\r')) {
    } else if (state == REF && c != ')') {
      if (refpos >= sizeof(ref) - 1) {
        fprintf(stderr, "Anchor reference in %s exceeds %zu bytes.\n", path,
                sizeof(ref) - 1);
        fclose(file);
        return 1;
      }
      ref[refpos] = c;
      refpos += 1;
      continue;
    } else if (state == REF && c == ')') {
      ref[refpos] = '\0';
      insert_array(md_refs, ref);
    }
    refpos = 0;
    state = NONE;
    continue;
  }
  fclose(file);

  if (md_refs->used == 0) {
    printf("Error %s didn't reference any anchor.", path);
    return 1;
  }

  return 0;
}
