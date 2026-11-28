/* SPDX-License-Identifier: CC0-1.0 */
#include "../src/anchors.h"
#include "../src/asset.h"
#include "../src/build.h"
#include "../src/files.h"
#include "../src/md.h"
#include "../src/mkdir_p.h"
#include "../src/refs.h"
#include "../src/render.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include <testconfig.h>

static void rm_rf(const char *path) {
  struct stat st;
  if (lstat(path, &st) != 0)
    return;
  if (!S_ISDIR(st.st_mode)) {
    unlink(path);
    return;
  }
  DIR *dir = opendir(path);
  if (dir != nullptr) {
    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr) {
      if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
        continue;
      char child[PATH_MAX];
      snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
      rm_rf(child);
    }
    closedir(dir);
  }
  rmdir(path);
}

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
  init_anchor_location_array(&empty_anchor_locations, 0);
  int render_rc = mmdoc_render_part(input_path, got_file, RENDER_TYPE_SINGLE,
                                    empty_anchor_locations, "/");
  free_anchor_location_array(&empty_anchor_locations);
  fclose(got_file);

  int failed =
      render_rc < 0 || test_files_match(example, expected_path, got_file_path);
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
  int render_rc = mmdoc_render_part(input_path, got_file, RENDER_TYPE_MULTIPAGE,
                                    anchor_locations, "/");
  fclose(got_file);

  int failed =
      render_rc < 0 || test_files_match(example, expected_path, got_file_path);
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
  if (output == nullptr)
    return 1;

  AnchorLocationArray empty_anchor_locations;
  init_anchor_location_array(&empty_anchor_locations, 0);
  int got = mmdoc_render_part(input_path, output, RENDER_TYPE_SINGLE,
                              empty_anchor_locations, "/");
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

  MdDoc doc;
  if (md_parse_file(&doc, input_path, true) != 0) {
    printf("%s: parse failed\n", example);
    free(input_path);
    return 1;
  }

  if (strcmp(doc.title, expected_title) != 0) {
    printf("%s: title didn't match\n    expected: %s\n         "
           "got: %s\n",
           example, expected_title, doc.title);
    md_doc_free(&doc);
    free(input_path);
    return 1;
  }
  printf("%s title test passed\n", example);
  md_doc_free(&doc);
  free(input_path);
  return 0;
}

int file_matches_bytes(char *path, const unsigned char *expected,
                       size_t expected_size) {
  unsigned char got[16];
  if (expected_size > sizeof(got))
    return 0;

  FILE *file = fopen(path, "rb");
  if (file == nullptr)
    return 0;
  size_t got_size = fread(got, 1, sizeof(got), file);
  int close_result = fclose(file);
  return close_result == 0 && got_size == expected_size &&
         memcmp(got, expected, expected_size) == 0;
}

int test_copy_nested_image() {
  char root[] = "/tmp/mmdoc-image-test-XXXXXX";
  if (mkdtemp(root) == nullptr)
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
  if (source == nullptr)
    return 1;
  int write_failed = fwrite(image, 1, sizeof(image), source) != sizeof(image);
  if (fclose(source) != 0 || write_failed)
    return 1;

  Inputs inputs = {
      .src = src_dir,
      .out_multi = multi_dir,
      .out_single = single_dir,
  };
  Array img_files;
  init_array(&img_files, 16);
  int rc = mmdoc_collect_source_files(nullptr, &img_files, inputs);
  if (rc == 0)
    rc = mmdoc_copy_img_files(&img_files, inputs);
  free_array(&img_files);
  if (rc != 0) {
    rm_rf(root);
    return 1;
  }

  int matched = file_matches_bytes(multi_path, image, sizeof(image)) &&
                file_matches_bytes(single_path, image, sizeof(image));
  rm_rf(root);
  if (!matched)
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
  int passed =
      build_result == 0 && found_first != nullptr &&
      strcmp(found_first->title, "First") == 0 && found_second != nullptr &&
      strcmp(found_second->title, "Second") == 0 && not_found == nullptr;
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
  if (file == nullptr)
    return 1;
  const char *markdown = "# Page {#page}\n\n"
                         "`{#not-an-anchor}`\n\n"
                         "::: {.example #example-anchor}\nExample\n:::\n\n"
                         "[`lib`]{#lib-anchor}\n\n"
                         "[]{#first-alias} []{#second-alias}\n";
  int write_failed = fputs(markdown, file) == EOF || fclose(file) != 0;

  MdDoc doc;
  int result = write_failed ? -1 : md_parse_file(&doc, path, true);
  const char *expected[] = {"#page", "#example-anchor", "#lib-anchor",
                            "#first-alias", "#second-alias"};
  int passed = result == 0 && doc.title != nullptr &&
               strcmp(doc.title, "Page") == 0 && doc.anchors.used == 5;
  for (size_t i = 0; passed && i < 5; i++)
    passed = strcmp(doc.anchors.array[i], expected[i]) == 0;
  if (result == 0)
    md_doc_free(&doc);
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
  if (file == nullptr)
    return 0;

  char *line = nullptr;
  size_t line_size = 0;
  int found = 0;
  while (getline(&line, &line_size, file) != -1) {
    if (strstr(line, expected) != nullptr) {
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
  if (file == nullptr)
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
  if (contents == nullptr) {
    fclose(file);
    return 0;
  }
  size_t bytes_read = fread(contents, 1, (size_t)file_size, file);
  contents[bytes_read] = '\0';
  char *first_position = strstr(contents, first);
  char *second_position = strstr(contents, second);
  int found = first_position != nullptr && second_position != nullptr &&
              first_position < second_position;
  free(contents);
  fclose(file);
  return found;
}

int file_exists_and_is_not_empty(char *path) {
  FILE *file = fopen(path, "rb");
  if (file == nullptr)
    return 0;
  int first = fgetc(file);
  fclose(file);
  return first != EOF;
}

int find_asset_name(char *dir, const char *prefix, const char *suffix,
                    char *result, size_t result_size) {
  DIR *directory = opendir(dir);
  if (directory == nullptr)
    return 0;
  struct dirent *entry;
  int found = 0;
  while ((entry = readdir(directory)) != nullptr) {
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

int write_text_file(char *path, const char *contents) {
  FILE *file = fopen(path, "w");
  if (file == nullptr)
    return 1;
  int failed = fputs(contents, file) == EOF;
  return fclose(file) != 0 || failed;
}

static int run_mmdoc_build(const char *project_name, const char *src_dir,
                           const char *out_dir) {
  char *argv[] = {(char *)"mmdoc", (char *)project_name, (char *)src_dir,
                  (char *)out_dir};
  Inputs inputs = {};
  if (mmdoc_inputs_derive(&inputs, argv) != 0)
    return -1;
  int rc = mmdoc_build(inputs);
  mmdoc_inputs_free(&inputs);
  return rc;
}

int test_multipage_shared_assets_and_accessible_controls() {
  char root[] = "/tmp/mmdoc-assets-test-XXXXXX";
  if (mkdtemp(root) == nullptr)
    return 1;

  char src_dir[PATH_MAX], out1_dir[PATH_MAX], out2_dir[PATH_MAX];
  char page_path[PATH_MAX], toc_path[PATH_MAX];
  char multi1[PATH_MAX], multi2[PATH_MAX], index1[PATH_MAX], index2[PATH_MAX];
  snprintf(src_dir, sizeof(src_dir), "%s/src", root);
  snprintf(out1_dir, sizeof(out1_dir), "%s/out1", root);
  snprintf(out2_dir, sizeof(out2_dir), "%s/out2", root);
  snprintf(page_path, sizeof(page_path), "%s/page.md", src_dir);
  snprintf(toc_path, sizeof(toc_path), "%s/toc.md", src_dir);
  snprintf(multi1, sizeof(multi1), "%s/multi", out1_dir);
  snprintf(multi2, sizeof(multi2), "%s/multi", out2_dir);
  snprintf(index1, sizeof(index1), "%s/index.html", multi1);
  snprintf(index2, sizeof(index2), "%s/index.html", multi2);
  if (mkdir_p(src_dir) != 0 ||
      write_text_file(page_path, "# Page {#page}\n\n```c\nint x;\n```\n") !=
          0 ||
      write_text_file(toc_path, "* [](#page)\n") != 0) {
    rm_rf(root);
    return 1;
  }

  int render_result = run_mmdoc_build("Project", src_dir, out1_dir);
  char search_index_name[ASSET_FILE_NAME_SIZE] = "";
  int search_index_found =
      find_asset_name(multi1, "search_index.", ".js", search_index_name,
                      sizeof(search_index_name));
  char first_search_index_name[ASSET_FILE_NAME_SIZE];
  snprintf(first_search_index_name, sizeof(first_search_index_name), "%s",
           search_index_name);
  int assets_found = search_index_found;
  for (AssetId id = 0; id < ASSET_COUNT; id++) {
    char asset_path[PATH_MAX];
    snprintf(asset_path, sizeof(asset_path), "%s/%s", multi1,
             asset_file_name(id));
    assets_found &= file_exists_and_is_not_empty(asset_path);
  }

  int names_are_hashed =
      asset_name_has_hash(asset_file_name(ASSET_A11Y_DARK_CSS), "a11y-dark",
                          "css") &&
      asset_name_has_hash(asset_file_name(ASSET_A11Y_LIGHT_CSS), "a11y-light",
                          "css") &&
      asset_name_has_hash(asset_file_name(ASSET_MMDOC_CSS), "mmdoc", "css") &&
      asset_name_has_hash(asset_file_name(ASSET_MMDOC_JS), "mmdoc", "js") &&
      asset_name_has_hash(asset_file_name(ASSET_MMDOC_SEARCH_JS),
                          "mmdoc_search", "js") &&
      asset_name_has_hash(asset_file_name(ASSET_FUSE_BASIC_MIN_JS),
                          "fuse.basic.min", "js") &&
      asset_name_has_hash(asset_file_name(ASSET_HIGHLIGHT_PACK_JS),
                          "highlight.pack", "js") &&
      asset_name_has_hash(search_index_name, "search_index", "js");
  int page_is_external =
      file_contains(index1, asset_file_name(ASSET_MMDOC_CSS)) &&
      file_contains(index1, asset_file_name(ASSET_MMDOC_JS)) &&
      file_contains(index1, asset_file_name(ASSET_MMDOC_SEARCH_JS)) &&
      file_contains(index1, asset_file_name(ASSET_HIGHLIGHT_PACK_JS)) &&
      file_contains(index1, search_index_name) &&
      file_contains(index1, asset_file_name(ASSET_FUSE_BASIC_MIN_JS)) &&
      !file_contains(index1, "href='mmdoc.css'") &&
      !file_contains(index1, "src='mmdoc.js'") &&
      !file_contains(index1, "<style>") &&
      !file_contains(index1, "Highlight.js 10.7.1");
  char search_script_path[PATH_MAX];
  snprintf(search_script_path, sizeof(search_script_path), "%s/%s", multi1,
           asset_file_name(ASSET_MMDOC_SEARCH_JS));
  int search_is_lazy =
      file_contains(search_script_path,
                    "loadSearchScript(searchIndexSource)") &&
      file_contains(search_script_path, "loadSearchScript(fuseSource)") &&
      file_contains(search_script_path, "window.mmdocSearchCorpus") &&
      file_contains(search_script_path, "limit: searchResultLimit");
  int page_is_accessible =
      file_contains(index1, "viewport-fit=cover") &&
      file_contains(index1, "mobile-top-nav") &&
      file_contains(index1, "<header class='nav-top-container'>") &&
      file_contains(index1, "class='skip-link' href='./#main-content'") &&
      file_contains(index1, "aria-controls='sidebar'") &&
      file_contains(index1, "aria-label='Open search'") &&
      file_contains(index1, "role='status' aria-live='polite'") &&
      file_contains(index1, "<main id='main-content' tabindex='-1'>");
  int theme_is_initialized_before_css =
      file_contains_before(index1, "localStorage.getItem('theme')",
                           asset_file_name(ASSET_MMDOC_CSS)) &&
      file_contains(index1, "document.documentElement.classList.add(theme + "
                            "'-theme')");
  char css_path[PATH_MAX];
  snprintf(css_path, sizeof(css_path), "%s/%s", multi1,
           asset_file_name(ASSET_MMDOC_CSS));
  int top_anchor_no_jump =
      file_contains(
          css_path,
          "scroll-margin-top: calc(var(--main-padding-top) + 1rem);") &&
      !file_contains(css_path, "scroll-behavior: smooth;");

  int no_code_render_result = 1;
  int highlighter_is_conditional = 0;
  int corpus_hash_changed = 0;
  if (write_text_file(page_path, "# Page {#page}\n") == 0) {
    no_code_render_result = run_mmdoc_build("Project", src_dir, out2_dir);
    char highlighter_path[PATH_MAX];
    snprintf(highlighter_path, sizeof(highlighter_path), "%s/%s", multi2,
             asset_file_name(ASSET_HIGHLIGHT_PACK_JS));
    highlighter_is_conditional =
        !file_exists_and_is_not_empty(highlighter_path) &&
        !file_contains(index2, asset_file_name(ASSET_HIGHLIGHT_PACK_JS));
    if (find_asset_name(multi2, "search_index.", ".js", search_index_name,
                        sizeof(search_index_name))) {
      corpus_hash_changed =
          strcmp(first_search_index_name, search_index_name) != 0;
    }
  }

  rm_rf(root);

  if (render_result != 0 || no_code_render_result != 0 || !assets_found ||
      !names_are_hashed || !page_is_external || !page_is_accessible ||
      !theme_is_initialized_before_css || !search_is_lazy ||
      !top_anchor_no_jump || !corpus_hash_changed ||
      !highlighter_is_conditional) {
    printf("multipage shared asset and accessibility test failed\n");
    return 1;
  }
  printf("multipage shared asset and accessibility test passed\n");
  return 0;
}

int test_single_page_accessible_controls() {
  char root[] = "/tmp/mmdoc-single-accessibility-test-XXXXXX";
  if (mkdtemp(root) == nullptr)
    return 1;

  char src_dir[PATH_MAX], out_dir[PATH_MAX], toc_path[PATH_MAX],
      page_path[PATH_MAX], index_path[PATH_MAX];
  snprintf(src_dir, sizeof(src_dir), "%s/src", root);
  snprintf(out_dir, sizeof(out_dir), "%s/out", root);
  snprintf(toc_path, sizeof(toc_path), "%s/toc.md", src_dir);
  snprintf(page_path, sizeof(page_path), "%s/page.md", src_dir);
  snprintf(index_path, sizeof(index_path), "%s/single/index.html", out_dir);
  if (mkdir_p(src_dir) != 0 ||
      write_text_file(toc_path, "* [](#page)\n") != 0 ||
      write_text_file(page_path, "# Page {#page}\n") != 0) {
    rm_rf(root);
    return 1;
  }

  int render_result = run_mmdoc_build("Project", src_dir, out_dir);
  int controls_found = file_contains(index_path, "<html lang='en'>") &&
                       file_contains(index_path, "viewport-fit=cover") &&
                       file_contains(index_path, "aria-controls='sidebar'") &&
                       file_contains(index_path, "aria-pressed='false'") &&
                       file_contains(index_path, "id='main-content'");
  int theme_is_initialized_before_css = file_contains_before(
      index_path, "localStorage.getItem('theme')", "<style>");

  rm_rf(root);

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
  if (mkdtemp(root) == nullptr)
    return 1;

  char src_dir[PATH_MAX], out_dir[PATH_MAX], toc_path[PATH_MAX];
  char p1[PATH_MAX], p2[PATH_MAX], p3[PATH_MAX], mid_html[PATH_MAX];
  snprintf(src_dir, sizeof(src_dir), "%s/src", root);
  snprintf(out_dir, sizeof(out_dir), "%s/out", root);
  snprintf(toc_path, sizeof(toc_path), "%s/toc.md", src_dir);
  snprintf(p1, sizeof(p1), "%s/preface.md", src_dir);
  snprintf(p2, sizeof(p2), "%s/middle.md", src_dir);
  snprintf(p3, sizeof(p3), "%s/overview.md", src_dir);
  snprintf(mid_html, sizeof(mid_html), "%s/multi/middle/index.html", out_dir);

  if (mkdir_p(src_dir) != 0 ||
      write_text_file(toc_path, "* [](#preface)\n* [](#middle)\n* "
                                "[](#overview-of-nixpkgs)\n") != 0 ||
      write_text_file(p1, "# Preface {#preface}\n") != 0 ||
      write_text_file(p2, "# Middle {#middle}\n") != 0 ||
      write_text_file(p3, "# Overview of Nixpkgs {#overview-of-nixpkgs}\n") !=
          0) {
    rm_rf(root);
    return 1;
  }

  int render_result = run_mmdoc_build("Nixpkgs", src_dir, out_dir);
  int found_previous =
      file_contains(mid_html,
                    "id='chapter-previous-button' class='chapter-previous' "
                    "href='./#preface'") &&
      file_contains(mid_html, "class='chapter-pagination-link "
                              "chapter-pagination-previous' href='./#preface'");
  int found_next =
      file_contains(mid_html, "id='chapter-next-button' class='chapter-next' "
                              "href='overview/#overview-of-nixpkgs'") &&
      file_contains(mid_html, "class='chapter-pagination-link "
                              "chapter-pagination-next' "
                              "href='overview/#overview-of-nixpkgs'");

  rm_rf(root);

  if (render_result != 0 || !found_previous || !found_next) {
    printf("multipage navigation anchor test failed\n");
    return 1;
  }
  printf("multipage navigation anchor test passed\n");
  return 0;
}

int test_multipage_cached_toc_current_page_links() {
  char root[] = "/tmp/mmdoc-cached-toc-test-XXXXXX";
  if (mkdtemp(root) == nullptr)
    return 1;

  char src_dir[PATH_MAX], out_dir[PATH_MAX], toc_path[PATH_MAX];
  char first_src[PATH_MAX], second_src[PATH_MAX];
  char first_out[PATH_MAX], second_out[PATH_MAX];
  snprintf(src_dir, sizeof(src_dir), "%s/src", root);
  snprintf(out_dir, sizeof(out_dir), "%s/out", root);
  snprintf(toc_path, sizeof(toc_path), "%s/toc.md", src_dir);
  snprintf(first_src, sizeof(first_src), "%s/first.md", src_dir);
  snprintf(second_src, sizeof(second_src), "%s/second.md", src_dir);
  snprintf(first_out, sizeof(first_out), "%s/multi/index.html", out_dir);
  snprintf(second_out, sizeof(second_out), "%s/multi/second/index.html",
           out_dir);

  if (mkdir_p(src_dir) != 0 ||
      write_text_file(toc_path, "# Contents {#contents}\n\n* [](#first)\n* "
                                "[](#second)\n") != 0 ||
      write_text_file(first_src, "# First {#first}\n") != 0 ||
      write_text_file(second_src, "# Second {#second}\n") != 0) {
    rm_rf(root);
    return 1;
  }

  int render_result = run_mmdoc_build("Project", src_dir, out_dir);

  int links_are_page_specific =
      file_contains(first_out, "<h1 id='contents'><a href='./#contents'>") &&
      file_contains(second_out,
                    "<h1 id='contents'><a href='second/#contents'>");
  int toc_links_are_resolved =
      file_contains(first_out, "href='./#first'") &&
      file_contains(first_out, "href='second/#second'") &&
      file_contains(second_out, "href='./#first'") &&
      file_contains(second_out, "href='second/#second'");

  rm_rf(root);

  if (render_result != 0 || !links_are_page_specific ||
      !toc_links_are_resolved) {
    printf("multipage cached TOC test failed\n");
    return 1;
  }
  printf("multipage cached TOC test passed\n");
  return 0;
}

int test_multipage_duplicate_output_paths() {
  char root[] = "/tmp/mmdoc-duplicate-output-test-XXXXXX";
  if (mkdtemp(root) == nullptr)
    return 1;

  char src_dir[PATH_MAX], out_dir[PATH_MAX], toc_path[PATH_MAX],
      source_path[PATH_MAX], multi_dir[PATH_MAX], output_path[PATH_MAX];
  snprintf(src_dir, sizeof(src_dir), "%s/src", root);
  snprintf(out_dir, sizeof(out_dir), "%s/out", root);
  snprintf(toc_path, sizeof(toc_path), "%s/toc.md", src_dir);
  snprintf(source_path, sizeof(source_path), "%s/page.md", src_dir);
  snprintf(multi_dir, sizeof(multi_dir), "%s/multi", out_dir);
  snprintf(output_path, sizeof(output_path), "%s/index.html", multi_dir);
  if (mkdir_p(src_dir) != 0 ||
      write_text_file(toc_path, "* [](#first)\n* [](#second)\n") != 0 ||
      write_text_file(source_path,
                      "# First {#first}\n\n## Second {#second}\n") != 0) {
    rm_rf(root);
    return 1;
  }

  int render_result = run_mmdoc_build("Project", src_dir, out_dir);

  char search_index_name[ASSET_FILE_NAME_SIZE];
  int search_index_found =
      find_asset_name(multi_dir, "search_index.", ".js", search_index_name,
                      sizeof(search_index_name));
  int search_index_replaced =
      search_index_found && file_contains(output_path, search_index_name) &&
      !file_contains(output_path, "search_index.0000000000000000.js");
  int page_navigation_is_unique =
      !file_contains(output_path, "chapter-previous-button") &&
      !file_contains(output_path, "chapter-next-button");

  rm_rf(root);

  if (render_result != 0 || !search_index_replaced ||
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
  if (file == nullptr) {
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

  MdDoc doc;
  int anchors_result = md_parse_file(&doc, anchor_path, true);
  if (anchors_result == 0)
    md_doc_free(&doc);
  unlink(anchor_path);

  if (refs_result == 0 || anchors_result == 0) {
    printf("overlong anchor rejection test failed\n");
    return 1;
  }
  printf("overlong anchors rejected\n");
  return 0;
}

static int test_render_io_cleanup(void) {
  AnchorLocationArray locations = {};
  FILE *output = tmpfile();
  if (output == nullptr)
    return 1;
  char missing[] = "/dev/null/missing.md";
  int failed = mmdoc_render_part(missing, output, RENDER_TYPE_SINGLE, locations,
                                 "") != -1;
  MdDoc doc;
  if (md_parse_file(&doc, missing, true) == 0) {
    md_doc_free(&doc);
    failed = 1;
  }
  fclose(output);
  output = fopen("/dev/full", "w");
  if (output != nullptr) {
    setvbuf(output, nullptr, _IONBF, 0);
    failed |= mmdoc_render_part(TEST_EXAMPLE_DIR "e001/input.md", output,
                                RENDER_TYPE_SINGLE, locations, "") != -1;
    fclose(output);
  }
  return failed;
}

static int assert_md_html_eq(const char *label, const char *md,
                             const char *expected);

static int test_md_anchor_and_admonition_edge_cases(void) {
  int f = 0;
  f += assert_md_html_eq(
      "heading-two-spaces", "# Heading  {#two-spaces}\n",
      "<h1 id='two-spaces'><a href='#two-spaces'>\nHeading\n</a></h1>\n");
  f += assert_md_html_eq("heading-empty-id", "# Heading {#}\n",
                         "<h1>Heading {#}</h1>\n");
  f += assert_md_html_eq(
      "heading-after-braces", "# Heading {foo} {#valid}\n",
      "<h1 id='valid'><a href='#valid'>\nHeading {foo}\n</a></h1>\n");
  f += assert_md_html_eq("consecutive-bracket-span", "[foo][bar]{#id}\n",
                         "<p>[foo]<span id='id'>bar</span></p>\n");
  f += assert_md_html_eq("nested-bracket-span", "[[nested]{#nid}\n",
                         "<p>[<span id='nid'>nested</span></p>\n");
  f += assert_md_html_eq("empty-span-id", "[foo]{#}\n", "<p>[foo]{#}</p>\n");
  f += assert_md_html_eq("span-after-non-id-brace", "[a]{b}[c]{#ok}\n",
                         "<p>[a]{b}<span id='ok'>c</span></p>\n");
  if (f == 0)
    printf("md anchor and admonition edge cases test passed\n");
  return f ? 1 : 0;
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
  if (out == nullptr)
    return 1;
  AnchorLocationArray empty;
  init_anchor_location_array(&empty, 0);
  int rc = mmdoc_render_part(input_path, out, RENDER_TYPE_SINGLE, empty, "");
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
  if (mkdtemp(root) == nullptr)
    return 1;

  char src_dir[PATH_MAX], out_dir[PATH_MAX], toc_path[PATH_MAX],
      p1_path[PATH_MAX], p2_path[PATH_MAX], index_path[PATH_MAX];
  snprintf(src_dir, sizeof(src_dir), "%s/src", root);
  snprintf(out_dir, sizeof(out_dir), "%s/out", root);
  snprintf(toc_path, sizeof(toc_path), "%s/toc.md", src_dir);
  snprintf(p1_path, sizeof(p1_path), "%s/p1.md", src_dir);
  snprintf(p2_path, sizeof(p2_path), "%s/p2.md", src_dir);
  snprintf(index_path, sizeof(index_path), "%s/single/index.html", out_dir);

  if (mkdir_p(src_dir) != 0 ||
      write_text_file(toc_path, "* [](#first)\n* [](#sub)\n* [](#second)\n") !=
          0 ||
      write_text_file(p1_path,
                      "# First & \\<One\\> {#first}\n\n## Sub {#sub}\n\n"
                      "See [](#second).\n") != 0 ||
      write_text_file(p2_path, "# Second's Page {#second}\n\nBody 2.\n") != 0) {
    rm_rf(root);
    return 1;
  }

  int rc = run_mmdoc_build("Proj <1> & '2'", src_dir, out_dir);

  /* Count occurrences of id='first' to confirm p1 was rendered only once. */
  int first_count = 0;
  FILE *f = fopen(index_path, "r");
  if (f != nullptr) {
    char *line = nullptr;
    size_t sz = 0;
    while (getline(&line, &sz, f) != -1) {
      if (strstr(line, "<h1 id='first'>") != nullptr)
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

  rm_rf(root);

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
  if (mkdtemp(root) == nullptr)
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
  Inputs inputs = {.src = root, .out_multi = nullptr, .out_single = nullptr};
  int walk_rc = mmdoc_collect_source_files(&md_files, nullptr, inputs);
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

  MdDoc doc;
  int anchor_rc = md_parse_file(&doc, input_path, true);
  int found_dd_span = 0;
  int found_dd_note = 0;
  if (anchor_rc == 0) {
    for (size_t i = 0; i < doc.anchors.used; i++) {
      if (strcmp(doc.anchors.array[i], "#dd-span") == 0)
        found_dd_span = 1;
      if (strcmp(doc.anchors.array[i], "#dd-note") == 0)
        found_dd_note = 1;
    }
    md_doc_free(&doc);
  }

  FILE *out = fopen(out_path, "w");
  if (out == nullptr)
    return 1;
  AnchorLocationArray locations;
  init_anchor_location_array(&locations, 1);
  AnchorLocation al = {.anchor = "#sec-config",
                       .file_path = input_path,
                       .title = "Config",
                       .multipage_url = "config/"};
  insert_anchor_location_array(&locations, &al);
  int index_rc = build_anchor_location_index(&locations);
  int rc = index_rc == 0 ? mmdoc_render_part(input_path, out,
                                             RENDER_TYPE_SINGLE, locations, "")
                         : -1;
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

  MdDoc doc;
  int arc = md_parse_file(&doc, input_path, true);
  int has_unesc_opt = 0, has_ex4 = 0;
  if (arc == 0) {
    for (size_t i = 0; i < doc.anchors.used; i++) {
      if (strcmp(doc.anchors.array[i], "#opt-a._name_.b") == 0)
        has_unesc_opt = 1;
      if (strcmp(doc.anchors.array[i], "#ex-four") == 0)
        has_ex4 = 1;
    }
    md_doc_free(&doc);
  }

  FILE *out = fopen(out_path, "w");
  if (out == nullptr) {
    unlink(input_path);
    unlink(out_path);
    return 1;
  }
  AnchorLocationArray locs;
  init_anchor_location_array(&locs, 0);
  int rc = mmdoc_render_part(input_path, out, RENDER_TYPE_SINGLE, locs, "");
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
  if (buf == nullptr)
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
  AnchorLocationArray empty = {};
  ChapterOutputs out = {};
  int rc = mmdoc_render_chapter_from_mddoc(&doc, &chap, &empty, &empty, nullptr,
                                           &out);
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
  num_failed += test_render_io_cleanup();
  num_tests++;
  num_failed += test_md_anchor_and_admonition_edge_cases();
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
