/* SPDX-License-Identifier: CC0-1.0 */
#include "asset.h"
#include "a11y-dark.css.h"
#include "a11y-light.css.h"
#include "fuse.basic.min.js.h"
#include "highlight.pack.js.h"
#include "mmdoc.css.h"
#include "mmdoc.js.h"
#include "mmdoc_search.js.h"

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int errno;

extern unsigned char ___src_asset_highlight_pack_js[];
extern unsigned int ___src_asset_highlight_pack_js_len;
extern unsigned char ___src_asset_fuse_basic_min_js[];
extern unsigned int ___src_asset_fuse_basic_min_js_len;
extern unsigned char ___src_asset_a11y_light_css[];
extern unsigned int ___src_asset_a11y_light_css_len;
extern unsigned char ___src_asset_a11y_dark_css[];
extern unsigned int ___src_asset_a11y_dark_css_len;
extern unsigned char ___src_asset_mmdoc_css[];
extern unsigned int ___src_asset_mmdoc_css_len;
extern unsigned char ___src_asset_mmdoc_js[];
extern unsigned int ___src_asset_mmdoc_js_len;
extern unsigned char ___src_asset_mmdoc_search_js[];
extern unsigned int ___src_asset_mmdoc_search_js_len;

static uint64_t asset_hash_update(uint64_t hash, const unsigned char *bytes,
                                  size_t length) {
  for (size_t i = 0; i < length; i++) {
    hash ^= bytes[i];
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

static uint64_t asset_hash(const unsigned char *bytes, size_t length) {
  return asset_hash_update(UINT64_C(14695981039346656037), bytes, length);
}

static void asset_hashed_file_name(const char *stem, const char *extension,
                                   const unsigned char *bytes, size_t length,
                                   char *file_name, size_t file_name_size) {
  snprintf(file_name, file_name_size, "%s.%016" PRIx64 ".%s", stem,
           asset_hash(bytes, length), extension);
}

void asset_file_names(AssetFileNames *names) {
  asset_hashed_file_name("fuse.basic.min", "js", ___src_asset_fuse_basic_min_js,
                         ___src_asset_fuse_basic_min_js_len,
                         names->fuse_basic_min_js,
                         sizeof(names->fuse_basic_min_js));
  asset_hashed_file_name("highlight.pack", "js", ___src_asset_highlight_pack_js,
                         ___src_asset_highlight_pack_js_len,
                         names->highlight_pack_js,
                         sizeof(names->highlight_pack_js));
  asset_hashed_file_name("a11y-dark", "css", ___src_asset_a11y_dark_css,
                         ___src_asset_a11y_dark_css_len, names->a11y_dark_css,
                         sizeof(names->a11y_dark_css));
  asset_hashed_file_name("a11y-light", "css", ___src_asset_a11y_light_css,
                         ___src_asset_a11y_light_css_len, names->a11y_light_css,
                         sizeof(names->a11y_light_css));
  asset_hashed_file_name("mmdoc", "css", ___src_asset_mmdoc_css,
                         ___src_asset_mmdoc_css_len, names->mmdoc_css,
                         sizeof(names->mmdoc_css));
  asset_hashed_file_name("mmdoc", "js", ___src_asset_mmdoc_js,
                         ___src_asset_mmdoc_js_len, names->mmdoc_js,
                         sizeof(names->mmdoc_js));
  asset_hashed_file_name("mmdoc_search", "js", ___src_asset_mmdoc_search_js,
                         ___src_asset_mmdoc_search_js_len,
                         names->mmdoc_search_js,
                         sizeof(names->mmdoc_search_js));
}

int asset_hashed_file_name_from_path(const char *path, const char *stem,
                                     const char *extension, char *file_name,
                                     size_t file_name_size) {
  FILE *file = fopen(path, "rb");
  if (file == NULL)
    return -1;

  uint64_t hash = UINT64_C(14695981039346656037);
  unsigned char buffer[64 * 1024];
  size_t bytes_read;
  while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0)
    hash = asset_hash_update(hash, buffer, bytes_read);
  int read_failed = ferror(file);
  int close_failed = fclose(file) != 0;
  int failed = read_failed || close_failed;
  if (failed)
    return -1;

  snprintf(file_name, file_name_size, "%s.%016" PRIx64 ".%s", stem, hash,
           extension);
  return 0;
}

static int asset_write_to_file(FILE *file, unsigned char *asset_array,
                               unsigned int asset_array_length) {
  if (fwrite(asset_array, 1, asset_array_length, file) != asset_array_length) {
    printf("Failed to write to file: %s\n", strerror(errno));
    return -1;
  }
  return 0;
}

static int asset_write_to_dir(char *dir, const char *asset_file_name,
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
  if (asset_write_to_file(asset_file, asset_array, asset_array_length) != 0) {
    printf("Failed to write to file %s: %s\n", asset_path, strerror(errno));
    free(asset_path);
    return -1;
  }
  if (fclose(asset_file) != 0) {
    printf("Failed to close file %s: %s\n", asset_path, strerror(errno));
    free(asset_path);
    return -1;
  };
  free(asset_path);
  return 0;
}

int asset_write_to_file_highlight_pack_js(FILE *file) {
  return asset_write_to_file(file, ___src_asset_highlight_pack_js,
                             ___src_asset_highlight_pack_js_len);
}
int asset_write_to_dir_highlight_pack_js(char *dir, const char *file_name) {
  return asset_write_to_dir(dir, file_name, ___src_asset_highlight_pack_js,
                            ___src_asset_highlight_pack_js_len);
}

int asset_write_to_dir_fuse_basic_min_js(char *dir, const char *file_name) {
  return asset_write_to_dir(dir, file_name, ___src_asset_fuse_basic_min_js,
                            ___src_asset_fuse_basic_min_js_len);
}
int asset_write_to_file_a11y_light_css(FILE *file) {
  return asset_write_to_file(file, ___src_asset_a11y_light_css,
                             ___src_asset_a11y_light_css_len);
}
int asset_write_to_dir_a11y_light_css(char *dir, const char *file_name) {
  return asset_write_to_dir(dir, file_name, ___src_asset_a11y_light_css,
                            ___src_asset_a11y_light_css_len);
}

int asset_write_to_file_a11y_dark_css(FILE *file) {
  return asset_write_to_file(file, ___src_asset_a11y_dark_css,
                             ___src_asset_a11y_dark_css_len);
}
int asset_write_to_dir_a11y_dark_css(char *dir, const char *file_name) {
  return asset_write_to_dir(dir, file_name, ___src_asset_a11y_dark_css,
                            ___src_asset_a11y_dark_css_len);
}

int asset_write_to_file_mmdoc_css(FILE *file) {
  return asset_write_to_file(file, ___src_asset_mmdoc_css,
                             ___src_asset_mmdoc_css_len);
}
int asset_write_to_dir_mmdoc_css(char *dir, const char *file_name) {
  return asset_write_to_dir(dir, file_name, ___src_asset_mmdoc_css,
                            ___src_asset_mmdoc_css_len);
}

int asset_write_to_file_mmdoc_js(FILE *file) {
  return asset_write_to_file(file, ___src_asset_mmdoc_js,
                             ___src_asset_mmdoc_js_len);
}
int asset_write_to_dir_mmdoc_js(char *dir, const char *file_name) {
  return asset_write_to_dir(dir, file_name, ___src_asset_mmdoc_js,
                            ___src_asset_mmdoc_js_len);
}

int asset_write_to_dir_mmdoc_search_js(char *dir, const char *file_name) {
  return asset_write_to_dir(dir, file_name, ___src_asset_mmdoc_search_js,
                            ___src_asset_mmdoc_search_js_len);
}
