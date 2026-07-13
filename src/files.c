/* SPDX-License-Identifier: CC0-1.0 */
#include "inputs.h"
#include "mkdir_p.h"
#include "types.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int ends_with(const char *str, size_t str_len, const char *suffix,
              size_t suffix_len) {
  return (str_len >= suffix_len) &&
         (!memcmp(str + str_len - suffix_len, suffix, suffix_len));
}

void mmdoc_md_files(Array *md_files, char *base_path) {
  struct dirent *dp;
  DIR *dir = opendir(base_path);

  if (!dir)
    return;

  while ((dp = readdir(dir)) != NULL) {
    if (strcmp(dp->d_name, ".") != 0 && strcmp(dp->d_name, "..") != 0) {
      size_t size = strlen(base_path) + 1 + strlen(dp->d_name) + 1;
      char *path = malloc(size);
      strcpy(path, base_path);
      strcat(path, "/");
      strcat(path, dp->d_name);
      if (ends_with(dp->d_name, strlen(dp->d_name), ".md", strlen(".md")))
        insert_array(md_files, path);
      mmdoc_md_files(md_files, path);
      free(path);
    }
  }
  closedir(dir);
  return;
}

void mmdoc_img_files(Array *img_files, char *base_path) {
  struct dirent *dp;
  DIR *dir = opendir(base_path);

  if (!dir)
    return;

  while ((dp = readdir(dir)) != NULL) {
    if (strcmp(dp->d_name, ".") != 0 && strcmp(dp->d_name, "..") != 0) {
      size_t size = strlen(base_path) + 1 + strlen(dp->d_name) + 1;
      char *path = malloc(size);
      strcpy(path, base_path);
      strcat(path, "/");
      strcat(path, dp->d_name);
      if (ends_with(dp->d_name, strlen(dp->d_name), ".svg", strlen(".svg")) ||
          ends_with(dp->d_name, strlen(dp->d_name), ".jpeg", strlen(".jpeg")) ||
          ends_with(dp->d_name, strlen(dp->d_name), ".jpg", strlen(".jpg")) ||
          ends_with(dp->d_name, strlen(dp->d_name), ".webp", strlen(".webp")) ||
          ends_with(dp->d_name, strlen(dp->d_name), ".png", strlen(".png")) ||
          ends_with(dp->d_name, strlen(dp->d_name), ".gif", strlen(".gif")) ||
          ends_with(dp->d_name, strlen(dp->d_name), ".bmp", strlen(".bmp")))
        insert_array(img_files, path);
      mmdoc_img_files(img_files, path);
      free(path);
    }
  }
  closedir(dir);
  return;
}

int copy_imgs(Inputs inputs) {
  Array img_files;
  init_array(&img_files, 100);
  mmdoc_img_files(&img_files, inputs.src);

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

    fclose(single);
    fclose(multi);
    fclose(source);
    free(single_path);
    free(multi_path);
  }
  free_array(&img_files);
  return 0;
}
