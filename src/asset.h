/* SPDX-License-Identifier: CC0-1.0 */
#pragma once

#include <stdint.h>
#include <stdio.h>

static constexpr size_t ASSET_FILE_NAME_SIZE = 96;

typedef enum : uint8_t {
  ASSET_FUSE_BASIC_MIN_JS = 0,
  ASSET_HIGHLIGHT_PACK_JS,
  ASSET_A11Y_DARK_CSS,
  ASSET_A11Y_LIGHT_CSS,
  ASSET_MMDOC_CSS,
  ASSET_MMDOC_JS,
  ASSET_MMDOC_SEARCH_JS,
  ASSET_COUNT,
} AssetId;

typedef struct {
  uint64_t total_len;
  uint64_t v1;
  uint64_t v2;
  uint64_t v3;
  uint64_t v4;
  unsigned char buf[32];
  size_t buf_len;
} AssetHashState;

const char *asset_file_name(AssetId id);
void asset_hash_state_init(AssetHashState *state);
void asset_hash_state_feed(AssetHashState *state, const unsigned char *bytes,
                           size_t length);
uint64_t asset_hash_state_final(const AssetHashState *state);
void asset_format_hashed_name(const char *stem, const char *extension,
                              uint64_t hash, char *file_name,
                              size_t file_name_size);

[[nodiscard]] int asset_write_to_dir(const char *dir, AssetId id);
[[nodiscard]] int asset_write_to_file(FILE *file, AssetId id);
