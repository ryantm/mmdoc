/* SPDX-License-Identifier: CC0-1.0 */
#include "types.h"
#include "inputs.h"
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

  init_array(md_files, 100);

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
    int ch;
    char *source_path = img_files.array[i];
    FILE *source = fopen(source_path, "r");

    if (NULL == source) {
      printf("Failed to open file %s for reading: %s\n", source_path,
             strerror(errno));
      free_array(&img_files);
      return -1;
    }

    char *rel_path = source_path + strlen(inputs.src);
    char *multi_path = malloc(strlen(inputs.out_multi) + 1 + strlen(rel_path) + 1);
    if (NULL == multi_path) {
      printf("Failed to allocate memory at %s line %d\n", __FILE__, __LINE__);
      fclose(source);
      free_array(&img_files);
      return -1;
    }
    sprintf(multi_path, "%s/%s", inputs.out_multi, rel_path);
    char *single_path = malloc(strlen(inputs.out_single) + 1 + strlen(rel_path) + 1);
    if (NULL == single_path) {
      printf("Failed to allocate memory at %s line %d\n", __FILE__, __LINE__);
      free(multi_path);
      fclose(source);
      free_array(&img_files);
      return -1;
    }

    sprintf(single_path, "%s/%s", inputs.out_single, rel_path);

    FILE *multi = fopen(multi_path, "w");
    if (multi == NULL) {
      printf("Failed to open file %s for writing: %s\n", multi_path,
             strerror(errno));
      free(single_path);
      free(multi_path);
      fclose(source);
      free_array(&img_files);
      return -1;
    }
    free(multi_path);

    FILE *single = fopen(single_path, "w");
    if (single == NULL) {
      printf("Failed to open file %s for writing: %s\n", single_path,
             strerror(errno));
      fclose(multi);
      fclose(source);
      free_array(&img_files);
      return -1;
    }
    free(single_path);

    while ((ch = fgetc(source)) != EOF) {
      int ret;
      ret = fputc(ch, multi);
      if (ret != ch) {
        fclose(single);
        fclose(multi);
        fclose(source);
        free_array(&img_files);
        return -1;
      }
      ret = fputc(ch, single);
      if (ret != ch) {
        fclose(single);
        fclose(multi);
        fclose(source);
        free_array(&img_files);
        return -1;
      }
    }

    fclose(single);
    fclose(multi);
    fclose(source);
  }
  free_array(&img_files);
  return 0;
}
