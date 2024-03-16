/* SPDX-License-Identifier: CC0-1.0 */
#include "html.h"
#include "asset.h"

int html_js(FILE *file) {
  fputs("<script>\n", file);
  if (asset_write_to_file_mmdoc_js(file) != 0)
    return -1;
  fputs("</script>\n", file);
  return 0;
}

int html_search_js(FILE *file) {
  fputs("<script defer='true'>\n", file);
  if (asset_write_to_file_fuse_basic_min_js(file) != 0)
    return -1;
  if (asset_write_to_file_mmdoc_search_js(file) != 0)
    return -1;
  fputs("</script>\n", file);
  return 0;
}

int html_highlight_js(FILE *file) {
  fputs("<script>\n", file);
  if (asset_write_to_file_highlight_pack_js(file) != 0)
    return -1;
  fputs("hljs.highlightAll()\n</script>\n", file);
  return 0;
}

int html_css(FILE *file) {
  fputs("<style>\n", file);
  if (asset_write_to_file_a11y_dark_css(file) != 0)
    return -1;
  if (asset_write_to_file_a11y_light_css(file) != 0)
    return -1;
  if (asset_write_to_file_mmdoc_css(file) != 0)
    return -1;
  fputs("</style>\n", file);
  return 0;
}
