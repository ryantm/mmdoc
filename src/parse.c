/* SPDX-License-Identifier: CC0-1.0 */
#include <stdio.h>

enum parse_heading_bracketed_span_state {
  HEADING_NONE,
  HEADING_SPACE,
  HEADING_L,
  HEADING_ID
};

int parse_heading_bracketed_span_id(const char *text, char *id) {
  if (text == NULL)
    return -1;
  int id_pos = 0;
  int bracket_found = 0;
  int bracket_start = 0;

  enum parse_heading_bracketed_span_state state = HEADING_NONE;

  for (int i = 0; text[i] != '\0'; i++) {
    char c = text[i];
    if (state == HEADING_NONE) {
      if (c == ' ') {
        state = HEADING_SPACE;
        bracket_start = i;
      }
      continue;
    }
    if (state == HEADING_SPACE) {
      if (c == ' ')
        continue;
      if (c == '{') {
        state = HEADING_L;
        continue;
      }
      state = HEADING_NONE;
      continue;
    }
    if (state == HEADING_L) {
      if (c == '#') {
        state = HEADING_ID;
        id_pos = 0;
        continue;
      }
      if (c == ' ') {
        state = HEADING_SPACE;
        bracket_start = i;
      } else {
        state = HEADING_NONE;
      }
      continue;
    }
    if (state == HEADING_ID) {
      if (c == '}') {
        if (id_pos > 0) {
          id[id_pos] = '\0';
          bracket_found = 1;
          break;
        }
        state = HEADING_NONE;
        continue;
      }
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
        id_pos = 0;
        if (c == ' ') {
          state = HEADING_SPACE;
          bracket_start = i;
        } else {
          state = HEADING_NONE;
        }
        continue;
      }
      id[id_pos++] = c;
      continue;
    }
  }

  return bracket_found ? bracket_start : -1;
}

enum parse_link_bracketed_span_state {
  LINK_NONE,
  LINK_TEXT,
  LINK_R_SQUARE,
  LINK_L,
  LINK_ID
};

int parse_link_bracketed_span_id(const char *text, char *span_text, char *id) {
  if (text == NULL)
    return -1;

  int text_pos = 0;
  int id_pos = 0;
  int bracket_found = 0;
  int bracket_start = 0;
  enum parse_link_bracketed_span_state state = LINK_NONE;

  for (int i = 0; text[i] != '\0'; i++) {
    char c = text[i];
    if (state == LINK_NONE) {
      if (c == '[') {
        state = LINK_TEXT;
        bracket_start = i;
        text_pos = 0;
      }
      continue;
    }
    if (state == LINK_TEXT) {
      if (c == '[') {
        bracket_start = i;
        text_pos = 0;
        continue;
      }
      if (c == ']') {
        span_text[text_pos] = '\0';
        state = LINK_R_SQUARE;
        continue;
      }
      span_text[text_pos++] = c;
      continue;
    }
    if (state == LINK_R_SQUARE) {
      if (c == '{') {
        state = LINK_L;
        continue;
      }
      if (c == '[') {
        state = LINK_TEXT;
        bracket_start = i;
        text_pos = 0;
        continue;
      }
      text_pos = 0;
      state = LINK_NONE;
      continue;
    }
    if (state == LINK_L) {
      if (c == '#') {
        state = LINK_ID;
        id_pos = 0;
        continue;
      }
      if (c == '[') {
        state = LINK_TEXT;
        bracket_start = i;
        text_pos = 0;
        continue;
      }
      text_pos = 0;
      state = LINK_NONE;
      continue;
    }
    if (state == LINK_ID) {
      if (c == '}') {
        if (id_pos > 0) {
          id[id_pos] = '\0';
          bracket_found = 1;
          break;
        }
        text_pos = 0;
        id_pos = 0;
        state = LINK_NONE;
        continue;
      }
      if (c == '[') {
        state = LINK_TEXT;
        bracket_start = i;
        text_pos = 0;
        id_pos = 0;
        continue;
      }
      id[id_pos++] = c;
      continue;
    }
  }

  return bracket_found ? bracket_start : -1;
}

enum parse_admonition_start_state {
  ADMONITION_START_NONE,
  ADMONITION_START_ONE,
  ADMONITION_START_TWO,
  ADMONITION_START_THREE,
  ADMONITION_START_TYPE,
  ADMONITION_START_TYPE_DONE,
  ADMONITION_START_ANCHOR,
};

int parse_admonition_start(const char *text, char *admonition_type,
                           char *admonition_anchor) {
  if (text == NULL)
    return -1;
  int type_pos = 0;
  int anchor_pos = 0;
  enum parse_admonition_start_state state = ADMONITION_START_NONE;

  for (int i = 0; text[i] != '\0'; i++) {
    if (state == ADMONITION_START_NONE && (text[i] != ':' && text[i] != ' '))
      return -1;
    if (state == ADMONITION_START_NONE && text[i] == ' ')
      continue;
    if (state == ADMONITION_START_NONE && text[i] == ':') {
      state = ADMONITION_START_ONE;
      continue;
    }
    if (state == ADMONITION_START_ONE && text[i] == ':') {
      state = ADMONITION_START_TWO;
      continue;
    }
    if (state == ADMONITION_START_TWO && text[i] == ':') {
      state = ADMONITION_START_THREE;
      continue;
    }
    if (state == ADMONITION_START_THREE &&
        (text[i] == ' ' || text[i] == '{' || text[i] == '.')) {
      continue;
    }
    if (state == ADMONITION_START_THREE) {
      state = ADMONITION_START_TYPE;
    }
    if (state == ADMONITION_START_TYPE && text[i] == '}') {
      break;
    }
    if (state == ADMONITION_START_TYPE && text[i] == ' ') {
      state = ADMONITION_START_TYPE_DONE;
      continue;
    }
    if (state == ADMONITION_START_TYPE) {
      admonition_type[type_pos] = text[i];
      type_pos++;
      continue;
    }
    if (state == ADMONITION_START_TYPE_DONE && text[i] == ' ') {
      continue;
    }
    if (state == ADMONITION_START_TYPE_DONE && text[i] == '}') {
      break;
    }
    if (state == ADMONITION_START_TYPE_DONE && text[i] == '#') {
      state = ADMONITION_START_ANCHOR;
      continue;
    }
    if (state == ADMONITION_START_ANCHOR &&
        (text[i] == ' ' || text[i] == '}')) {
      break;
    }
    if (state == ADMONITION_START_ANCHOR) {
      admonition_anchor[anchor_pos] = text[i];
      anchor_pos++;
      continue;
    }
    return -1;
  }
  if (state >= ADMONITION_START_TYPE) {
    admonition_type[type_pos] = '\0';
    admonition_anchor[anchor_pos] = '\0';
    return 0;
  }
  return -1;
}

int parse_admonition_end(const char *text) {
  if (text == NULL)
    return -1;
  const char *p = text;
  while (*p == ' ')
    p++;
  int colons = 0;
  while (*p == ':') {
    colons++;
    p++;
  }
  if (colons < 3)
    return -1;
  while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
    p++;
  return *p == '\0' ? 0 : -1;
}

enum parse_dd_state {
  DD_NONE,
  DD_COLON,
  DD_SPACE,
};

int parse_dd(const char *text) {
  if (text == NULL)
    return -1;
  enum parse_dd_state state = DD_NONE;

  for (int i = 0; text[i] != '\0'; i++) {
    if (state == DD_NONE && (text[i] != ':' && text[i] != ' '))
      return -1;
    if (state == DD_NONE && text[i] == ' ')
      continue;
    if (state == DD_NONE && text[i] == ':') {
      state = DD_COLON;
      continue;
    }
    if (state == DD_COLON && (text[i] == ' ' || text[i] == '\t')) {
      state = DD_SPACE;
      continue;
    }
    if (state == DD_SPACE && (text[i] == ' ' || text[i] == '\t'))
      continue;
    if (state == DD_SPACE)
      return i;
    return -1;
  }
  return -1;
}
