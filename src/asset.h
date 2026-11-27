/* SPDX-License-Identifier: CC0-1.0 */
#pragma once

#include <stdint.h>
#include <stdio.h>

#define ASSET_FILE_NAME_SIZE 96

typedef struct {
  char fuse_basic_min_js[ASSET_FILE_NAME_SIZE];
  char highlight_pack_js[ASSET_FILE_NAME_SIZE];
  char a11y_dark_css[ASSET_FILE_NAME_SIZE];
  char a11y_light_css[ASSET_FILE_NAME_SIZE];
  char mmdoc_css[ASSET_FILE_NAME_SIZE];
  char mmdoc_js[ASSET_FILE_NAME_SIZE];
  char mmdoc_search_js[ASSET_FILE_NAME_SIZE];
} AssetFileNames;

typedef struct {
  uint64_t total_len;
  uint64_t v1;
  uint64_t v2;
  uint64_t v3;
  uint64_t v4;
  unsigned char buf[32];
  size_t buf_len;
} AssetHashState;

void asset_file_names(AssetFileNames *names);
void asset_hash_state_init(AssetHashState *state);
void asset_hash_state_feed(AssetHashState *state, const unsigned char *bytes,
                           size_t length);
uint64_t asset_hash_state_final(const AssetHashState *state);
uint64_t asset_hash_init(void);
uint64_t asset_hash_feed(uint64_t hash, const unsigned char *bytes,
                         size_t length);
void asset_format_hashed_name(const char *stem, const char *extension,
                              uint64_t hash, char *file_name,
                              size_t file_name_size);
int asset_hashed_file_name_from_path(const char *path, const char *stem,
                                     const char *extension, char *file_name,
                                     size_t file_name_size);

int asset_write_to_dir_fuse_basic_min_js(char *dir, const char *file_name);
int asset_write_to_dir_highlight_pack_js(char *dir, const char *file_name);
int asset_write_to_dir_a11y_dark_css(char *dir, const char *file_name);
int asset_write_to_dir_a11y_light_css(char *dir, const char *file_name);
int asset_write_to_dir_mmdoc_css(char *dir, const char *file_name);
int asset_write_to_dir_mmdoc_js(char *dir, const char *file_name);
int asset_write_to_dir_mmdoc_search_js(char *dir, const char *file_name);

int asset_write_to_file_a11y_dark_css(FILE *file);
int asset_write_to_file_a11y_light_css(FILE *file);
int asset_write_to_file_highlight_pack_js(FILE *file);
int asset_write_to_file_mmdoc_css(FILE *file);
int asset_write_to_file_mmdoc_js(FILE *file);
