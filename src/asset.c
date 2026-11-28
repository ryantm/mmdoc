/* SPDX-License-Identifier: CC0-1.0 */
#include "asset.h"
#include "files.h"

#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const unsigned char asset_highlight_pack_js[] = {
#embed "asset/highlight.pack.js"
};
static const unsigned char asset_fuse_basic_min_js[] = {
#embed "asset/fuse.basic.min.js"
};
static const unsigned char asset_a11y_light_css[] = {
#embed "asset/a11y-light.css"
};
static const unsigned char asset_a11y_dark_css[] = {
#embed "asset/a11y-dark.css"
};
static const unsigned char asset_mmdoc_css[] = {
#embed "asset/mmdoc.css"
};
static const unsigned char asset_mmdoc_js[] = {
#embed "asset/mmdoc.js"
};
static const unsigned char asset_mmdoc_search_js[] = {
#embed "asset/mmdoc_search.js"
};

typedef struct {
  const char *stem;
  const char *ext;
  const unsigned char *data;
  size_t len;
} AssetDesc;

static const AssetDesc asset_table[ASSET_COUNT] = {
    [ASSET_FUSE_BASIC_MIN_JS] = {"fuse.basic.min", "js",
                                 asset_fuse_basic_min_js,
                                 sizeof(asset_fuse_basic_min_js)},
    [ASSET_HIGHLIGHT_PACK_JS] = {"highlight.pack", "js",
                                 asset_highlight_pack_js,
                                 sizeof(asset_highlight_pack_js)},
    [ASSET_A11Y_DARK_CSS] = {"a11y-dark", "css", asset_a11y_dark_css,
                             sizeof(asset_a11y_dark_css)},
    [ASSET_A11Y_LIGHT_CSS] = {"a11y-light", "css", asset_a11y_light_css,
                              sizeof(asset_a11y_light_css)},
    [ASSET_MMDOC_CSS] = {"mmdoc", "css", asset_mmdoc_css,
                         sizeof(asset_mmdoc_css)},
    [ASSET_MMDOC_JS] = {"mmdoc", "js", asset_mmdoc_js, sizeof(asset_mmdoc_js)},
    [ASSET_MMDOC_SEARCH_JS] = {"mmdoc_search", "js", asset_mmdoc_search_js,
                               sizeof(asset_mmdoc_search_js)},
};

/* Fast 64-bit XXH64 streaming hash (4-lane 32 bytes/iter). */
static constexpr uint64_t XXH_PRIME1 = UINT64_C(0x9E3779B185EBCA87);
static constexpr uint64_t XXH_PRIME2 = UINT64_C(0xC2B2AE3D27D4EB4F);
static constexpr uint64_t XXH_PRIME3 = UINT64_C(0x165667B19E3779F9);
static constexpr uint64_t XXH_PRIME4 = UINT64_C(0x85EBCA77C2B2AE63);
static constexpr uint64_t XXH_PRIME5 = UINT64_C(0x27D4EB2F165667C5);

static inline uint64_t xxh_rotl64(uint64_t x, int r) {
  return (x << r) | (x >> (64 - r));
}

static inline uint64_t xxh_read64(const unsigned char *p) {
  uint64_t v;
  memcpy(&v, p, sizeof(v));
  return v;
}

static inline uint32_t xxh_read32(const unsigned char *p) {
  uint32_t v;
  memcpy(&v, p, sizeof(v));
  return v;
}

static inline uint64_t xxh_round(uint64_t acc, uint64_t input) {
  acc += input * XXH_PRIME2;
  acc = xxh_rotl64(acc, 31);
  acc *= XXH_PRIME1;
  return acc;
}

static inline uint64_t xxh_merge_round(uint64_t acc, uint64_t val) {
  val = xxh_round(0, val);
  acc ^= val;
  acc = acc * XXH_PRIME1 + XXH_PRIME4;
  return acc;
}

void asset_hash_state_init(AssetHashState *state) {
  state->total_len = 0;
  state->v1 = XXH_PRIME1 + XXH_PRIME2;
  state->v2 = XXH_PRIME2;
  state->v3 = 0;
  state->v4 = (uint64_t)(0 - XXH_PRIME1);
  state->buf_len = 0;
}

void asset_hash_state_feed(AssetHashState *state, const unsigned char *bytes,
                           size_t length) {
  if (length == 0)
    return;
  state->total_len += length;
  const unsigned char *p = bytes;
  const unsigned char *end = bytes + length;

  if (state->buf_len + length < 32) {
    memcpy(state->buf + state->buf_len, bytes, length);
    state->buf_len += length;
    return;
  }

  if (state->buf_len > 0) {
    size_t fill = 32 - state->buf_len;
    memcpy(state->buf + state->buf_len, p, fill);
    state->v1 = xxh_round(state->v1, xxh_read64(state->buf));
    state->v2 = xxh_round(state->v2, xxh_read64(state->buf + 8));
    state->v3 = xxh_round(state->v3, xxh_read64(state->buf + 16));
    state->v4 = xxh_round(state->v4, xxh_read64(state->buf + 24));
    p += fill;
    state->buf_len = 0;
  }

  if (p + 32 <= end) {
    const unsigned char *limit = end - 32;
    uint64_t v1 = state->v1;
    uint64_t v2 = state->v2;
    uint64_t v3 = state->v3;
    uint64_t v4 = state->v4;
    do {
      v1 = xxh_round(v1, xxh_read64(p));
      v2 = xxh_round(v2, xxh_read64(p + 8));
      v3 = xxh_round(v3, xxh_read64(p + 16));
      v4 = xxh_round(v4, xxh_read64(p + 24));
      p += 32;
    } while (p <= limit);
    state->v1 = v1;
    state->v2 = v2;
    state->v3 = v3;
    state->v4 = v4;
  }

  if (p < end) {
    state->buf_len = (size_t)(end - p);
    memcpy(state->buf, p, state->buf_len);
  }
}

uint64_t asset_hash_state_final(const AssetHashState *state) {
  uint64_t h64;
  if (state->total_len >= 32) {
    h64 = xxh_rotl64(state->v1, 1) + xxh_rotl64(state->v2, 7) +
          xxh_rotl64(state->v3, 12) + xxh_rotl64(state->v4, 18);
    h64 = xxh_merge_round(h64, state->v1);
    h64 = xxh_merge_round(h64, state->v2);
    h64 = xxh_merge_round(h64, state->v3);
    h64 = xxh_merge_round(h64, state->v4);
  } else {
    h64 = XXH_PRIME5;
  }
  h64 += state->total_len;

  const unsigned char *p = state->buf;
  const unsigned char *end = state->buf + state->buf_len;
  while (p + 8 <= end) {
    uint64_t k1 = xxh_round(0, xxh_read64(p));
    h64 ^= k1;
    h64 = xxh_rotl64(h64, 27) * XXH_PRIME1 + XXH_PRIME4;
    p += 8;
  }
  if (p + 4 <= end) {
    h64 ^= (uint64_t)xxh_read32(p) * XXH_PRIME1;
    h64 = xxh_rotl64(h64, 23) * XXH_PRIME2 + XXH_PRIME3;
    p += 4;
  }
  while (p < end) {
    h64 ^= (*p) * XXH_PRIME5;
    h64 = xxh_rotl64(h64, 11) * XXH_PRIME1;
    p++;
  }

  h64 ^= h64 >> 33;
  h64 *= XXH_PRIME2;
  h64 ^= h64 >> 29;
  h64 *= XXH_PRIME3;
  h64 ^= h64 >> 32;
  return h64;
}

void asset_format_hashed_name(const char *stem, const char *extension,
                              uint64_t hash, char *file_name,
                              size_t file_name_size) {
  snprintf(file_name, file_name_size, "%s.%016" PRIx64 ".%s", stem, hash,
           extension);
}

static pthread_once_t cached_asset_names_once = PTHREAD_ONCE_INIT;
static char cached_asset_names[ASSET_COUNT][ASSET_FILE_NAME_SIZE];

static void init_cached_asset_names(void) {
  for (size_t i = 0; i < ASSET_COUNT; i++) {
    AssetHashState state;
    asset_hash_state_init(&state);
    asset_hash_state_feed(&state, asset_table[i].data, asset_table[i].len);
    asset_format_hashed_name(asset_table[i].stem, asset_table[i].ext,
                             asset_hash_state_final(&state),
                             cached_asset_names[i], ASSET_FILE_NAME_SIZE);
  }
}

const char *asset_file_name(AssetId id) {
  pthread_once(&cached_asset_names_once, init_cached_asset_names);
  return cached_asset_names[id];
}

int asset_write_to_file(FILE *file, AssetId id) {
  const AssetDesc *a = &asset_table[id];
  if (fwrite(a->data, 1, a->len, file) != a->len) {
    printf("Failed to write to file: %s\n", strerror(errno));
    return -1;
  }
  return 0;
}

int asset_write_to_dir(const char *dir, AssetId id) {
  char *asset_path = mmdoc_file_path(dir, asset_file_name(id));
  if (asset_path == nullptr) {
    printf("Failed to allocate memory at %s line %d\n", __FILE__, __LINE__);
    return -1;
  }
  FILE *asset_file = fopen(asset_path, "wb");
  if (asset_file == nullptr) {
    printf("Failed to open file %s for writing: %s\n", asset_path,
           strerror(errno));
    free(asset_path);
    return -1;
  }
  bool failed = asset_write_to_file(asset_file, id) != 0;
  if (failed)
    printf("Failed to write to file %s: %s\n", asset_path, strerror(errno));
  if (fclose(asset_file) != 0) {
    printf("Failed to close file %s: %s\n", asset_path, strerror(errno));
    failed = true;
  }
  free(asset_path);
  return failed ? -1 : 0;
}
