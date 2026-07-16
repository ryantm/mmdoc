/* SPDX-License-Identifier: CC0-1.0 */
#include "../src/anchors.h"
#include "../src/files.h"
#include "../src/mkdir_p.h"
#include "../src/multi.h"
#include "../src/refs.h"
#include "../src/render.h"
#include "../src/single.h"
#include <errno.h>
#include <limits.h>
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
                    empty_anchor_locations, "/", NULL);
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
                    anchor_locations, "/", NULL);
  fclose(got_file);

  return test_files_match(example, expected_path, got_file_path);
}

int test_code_block_detection(char *example, int expected) {
  char input_path[PATH_MAX];
  snprintf(input_path, sizeof(input_path), "%s%s/input.md", TEST_EXAMPLE_DIR,
           example);
  FILE *output = tmpfile();
  if (output == NULL)
    return 1;

  AnchorLocationArray empty_anchor_locations;
  AnchorLocation anchor_location = {.title = "page title"};
  init_anchor_location_array(&empty_anchor_locations, 0);
  int got =
      mmdoc_render_part(input_path, output, RENDER_TYPE_SINGLE,
                        &anchor_location, empty_anchor_locations, "/", NULL);
  free_anchor_location_array(&empty_anchor_locations);
  fclose(output);

  if (got != expected) {
    printf("%s code block detection failed\n", example);
    return 1;
  }
  printf("%s code block detection passed\n", example);
  return 0;
}

int test_e007() {
  AnchorLocationArray anchor_locations;
  init_anchor_location_array(&anchor_locations, 1);
  AnchorLocation *al = malloc(sizeof *al);
  al->anchor = "#first_section";
  al->title = "First section";
  al->multipage_url = "url";
  al->multipage_output_directory_path = "output_path/";
  al->multipage_output_file_path = "output_path/index.html";
  insert_anchor_location_array(&anchor_locations, al);

  AnchorLocation *al2 = malloc(sizeof *al2);
  al2->anchor = "#second_section";
  al2->title = "Second section";
  al2->multipage_url = "url";
  al2->multipage_output_directory_path = "output_path/";
  al2->multipage_output_file_path = "output_path/index.html";
  insert_anchor_location_array(&anchor_locations, al2);

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

int file_matches_bytes(char *path, const unsigned char *expected,
                       size_t expected_size) {
  unsigned char got[16];
  if (expected_size > sizeof(got))
    return 0;

  FILE *file = fopen(path, "rb");
  if (file == NULL)
    return 0;
  size_t got_size = fread(got, 1, sizeof(got), file);
  int close_result = fclose(file);
  return close_result == 0 && got_size == expected_size &&
         memcmp(got, expected, expected_size) == 0;
}

int test_copy_nested_image() {
  char root[] = "/tmp/mmdoc-image-test-XXXXXX";
  if (mkdtemp(root) == NULL)
    return 1;

  char src_dir[PATH_MAX];
  char source_dir[PATH_MAX];
  char source_path[PATH_MAX];
  char multi_dir[PATH_MAX];
  char multi_path[PATH_MAX];
  char single_dir[PATH_MAX];
  char single_path[PATH_MAX];
  snprintf(src_dir, sizeof(src_dir), "%s/src", root);
  snprintf(source_dir, sizeof(source_dir), "%s/assets/icons", src_dir);
  snprintf(source_path, sizeof(source_path), "%s/pixel.png", source_dir);
  snprintf(multi_dir, sizeof(multi_dir), "%s/out/multi", root);
  snprintf(multi_path, sizeof(multi_path), "%s/assets/icons/pixel.png",
           multi_dir);
  snprintf(single_dir, sizeof(single_dir), "%s/out/single", root);
  snprintf(single_path, sizeof(single_path), "%s/assets/icons/pixel.png",
           single_dir);

  if (mkdir_p(source_dir) != 0)
    return 1;

  const unsigned char image[] = {0, 1, 2, 0xff};
  FILE *source = fopen(source_path, "wb");
  if (source == NULL)
    return 1;
  int write_failed = fwrite(image, 1, sizeof(image), source) != sizeof(image);
  if (fclose(source) != 0 || write_failed)
    return 1;

  Inputs inputs = {
      .src = src_dir,
      .out_multi = multi_dir,
      .out_single = single_dir,
  };
  if (copy_imgs(inputs) != 0)
    return 1;

  if (!file_matches_bytes(multi_path, image, sizeof(image)) ||
      !file_matches_bytes(single_path, image, sizeof(image)))
    return 1;

  printf("nested image copy test passed\n");
  return 0;
}

int test_zero_capacity_arrays() {
  Array array;
  init_array(&array, 0);
  insert_array(&array, "value");
  if (array.used != 1 || array.size != 1 ||
      strcmp(array.array[0], "value") != 0) {
    printf("zero-capacity string array test failed\n");
    free_array(&array);
    return 1;
  }
  free_array(&array);

  AnchorLocationArray anchor_locations;
  init_anchor_location_array(&anchor_locations, 0);
  AnchorLocation anchor_location = {.anchor = "#anchor"};
  insert_anchor_location_array(&anchor_locations, &anchor_location);
  if (anchor_locations.used != 1 || anchor_locations.size != 1 ||
      strcmp(anchor_locations.array[0].anchor, "#anchor") != 0) {
    printf("zero-capacity anchor-location array test failed\n");
    free_anchor_location_array(&anchor_locations);
    return 1;
  }
  free_anchor_location_array(&anchor_locations);
  printf("zero-capacity array tests passed\n");
  return 0;
}

int file_contains(char *path, const char *expected) {
  FILE *file = fopen(path, "r");
  if (file == NULL)
    return 0;

  char *line = NULL;
  size_t line_size = 0;
  int found = 0;
  while (getline(&line, &line_size, file) != -1) {
    if (strstr(line, expected) != NULL) {
      found = 1;
      break;
    }
  }
  free(line);
  fclose(file);
  return found;
}

int file_exists_and_is_not_empty(char *path) {
  FILE *file = fopen(path, "rb");
  if (file == NULL)
    return 0;
  int first = fgetc(file);
  fclose(file);
  return first != EOF;
}

int write_text_file(char *path, const char *contents) {
  FILE *file = fopen(path, "w");
  if (file == NULL)
    return 1;
  int failed = fputs(contents, file) == EOF;
  return fclose(file) != 0 || failed;
}

int test_multipage_shared_assets_and_accessible_controls() {
  char root[] = "/tmp/mmdoc-assets-test-XXXXXX";
  if (mkdtemp(root) == NULL)
    return 1;

  char page_path[PATH_MAX];
  char toc_path[PATH_MAX];
  char index_path[PATH_MAX];
  snprintf(page_path, sizeof(page_path), "%s/page.md", root);
  snprintf(toc_path, sizeof(toc_path), "%s/toc.md", root);
  snprintf(index_path, sizeof(index_path), "%s/index.html", root);
  if (write_text_file(page_path, "# Page {#page}\n\n```c\nint x;\n```\n") !=
          0 ||
      write_text_file(toc_path, "") != 0)
    return 1;

  Inputs inputs = {
      .project_name = "Project",
      .toc_path = toc_path,
      .out_multi = root,
  };
  AnchorLocation page = {
      .file_path = page_path,
      .multipage_output_file_path = index_path,
      .multipage_output_directory_path = root,
      .multipage_base_href = "",
      .multipage_url = "./",
      .anchor = "#page",
      .title = "Page",
  };
  AnchorLocationArray toc_anchor_locations;
  AnchorLocationArray anchor_locations;
  init_anchor_location_array(&toc_anchor_locations, 1);
  init_anchor_location_array(&anchor_locations, 0);
  insert_anchor_location_array(&toc_anchor_locations, &page);

  int render_result =
      mmdoc_multi(inputs, toc_anchor_locations, anchor_locations);
  const char *asset_names[] = {
      "a11y-dark.css",     "a11y-light.css",  "mmdoc.css",
      "mmdoc.js",          "mmdoc_search.js", "fuse.basic.min.js",
      "highlight.pack.js", "search_index.js",
  };
  int assets_found = 1;
  for (size_t i = 0; i < sizeof(asset_names) / sizeof(asset_names[0]); i++) {
    char asset_path[PATH_MAX];
    snprintf(asset_path, sizeof(asset_path), "%s/%s", root, asset_names[i]);
    assets_found &= file_exists_and_is_not_empty(asset_path);
  }

  int page_is_external = file_contains(index_path, "href='mmdoc.css'") &&
                         file_contains(index_path, "src='mmdoc.js'") &&
                         file_contains(index_path, "src='highlight.pack.js'") &&
                         !file_contains(index_path, "<style>") &&
                         !file_contains(index_path, "Highlight.js 10.7.1");
  int page_is_accessible =
      file_contains(index_path, "class='skip-link' href='#main-content'") &&
      file_contains(index_path, "aria-controls='sidebar'") &&
      file_contains(index_path, "aria-label='Open search'") &&
      file_contains(index_path, "role='status' aria-live='polite'") &&
      file_contains(index_path, "<main id='main-content' tabindex='-1'>");

  for (size_t i = 0; i < sizeof(asset_names) / sizeof(asset_names[0]); i++) {
    char asset_path[PATH_MAX];
    snprintf(asset_path, sizeof(asset_path), "%s/%s", root, asset_names[i]);
    unlink(asset_path);
  }
  int no_code_render_result = 1;
  int highlighter_is_conditional = 0;
  if (write_text_file(page_path, "# Page {#page}\n") == 0) {
    no_code_render_result =
        mmdoc_multi(inputs, toc_anchor_locations, anchor_locations);
    char highlighter_path[PATH_MAX];
    snprintf(highlighter_path, sizeof(highlighter_path), "%s/highlight.pack.js",
             root);
    highlighter_is_conditional =
        !file_exists_and_is_not_empty(highlighter_path) &&
        !file_contains(index_path, "src='highlight.pack.js'");
  }
  for (size_t i = 0; i < sizeof(asset_names) / sizeof(asset_names[0]); i++) {
    char asset_path[PATH_MAX];
    snprintf(asset_path, sizeof(asset_path), "%s/%s", root, asset_names[i]);
    unlink(asset_path);
  }

  free_anchor_location_array(&toc_anchor_locations);
  free_anchor_location_array(&anchor_locations);
  unlink(index_path);
  unlink(page_path);
  unlink(toc_path);
  rmdir(root);

  if (render_result != 0 || no_code_render_result != 0 || !assets_found ||
      !page_is_external || !page_is_accessible || !highlighter_is_conditional) {
    printf("multipage shared asset and accessibility test failed\n");
    return 1;
  }
  printf("multipage shared asset and accessibility test passed\n");
  return 0;
}

int test_single_page_accessible_controls() {
  char root[] = "/tmp/mmdoc-single-accessibility-test-XXXXXX";
  if (mkdtemp(root) == NULL)
    return 1;

  char toc_path[PATH_MAX];
  char out_path[PATH_MAX];
  char index_path[PATH_MAX];
  snprintf(toc_path, sizeof(toc_path), "%s/toc.md", root);
  snprintf(out_path, sizeof(out_path), "%s/single", root);
  snprintf(index_path, sizeof(index_path), "%s/index.html", out_path);
  if (write_text_file(toc_path, "") != 0)
    return 1;

  Inputs inputs = {
      .project_name = "Project",
      .toc_path = toc_path,
      .out_single = out_path,
  };
  AnchorLocationArray toc_anchor_locations;
  init_anchor_location_array(&toc_anchor_locations, 0);
  int render_result = mmdoc_single(inputs, toc_anchor_locations);
  int controls_found = file_contains(index_path, "<html lang='en'>") &&
                       file_contains(index_path, "aria-controls='sidebar'") &&
                       file_contains(index_path, "aria-pressed='false'") &&
                       file_contains(index_path, "id='main-content'");

  free_anchor_location_array(&toc_anchor_locations);
  unlink(index_path);
  unlink(toc_path);
  rmdir(out_path);
  rmdir(root);

  if (render_result != 0 || !controls_found) {
    printf("single-page accessibility test failed\n");
    return 1;
  }
  printf("single-page accessibility test passed\n");
  return 0;
}

int test_multipage_navigation_anchor() {
  char root[] = "/tmp/mmdoc-navigation-test-XXXXXX";
  if (mkdtemp(root) == NULL)
    return 1;

  char page_path[PATH_MAX];
  char toc_path[PATH_MAX];
  snprintf(page_path, sizeof(page_path), "%s/index.html", root);
  snprintf(toc_path, sizeof(toc_path), "%s/toc.md", root);

  FILE *toc_file = fopen(toc_path, "w");
  if (toc_file == NULL)
    return 1;
  if (fclose(toc_file) != 0)
    return 1;

  FILE *search_index_file = tmpfile();
  if (search_index_file == NULL)
    return 1;

  Inputs inputs = {
      .project_name = "Nixpkgs",
      .toc_path = toc_path,
  };
  AnchorLocation current = {
      .file_path = NULL,
      .multipage_output_file_path = page_path,
      .multipage_base_href = "",
      .multipage_url = "./",
      .anchor = "#preface",
      .title = "Preface",
  };
  AnchorLocation previous = {
      .multipage_url = "./",
      .anchor = "#preface",
      .title = "Preface",
  };
  AnchorLocation next = {
      .multipage_url = "preface/",
      .anchor = "#overview-of-nixpkgs",
      .title = "Overview of Nixpkgs",
  };
  AnchorLocationArray anchor_locations;
  init_anchor_location_array(&anchor_locations, 0);

  int render_result =
      mmdoc_multi_page(inputs, anchor_locations, search_index_file, &current,
                       &previous, &next, NULL);
  int found_previous = file_contains(
      page_path, "id='chapter-previous-button' class='chapter-previous' "
                 "href='./#preface'");
  int found_next =
      file_contains(page_path, "id='chapter-next-button' class='chapter-next' "
                               "href='preface/#overview-of-nixpkgs'");

  free_anchor_location_array(&anchor_locations);
  fclose(search_index_file);
  unlink(page_path);
  unlink(toc_path);
  rmdir(root);

  if (render_result != 0 || !found_previous || !found_next) {
    printf("multipage navigation anchor test failed\n");
    return 1;
  }
  printf("multipage navigation anchor test passed\n");
  return 0;
}

int write_overlong_id(char *path, const char *prefix, const char *suffix) {
  int fd = mkstemps(path, 5);
  if (fd == -1)
    return 1;

  FILE *file = fdopen(fd, "w");
  if (file == NULL) {
    close(fd);
    return 1;
  }

  int failed = fputs(prefix, file) == EOF;
  for (int i = 0; i < 1024 && !failed; i++)
    failed = fputc('a', file) == EOF;
  if (!failed)
    failed = fputs(suffix, file) == EOF;
  return fclose(file) != 0 || failed;
}

int test_reject_overlong_ids() {
  char ref_path[] = "/tmp/mmdoc-ref-XXXXXX.html";
  if (write_overlong_id(ref_path, "[page](#", ")") != 0)
    return 1;

  Array refs;
  int refs_result = mmdoc_refs(&refs, ref_path);
  unlink(ref_path);
  free_array(&refs);

  char anchor_path[] = "/tmp/mmdoc-anchor-XXXXXX.html";
  if (write_overlong_id(anchor_path, "{#", "}") != 0)
    return 1;

  Array anchors;
  init_array(&anchors, 1);
  int anchors_result = mmdoc_anchors(&anchors, anchor_path);
  unlink(anchor_path);
  free_array(&anchors);

  if (refs_result == 0 || anchors_result == 0) {
    printf("overlong anchor rejection test failed\n");
    return 1;
  }
  printf("overlong anchors rejected\n");
  return 0;
}

int main(int argc, char *argv[]) {
  int num_failed = 0;
  int num_tests = 0;
  num_failed += test_render("e001");
  num_tests++;
  num_failed += test_code_block_detection("e001", 0);
  num_tests++;
  num_failed += test_code_block_detection("e012", 1);
  num_tests++;
  num_failed += test_title("e001", "Header");
  num_tests++;
  num_failed += test_title("e010", "Formatted title");
  num_tests++;
  num_failed += test_title("e011", "");
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
  num_failed += test_render("e009");
  num_tests++;
  num_failed += test_copy_nested_image();
  num_tests++;
  num_failed += test_zero_capacity_arrays();
  num_tests++;
  num_failed += test_multipage_navigation_anchor();
  num_tests++;
  num_failed += test_multipage_shared_assets_and_accessible_controls();
  num_tests++;
  num_failed += test_single_page_accessible_controls();
  num_tests++;
  num_failed += test_reject_overlong_ids();
  num_tests++;

  printf("%d of %d tests passed.", num_tests - num_failed, num_tests);
  if (num_failed > 0)
    return 1;
  return 0;
}
