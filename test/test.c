/* SPDX-License-Identifier: CC0-1.0 */
#include "../src/anchors.h"
#include "../src/files.h"
#include "../src/md.h"
#include "../src/mkdir_p.h"
#include "../src/multi.h"
#include "../src/parse.h"
#include "../src/refs.h"
#include "../src/render.h"
#include "../src/single.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <testconfig.h>

extern int errno;

int test_files_match(char *example_name, char *expected_file_path,
                     char *got_file_path) {
  char expected_line[4096];
  char got_line[4096];
  int ret_val = 0;
  int first_col_diff = -1;
  int expected;
  int got;

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

  fclose(got_file);
  fclose(expected_file);
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

  int failed = test_files_match(example, expected_path, got_file_path);
  close(ret);
  unlink(got_file_path);
  free(input_path);
  free(expected_path);
  return failed;
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

  int failed = test_files_match(example, expected_path, got_file_path);
  close(ret);
  unlink(got_file_path);
  free(input_path);
  free(expected_path);
  return failed;
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
  free(al);

  AnchorLocation *al2 = malloc(sizeof *al2);
  al2->anchor = "#second_section";
  al2->title = "Second section";
  al2->multipage_url = "url";
  al2->multipage_output_directory_path = "output_path/";
  al2->multipage_output_file_path = "output_path/index.html";
  insert_anchor_location_array(&anchor_locations, al2);
  free(al2);

  int retval = test_multipage_render("e007", anchor_locations);
  free_anchor_location_array(&anchor_locations);
  return retval;
}

int test_e015() {
  AnchorLocationArray anchor_locations;
  init_anchor_location_array(&anchor_locations, 0);
  int retval = test_multipage_render("e015", anchor_locations);
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

int test_anchor_location_index() {
  AnchorLocationArray anchor_locations;
  init_anchor_location_array(&anchor_locations, 3);
  AnchorLocation first = {.anchor = "#first", .title = "First"};
  AnchorLocation second = {.anchor = "#second", .title = "Second"};
  AnchorLocation duplicate = {.anchor = "#first", .title = "Duplicate"};
  insert_anchor_location_array(&anchor_locations, &first);
  insert_anchor_location_array(&anchor_locations, &second);
  insert_anchor_location_array(&anchor_locations, &duplicate);

  int build_result = build_anchor_location_index(&anchor_locations);
  AnchorLocation *found_first =
      find_anchor_location(&anchor_locations, "#first");
  AnchorLocation *found_second =
      find_anchor_location(&anchor_locations, "#second");
  AnchorLocation *not_found =
      find_anchor_location(&anchor_locations, "#missing");
  int passed = build_result == 0 && found_first != NULL &&
               strcmp(found_first->title, "First") == 0 &&
               found_second != NULL &&
               strcmp(found_second->title, "Second") == 0 && not_found == NULL;
  free_anchor_location_array(&anchor_locations);

  if (!passed) {
    printf("anchor location index test failed\n");
    return 1;
  }
  printf("anchor location index test passed\n");
  return 0;
}

int test_anchor_discovery_uses_markdown_ast() {
  char path[] = "/tmp/mmdoc-anchor-discovery-XXXXXX.md";
  int fd = mkstemps(path, 3);
  if (fd == -1)
    return 1;
  FILE *file = fdopen(fd, "w");
  if (file == NULL)
    return 1;
  const char *markdown = "# Page {#page}\n\n"
                         "`{#not-an-anchor}`\n\n"
                         "::: {.example #example-anchor}\nExample\n:::\n\n"
                         "[`lib`]{#lib-anchor}\n\n"
                         "[]{#first-alias} []{#second-alias}\n";
  int write_failed = fputs(markdown, file) == EOF || fclose(file) != 0;

  Array anchors;
  init_array(&anchors, 0);
  int result = write_failed ? -1 : mmdoc_anchors(&anchors, path);
  const char *expected[] = {"#page", "#example-anchor", "#lib-anchor",
                            "#first-alias", "#second-alias"};
  int passed = result == 0 && anchors.used == 5;
  for (size_t i = 0; passed && i < 5; i++)
    passed = strcmp(anchors.array[i], expected[i]) == 0;

  free_array(&anchors);
  init_array(&anchors, 0);
  char *title = NULL;
  int metadata_result = mmdoc_render_collect_metadata(path, &anchors, &title);
  passed = passed && metadata_result == 0 && title != NULL &&
           strcmp(title, "Page") == 0 && anchors.used == 5;
  for (size_t i = 0; passed && i < 5; i++)
    passed = strcmp(anchors.array[i], expected[i]) == 0;
  free(title);
  free_array(&anchors);
  unlink(path);
  if (!passed) {
    printf("AST anchor discovery test failed\n");
    return 1;
  }
  printf("AST anchor discovery test passed\n");
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

int file_contains_before(char *path, const char *first, const char *second) {
  FILE *file = fopen(path, "r");
  if (file == NULL)
    return 0;

  if (fseek(file, 0, SEEK_END) != 0) {
    fclose(file);
    return 0;
  }
  long file_size = ftell(file);
  if (file_size < 0 || fseek(file, 0, SEEK_SET) != 0) {
    fclose(file);
    return 0;
  }

  char *contents = malloc((size_t)file_size + 1);
  if (contents == NULL) {
    fclose(file);
    return 0;
  }
  size_t bytes_read = fread(contents, 1, (size_t)file_size, file);
  contents[bytes_read] = '\0';
  char *first_position = strstr(contents, first);
  char *second_position = strstr(contents, second);
  int found = first_position != NULL && second_position != NULL &&
              first_position < second_position;
  free(contents);
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

int find_asset_name(char *dir, const char *prefix, const char *suffix,
                    char *result, size_t result_size) {
  DIR *directory = opendir(dir);
  if (directory == NULL)
    return 0;
  struct dirent *entry;
  int found = 0;
  while ((entry = readdir(directory)) != NULL) {
    size_t name_length = strlen(entry->d_name);
    size_t prefix_length = strlen(prefix);
    size_t suffix_length = strlen(suffix);
    if (name_length >= prefix_length + suffix_length &&
        strncmp(entry->d_name, prefix, prefix_length) == 0 &&
        strcmp(entry->d_name + name_length - suffix_length, suffix) == 0) {
      snprintf(result, result_size, "%s", entry->d_name);
      found = 1;
      break;
    }
  }
  closedir(directory);
  return found;
}

int asset_name_has_hash(const char *file_name, const char *stem,
                        const char *extension) {
  size_t stem_length = strlen(stem);
  size_t extension_length = strlen(extension);
  size_t expected_length = stem_length + 1 + 16 + 1 + extension_length;
  if (strlen(file_name) != expected_length ||
      strncmp(file_name, stem, stem_length) != 0 ||
      file_name[stem_length] != '.' ||
      strcmp(file_name + expected_length - extension_length, extension) != 0)
    return 0;
  for (size_t i = stem_length + 1; i < stem_length + 17; i++)
    if (!isxdigit((unsigned char)file_name[i]))
      return 0;
  return file_name[stem_length + 17] == '.';
}

void unlink_asset(char *dir, const char *file_name) {
  char path[PATH_MAX];
  snprintf(path, sizeof(path), "%s/%s", dir, file_name);
  unlink(path);
}

static void unlink_markdown(char *dir) {
  unlink_asset(dir, "index.md");
  unlink_asset(dir, "llms.txt");
  unlink_asset(dir, "llms-full.txt");
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
  AssetFileNames generated_names;
  asset_file_names(&generated_names);
  char search_index_name[ASSET_FILE_NAME_SIZE] = "";
  int search_index_found =
      find_asset_name(root, "search_index.", ".js", search_index_name,
                      sizeof(search_index_name));
  char first_search_index_name[ASSET_FILE_NAME_SIZE];
  snprintf(first_search_index_name, sizeof(first_search_index_name), "%s",
           search_index_name);
  const char *asset_names[] = {
      generated_names.a11y_dark_css,     generated_names.a11y_light_css,
      generated_names.mmdoc_css,         generated_names.mmdoc_js,
      generated_names.mmdoc_search_js,   generated_names.fuse_basic_min_js,
      generated_names.highlight_pack_js,
  };
  int assets_found = search_index_found;
  for (size_t i = 0; i < sizeof(asset_names) / sizeof(asset_names[0]); i++) {
    char asset_path[PATH_MAX];
    snprintf(asset_path, sizeof(asset_path), "%s/%s", root, asset_names[i]);
    assets_found &= file_exists_and_is_not_empty(asset_path);
  }

  int names_are_hashed =
      asset_name_has_hash(generated_names.a11y_dark_css, "a11y-dark", "css") &&
      asset_name_has_hash(generated_names.a11y_light_css, "a11y-light",
                          "css") &&
      asset_name_has_hash(generated_names.mmdoc_css, "mmdoc", "css") &&
      asset_name_has_hash(generated_names.mmdoc_js, "mmdoc", "js") &&
      asset_name_has_hash(generated_names.mmdoc_search_js, "mmdoc_search",
                          "js") &&
      asset_name_has_hash(generated_names.fuse_basic_min_js, "fuse.basic.min",
                          "js") &&
      asset_name_has_hash(generated_names.highlight_pack_js, "highlight.pack",
                          "js") &&
      asset_name_has_hash(search_index_name, "search_index", "js");
  int page_is_external =
      file_contains(index_path, generated_names.mmdoc_css) &&
      file_contains(index_path, generated_names.mmdoc_js) &&
      file_contains(index_path, generated_names.mmdoc_search_js) &&
      file_contains(index_path, generated_names.highlight_pack_js) &&
      file_contains(index_path, search_index_name) &&
      file_contains(index_path, generated_names.fuse_basic_min_js) &&
      !file_contains(index_path, "href='mmdoc.css'") &&
      !file_contains(index_path, "src='mmdoc.js'") &&
      !file_contains(index_path, "<style>") &&
      !file_contains(index_path, "Highlight.js 10.7.1");
  char search_script_path[PATH_MAX];
  snprintf(search_script_path, sizeof(search_script_path), "%s/%s", root,
           generated_names.mmdoc_search_js);
  int search_is_lazy =
      file_contains(search_script_path,
                    "loadSearchScript(searchIndexSource)") &&
      file_contains(search_script_path, "loadSearchScript(fuseSource)") &&
      file_contains(search_script_path, "window.mmdocSearchCorpus") &&
      file_contains(search_script_path, "limit: searchResultLimit");
  int page_is_accessible =
      file_contains(index_path, "viewport-fit=cover") &&
      file_contains(index_path, "mobile-top-nav") &&
      file_contains(index_path, "<header class='nav-top-container'>") &&
      file_contains(index_path, "class='skip-link' href='./#main-content'") &&
      file_contains(index_path, "aria-controls='sidebar'") &&
      file_contains(index_path, "aria-label='Open search'") &&
      file_contains(index_path, "role='status' aria-live='polite'") &&
      file_contains(index_path, "<main id='main-content' tabindex='-1'>");
  int theme_is_initialized_before_css =
      file_contains_before(index_path, "localStorage.getItem('theme')",
                           generated_names.mmdoc_css) &&
      file_contains(index_path,
                    "document.documentElement.classList.add(theme + "
                    "'-theme')");

  for (size_t i = 0; i < sizeof(asset_names) / sizeof(asset_names[0]); i++) {
    unlink_asset(root, asset_names[i]);
  }
  unlink_asset(root, search_index_name);
  int no_code_render_result = 1;
  int highlighter_is_conditional = 0;
  int corpus_hash_changed = 0;
  if (write_text_file(page_path, "# Page {#page}\n") == 0) {
    no_code_render_result =
        mmdoc_multi(inputs, toc_anchor_locations, anchor_locations);
    char highlighter_path[PATH_MAX];
    snprintf(highlighter_path, sizeof(highlighter_path), "%s/%s", root,
             generated_names.highlight_pack_js);
    highlighter_is_conditional =
        !file_exists_and_is_not_empty(highlighter_path) &&
        !file_contains(index_path, generated_names.highlight_pack_js);
  }
  for (size_t i = 0; i < sizeof(asset_names) / sizeof(asset_names[0]); i++) {
    unlink_asset(root, asset_names[i]);
  }
  if (find_asset_name(root, "search_index.", ".js", search_index_name,
                      sizeof(search_index_name))) {
    corpus_hash_changed =
        strcmp(first_search_index_name, search_index_name) != 0;
    unlink_asset(root, search_index_name);
  }

  free_anchor_location_array(&toc_anchor_locations);
  free_anchor_location_array(&anchor_locations);
  unlink(index_path);
  unlink(page_path);
  unlink(toc_path);
  unlink_markdown(root);
  rmdir(root);

  if (render_result != 0 || no_code_render_result != 0 || !assets_found ||
      !names_are_hashed || !page_is_external || !page_is_accessible ||
      !theme_is_initialized_before_css || !search_is_lazy ||
      !corpus_hash_changed || !highlighter_is_conditional) {
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
                       file_contains(index_path, "viewport-fit=cover") &&
                       file_contains(index_path, "aria-controls='sidebar'") &&
                       file_contains(index_path, "aria-pressed='false'") &&
                       file_contains(index_path, "id='main-content'");
  int theme_is_initialized_before_css = file_contains_before(
      index_path, "localStorage.getItem('theme')", "<style>");

  free_anchor_location_array(&toc_anchor_locations);
  unlink(index_path);
  unlink(toc_path);
  rmdir(out_path);
  rmdir(root);

  if (render_result != 0 || !controls_found ||
      !theme_is_initialized_before_css) {
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
  AssetFileNames generated_names;
  asset_file_names(&generated_names);

  int render_result =
      mmdoc_multi_page(inputs, anchor_locations, "", 0, &generated_names,
                       "search_index.0000000000000000.js", search_index_file,
                       NULL, &current, &previous, &next, NULL);
  int found_previous =
      file_contains(page_path,
                    "id='chapter-previous-button' class='chapter-previous' "
                    "href='./#preface'") &&
      file_contains(page_path,
                    "class='chapter-pagination-link "
                    "chapter-pagination-previous' href='./#preface'");
  int found_next =
      file_contains(page_path, "id='chapter-next-button' class='chapter-next' "
                               "href='preface/#overview-of-nixpkgs'") &&
      file_contains(page_path, "class='chapter-pagination-link "
                               "chapter-pagination-next' "
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

int test_multipage_cached_toc_current_page_links() {
  char root[] = "/tmp/mmdoc-cached-toc-test-XXXXXX";
  if (mkdtemp(root) == NULL)
    return 1;

  char toc_path[PATH_MAX];
  char first_source_path[PATH_MAX];
  char second_source_path[PATH_MAX];
  char first_output_path[PATH_MAX];
  char second_output_path[PATH_MAX];
  snprintf(toc_path, sizeof(toc_path), "%s/toc.md", root);
  snprintf(first_source_path, sizeof(first_source_path), "%s/first.md", root);
  snprintf(second_source_path, sizeof(second_source_path), "%s/second.md",
           root);
  snprintf(first_output_path, sizeof(first_output_path), "%s/first.html", root);
  snprintf(second_output_path, sizeof(second_output_path), "%s/second.html",
           root);

  if (write_text_file(toc_path, "# Contents {#contents}\n\n* [](#first)\n* "
                                "[](#second)\n") != 0 ||
      write_text_file(first_source_path, "# First {#first}\n") != 0 ||
      write_text_file(second_source_path, "# Second {#second}\n") != 0)
    return 1;

  Inputs inputs = {
      .project_name = "Project",
      .toc_path = toc_path,
      .out_multi = root,
  };
  AnchorLocation first = {
      .file_path = first_source_path,
      .multipage_output_file_path = first_output_path,
      .multipage_output_directory_path = root,
      .multipage_base_href = "",
      .multipage_url = "first/",
      .anchor = "#first",
      .title = "First",
  };
  AnchorLocation second = {
      .file_path = second_source_path,
      .multipage_output_file_path = second_output_path,
      .multipage_output_directory_path = root,
      .multipage_base_href = "",
      .multipage_url = "second/",
      .anchor = "#second",
      .title = "Second",
  };
  AnchorLocationArray toc_anchor_locations;
  AnchorLocationArray anchor_locations;
  init_anchor_location_array(&toc_anchor_locations, 2);
  init_anchor_location_array(&anchor_locations, 2);
  insert_anchor_location_array(&toc_anchor_locations, &first);
  insert_anchor_location_array(&toc_anchor_locations, &second);
  insert_anchor_location_array(&anchor_locations, &first);
  insert_anchor_location_array(&anchor_locations, &second);
  int index_result = build_anchor_location_index(&anchor_locations);
  int render_result =
      mmdoc_multi(inputs, toc_anchor_locations, anchor_locations);

  int links_are_page_specific =
      file_contains(first_output_path,
                    "<h1 id='contents'><a href='first/#contents'>") &&
      file_contains(second_output_path,
                    "<h1 id='contents'><a href='second/#contents'>");
  int toc_links_are_resolved =
      file_contains(first_output_path, "href='first/#first'") &&
      file_contains(first_output_path, "href='second/#second'") &&
      file_contains(second_output_path, "href='first/#first'") &&
      file_contains(second_output_path, "href='second/#second'");

  AssetFileNames generated_names;
  asset_file_names(&generated_names);
  const char *asset_names[] = {
      generated_names.a11y_dark_css,   generated_names.a11y_light_css,
      generated_names.mmdoc_css,       generated_names.mmdoc_js,
      generated_names.mmdoc_search_js, generated_names.fuse_basic_min_js,
  };
  for (size_t i = 0; i < sizeof(asset_names) / sizeof(asset_names[0]); i++) {
    unlink_asset(root, asset_names[i]);
  }
  char search_index_name[ASSET_FILE_NAME_SIZE];
  if (find_asset_name(root, "search_index.", ".js", search_index_name,
                      sizeof(search_index_name)))
    unlink_asset(root, search_index_name);
  free_anchor_location_array(&toc_anchor_locations);
  free_anchor_location_array(&anchor_locations);
  unlink(first_output_path);
  unlink(second_output_path);
  unlink(first_source_path);
  unlink(second_source_path);
  unlink(toc_path);
  unlink_markdown(root);
  rmdir(root);

  if (index_result != 0 || render_result != 0 || !links_are_page_specific ||
      !toc_links_are_resolved) {
    printf("multipage cached TOC test failed\n");
    return 1;
  }
  printf("multipage cached TOC test passed\n");
  return 0;
}

int test_multipage_duplicate_output_paths() {
  char root[] = "/tmp/mmdoc-duplicate-output-test-XXXXXX";
  if (mkdtemp(root) == NULL)
    return 1;

  char toc_path[PATH_MAX];
  char source_path[PATH_MAX];
  char output_path[PATH_MAX];
  snprintf(toc_path, sizeof(toc_path), "%s/toc.md", root);
  snprintf(source_path, sizeof(source_path), "%s/page.md", root);
  snprintf(output_path, sizeof(output_path), "%s/index.html", root);
  if (write_text_file(toc_path, "") != 0 ||
      write_text_file(source_path,
                      "# First {#first}\n\n## Second {#second}\n") != 0)
    return 1;

  Inputs inputs = {
      .project_name = "Project",
      .toc_path = toc_path,
      .out_multi = root,
  };
  AnchorLocation first = {
      .file_path = source_path,
      .multipage_output_file_path = output_path,
      .multipage_output_directory_path = root,
      .multipage_base_href = "",
      .multipage_url = "./",
      .anchor = "#first",
      .title = "First",
  };
  AnchorLocation second = {
      .file_path = source_path,
      .multipage_output_file_path = output_path,
      .multipage_output_directory_path = root,
      .multipage_base_href = "",
      .multipage_url = "./",
      .anchor = "#second",
      .title = "A much longer second title",
  };
  AnchorLocationArray toc_anchor_locations;
  AnchorLocationArray anchor_locations;
  init_anchor_location_array(&toc_anchor_locations, 2);
  init_anchor_location_array(&anchor_locations, 2);
  insert_anchor_location_array(&toc_anchor_locations, &first);
  insert_anchor_location_array(&toc_anchor_locations, &second);
  insert_anchor_location_array(&anchor_locations, &first);
  insert_anchor_location_array(&anchor_locations, &second);
  int index_result = build_anchor_location_index(&anchor_locations);
  int render_result =
      mmdoc_multi(inputs, toc_anchor_locations, anchor_locations);

  AssetFileNames generated_names;
  asset_file_names(&generated_names);
  char search_index_name[ASSET_FILE_NAME_SIZE];
  int search_index_found =
      find_asset_name(root, "search_index.", ".js", search_index_name,
                      sizeof(search_index_name));
  int search_index_replaced =
      search_index_found && file_contains(output_path, search_index_name) &&
      !file_contains(output_path, "search_index.0000000000000000.js");
  int page_navigation_is_unique =
      !file_contains(output_path, "chapter-previous-button") &&
      !file_contains(output_path, "chapter-next-button");

  const char *asset_names[] = {
      generated_names.a11y_dark_css,   generated_names.a11y_light_css,
      generated_names.mmdoc_css,       generated_names.mmdoc_js,
      generated_names.mmdoc_search_js, generated_names.fuse_basic_min_js,
  };
  for (size_t i = 0; i < sizeof(asset_names) / sizeof(asset_names[0]); i++)
    unlink_asset(root, asset_names[i]);
  if (search_index_found)
    unlink_asset(root, search_index_name);
  free_anchor_location_array(&toc_anchor_locations);
  free_anchor_location_array(&anchor_locations);
  unlink(output_path);
  unlink(source_path);
  unlink(toc_path);
  unlink_markdown(root);
  rmdir(root);

  if (index_result != 0 || render_result != 0 || !search_index_replaced ||
      !page_navigation_is_unique) {
    printf("multipage duplicate output paths test failed\n");
    return 1;
  }
  printf("multipage duplicate output paths test passed\n");
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

static int test_render_io_and_man_cleanup(void) {
  AnchorLocationArray locations = {0};
  Array anchors = {0};
  FILE *output = tmpfile();
  if (output == NULL)
    return 1;
  char missing[] = "/dev/null/missing.md";
  int failed = mmdoc_render_part(missing, output, RENDER_TYPE_SINGLE, NULL,
                                 locations, "", NULL) != -1;
  char *title = mmdoc_render_get_title_from_file(missing);
  failed |= title != NULL;
  free(title);
  failed |= mmdoc_render_collect_anchors(missing, &anchors) != -1;
  free_array(&anchors);
  failed |= mmdoc_render_part(TEST_EXAMPLE_DIR "e001/input.md", output,
                              RENDER_TYPE_MAN, NULL, locations, "", NULL) < 0;
  fclose(output);
  output = fopen("/dev/full", "w");
  if (output != NULL) {
    setvbuf(output, NULL, _IONBF, 0);
    failed |=
        mmdoc_render_part(TEST_EXAMPLE_DIR "e001/input.md", output,
                          RENDER_TYPE_SINGLE, NULL, locations, "", NULL) != -1;
    fclose(output);
  }
  return failed;
}

static int test_parse_edge_cases(void) {
  char id[64];
  char span[64];

  /* Heading attributes: multiple spaces, empty ID rejection, preceding braces
   */
  if (parse_heading_bracketed_span_id("Heading  {#two-spaces}", id) != 7 ||
      strcmp(id, "two-spaces") != 0) {
    printf("heading two-spaces parse test failed\n");
    return 1;
  }
  if (parse_heading_bracketed_span_id("Heading {#}", id) != -1) {
    printf("heading empty-id rejection test failed\n");
    return 1;
  }
  if (parse_heading_bracketed_span_id("Heading {foo} {#valid}", id) != 13 ||
      strcmp(id, "valid") != 0) {
    printf("heading after-braces parse test failed\n");
    return 1;
  }

  /* Link bracketed spans: consecutive brackets, nested [, empty ID */
  if (parse_link_bracketed_span_id("[foo][bar]{#id}", span, id) != 5 ||
      strcmp(span, "bar") != 0 || strcmp(id, "id") != 0) {
    printf("consecutive bracket span parse test failed\n");
    return 1;
  }
  if (parse_link_bracketed_span_id("[[nested]{#nid}", span, id) != 1 ||
      strcmp(span, "nested") != 0 || strcmp(id, "nid") != 0) {
    printf("nested bracket span parse test failed\n");
    return 1;
  }
  if (parse_link_bracketed_span_id("[foo]{#}", span, id) != -1) {
    printf("empty span id rejection test failed\n");
    return 1;
  }
  if (parse_link_bracketed_span_id("[a]{b}[c]{#ok}", span, id) != 6 ||
      strcmp(span, "c") != 0 || strcmp(id, "ok") != 0) {
    printf("span after non-id brace test failed\n");
    return 1;
  }

  /* Admonition end: only colons + optional trailing whitespace */
  if (parse_admonition_end(":::") != 0 ||
      parse_admonition_end("   :::   ") != 0 ||
      parse_admonition_end("::::") != 0 ||
      parse_admonition_end("::: foo bar") != -1 ||
      parse_admonition_end(":::note") != -1) {
    printf("admonition end parse test failed\n");
    return 1;
  }

  printf("parse edge cases test passed\n");
  return 0;
}

static int test_admonition_blank_lines_and_safety(void) {
  char input_path[] = "/tmp/mmdoc-admon-in-XXXXXX.md";
  char out_path[] = "/tmp/mmdoc-admon-out-XXXXXX.html";
  int in_fd = mkstemps(input_path, 3);
  int out_fd = mkstemps(out_path, 5);
  if (in_fd == -1 || out_fd == -1)
    return 1;
  close(in_fd);
  close(out_fd);

  const char *md = "::: warning\n\nBody text.\n\n:::\n\n"
                   "::: foo bar\n\n"
                   "*::: note*\n";
  if (write_text_file(input_path, md) != 0)
    return 1;

  FILE *out = fopen(out_path, "w");
  if (out == NULL)
    return 1;
  AnchorLocationArray empty;
  init_anchor_location_array(&empty, 0);
  AnchorLocation al = {.title = "Test"};
  int rc = mmdoc_render_part(input_path, out, RENDER_TYPE_SINGLE, &al, empty,
                             "", NULL);
  free_anchor_location_array(&empty);
  fclose(out);

  int passed = rc >= 0 &&
               file_contains(out_path, "<div class='admonition warning'>") &&
               file_contains(out_path, "<p>Body text.</p>") &&
               !file_contains(out_path, "<p></p>") &&
               file_contains(out_path, "<p>::: foo bar</p>") &&
               file_contains(out_path, "<em>::: note</em>");
  unlink(input_path);
  unlink(out_path);
  if (!passed) {
    printf("admonition blank lines and safety test failed\n");
    return 1;
  }
  printf("admonition blank lines and safety test passed\n");
  return 0;
}

static int test_refs_uses_markdown_ast(void) {
  char path[] = "/tmp/mmdoc-refs-ast-XXXXXX.md";
  int fd = mkstemps(path, 3);
  if (fd == -1)
    return 1;
  close(fd);

  const char *toc = "## Contents\n\n"
                    "* [Intro][i]\n"
                    "* [Second](#second \"Title\")\n"
                    "* `(#not-a-ref)`\n"
                    "* Plain (#also-not-a-ref)\n"
                    "<!-- [Commented](#commented) -->\n\n"
                    "[i]: #intro\n";
  if (write_text_file(path, toc) != 0)
    return 1;

  Array refs;
  int rc = mmdoc_refs(&refs, path);
  int passed = rc == 0 && refs.used == 2 &&
               strcmp(refs.array[0], "#intro") == 0 &&
               strcmp(refs.array[1], "#second") == 0;
  free_array(&refs);
  unlink(path);
  if (!passed) {
    printf("refs markdown AST test failed\n");
    return 1;
  }
  printf("refs markdown AST test passed\n");
  return 0;
}

static int test_single_dedup_and_link_titles(void) {
  char root[] = "/tmp/mmdoc-single-dedup-XXXXXX";
  if (mkdtemp(root) == NULL)
    return 1;

  char toc_path[PATH_MAX], p1_path[PATH_MAX], p2_path[PATH_MAX],
      out_dir[PATH_MAX], index_path[PATH_MAX];
  snprintf(toc_path, sizeof(toc_path), "%s/toc.md", root);
  snprintf(p1_path, sizeof(p1_path), "%s/p1.md", root);
  snprintf(p2_path, sizeof(p2_path), "%s/p2.md", root);
  snprintf(out_dir, sizeof(out_dir), "%s/single", root);
  snprintf(index_path, sizeof(index_path), "%s/single/index.html", root);

  if (write_text_file(toc_path, "* [](#first)\n* [](#sub)\n* [](#second)\n") !=
          0 ||
      write_text_file(p1_path, "# First & <One> {#first}\n\n## Sub {#sub}\n\n"
                               "See [](#second).\n") != 0 ||
      write_text_file(p2_path, "# Second's Page {#second}\n\nBody 2.\n") != 0)
    return 1;

  Inputs inputs = {
      .project_name = "Proj <1> & '2'",
      .toc_path = toc_path,
      .out_single = out_dir,
  };
  AnchorLocation l1 = {.file_path = p1_path,
                       .multipage_output_file_path = "p1/index.html",
                       .anchor = "#first",
                       .title = "First & <One>"};
  AnchorLocation l1b = {.file_path = p1_path,
                        .multipage_output_file_path = "p1/index.html",
                        .anchor = "#sub",
                        .title = "First & <One>"};
  AnchorLocation l2 = {.file_path = p2_path,
                       .multipage_output_file_path = "p2/index.html",
                       .anchor = "#second",
                       .title = "Second's Page"};
  AnchorLocationArray toc_locs, all_locs;
  init_anchor_location_array(&toc_locs, 3);
  init_anchor_location_array(&all_locs, 3);
  insert_anchor_location_array(&toc_locs, &l1);
  insert_anchor_location_array(&toc_locs, &l1b);
  insert_anchor_location_array(&toc_locs, &l2);
  insert_anchor_location_array(&all_locs, &l1);
  insert_anchor_location_array(&all_locs, &l1b);
  insert_anchor_location_array(&all_locs, &l2);
  build_anchor_location_index(&all_locs);

  int rc = mmdoc_single_with_anchors(inputs, toc_locs, all_locs);

  /* Count occurrences of id='first' to confirm p1 was rendered only once. */
  int first_count = 0;
  FILE *f = fopen(index_path, "r");
  if (f != NULL) {
    char *line = NULL;
    size_t sz = 0;
    while (getline(&line, &sz, f) != -1) {
      if (strstr(line, "<h1 id='first'>") != NULL)
        first_count++;
    }
    free(line);
    fclose(f);
  }

  int passed =
      rc == 0 && first_count == 1 &&
      file_contains(index_path,
                    "<title>Proj &lt;1&gt; &amp; &#39;2&#39;</title>") &&
      file_contains(index_path,
                    "<a href=\"#first\">First &amp; &lt;One&gt;</a>") &&
      file_contains(index_path, "<a href=\"#second\">Second's Page</a>") &&
      file_contains(index_path, "<h1 id='first'><a href='#first'>") &&
      !file_contains(index_path, "href='/#first'");

  free_anchor_location_array(&toc_locs);
  free_anchor_location_array(&all_locs);
  unlink(index_path);
  unlink(toc_path);
  unlink(p1_path);
  unlink(p2_path);
  rmdir(out_dir);
  rmdir(root);

  if (!passed) {
    printf("single dedup and link titles test failed\n");
    return 1;
  }
  printf("single dedup and link titles test passed\n");
  return 0;
}

static int test_mkdir_p_and_symlink_loop(void) {
  if (mkdir_p("") != -1) {
    printf("mkdir_p empty path rejection failed\n");
    return 1;
  }

  char root[] = "/tmp/mmdoc-loop-test-XXXXXX";
  if (mkdtemp(root) == NULL)
    return 1;

  char file_path[PATH_MAX], md_path[PATH_MAX], loop_path[PATH_MAX];
  snprintf(file_path, sizeof(file_path), "%s/plain.txt", root);
  snprintf(md_path, sizeof(md_path), "%s/page.md", root);
  snprintf(loop_path, sizeof(loop_path), "%s/self-loop", root);
  if (write_text_file(file_path, "x") != 0 ||
      write_text_file(md_path, "# Page {#p}\n") != 0 ||
      symlink(root, loop_path) != 0)
    return 1;

  int conflict_rejected = mkdir_p(file_path) == -1 && errno == ENOTDIR;

  Array md_files;
  init_array(&md_files, 4);
  Inputs inputs = {.src = root, .out_multi = NULL, .out_single = NULL};
  int walk_rc = mmdoc_md_files(&md_files, inputs);
  int loop_safe = walk_rc == 0 && md_files.used == 1 &&
                  strcmp(md_files.array[0], md_path) == 0;
  free_array(&md_files);

  unlink(loop_path);
  unlink(md_path);
  unlink(file_path);
  rmdir(root);

  if (!conflict_rejected || !loop_safe) {
    printf("mkdir_p and symlink loop test failed\n");
    return 1;
  }
  printf("mkdir_p and symlink loop test passed\n");
  return 0;
}

static int test_definition_list_continuations(void) {
  char input_path[] = "/tmp/mmdoc-deflist-in-XXXXXX.md";
  char out_path[] = "/tmp/mmdoc-deflist-out-XXXXXX.html";
  int in_fd = mkstemps(input_path, 3);
  int out_fd = mkstemps(out_path, 5);
  if (in_fd == -1 || out_fd == -1)
    return 1;
  close(in_fd);
  close(out_fd);

  const char *md = "# Config {#sec-config}\n\n"
                   "Strings\n\n"
                   ":   Strings are enclosed in double quotes, e.g.\n\n"
                   "    ```nix\n"
                   "    {\n"
                   "      networking.hostName = \"dexter\";\n"
                   "    }\n"
                   "    ```\n\n"
                   "    Special characters can be escaped (e.g. `\\\"`).\n\n"
                   "    See [](#sec-config) and [span]{#dd-span}.\n\n"
                   "Booleans\n\n"
                   ":   These can be `true` or `false`, e.g.\n\n"
                   "    ```nix\n"
                   "    {\n"
                   "      networking.firewall.enable = true;\n"
                   "    }\n"
                   "    ```\n\n"
                   "Nested\n\n"
                   ":   Outer def.\n\n"
                   "    InnerTerm\n\n"
                   "    :   Inner def.\n\n"
                   "Compact\n"
                   ":   First line.\n\n"
                   "    ::: {.note #dd-note}\n"
                   "    Note body.\n"
                   "    :::\n\n"
                   "TwoSpace\n\n"
                   ": First line.\n\n"
                   "  - item one\n"
                   "  - item two\n\n"
                   "Outside paragraph.\n\n"
                   "### Type {#sec-type}\n\n"
                   "    foldl :: a -> b\n";
  if (write_text_file(input_path, md) != 0)
    return 1;

  Array anchors;
  init_array(&anchors, 8);
  int anchor_rc = mmdoc_render_collect_anchors(input_path, &anchors);
  int found_dd_span = 0;
  int found_dd_note = 0;
  for (size_t i = 0; i < anchors.used; i++) {
    if (strcmp(anchors.array[i], "#dd-span") == 0)
      found_dd_span = 1;
    if (strcmp(anchors.array[i], "#dd-note") == 0)
      found_dd_note = 1;
  }
  free_array(&anchors);

  FILE *out = fopen(out_path, "w");
  if (out == NULL)
    return 1;
  AnchorLocationArray locations;
  init_anchor_location_array(&locations, 1);
  AnchorLocation al = {.anchor = "#sec-config",
                       .file_path = input_path,
                       .title = "Config",
                       .multipage_url = "config/"};
  insert_anchor_location_array(&locations, &al);
  build_anchor_location_index(&locations);
  int rc = mmdoc_render_part(input_path, out, RENDER_TYPE_SINGLE, &al,
                             locations, "", NULL);
  free_anchor_location_array(&locations);
  fclose(out);

  int passed =
      anchor_rc == 0 && found_dd_span && found_dd_note && rc == 1 &&
      !file_contains(out_path, "```nix") &&
      file_contains(out_path, "<pre><code class=\"language-nix\">{") &&
      file_contains(out_path, "  networking.hostName = &quot;dexter&quot;;") &&
      file_contains(out_path, "<p>Special characters can be escaped (e.g. "
                              "<code>\\&quot;</code>).</p>") &&
      file_contains(out_path, "<a href=\"#sec-config\">Config</a>") &&
      file_contains(out_path, "<span id='dd-span'>span</span>") &&
      file_contains(out_path, "<p>Booleans</p>") &&
      file_contains(out_path, "<p>InnerTerm</p>") &&
      file_contains(out_path, "<p>Inner def.</p>") &&
      file_contains_before(out_path, "<p>Outer def.</p>\n<dl>",
                           "<p>InnerTerm</p>") &&
      file_contains(out_path, "<div class='admonition note'>"
                              "<h3 id='dd-note' class='title'>") &&
      file_contains(out_path, "<li>item one</li>") &&
      file_contains_before(out_path, "</dd>\n</dl>\n<p>Outside paragraph.</p>",
                           "<pre><code>foldl :: a -&gt; b") &&
      file_contains(out_path, "<pre><code>foldl :: a -&gt; b");
  unlink(input_path);
  unlink(out_path);
  if (!passed) {
    printf("definition list continuations test failed\n");
    return 1;
  }
  printf("definition list continuations test passed\n");
  return 0;
}

static int test_markdown_engine_commonmark_and_gfm(void) {
  char input_path[] = "/tmp/mmdoc-cm-in-XXXXXX.md";
  char out_path[] = "/tmp/mmdoc-cm-out-XXXXXX.html";
  int in_fd = mkstemps(input_path, 3);
  int out_fd = mkstemps(out_path, 5);
  if (in_fd == -1 || out_fd == -1) {
    if (in_fd != -1) {
      close(in_fd);
      unlink(input_path);
    }
    if (out_fd != -1) {
      close(out_fd);
      unlink(out_path);
    }
    return 1;
  }
  close(in_fd);
  close(out_fd);

  const char *md =
      "Setext Title\n"
      "============\n\n"
      "## `opt.\\_name\\_` {#opt-a.\\_name\\_.b}\n\n"
      "Keep foo_bar_baz literal, *em*, **strong**, ***both***, `` `code` "
      "``.\n\n"
      "Entities: &mdash; &#65; &#x42; &#0; &copy;\n\n"
      "Auto: <https://example.org> and <user@example.org> and [Ref][r1].\n\n"
      "[r1]: https://nixos.org \"NixOS\"\n\n"
      "Intro line directly before table:\n"
      "| Left | Center | Right |\n"
      "| :--- | :----: | ----: |\n"
      "| `a\\|b` | x\\|y | 42 |\n\n"
      "::::{.example #ex-four}\n"
      "Outer example.\n\n"
      ":::{.note}\n"
      "Nested note.\n"
      ":::\n"
      "::::\n\n"
      "> Lazy quote line 1\n"
      "lazy line 2.\n\n"
      "- Tight outer 1\n"
      "- Tight outer 2\n"
      "  - Sub 1\n\n"
      "    Sub para 2\n";

  if (write_text_file(input_path, md) != 0) {
    unlink(input_path);
    unlink(out_path);
    return 1;
  }

  Array anchors;
  init_array(&anchors, 4);
  int arc = mmdoc_render_collect_anchors(input_path, &anchors);
  int has_unesc_opt = 0, has_ex4 = 0;
  for (size_t i = 0; i < anchors.used; i++) {
    if (strcmp(anchors.array[i], "#opt-a._name_.b") == 0)
      has_unesc_opt = 1;
    if (strcmp(anchors.array[i], "#ex-four") == 0)
      has_ex4 = 1;
  }
  free_array(&anchors);

  FILE *out = fopen(out_path, "w");
  if (out == NULL) {
    unlink(input_path);
    unlink(out_path);
    return 1;
  }
  AnchorLocationArray locs;
  init_anchor_location_array(&locs, 0);
  AnchorLocation al = {0};
  int rc = mmdoc_render_part(input_path, out, RENDER_TYPE_SINGLE, &al, locs, "",
                             NULL);
  free_anchor_location_array(&locs);
  fclose(out);

  int passed =
      arc == 0 && rc == 0 && has_unesc_opt && has_ex4 &&
      file_contains(out_path, "<h1>Setext Title</h1>") &&
      file_contains(out_path, "<h2 id='opt-a._name_.b'>") &&
      file_contains(out_path, "foo_bar_baz literal, <em>em</em>, "
                              "<strong>strong</strong>, "
                              "<em><strong>both</strong></em>, "
                              "<code>`code`</code>.") &&
      file_contains(out_path, "\xe2\x80\x94 A B \xef\xbf\xbd \xc2\xa9") &&
      file_contains(
          out_path,
          "<a href=\"https://example.org\">https://example.org</a>") &&
      file_contains(
          out_path,
          "<a href=\"mailto:user@example.org\">user@example.org</a>") &&
      file_contains(out_path,
                    "<a href=\"https://nixos.org\" title=\"NixOS\">Ref</a>") &&
      file_contains_before(out_path,
                           "<p>Intro line directly before table:</p>\n"
                           "<table>\n<thead>\n<tr>\n"
                           "<th align=\"left\">Left</th>\n"
                           "<th align=\"center\">Center</th>\n"
                           "<th align=\"right\">Right</th>",
                           "<td align=\"left\"><code>a|b</code></td>") &&
      file_contains(out_path, "<td align=\"center\">x|y</td>") &&
      file_contains(out_path, "<div class='admonition example'>"
                              "<h3 id='ex-four' class='title'>") &&
      file_contains(out_path, "<div class='admonition note'>"
                              "<h3 class='title'>Note</h3>") &&
      file_contains_before(out_path,
                           "<blockquote>\n<p>Lazy quote line 1\n"
                           "lazy line 2.</p>\n</blockquote>",
                           "<li>Tight outer 1</li>");

  unlink(input_path);
  unlink(out_path);
  if (!passed) {
    printf("markdown engine CommonMark and GFM test failed\n");
    return 1;
  }
  printf("markdown engine CommonMark and GFM test passed\n");
  return 0;
}

static int test_markdown_engine_linear_time_dos(void) {
  const size_t n = 50000;
  char *buf = malloc(n * 3 + 16);
  if (buf == NULL)
    return 1;

  struct timespec t0, t1;
  clock_gettime(CLOCK_MONOTONIC, &t0);

  /* 1. Unclosed '[' */
  memset(buf, '[', n);
  buf[n] = '\0';
  MdDoc doc;
  if (md_parse_buffer(&doc, buf, n, "dos-brackets.md", 1) != 0) {
    free(buf);
    return 1;
  }
  md_doc_free(&doc);

  /* 2. Unclosed backticks */
  for (size_t i = 0; i < n; i++) {
    buf[i * 2] = '`';
    buf[i * 2 + 1] = 'a';
  }
  if (md_parse_buffer(&doc, buf, n * 2, "dos-ticks.md", 0) != 0) {
    free(buf);
    return 1;
  }
  md_doc_free(&doc);

  /* 3. Unmatched emphasis openers */
  for (size_t i = 0; i < n; i++) {
    buf[i * 3] = '*';
    buf[i * 3 + 1] = 'a';
    buf[i * 3 + 2] = ' ';
  }
  if (md_parse_buffer(&doc, buf, n * 3, "dos-emph.md", 0) != 0) {
    free(buf);
    return 1;
  }
  md_doc_free(&doc);

  /* 4. Deeply nested blockquotes (exercises recursion depth cap) */
  for (size_t i = 0; i < 500; i++) {
    buf[i * 2] = '>';
    buf[i * 2 + 1] = ' ';
  }
  memcpy(buf + 1000, "deep", 4);
  if (md_parse_buffer(&doc, buf, 1004, "dos-quote.md", 0) != 0) {
    free(buf);
    return 1;
  }
  md_doc_free(&doc);

  clock_gettime(CLOCK_MONOTONIC, &t1);
  free(buf);

  double elapsed_ms = (double)(t1.tv_sec - t0.tv_sec) * 1000.0 +
                      (double)(t1.tv_nsec - t0.tv_nsec) / 1e6;
  if (elapsed_ms > 2000.0) {
    printf("linear-time DoS test too slow: %.1f ms\n", elapsed_ms);
    return 1;
  }
  printf("markdown engine linear-time DoS test passed (%.1f ms)\n", elapsed_ms);
  return 0;
}

static int assert_md_html_eq(const char *label, const char *md,
                             const char *expected) {
  MdDoc doc;
  if (md_parse_buffer(&doc, md, strlen(md), "spec.md", 0) != 0) {
    printf("spec test '%s': parse failed\n", label);
    return 1;
  }
  AnchorLocation chap = {.anchor = "#top",
                         .file_path = "spec.md",
                         .title = "Spec",
                         .multipage_url = "./",
                         .multipage_base_href = ""};
  AnchorLocationArray empty = {0};
  ChapterOutputs out = {0};
  int rc =
      mmdoc_render_chapter_from_mddoc(&doc, &chap, &empty, &empty, NULL, &out);
  int failed =
      (rc != 0 || out.single_html.len != strlen(expected) ||
       memcmp(out.single_html.data, expected, out.single_html.len) != 0);
  if (failed) {
    printf("spec test '%s' failed\n  expected: %s\n       got: %.*s\n", label,
           expected, (int)out.single_html.len,
           out.single_html.data ? out.single_html.data : "");
  }
  chapter_outputs_free(&out);
  md_doc_free(&doc);
  return failed;
}

static int test_markdown_engine_fuzz_spec_regressions(void) {
  int f = 0;
  /* Trailing tab before newline is stripped (never a hard break) */
  f += assert_md_html_eq("trailing-tab-nl", ")\t\n[", "<p>)\n[</p>\n");
  /* Empty list item cannot lazy-continue or span a blank line */
  f += assert_md_html_eq("empty-ol-no-lazy", "2.\nt",
                         "<ol start=\"2\">\n<li></li>\n</ol>\n<p>t</p>\n");
  f += assert_md_html_eq("empty-ol-blank", "1.\n\n   )",
                         "<ol>\n<li></li>\n</ol>\n<p>)</p>\n");
  /* Unclosed fenced code block at EOF terminates content with newline */
  f += assert_md_html_eq("fence-eof-nl", "```\n]",
                         "<pre><code>]\n</code></pre>\n");
  /* List marker tab-stop math & partial tab stripping */
  f += assert_md_html_eq("list-marker-tab",
                         "- \t=", "<ul>\n<li>=</li>\n</ul>\n");
  f += assert_md_html_eq("list-partial-tab", "*\n\tb",
                         "<ul>\n<li>b</li>\n</ul>\n");
  /* GFM table delim vs list marker & body rows without pipe / list stop */
  f += assert_md_html_eq("table-vs-list", "A | B\n- | -",
                         "<p>A | B</p>\n<ul>\n<li>| -</li>\n</ul>\n");
  f += assert_md_html_eq(
      "table-body-no-pipe", "A | B\n-- | --\nx\n3.",
      "<table>\n<thead>\n<tr>\n<th>A</th>\n<th>B</th>\n</tr>\n</thead>\n"
      "<tbody>\n<tr>\n<td>x</td>\n<td></td>\n</tr>\n</tbody>\n</table>\n"
      "<ol start=\"3\">\n<li></li>\n</ol>\n");
  f += assert_md_html_eq(
      "table-code-esc-pipe", "| H |\n| - |\n| `a\\|b` |",
      "<table>\n<thead>\n<tr>\n<th>H</th>\n</tr>\n</thead>\n"
      "<tbody>\n<tr>\n<td><code>a|b</code></td>\n</tr>\n</tbody>\n</table>\n");
  /* Lazy continuation lines cannot be setext underlines or table delims */
  f += assert_md_html_eq("lazy-no-setext-list",
                         "- a\n=", "<ul>\n<li>a\n=</li>\n</ul>\n");
  f += assert_md_html_eq("lazy-no-setext-quote",
                         ">a\n=", "<blockquote>\n<p>a\n=</p>\n</blockquote>\n");
  f += assert_md_html_eq("lazy-no-table", "2) a|b\n-|-",
                         "<ol start=\"2\">\n<li>a|b\n-|-</li>\n</ol>\n");
  /* Lazy continuation stops after empty nested containers & HTML blocks */
  f += assert_md_html_eq(
      "lazy-stop-empty-nested", "- *\nn",
      "<ul>\n<li>\n<ul>\n<li></li>\n</ul>\n</li>\n</ul>\n<p>n</p>\n");
  f += assert_md_html_eq(
      "lazy-stop-quote-list", ">*\n=",
      "<blockquote>\n<ul>\n<li></li>\n</ul>\n</blockquote>\n<p>=</p>\n");
  f += assert_md_html_eq("lazy-stop-html", "+ c\n<v>",
                         "<ul>\n<li>c</li>\n</ul>\n<v>\n");
  /* Tab expansion after '>' in blockquote */
  f += assert_md_html_eq("quote-tab", ">\t~",
                         "<blockquote>\n<p>~</p>\n</blockquote>\n");
  /* Short HTML comments <!--> and <!---> */
  f += assert_md_html_eq("short-comments", "a<!-->b<!--->",
                         "<p>a<!-->b<!---></p>\n");
  /* HTML block Type 4 requires uppercase ASCII after '<!' */
  f += assert_md_html_eq("html-type4-lower", "<!a", "<p>&lt;!a</p>\n");
  f += assert_md_html_eq("html-type4-upper", "<!DOCTYPE html>",
                         "<!DOCTYPE html>\n");
  if (f == 0)
    printf("markdown engine fuzz spec regressions test passed\n");
  return f ? 1 : 0;
}

int main(int argc, char *argv[]) {
  int num_failed = 0;
  int num_tests = 0;
  num_failed += test_render_io_and_man_cleanup();
  num_tests++;
  num_failed += test_parse_edge_cases();
  num_tests++;
  num_failed += test_admonition_blank_lines_and_safety();
  num_tests++;
  num_failed += test_definition_list_continuations();
  num_tests++;
  num_failed += test_refs_uses_markdown_ast();
  num_tests++;
  num_failed += test_single_dedup_and_link_titles();
  num_tests++;
  num_failed += test_mkdir_p_and_symlink_loop();
  num_tests++;
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
  num_failed += test_render("e013");
  num_tests++;
  num_failed += test_render("e014");
  num_tests++;
  num_failed += test_e015();
  num_tests++;
  num_failed += test_render("e016");
  num_tests++;
  num_failed += test_render("e017");
  num_tests++;
  num_failed += test_copy_nested_image();
  num_tests++;
  num_failed += test_zero_capacity_arrays();
  num_tests++;
  num_failed += test_anchor_location_index();
  num_tests++;
  num_failed += test_anchor_discovery_uses_markdown_ast();
  num_tests++;
  num_failed += test_multipage_navigation_anchor();
  num_tests++;
  num_failed += test_multipage_cached_toc_current_page_links();
  num_tests++;
  num_failed += test_multipage_duplicate_output_paths();
  num_tests++;
  num_failed += test_multipage_shared_assets_and_accessible_controls();
  num_tests++;
  num_failed += test_single_page_accessible_controls();
  num_tests++;
  num_failed += test_reject_overlong_ids();
  num_tests++;
  num_failed += test_markdown_engine_commonmark_and_gfm();
  num_tests++;
  num_failed += test_markdown_engine_linear_time_dos();
  num_tests++;
  num_failed += test_markdown_engine_fuzz_spec_regressions();
  num_tests++;

  printf("%d of %d tests passed.", num_tests - num_failed, num_tests);
  if (num_failed > 0)
    return 1;
  return 0;
}
