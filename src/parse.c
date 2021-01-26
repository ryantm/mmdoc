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
    if (state == HEADING_NONE && text[i] == ' ') {
      state = HEADING_SPACE;
      bracket_start = i;
      continue;
    }
    if (state == HEADING_SPACE && text[i] == '{') {
      state = HEADING_L;
      continue;
    }
    if (state == HEADING_L && text[i] == '#') {
      state = HEADING_ID;
      continue;
    }
    if (state == HEADING_ID && text[i] != '}') {
      id[id_pos] = text[i];
      id_pos++;
      continue;
    } else if (state == HEADING_ID && text[i] == '}') {
      id[id_pos] = '\0';
      bracket_found = 1;
      break;
    }
    id_pos = 0;
    state = HEADING_NONE;
    continue;
  }

  if (bracket_found == 1)
    return bracket_start;
  else
    return -1;
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
    if (state == LINK_NONE && text[i] == '[') {
      state = LINK_TEXT;
      bracket_start = i;
      continue;
    }
    if (state == LINK_TEXT && text[i] != ']') {
      span_text[text_pos] = text[i];
      text_pos++;
      continue;
    }
    if (state == LINK_TEXT && text[i] == ']') {
      span_text[text_pos] = '\0';
      state = LINK_R_SQUARE;
      continue;
    }
    if (state == LINK_R_SQUARE && text[i] == '{') {
      state = LINK_L;
      continue;
    }
    if (state == LINK_L && text[i] == '#') {
      state = LINK_ID;
      continue;
    }
    if (state == LINK_ID && text[i] != '}') {
      id[id_pos] = text[i];
      id_pos++;
      continue;
    } else if (state == LINK_ID && text[i] == '}') {
      id[id_pos] = '\0';
      bracket_found = 1;
      break;
    }
    text_pos = 0;
    id_pos = 0;
    state = LINK_NONE;
    continue;
  }

  if (bracket_found == 1)
    return bracket_start;
  else
    return -1;
}
