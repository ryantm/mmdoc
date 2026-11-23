/* SPDX-License-Identifier: CC0-1.0 */
#include "files.h"
#include "mkdir_p.h"
#include "types.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

char *mmdoc_file_path(const char *directory, const char *name) {
  char *path = malloc(strlen(directory) + strlen(name) + 2);
  if (path == NULL)
    return NULL;
  sprintf(path, "%s/%s", directory, name);
  return path;
}

static const char *const markdown_extensions[] = {".md", NULL};
static const char *const image_extensions[] = {".svg", ".jpeg", ".jpg", ".webp",
                                               ".png", ".gif",  ".bmp", NULL};

static int collect_files(Array *files, char *path,
                         const char *const extensions[],
                         const struct stat excluded[], size_t excluded_count) {
  DIR *dir = opendir(path);
  if (dir == NULL) {
    const char *ext = strrchr(path, '.');
    for (size_t i = 0; ext != NULL && extensions[i] != NULL; i++)
      if (strcmp(ext, extensions[i]) == 0) {
        insert_array(files, path);
        break;
      }
    return 0;
  }

  struct stat current;
  if (fstat(dirfd(dir), &current) == 0)
    for (size_t i = 0; i < excluded_count; i++)
      if (current.st_dev == excluded[i].st_dev &&
          current.st_ino == excluded[i].st_ino) {
        closedir(dir);
        return 0;
      }

  struct dirent *entry;
  int failed = 0;
  while ((entry = readdir(dir)) != NULL) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
      continue;
    char *child = mmdoc_file_path(path, entry->d_name);
    failed = child == NULL || collect_files(files, child, extensions, excluded,
                                            excluded_count) != 0;
    free(child);
    if (failed)
      break;
  }
  closedir(dir);
  return failed ? -1 : 0;
}

/* A build below SRC must not read its own chapters or copy its own images. */
static int source_files(Array *files, Inputs inputs,
                        const char *const extensions[]) {
  struct stat excluded[2];
  size_t count = 0;
  if (stat(inputs.out_multi, &excluded[count]) == 0)
    count++;
  if (stat(inputs.out_single, &excluded[count]) == 0)
    count++;
  return collect_files(files, inputs.src, extensions, excluded, count);
}

int mmdoc_md_files(Array *files, Inputs inputs) {
  return source_files(files, inputs, markdown_extensions);
}

int copy_imgs(Inputs inputs) {
  Array img_files;
  init_array(&img_files, 100);
  if (source_files(&img_files, inputs, image_extensions) != 0) {
    free_array(&img_files);
    return -1;
  }

  for (int i = 0; i < img_files.used; i++) {
    char *source_path = img_files.array[i];
    FILE *source = fopen(source_path, "rb");

    if (NULL == source) {
      printf("Failed to open file %s for reading: %s\n", source_path,
             strerror(errno));
      free_array(&img_files);
      return -1;
    }

    char *rel_path = source_path + strlen(inputs.src);
    char *multi_path = malloc(strlen(inputs.out_multi) + strlen(rel_path) + 1);
    if (NULL == multi_path) {
      printf("Failed to allocate memory at %s line %d\n", __FILE__, __LINE__);
      fclose(source);
      free_array(&img_files);
      return -1;
    }
    sprintf(multi_path, "%s%s", inputs.out_multi, rel_path);
    char *single_path =
        malloc(strlen(inputs.out_single) + strlen(rel_path) + 1);
    if (NULL == single_path) {
      printf("Failed to allocate memory at %s line %d\n", __FILE__, __LINE__);
      free(multi_path);
      fclose(source);
      free_array(&img_files);
      return -1;
    }

    sprintf(single_path, "%s%s", inputs.out_single, rel_path);

    char *multi_file_name = strrchr(multi_path, '/');
    char *single_file_name = strrchr(single_path, '/');
    if (multi_file_name == NULL || single_file_name == NULL) {
      printf("Failed to determine image output directories\n");
      free(single_path);
      free(multi_path);
      fclose(source);
      free_array(&img_files);
      return -1;
    }
    *multi_file_name = '\0';
    *single_file_name = '\0';
    int mkdir_failed = mkdir_p(multi_path) != 0 || mkdir_p(single_path) != 0;
    *multi_file_name = '/';
    *single_file_name = '/';
    if (mkdir_failed) {
      printf("Failed to create image output directories: %s\n",
             strerror(errno));
      free(single_path);
      free(multi_path);
      fclose(source);
      free_array(&img_files);
      return -1;
    }

    FILE *multi = fopen(multi_path, "wb");
    if (multi == NULL) {
      printf("Failed to open file %s for writing: %s\n", multi_path,
             strerror(errno));
      free(single_path);
      free(multi_path);
      fclose(source);
      free_array(&img_files);
      return -1;
    }

    FILE *single = fopen(single_path, "wb");
    if (single == NULL) {
      printf("Failed to open file %s for writing: %s\n", single_path,
             strerror(errno));
      fclose(multi);
      fclose(source);
      free(single_path);
      free(multi_path);
      free_array(&img_files);
      return -1;
    }

    unsigned char buffer[64 * 1024];
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), source)) > 0) {
      if (fwrite(buffer, 1, bytes_read, multi) != bytes_read) {
        fclose(single);
        fclose(multi);
        fclose(source);
        free(single_path);
        free(multi_path);
        free_array(&img_files);
        return -1;
      }
      if (fwrite(buffer, 1, bytes_read, single) != bytes_read) {
        fclose(single);
        fclose(multi);
        fclose(source);
        free(single_path);
        free(multi_path);
        free_array(&img_files);
        return -1;
      }
    }

    if (ferror(source)) {
      printf("Failed to read file %s: %s\n", source_path, strerror(errno));
      fclose(single);
      fclose(multi);
      fclose(source);
      free(single_path);
      free(multi_path);
      free_array(&img_files);
      return -1;
    }

    int failed = fclose(single) != 0;
    if (fclose(multi) != 0)
      failed = 1;
    if (fclose(source) != 0)
      failed = 1;
    free(single_path);
    free(multi_path);
    if (failed) {
      free_array(&img_files);
      return -1;
    }
  }
  free_array(&img_files);
  return 0;
}
