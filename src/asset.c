/* SPDX-License-Identifier: CC0-1.0 */
#include "epub.css.h"
#include "fuse.basic.min.js.h"
#include "highlight.pack.js.h"
#include "minimal.css.h"
#include "mono-blue.css.h"
#include "search.js.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int errno;

static int asset_write_to_dir(char *dir, char *asset_file_name,
                              unsigned char *asset_array,
                              unsigned int asset_array_length) {
  char *asset_path = malloc(strlen(dir) + 1 + strlen(asset_file_name) + 1);
  if (asset_path == NULL) {
    printf("Failed to allocate memory at %s line %d\n", __FILE__, __LINE__);
    return -1;
  }
  sprintf(asset_path, "%s/%s", dir, asset_file_name);
  FILE *asset_file = fopen(asset_path, "w");
  if (asset_file == NULL) {
    printf("Failed to open file %s for writing: %s\n", asset_path,
           strerror(errno));
    free(asset_path);
    return -1;
  }
  for (int i = 0; i < asset_array_length; i++) {
    int ret = fputc(asset_array[i], asset_file);
    if (ret == EOF) {
      printf("Failed to write to file %s: %s\n", asset_path, strerror(errno));
      free(asset_path);
      return -1;
    }
  }
  free(asset_path);
  if (fclose(asset_file) != 0) {
    printf("Failed to close file %s: %s\n", asset_path, strerror(errno));
    return -1;
  };
  return 0;
}

extern unsigned char ___src_asset_highlight_pack_js[];
extern unsigned int ___src_asset_highlight_pack_js_len;
int asset_write_to_dir_highlight_pack_js(char *dir) {
  return asset_write_to_dir(dir, "highlight.pack.js",
                            ___src_asset_highlight_pack_js,
                            ___src_asset_highlight_pack_js_len);
}

extern unsigned char ___src_asset_fuse_basic_min_js[];
extern unsigned int ___src_asset_fuse_basic_min_js_len;
int asset_write_to_dir_fuse_basic_min_js(char *dir) {
  return asset_write_to_dir(dir, "fuse.basic.min.js",
                            ___src_asset_fuse_basic_min_js,
                            ___src_asset_fuse_basic_min_js_len);
}

extern unsigned char ___src_asset_mono_blue_css[];
extern unsigned int ___src_asset_mono_blue_css_len;
int asset_write_to_dir_mono_blue_css(char *dir) {
  return asset_write_to_dir(dir, "mono-blue.css", ___src_asset_mono_blue_css,
                            ___src_asset_mono_blue_css_len);
}

extern unsigned char ___src_asset_epub_css[];
extern unsigned int ___src_asset_epub_css_len;
int asset_write_to_dir_epub_css(char *dir) {
  return asset_write_to_dir(dir, "epub.css", ___src_asset_epub_css,
                            ___src_asset_epub_css_len);
}

extern unsigned char ___src_asset_minimal_css[];
extern unsigned int ___src_asset_minimal_css_len;
int asset_write_to_dir_minimal_css(char *dir) {
  return asset_write_to_dir(dir, "minimal.css", ___src_asset_minimal_css,
                            ___src_asset_minimal_css_len);
}

extern unsigned char ___src_asset_search_js[];
extern unsigned int ___src_asset_search_js_len;
int asset_write_to_dir_search_js(char *dir) {
  return asset_write_to_dir(dir, "search.js", ___src_asset_search_js,
                            ___src_asset_search_js_len);
}
