/* SPDX-License-Identifier: CC0-1.0 */
/* Exercise the published Markdown, HTML, and CLI behavior of mmdoc. */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static char *join_path(const char *a, const char *b) {
  size_t la = strlen(a), lb = strlen(b);
  char *out = malloc(la + 1 + lb + 1);
  if (out == nullptr) {
    perror("malloc");
    exit(1);
  }
  memcpy(out, a, la);
  out[la] = '/';
  memcpy(out + la + 1, b, lb + 1);
  return out;
}

static void write_file_bytes(const char *path, const char *data, size_t len,
                             bool append) {
  FILE *f = fopen(path, append ? "ab" : "wb");
  if (f == nullptr) {
    perror(path);
    exit(1);
  }
  if (len > 0 && fwrite(data, 1, len, f) != len) {
    perror(path);
    fclose(f);
    exit(1);
  }
  if (fclose(f) != 0) {
    perror(path);
    exit(1);
  }
}

static void write_text(const char *path, const char *text) {
  write_file_bytes(path, text, strlen(text), false);
}

static char *read_text(const char *path) {
  FILE *f = fopen(path, "rb");
  if (f == nullptr) {
    fprintf(stderr, "Failed to open %s: %s\n", path, strerror(errno));
    exit(1);
  }
  if (fseek(f, 0, SEEK_END) != 0) {
    perror(path);
    fclose(f);
    exit(1);
  }
  long sz = ftell(f);
  if (sz < 0 || fseek(f, 0, SEEK_SET) != 0) {
    perror(path);
    fclose(f);
    exit(1);
  }
  char *buf = malloc((size_t)sz + 1);
  if (buf == nullptr) {
    perror("malloc");
    fclose(f);
    exit(1);
  }
  if (sz > 0 && fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
    perror(path);
    free(buf);
    fclose(f);
    exit(1);
  }
  buf[sz] = '\0';
  fclose(f);
  return buf;
}

static bool path_exists(const char *path) {
  struct stat st;
  return lstat(path, &st) == 0;
}

static bool path_is_file(const char *path) {
  struct stat st;
  return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static void rm_rf(const char *path) {
  struct stat st;
  if (lstat(path, &st) != 0)
    return;
  if (S_ISDIR(st.st_mode)) {
    DIR *d = opendir(path);
    if (d != nullptr) {
      struct dirent *de;
      while ((de = readdir(d)) != nullptr) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
          continue;
        char *child = join_path(path, de->d_name);
        rm_rf(child);
        free(child);
      }
      closedir(d);
    }
    rmdir(path);
  } else {
    unlink(path);
  }
}

static int run_cmd(const char *cwd, char *const argv[], bool quiet_stdout,
                   bool quiet_stderr) {
  pid_t pid = fork();
  if (pid < 0) {
    perror("fork");
    exit(1);
  }
  if (pid == 0) {
    if (cwd != nullptr && chdir(cwd) != 0) {
      perror(cwd);
      _exit(127);
    }
    if (quiet_stdout || quiet_stderr) {
      int devnull = open("/dev/null", O_WRONLY);
      if (devnull >= 0) {
        if (quiet_stdout)
          dup2(devnull, STDOUT_FILENO);
        if (quiet_stderr)
          dup2(devnull, STDERR_FILENO);
        close(devnull);
      }
    }
    execv(argv[0], argv);
    perror(argv[0]);
    _exit(127);
  }
  int status = 0;
  if (waitpid(pid, &status, 0) < 0) {
    perror("waitpid");
    exit(1);
  }
  if (WIFEXITED(status))
    return WEXITSTATUS(status);
  return 128;
}

static size_t count_substr(const char *haystack, const char *needle) {
  size_t count = 0;
  size_t nlen = strlen(needle);
  if (nlen == 0)
    return 0;
  const char *p = haystack;
  while ((p = strstr(p, needle)) != nullptr) {
    count++;
    p += nlen;
  }
  return count;
}

static int find_matching_files(const char *dir_path, const char *prefix,
                               const char *suffix, char *first_out,
                               size_t first_cap) {
  DIR *d = opendir(dir_path);
  if (d == nullptr)
    return -1;
  size_t plen = strlen(prefix);
  size_t slen = strlen(suffix);
  int count = 0;
  struct dirent *de;
  while ((de = readdir(d)) != nullptr) {
    size_t nlen = strlen(de->d_name);
    if (nlen >= plen + slen && strncmp(de->d_name, prefix, plen) == 0 &&
        strcmp(de->d_name + nlen - slen, suffix) == 0) {
      if (count == 0 && first_out != nullptr) {
        snprintf(first_out, first_cap, "%s/%s", dir_path, de->d_name);
      }
      count++;
    }
  }
  closedir(d);
  return count;
}

#define CHECK(cond, msg)                                                       \
  do {                                                                         \
    if (!(cond)) {                                                             \
      fprintf(stderr, "FAIL (%s:%d): %s\n", __FILE__, __LINE__, (msg));        \
      failed = 1;                                                              \
      goto cleanup;                                                            \
    }                                                                          \
  } while (0)

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s <mmdoc-binary>\n", argv[0]);
    return 1;
  }

  char mmdoc_bin[PATH_MAX];
  if (realpath(argv[1], mmdoc_bin) == nullptr) {
    perror(argv[1]);
    return 1;
  }

  char tmpdir[] = "/tmp/mmdoc-cli-test-XXXXXX";
  if (mkdtemp(tmpdir) == nullptr) {
    perror("mkdtemp");
    return 1;
  }

  int failed = 0;
  char *assets_dir = join_path(tmpdir, "images");
  char *diagram_path = join_path(assets_dir, "diagram.png");
  char *guide_dir = join_path(tmpdir, "guide");
  char *toc_path = join_path(tmpdir, "toc.md");
  char *intro_path = join_path(tmpdir, "intro.md");
  char *tips_src = join_path(guide_dir, "tips #1.md");
  char *private_src = join_path(tmpdir, "private.md");

  char *build_dir = join_path(tmpdir, "build");
  char *out_dir = join_path(build_dir, "multi");
  char *single_html_path = join_path(build_dir, "single/index.html");
  char *llms_txt_path = join_path(out_dir, "llms.txt");
  char *llms_full_path = join_path(out_dir, "llms-full.txt");
  char *root_md_path = join_path(out_dir, "index.md");
  char *root_html_path = join_path(out_dir, "index.html");
  char *tips_md_path = join_path(out_dir, "guide/tips #1/index.md");
  char *tips_html_path = join_path(out_dir, "guide/tips #1/index.html");
  char *private_out_dir = join_path(out_dir, "private");
  char *private_out_md = join_path(out_dir, "private/index.md");
  char *nested_build_out = join_path(out_dir, "build");

  char *index_text = nullptr;
  char *root_md = nullptr;
  char *tips_md = nullptr;
  char *full_md = nullptr;
  char *tips_html = nullptr;
  char *root_html = nullptr;
  char *search_text = nullptr;
  char *single_html = nullptr;
  char *full_after = nullptr;
  char *index_after = nullptr;

  CHECK(mkdir(assets_dir, 0755) == 0, "mkdir images");
  write_file_bytes(diagram_path, "diagram", 7, false);
  CHECK(mkdir(guide_dir, 0755) == 0, "mkdir guide");

  write_text(toc_path,
             "* [](#start)\n* [](#tip)\n* [](#root-alias)\n* [](#warning)\n");
  write_text(
      intro_path,
      "# Welcome {#start}\n\n[Root]{#root-alias}\n\nSee [](#warning).\n");
  write_text(
      tips_src,
      "# `Tips` {#tip}\n\n[Return][home] or [stay](#term).\n\n"
      "[home]: #root-alias\n\n"
      "[`value`]{#term}\n\n::: {.warning #warning}\nKeep a backup.\n:::\n\n"
      "* Run `help`.\n\nTerm\n: Meaning\n\n| Key | Value |\n| --- | --- |\n"
      "| a | 42 |\n\n"
      "Multi\n\n:   Lead paragraph.\n\n    ```nix\n    { ok = true; }\n"
      "    ```\n\n"
      "    Back to [](#start).\n\n"
      "![Diagram](images/diagram.png) and [example](https://example.org/a#b)"
      "\n\n"
      "```sh\nprintf '%s\\n' '[literal](#start)'\n```\n");
  write_text(private_src, "# Unlisted {#hidden}\n\nUnlisted chapter.\n");

  char *run_args[] = {mmdoc_bin, "My Manual", ".", "build", nullptr};
  CHECK(run_cmd(tmpdir, run_args, false, false) == 0, "mmdoc initial build");

  index_text = read_text(llms_txt_path);
  const char *expected_index =
      "# My Manual\n\n[Full manual](llms-full.txt)\n\n## Chapters\n\n"
      "- [Welcome](index.md)\n"
      "- [Tips](guide/tips%20%231/index.md)\n";
  CHECK(strcmp(index_text, expected_index) == 0, "llms.txt exact content");

  root_md = read_text(root_md_path);
  tips_md = read_text(tips_md_path);
  full_md = read_text(llms_full_path);

  CHECK(strstr(root_md, "[Tips](guide/tips%20%231/index.md#warning)") !=
            nullptr,
        "root cross-chapter link");
  CHECK(strstr(tips_md, "[Return](../../index.md#root-alias)") != nullptr,
        "tips relative return link");
  CHECK(strstr(full_md, "[Return](#root-alias)") != nullptr &&
            strstr(full_md, "[Tips](#warning)") != nullptr,
        "full local anchor links");
  CHECK(strstr(tips_md, "![Diagram](../../images/diagram.png)") != nullptr,
        "tips relative image path");
  CHECK(strstr(full_md, "![Diagram](images/diagram.png)") != nullptr,
        "full image path");

  const char *snippets[] = {
      "# `Tips` {#tip}",
      "[stay](#term)",
      "[`value`]{#term}",
      "::: {.warning #warning}\nKeep a backup.\n:::",
      "Run `help`.",
      "Term\n: Meaning",
      "| a | 42 |",
      "[example](https://example.org/a#b)",
      "printf '%s\\n' '[literal](#start)'",
      "``` sh\n",
      "``` nix\n{ ok = true; }\n```",
  };
  for (size_t i = 0; i < sizeof(snippets) / sizeof(snippets[0]); i++) {
    CHECK(strstr(tips_md, snippets[i]) != nullptr &&
              strstr(full_md, snippets[i]) != nullptr,
          snippets[i]);
  }
  CHECK(strstr(tips_md, "Back to [Welcome](../../index.md#start).") != nullptr,
        "tips deflist continuation link");
  CHECK(strstr(full_md, "Back to [Welcome](#start).") != nullptr,
        "full deflist continuation link");

  tips_html = read_text(tips_html_path);
  CHECK(strstr(tips_html, "<code class=\"language-nix\">{ ok = true; }") !=
            nullptr,
        "tips HTML nix code block");
  CHECK(strstr(tips_html, "```nix") == nullptr, "no raw nix fence in HTML");

  const char *bad_tags[] = {"<nav", "<script", "<style", "<html"};
  for (size_t i = 0; i < sizeof(bad_tags) / sizeof(bad_tags[0]); i++)
    CHECK(strstr(full_md, bad_tags[i]) == nullptr, "no HTML tags in llms-full");

  CHECK(strstr(full_md, "Unlisted chapter.") == nullptr, "unlisted excluded");
  CHECK(!path_exists(private_out_md) && !path_exists(private_out_dir),
        "private dir not generated");
  CHECK(count_substr(full_md, "# Welcome {#start}") == 1 &&
            count_substr(full_md, "# `Tips` {#tip}") == 1,
        "each chapter once in llms-full");
  CHECK(strstr(full_md, "# Welcome") < strstr(full_md, "# `Tips`"),
        "chapter order in llms-full");
  CHECK(path_is_file(root_html_path) && path_is_file(tips_html_path),
        "multi HTML files exist");

  /* HTML links and search index percent-encode special characters. */
  root_html = read_text(root_html_path);
  CHECK(strstr(root_html, "guide/tips%20%231/#warning") != nullptr,
        "percent-encoded HTML link");
  CHECK(strstr(root_html, "guide/tips #1/") == nullptr,
        "no unencoded space/# in HTML link");

  char search_path[PATH_MAX];
  CHECK(find_matching_files(out_dir, "search_index.", ".js", search_path,
                            sizeof(search_path)) == 1,
        "exactly one search_index.*.js");
  search_text = read_text(search_path);
  CHECK(strstr(search_text, "\"url\": \"guide/tips%20%231/\"") != nullptr,
        "percent-encoded search index url");
  CHECK(strstr(search_text, "Welcome\\n\\nRoot") != nullptr,
        "search index body text");

  /* Single-page output deduplicates chapters, fills empty cross-ref titles,
   * and uses local #anchor heading links. */
  single_html = read_text(single_html_path);
  CHECK(count_substr(single_html, "<h1 id='start'>") == 1 &&
            count_substr(single_html, "<h1 id='tip'>") == 1,
        "single-page deduplicates chapters");
  CHECK(strstr(single_html, "<a href=\"#start\">Welcome</a>") != nullptr &&
            strstr(single_html, "<a href=\"#tip\">Tips</a>") != nullptr,
        "single-page fills empty link titles");
  CHECK(strstr(single_html, "<h1 id='start'><a href='#start'>") != nullptr &&
            strstr(single_html, "href='/#start'") == nullptr,
        "single-page local heading anchors");

  /* CLI help/version flags and trailing slashes work cleanly. */
  char *help_args[] = {mmdoc_bin, "--help", nullptr};
  char *ver_args[] = {mmdoc_bin, "-v", nullptr};
  char *slash_args[] = {mmdoc_bin, "My Manual", "./", "build/", nullptr};
  CHECK(run_cmd(tmpdir, help_args, true, false) == 0, "mmdoc --help");
  CHECK(run_cmd(tmpdir, ver_args, true, false) == 0, "mmdoc -v");
  CHECK(run_cmd(tmpdir, slash_args, false, false) == 0,
        "mmdoc trailing slashes");
  full_after = read_text(llms_full_path);
  CHECK(strcmp(full_after, full_md) == 0, "trailing slashes deterministic");
  free(full_after);
  full_after = nullptr;

  /* Rebuilding into SRC must not rediscover previously generated chapters. */
  CHECK(run_cmd(tmpdir, run_args, false, false) == 0, "rebuild into src");
  full_after = read_text(llms_full_path);
  CHECK(strcmp(full_after, full_md) == 0, "rebuild deterministic");
  CHECK(!path_exists(nested_build_out), "no nested build output discovered");
  free(full_after);
  full_after = nullptr;

  /* Atomic publish on failure: existing outputs preserved, no .mmdoc-* left. */
  write_file_bytes(intro_path, "\nNew content.\n", 14, true);
  CHECK(unlink(tips_md_path) == 0, "unlink tips index.md");
  CHECK(mkdir(tips_md_path, 0755) == 0, "mkdir blocking tips index.md");
  CHECK(run_cmd(tmpdir, run_args, true, true) != 0,
        "publish fails on dir conflict");
  full_after = read_text(llms_full_path);
  index_after = read_text(llms_txt_path);
  CHECK(strcmp(full_after, full_md) == 0, "llms-full.txt unchanged on failure");
  CHECK(strcmp(index_after, index_text) == 0, "llms.txt unchanged on failure");
  CHECK(find_matching_files(out_dir, ".mmdoc-", "", nullptr, 0) == 0,
        "no leftover .mmdoc-* staging dirs");

  printf("CLI markdown integration test passed\n");

cleanup:
  free(index_after);
  free(full_after);
  free(single_html);
  free(search_text);
  free(root_html);
  free(tips_html);
  free(full_md);
  free(tips_md);
  free(root_md);
  free(index_text);
  free(nested_build_out);
  free(private_out_md);
  free(private_out_dir);
  free(tips_html_path);
  free(tips_md_path);
  free(root_html_path);
  free(root_md_path);
  free(llms_full_path);
  free(llms_txt_path);
  free(single_html_path);
  free(out_dir);
  free(build_dir);
  free(private_src);
  free(tips_src);
  free(intro_path);
  free(toc_path);
  free(guide_dir);
  free(diagram_path);
  free(assets_dir);
  rm_rf(tmpdir);
  return failed;
}
