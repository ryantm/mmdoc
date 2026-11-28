/* SPDX-License-Identifier: CC0-1.0 */
#include "markdown.h"
#include "files.h"
#include "md.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int publish(const char *temporary, const char *directory,
                   const char *name) {
  char *path = mmdoc_file_path(directory, name);
  if (path == nullptr)
    return -1;
  int result = rename(temporary, path);
  if (result != 0)
    perror(path);
  free(path);
  return result;
}

static char *chapter_url(const char *base, const AnchorLocation *chapter,
                         const char *fragment) {
  const char *dir = chapter->multipage_url;
  if (strcmp(dir, "./") == 0)
    dir = "";
  size_t blen = strlen(base);
  size_t flen = strlen(fragment);
  char *url = malloc(blen + 3 * strlen(dir) + sizeof("index.md") + flen);
  if (url == nullptr)
    return nullptr;
  char *out = url;
  memcpy(out, base, blen);
  out += blen;
  for (const unsigned char *p = (const unsigned char *)dir; *p; p++) {
    if (isalnum(*p) || strchr("-._~/", *p)) {
      *out++ = (char)*p;
    } else if (*p == '%' && isxdigit(p[1]) && isxdigit(p[2])) {
      *out++ = (char)*p;
    } else {
      out += sprintf(out, "%%%02X", *p);
    }
  }
  memcpy(out, "index.md", 8);
  out += 8;
  memcpy(out, fragment, flen + 1);
  return url;
}

static int write_index_entry(FILE *index, const AnchorLocation *chapter) {
  char *url = chapter_url("", chapter, "");
  if (url == nullptr)
    return -1;
  const char *label = *chapter->title ? chapter->title : chapter->anchor + 1;
  bool failed = fputs("- [", index) == EOF;
  for (const unsigned char *p = (const unsigned char *)label; !failed && *p;
       p++) {
    if (*p == '[' || *p == ']' || *p == '\\' || *p == '*' || *p == '_' ||
        *p == '`' || *p == '<' || *p == '>' || *p == '~' || *p == '!') {
      if (fputc('\\', index) == EOF)
        failed = true;
    }
    if (!failed && fputc((int)*p, index) == EOF)
      failed = true;
  }
  if (!failed && fprintf(index, "](%s)\n", url) < 0)
    failed = true;
  free(url);
  return failed ? -1 : 0;
}

int mmdoc_markdown_write_index_entry(FILE *index,
                                     const AnchorLocation *chapter) {
  return write_index_entry(index, chapter);
}

static int write_buffer_file(const char *path, const char *data, size_t len) {
  FILE *file = fopen(path, "wb");
  if (file == nullptr)
    return -1;
  bool failed = len > 0 && fwrite(data, 1, len, file) != len;
  if (fclose(file) != 0)
    failed = true;
  return failed ? -1 : 0;
}

int mmdoc_markdown_write_from_chapters(Inputs inputs, AnchorLocationArray toc,
                                       const size_t *page_indices,
                                       size_t page_count,
                                       const ChapterOutputs *chapters) {
  char *staging = mmdoc_file_path(inputs.out_multi, ".mmdoc-XXXXXX");
  if (staging == nullptr || mkdtemp(staging) == nullptr) {
    free(staging);
    return -1;
  }
  char *index_path = mmdoc_file_path(staging, "llms.txt");
  char *full_path = mmdoc_file_path(staging, "llms-full.txt");
  char *chapter_path = mmdoc_file_path(staging, "index.md");

  FILE *index = nullptr, *full = nullptr;
  int result = -1;
  if (!index_path || !full_path || !chapter_path)
    goto done;
  index = fopen(index_path, "wb");
  full = fopen(full_path, "wb");
  if (index == nullptr || full == nullptr ||
      fprintf(index, "# %s\n\n[Full manual](llms-full.txt)\n\n## Chapters\n\n",
              inputs.project_name) < 0)
    goto done;

  char full_buf[65536];
  setvbuf(full, full_buf, _IOFBF, sizeof(full_buf));

  for (size_t i = 0; i < page_count; i++) {
    const AnchorLocation *chapter = &toc.array[page_indices[i]];
    const MmdocBuf *chap_md = chapters[i].chapter_md.data != nullptr
                                  ? &chapters[i].chapter_md
                                  : &chapters[i].full_md;
    if (write_index_entry(index, chapter) != 0 ||
        (i > 0 && fputc('\n', full) == EOF) ||
        (chapters[i].full_md.len > 0 &&
         fwrite(chapters[i].full_md.data, 1, chapters[i].full_md.len, full) !=
             chapters[i].full_md.len) ||
        write_buffer_file(chapter_path, chap_md->data, chap_md->len) != 0 ||
        publish(chapter_path, chapter->multipage_output_directory_path,
                "index.md") != 0)
      goto done;
  }
  result = 0;

done:
  if (index != nullptr && fclose(index) != 0)
    result = -1;
  if (full != nullptr && fclose(full) != 0)
    result = -1;
  if (result == 0)
    result = publish(full_path, inputs.out_multi, "llms-full.txt");
  if (result == 0)
    result = publish(index_path, inputs.out_multi, "llms.txt");
  if (chapter_path != nullptr)
    unlink(chapter_path);
  if (full_path != nullptr)
    unlink(full_path);
  if (index_path != nullptr)
    unlink(index_path);
  rmdir(staging);
  free(chapter_path);
  free(full_path);
  free(index_path);
  free(staging);
  return result;
}

int mmdoc_markdown(Inputs inputs, AnchorLocationArray toc,
                   const size_t *page_indices, size_t page_count,
                   AnchorLocationArray anchors) {
  char *staging = mmdoc_file_path(inputs.out_multi, ".mmdoc-XXXXXX");
  if (staging == nullptr || mkdtemp(staging) == nullptr) {
    free(staging);
    return -1;
  }
  char *index_path = mmdoc_file_path(staging, "llms.txt");
  char *full_path = mmdoc_file_path(staging, "llms-full.txt");
  char *chapter_path = mmdoc_file_path(staging, "index.md");
  AnchorLocationArray chapters_by_source;
  init_anchor_location_array(&chapters_by_source, page_count);
  for (size_t i = 0; i < page_count; i++) {
    AnchorLocation chapter = toc.array[page_indices[i]];
    chapter.anchor = chapter.file_path;
    insert_anchor_location_array(&chapters_by_source, &chapter);
  }

  FILE *index = nullptr, *full = nullptr;
  int result = -1;
  if (!index_path || !full_path || !chapter_path ||
      build_anchor_location_index(&chapters_by_source) != 0)
    goto done;
  index = fopen(index_path, "wb");
  full = fopen(full_path, "wb");
  if (index == nullptr || full == nullptr ||
      fprintf(index, "# %s\n\n[Full manual](llms-full.txt)\n\n## Chapters\n\n",
              inputs.project_name) < 0)
    goto done;

  for (size_t i = 0; i < page_count; i++) {
    AnchorLocation *chapter = &toc.array[page_indices[i]];
    MdDoc doc;
    if (md_parse_file(&doc, chapter->file_path, false) != 0)
      goto done;
    MmdocBuf full_md = {}, chap_md = {};
    int rc = mmdoc_render_markdown_dual(
        &doc, chapter, &anchors, &chapters_by_source, &full_md, &chap_md);
    md_doc_free(&doc);
    if (rc == 0) {
      const MmdocBuf *cm = chap_md.data != nullptr ? &chap_md : &full_md;
      if (write_index_entry(index, chapter) != 0 ||
          (i > 0 && fputc('\n', full) == EOF) ||
          (full_md.len > 0 &&
           fwrite(full_md.data, 1, full_md.len, full) != full_md.len) ||
          write_buffer_file(chapter_path, cm->data, cm->len) != 0 ||
          publish(chapter_path, chapter->multipage_output_directory_path,
                  "index.md") != 0)
        rc = -1;
    }
    mmdoc_buf_free(&full_md);
    mmdoc_buf_free(&chap_md);
    if (rc != 0)
      goto done;
  }
  result = 0;

done:
  if (index != nullptr && fclose(index) != 0)
    result = -1;
  if (full != nullptr && fclose(full) != 0)
    result = -1;
  if (result == 0)
    result = publish(full_path, inputs.out_multi, "llms-full.txt");
  if (result == 0)
    result = publish(index_path, inputs.out_multi, "llms.txt");
  if (chapter_path != nullptr)
    unlink(chapter_path);
  if (full_path != nullptr)
    unlink(full_path);
  if (index_path != nullptr)
    unlink(index_path);
  rmdir(staging);
  free(chapter_path);
  free(full_path);
  free(index_path);
  free(staging);
  free_anchor_location_array(&chapters_by_source);
  return result;
}
