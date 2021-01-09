#include "cmark-gfm.h"

static cmark_llist *syntax_extensions = NULL;

int main() { 
  cmark_node * document = cmark_parse_document("Hello *world*", 13,
                                               CMARK_OPT_DEFAULT);
  printf("%s", cmark_render_html(document, CMARK_OPT_DEFAULT, syntax_extensions));
  return 1; 
}
