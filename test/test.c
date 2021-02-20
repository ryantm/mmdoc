/* SPDX-License-Identifier: CC0-1.0 */
#include "../src/render.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <testconfig.h>

extern int errno;

int test_files_match(char *example_name, char *expected_file_path,
                     char *got_file_path) {
  char expected_line[4096];
  char got_line[4096];
  int ret_val = 0;
  int first_col_diff = -1;
  char expected;
  char got;

  FILE *got_file = fopen(got_file_path, "r");
  FILE *expected_file = fopen(expected_file_path, "r");

  for (int line = 0;; line++) {
    for (int col = 0;; col++) {
      expected = fgetc(expected_file);
      got = fgetc(got_file);
      if (got == '\n' && expected == '\n') {
        expected_line[col] = '\0';
        got_line[col] = '\0';
        break;
      }
      expected_line[col] = expected;
      got_line[col] = got;
      if (got == expected && got != EOF)
        continue;
      if (expected == EOF || got == EOF) {
        expected_line[col] = '\0';
        got_line[col] = '\0';
        break;
      }
      if (first_col_diff == -1)
        first_col_diff = col;
    }

    if (first_col_diff != -1) {
      printf("%s: %s differed from %s at %d:%d\n    expected: %s\n         "
             "got: %s\n",
             example_name, got_file_path, expected_file_path, line,
             first_col_diff, expected_line, got_line);
      for (int i = 0; i < first_col_diff + 14; i++)
        printf(" ");
      printf("^\n");
      first_col_diff = -1;
      ret_val = 1;
    }

    if (expected == EOF && got == EOF)
      break;
    if (expected == EOF) {
      printf("%s: %s is longer than expected %s\n", example_name, got_file_path,
             expected_file_path);
      ret_val = 1;
      break;
    }
    if (got == EOF) {
      printf("%s: %s is shorter than expected %s\n", example_name,
             got_file_path, expected_file_path);
      ret_val = 1;
      break;
    }
  }

  if (ret_val == 0)
    printf("%s passed\n", example_name);

  return ret_val;
}

int test_render(char *example) {
  char *example_dir = TEST_EXAMPLE_DIR;
  char *input_filename = "input.md";
  char *expected_filename = "expected.html";
  char *input_path = malloc(strlen(example_dir) + strlen(example) + 1 +
                            strlen(input_filename) + 1);
  char *expected_path = malloc(strlen(example_dir) + strlen(example) + 1 +
                               strlen(expected_filename) + 1);

  strcpy(input_path, example_dir);
  strcat(input_path, example);
  strcat(input_path, "/");
  strcat(input_path, input_filename);

  strcpy(expected_path, example_dir);
  strcat(expected_path, example);
  strcat(expected_path, "/");
  strcat(expected_path, expected_filename);

  char got_file_path[] = "/tmp/mmdocXXXXXX.html";
  int ret = mkstemps(got_file_path, 5);
  if (ret == -1) {
    perror("mkstemp failed");
    return 1;
  }

  FILE *got_file = fopen(got_file_path, "w");
  AnchorLocationArray empty_anchor_locations;
  AnchorLocation al;
  init_anchor_location_array(&empty_anchor_locations, 0);
  mmdoc_render_part(input_path, got_file, RENDER_TYPE_SINGLE, &al,
                    empty_anchor_locations, NULL, NULL);
  free_anchor_location_array(&empty_anchor_locations);
  fclose(got_file);

  return test_files_match(example, expected_path, got_file_path);
}

int test_multipage_render(char *example, AnchorLocationArray anchor_locations) {
  char *example_dir = TEST_EXAMPLE_DIR;
  char *input_filename = "input.md";
  char *expected_filename = "expected.html";
  char *input_path = malloc(strlen(example_dir) + strlen(example) + 1 +
                            strlen(input_filename) + 1);
  char *expected_path = malloc(strlen(example_dir) + strlen(example) + 1 +
                               strlen(expected_filename) + 1);

  strcpy(input_path, example_dir);
  strcat(input_path, example);
  strcat(input_path, "/");
  strcat(input_path, input_filename);

  strcpy(expected_path, example_dir);
  strcat(expected_path, example);
  strcat(expected_path, "/");
  strcat(expected_path, expected_filename);

  char got_file_path[] = "/tmp/mmdocXXXXXX.html";
  int ret = mkstemps(got_file_path, 5);
  if (ret == -1) {
    perror("mkstemp failed");
    return 1;
  }

  FILE *got_file = fopen(got_file_path, "w");
  AnchorLocation al;
  al.title = "page title";
  mmdoc_render_part(input_path, got_file, RENDER_TYPE_MULTIPAGE, &al,
                    anchor_locations, NULL, NULL);
  fclose(got_file);

  return test_files_match(example, expected_path, got_file_path);
}

int test_e007() {
  AnchorLocationArray anchor_locations;
  init_anchor_location_array(&anchor_locations, 1);
  AnchorLocation *al = malloc(sizeof *al);
  al->anchor = "#first_section";
  al->multipage_url = "url";
  al->multipage_output_directory_path = "output_path/";
  al->multipage_output_file_path = "output_path/index.html";
  insert_anchor_location_array(&anchor_locations, al);
  int retval = test_multipage_render("e007", anchor_locations);
  free_anchor_location_array(&anchor_locations);
  return retval;
}

int test_title(char *example, char *expected_title) {
  char *example_dir = TEST_EXAMPLE_DIR;
  char *input_filename = "input.md";
  char *input_path = malloc(strlen(example_dir) + strlen(example) + 1 +
                            strlen(input_filename) + 1);
  sprintf(input_path, "%s%s/%s", example_dir, example, input_filename);

  char *got_title = mmdoc_render_get_title_from_file(input_path);

  if (strcmp(got_title, expected_title) != 0) {
    printf("%s: title didn't match\n    expected: %s\n         "
           "got: %s\n",
           example, expected_title, got_title);
    free(input_path);
    return 1;
  }
  printf("%s title test passed\n", example);
  free(input_path);
  free(got_title);
  return 0;
}

int main(int argc, char *argv[]) {
  uint num_failed = 0;
  uint num_tests = 0;
  num_failed += test_render("e001");
  num_tests++;
  num_failed += test_title("e001", "Header");
  num_tests++;
  num_failed += test_render("e002");
  num_tests++;
  num_failed += test_render("e003");
  num_tests++;
  num_failed += test_render("e004");
  num_tests++;
  num_failed += test_render("e005");
  num_tests++;
  num_failed += test_render("e006");
  num_tests++;
  num_failed += test_e007();
  num_tests++;
  num_failed += test_render("e008");
  num_tests++;

  printf("%d of %d tests passed.", num_tests - num_failed, num_tests);
  if (num_failed > 0)
    return 1;
  return 0;
}
