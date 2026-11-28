/* SPDX-License-Identifier: CC0-1.0 */
#include "asset.h"

#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int errno;

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

uint64_t asset_hash_init(void) { return UINT64_C(14695981039346656037); }

uint64_t asset_hash_feed(uint64_t hash, const unsigned char *bytes,
                         size_t length) {
  for (size_t i = 0; i < length; i++) {
    hash ^= bytes[i];
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

void asset_format_hashed_name(const char *stem, const char *extension,
                              uint64_t hash, char *file_name,
                              size_t file_name_size) {
  snprintf(file_name, file_name_size, "%s.%016" PRIx64 ".%s", stem, hash,
           extension);
}

static uint64_t asset_hash(const unsigned char *bytes, size_t length) {
  AssetHashState state;
  asset_hash_state_init(&state);
  asset_hash_state_feed(&state, bytes, length);
  return asset_hash_state_final(&state);
}

static void asset_hashed_file_name(const char *stem, const char *extension,
                                   const unsigned char *bytes, size_t length,
                                   char *file_name, size_t file_name_size) {
  asset_format_hashed_name(stem, extension, asset_hash(bytes, length),
                           file_name, file_name_size);
}

static pthread_once_t cached_asset_names_once = PTHREAD_ONCE_INIT;
static AssetFileNames cached_asset_names;

static void init_cached_asset_names(void) {
  asset_hashed_file_name("fuse.basic.min", "js", asset_fuse_basic_min_js,
                         sizeof(asset_fuse_basic_min_js),
                         cached_asset_names.fuse_basic_min_js,
                         sizeof(cached_asset_names.fuse_basic_min_js));
  asset_hashed_file_name("highlight.pack", "js", asset_highlight_pack_js,
                         sizeof(asset_highlight_pack_js),
                         cached_asset_names.highlight_pack_js,
                         sizeof(cached_asset_names.highlight_pack_js));
  asset_hashed_file_name("a11y-dark", "css", asset_a11y_dark_css,
                         sizeof(asset_a11y_dark_css),
                         cached_asset_names.a11y_dark_css,
                         sizeof(cached_asset_names.a11y_dark_css));
  asset_hashed_file_name("a11y-light", "css", asset_a11y_light_css,
                         sizeof(asset_a11y_light_css),
                         cached_asset_names.a11y_light_css,
                         sizeof(cached_asset_names.a11y_light_css));
  asset_hashed_file_name("mmdoc", "css", asset_mmdoc_css,
                         sizeof(asset_mmdoc_css), cached_asset_names.mmdoc_css,
                         sizeof(cached_asset_names.mmdoc_css));
  asset_hashed_file_name("mmdoc", "js", asset_mmdoc_js, sizeof(asset_mmdoc_js),
                         cached_asset_names.mmdoc_js,
                         sizeof(cached_asset_names.mmdoc_js));
  asset_hashed_file_name("mmdoc_search", "js", asset_mmdoc_search_js,
                         sizeof(asset_mmdoc_search_js),
                         cached_asset_names.mmdoc_search_js,
                         sizeof(cached_asset_names.mmdoc_search_js));
}

void asset_file_names(AssetFileNames *names) {
  pthread_once(&cached_asset_names_once, init_cached_asset_names);
  *names = cached_asset_names;
}

int asset_hashed_file_name_from_path(const char *path, const char *stem,
                                     const char *extension, char *file_name,
                                     size_t file_name_size) {
  FILE *file = fopen(path, "rb");
  if (file == nullptr)
    return -1;

  AssetHashState state;
  asset_hash_state_init(&state);
  unsigned char buffer[64 * 1024];
  size_t bytes_read;
  while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0)
    asset_hash_state_feed(&state, buffer, bytes_read);
  bool read_failed = ferror(file) != 0;
  bool close_failed = fclose(file) != 0;
  if (read_failed || close_failed)
    return -1;

  asset_format_hashed_name(stem, extension, asset_hash_state_final(&state),
                           file_name, file_name_size);
  return 0;
}

static int asset_write_to_file(FILE *file, const unsigned char *asset_array,
                               size_t asset_array_length) {
  if (fwrite(asset_array, 1, asset_array_length, file) != asset_array_length) {
    printf("Failed to write to file: %s\n", strerror(errno));
    return -1;
  }
  return 0;
}

static int asset_write_to_dir(char *dir, const char *asset_file_name,
                              const unsigned char *asset_array,
                              size_t asset_array_length) {
  char *asset_path = malloc(strlen(dir) + 1 + strlen(asset_file_name) + 1);
  if (asset_path == nullptr) {
    printf("Failed to allocate memory at %s line %d\n", __FILE__, __LINE__);
    return -1;
  }
  sprintf(asset_path, "%s/%s", dir, asset_file_name);
  FILE *asset_file = fopen(asset_path, "w");
  if (asset_file == nullptr) {
    printf("Failed to open file %s for writing: %s\n", asset_path,
           strerror(errno));
    free(asset_path);
    return -1;
  }
  if (asset_write_to_file(asset_file, asset_array, asset_array_length) != 0) {
    fclose(asset_file);
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
  return asset_write_to_file(file, asset_highlight_pack_js,
                             sizeof(asset_highlight_pack_js));
}
int asset_write_to_dir_highlight_pack_js(char *dir, const char *file_name) {
  return asset_write_to_dir(dir, file_name, asset_highlight_pack_js,
                            sizeof(asset_highlight_pack_js));
}

int asset_write_to_dir_fuse_basic_min_js(char *dir, const char *file_name) {
  return asset_write_to_dir(dir, file_name, asset_fuse_basic_min_js,
                            sizeof(asset_fuse_basic_min_js));
}
int asset_write_to_file_a11y_light_css(FILE *file) {
  return asset_write_to_file(file, asset_a11y_light_css,
                             sizeof(asset_a11y_light_css));
}
int asset_write_to_dir_a11y_light_css(char *dir, const char *file_name) {
  return asset_write_to_dir(dir, file_name, asset_a11y_light_css,
                            sizeof(asset_a11y_light_css));
}

int asset_write_to_file_a11y_dark_css(FILE *file) {
  return asset_write_to_file(file, asset_a11y_dark_css,
                             sizeof(asset_a11y_dark_css));
}
int asset_write_to_dir_a11y_dark_css(char *dir, const char *file_name) {
  return asset_write_to_dir(dir, file_name, asset_a11y_dark_css,
                            sizeof(asset_a11y_dark_css));
}

int asset_write_to_file_mmdoc_css(FILE *file) {
  return asset_write_to_file(file, asset_mmdoc_css, sizeof(asset_mmdoc_css));
}
int asset_write_to_dir_mmdoc_css(char *dir, const char *file_name) {
  return asset_write_to_dir(dir, file_name, asset_mmdoc_css,
                            sizeof(asset_mmdoc_css));
}

int asset_write_to_file_mmdoc_js(FILE *file) {
  return asset_write_to_file(file, asset_mmdoc_js, sizeof(asset_mmdoc_js));
}
int asset_write_to_dir_mmdoc_js(char *dir, const char *file_name) {
  return asset_write_to_dir(dir, file_name, asset_mmdoc_js,
                            sizeof(asset_mmdoc_js));
}

int asset_write_to_dir_mmdoc_search_js(char *dir, const char *file_name) {
  return asset_write_to_dir(dir, file_name, asset_mmdoc_search_js,
                            sizeof(asset_mmdoc_search_js));
}
