/* SPDX-License-Identifier: CC0-1.0 */
#include "files.h"
#include "mkdir_p.h"
#include "types.h"
#include <dirent.h>
#include <errno.h>
#include <stdckdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

char *mmdoc_file_path(const char *directory, const char *name) {
  size_t dlen = strlen(directory);
  size_t nlen = strlen(name);
  bool need_slash = dlen > 0 && directory[dlen - 1] != '/';
  char *path = malloc(dlen + (size_t)need_slash + nlen + 1);
  if (path == nullptr)
    return nullptr;
  memcpy(path, directory, dlen);
  if (need_slash)
    path[dlen] = '/';
  memcpy(path + dlen + (size_t)need_slash, name, nlen + 1);
  return path;
}

static const char *const markdown_extensions[] = {".md", nullptr};
static const char *const image_extensions[] = {
    ".svg", ".jpeg", ".jpg", ".webp", ".png", ".gif", ".bmp", nullptr};

static bool has_extension(const char *name, const char *const extensions[]) {
  const char *ext = strrchr(name, '.');
  if (ext == nullptr)
    return false;
  for (size_t i = 0; extensions[i] != nullptr; i++)
    if (strcmp(ext, extensions[i]) == 0)
      return true;
  return false;
}

typedef struct {
  dev_t dev;
  ino_t ino;
} DirId;

typedef struct {
  DirId *items;
  size_t used;
  size_t cap;
} DirIdStack;

static bool stack_contains(const DirIdStack *s, dev_t dev, ino_t ino) {
  for (size_t i = 0; i < s->used; i++)
    if (s->items[i].dev == dev && s->items[i].ino == ino)
      return true;
  return false;
}

static int stack_push(DirIdStack *s, dev_t dev, ino_t ino) {
  if (s->used == s->cap) {
    size_t nc = 16;
    if (s->cap != 0 && ckd_mul(&nc, s->cap, 2))
      return -1;
    size_t bytes = 0;
    if (ckd_mul(&bytes, nc, sizeof(*s->items)))
      return -1;
    DirId *next = realloc(s->items, bytes);
    if (next == nullptr)
      return -1;
    s->items = next;
    s->cap = nc;
  }
  s->items[s->used].dev = dev;
  s->items[s->used].ino = ino;
  s->used++;
  return 0;
}

static int compare_strings(const void *a, const void *b) {
  const char *const *sa = a;
  const char *const *sb = b;
  return strcmp(*sa, *sb);
}

static void record_file(const char *path, const char *name, Array *md_files,
                        Array *img_files) {
  if (md_files != nullptr && has_extension(name, markdown_extensions))
    insert_array(md_files, (char *)path);
  else if (img_files != nullptr && has_extension(name, image_extensions))
    insert_array(img_files, (char *)path);
}

static int walk_directory(const char *path, Array *md_files, Array *img_files,
                          const struct stat excluded[], size_t excluded_count,
                          DirIdStack *ancestors) {
  DIR *dir = opendir(path);
  if (dir == nullptr) {
    if (errno == ENOTDIR) {
      const char *slash = strrchr(path, '/');
      record_file(path, slash != nullptr ? slash + 1 : path, md_files,
                  img_files);
      return 0;
    }
    fprintf(stderr, "Failed to open directory %s: %s\n", path, strerror(errno));
    return -1;
  }

  struct stat current;
  bool pushed = false;
  if (fstat(dirfd(dir), &current) == 0) {
    for (size_t i = 0; i < excluded_count; i++) {
      if (current.st_dev == excluded[i].st_dev &&
          current.st_ino == excluded[i].st_ino) {
        closedir(dir);
        return 0;
      }
    }
    if (stack_contains(ancestors, current.st_dev, current.st_ino)) {
      closedir(dir);
      return 0;
    }
    if (stack_push(ancestors, current.st_dev, current.st_ino) != 0) {
      closedir(dir);
      return -1;
    }
    pushed = true;
  }

  struct dirent *entry;
  bool failed = false;
  while ((entry = readdir(dir)) != nullptr) {
    const char *name = entry->d_name;
    if (name[0] == '.' &&
        (name[1] == '\0' || (name[1] == '.' && name[2] == '\0')))
      continue;

    bool is_dir = false;
    bool is_reg = false;
#ifdef _DIRENT_HAVE_D_TYPE
    if (entry->d_type == DT_REG) {
      is_reg = true;
    } else if (entry->d_type == DT_DIR) {
      is_dir = true;
    }
#endif
    if (is_reg) {
      if ((md_files != nullptr && has_extension(name, markdown_extensions)) ||
          (img_files != nullptr && has_extension(name, image_extensions))) {
        char *child = mmdoc_file_path(path, name);
        if (child == nullptr) {
          failed = true;
          break;
        }
        record_file(child, name, md_files, img_files);
        free(child);
      }
      continue;
    }

    char *child = mmdoc_file_path(path, name);
    if (child == nullptr) {
      failed = true;
      break;
    }
    if (is_dir) {
      failed = walk_directory(child, md_files, img_files, excluded,
                              excluded_count, ancestors) != 0;
    } else {
      struct stat st;
      if (stat(child, &st) == 0) {
        if (S_ISDIR(st.st_mode))
          failed = walk_directory(child, md_files, img_files, excluded,
                                  excluded_count, ancestors) != 0;
        else if (S_ISREG(st.st_mode))
          record_file(child, name, md_files, img_files);
      }
    }
    free(child);
    if (failed)
      break;
  }

  if (pushed)
    ancestors->used--;
  closedir(dir);
  return failed ? -1 : 0;
}

/* A build below SRC must not read its own chapters or copy its own images. */
int mmdoc_collect_source_files(Array *md_files, Array *img_files,
                               Inputs inputs) {
  struct stat excluded[2];
  size_t count = 0;
  if (inputs.out_multi != nullptr &&
      stat(inputs.out_multi, &excluded[count]) == 0)
    count++;
  if (inputs.out_single != nullptr &&
      stat(inputs.out_single, &excluded[count]) == 0)
    count++;

  DirIdStack ancestors = {};
  int result = walk_directory(inputs.src, md_files, img_files, excluded, count,
                              &ancestors);
  free(ancestors.items);

  if (result == 0) {
    if (md_files != nullptr && md_files->used > 1)
      qsort(md_files->array, md_files->used, sizeof(char *), compare_strings);
    if (img_files != nullptr && img_files->used > 1)
      qsort(img_files->array, img_files->used, sizeof(char *), compare_strings);
  }
  return result;
}

int mmdoc_copy_img_files(const Array *img_files, Inputs inputs) {
  size_t src_len = strlen(inputs.src);
  size_t out_multi_len = strlen(inputs.out_multi);
  size_t out_single_len = strlen(inputs.out_single);
  char *last_dir = nullptr;
  size_t last_dir_len = 0;
  unsigned char buffer[64 * 1024];

  for (size_t i = 0; i < img_files->used; i++) {
    const char *source_path = img_files->array[i];
    FILE *source = fopen(source_path, "rb");
    if (source == nullptr) {
      printf("Failed to open file %s for reading: %s\n", source_path,
             strerror(errno));
      free(last_dir);
      return -1;
    }

    const char *rel_path = source_path + src_len;
    size_t rel_len = strlen(rel_path);
    bool need_slash = rel_len > 0 && rel_path[0] != '/';
    char *multi_path = malloc(out_multi_len + (size_t)need_slash + rel_len + 1);
    char *single_path =
        malloc(out_single_len + (size_t)need_slash + rel_len + 1);
    if (multi_path == nullptr || single_path == nullptr) {
      printf("Failed to allocate memory at %s line %d\n", __FILE__, __LINE__);
      free(single_path);
      free(multi_path);
      fclose(source);
      free(last_dir);
      return -1;
    }
    sprintf(multi_path, "%s%s%s", inputs.out_multi, need_slash ? "/" : "",
            rel_path);
    sprintf(single_path, "%s%s%s", inputs.out_single, need_slash ? "/" : "",
            rel_path);

    char *multi_file_name = strrchr(multi_path, '/');
    char *single_file_name = strrchr(single_path, '/');
    if (multi_file_name == nullptr || single_file_name == nullptr) {
      printf("Failed to determine image output directories\n");
      free(single_path);
      free(multi_path);
      fclose(source);
      free(last_dir);
      return -1;
    }

    size_t dir_len = (size_t)(multi_file_name - multi_path);
    if (last_dir == nullptr || dir_len != last_dir_len ||
        memcmp(last_dir, multi_path, dir_len) != 0) {
      *multi_file_name = '\0';
      *single_file_name = '\0';
      bool mkdir_failed = mkdir_p(multi_path) != 0 || mkdir_p(single_path) != 0;
      *multi_file_name = '/';
      *single_file_name = '/';
      if (mkdir_failed) {
        printf("Failed to create image output directories: %s\n",
               strerror(errno));
        free(single_path);
        free(multi_path);
        fclose(source);
        free(last_dir);
        return -1;
      }
      char *next_last = realloc(last_dir, dir_len + 1);
      if (next_last != nullptr) {
        memcpy(next_last, multi_path, dir_len);
        next_last[dir_len] = '\0';
        last_dir = next_last;
        last_dir_len = dir_len;
      }
    }

    FILE *multi = fopen(multi_path, "wb");
    if (multi == nullptr) {
      printf("Failed to open file %s for writing: %s\n", multi_path,
             strerror(errno));
      free(single_path);
      free(multi_path);
      fclose(source);
      free(last_dir);
      return -1;
    }

    FILE *single = fopen(single_path, "wb");
    if (single == nullptr) {
      printf("Failed to open file %s for writing: %s\n", single_path,
             strerror(errno));
      fclose(multi);
      fclose(source);
      free(single_path);
      free(multi_path);
      free(last_dir);
      return -1;
    }

    size_t bytes_read;
    bool io_failed = false;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), source)) > 0) {
      if (fwrite(buffer, 1, bytes_read, multi) != bytes_read ||
          fwrite(buffer, 1, bytes_read, single) != bytes_read) {
        io_failed = true;
        break;
      }
    }
    if (!io_failed && ferror(source)) {
      printf("Failed to read file %s: %s\n", source_path, strerror(errno));
      io_failed = true;
    }

    if (fclose(single) != 0)
      io_failed = true;
    if (fclose(multi) != 0)
      io_failed = true;
    if (fclose(source) != 0)
      io_failed = true;
    free(single_path);
    free(multi_path);
    if (io_failed) {
      free(last_dir);
      return -1;
    }
  }
  free(last_dir);
  return 0;
}
