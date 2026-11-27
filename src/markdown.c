/* SPDX-License-Identifier: CC0-1.0 */
#include "markdown.h"
#include "arena.h"
#include "files.h"
#include "parse.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int publish(const char *temporary, const char *directory,
                   const char *name) {
  char *path = mmdoc_file_path(directory, name);
  if (path == NULL)
    return -1;
  int result = rename(temporary, path);
  if (result != 0)
    perror(path);
  free(path);
  return result;
}

/* Directory names come from the filesystem and may contain URL delimiters.
 * If already percent-encoded (from anchors.c), avoid double-encoding %XX. */
static char *chapter_url(const char *base, const AnchorLocation *chapter,
                         const char *fragment) {
  const char *dir = chapter->multipage_url;
  if (strcmp(dir, "./") == 0)
    dir = "";
  char *url = malloc(strlen(base) + 3 * strlen(dir) + sizeof("index.md") +
                     strlen(fragment));
  if (url == NULL)
    return NULL;
  char *out = stpcpy(url, base);
  for (const unsigned char *p = (const unsigned char *)dir; *p; p++) {
    if (isalnum(*p) || strchr("-._~/", *p)) {
      *out++ = (char)*p;
    } else if (*p == '%' && isxdigit(p[1]) && isxdigit(p[2])) {
      *out++ = (char)*p;
    } else {
      out += sprintf(out, "%%%02X", *p);
    }
  }
  strcpy(stpcpy(out, "index.md"), fragment);
  return url;
}

static int write_markdown(FILE *file, cmark_node *node) {
  char *markdown = cmark_render_commonmark(node, CMARK_OPT_DEFAULT, 0);
  if (markdown == NULL)
    return -1;
  int failed = fputs(markdown, file) == EOF;
  cmark_get_default_mem_allocator()->free(markdown);
  return failed ? -1 : 0;
}

static int title_needs_markdown_escaping(const char *s) {
  for (const unsigned char *p = (const unsigned char *)s; *p != '\0'; p++) {
    if (*p < 0x20 || *p == '[' || *p == ']' || *p == '\\' || *p == '*' ||
        *p == '_' || *p == '`' || *p == '<' || *p == '>' || *p == '&' ||
        *p == '~' || *p == '!' || *p == '"')
      return 1;
  }
  return 0;
}

static int write_index_entry(FILE *index, const AnchorLocation *chapter) {
  char *url = chapter_url("", chapter, "");
  if (url == NULL)
    return -1;
  const char *label = *chapter->title ? chapter->title : chapter->anchor + 1;
  if (!title_needs_markdown_escaping(label)) {
    int failed = fprintf(index, "- [%s](%s)\n", label, url) < 0;
    free(url);
    return failed ? -1 : 0;
  }
  cmark_node *link = cmark_node_new(CMARK_NODE_LINK);
  cmark_node_set_url(link, url);
  free(url);
  cmark_node *text = cmark_node_new(CMARK_NODE_TEXT);
  cmark_node_set_literal(text, label);
  cmark_node_append_child(link, text);
  int failed = fputs("- ", index) == EOF || write_markdown(index, link) != 0;
  cmark_node_free(link);
  return failed ? -1 : 0;
}

int mmdoc_markdown_write_index_entry(FILE *index,
                                     const AnchorLocation *chapter) {
  return write_index_entry(index, chapter);
}

static int write_buffer_file(const char *path, const char *data, size_t len) {
  FILE *file = fopen(path, "wb");
  if (file == NULL)
    return -1;
  int failed = len > 0 && fwrite(data, 1, len, file) != len;
  if (fclose(file) != 0)
    failed = 1;
  return failed ? -1 : 0;
}

int mmdoc_markdown_write_from_chapters(Inputs inputs, AnchorLocationArray toc,
                                       const size_t *page_indices,
                                       size_t page_count,
                                       const ChapterOutputs *chapters) {
  char *staging = mmdoc_file_path(inputs.out_multi, ".mmdoc-XXXXXX");
  if (staging == NULL || mkdtemp(staging) == NULL) {
    free(staging);
    return -1;
  }
  char *index_path = mmdoc_file_path(staging, "llms.txt");
  char *full_path = mmdoc_file_path(staging, "llms-full.txt");
  char *chapter_path = mmdoc_file_path(staging, "index.md");

  FILE *index = NULL, *full = NULL;
  int result = -1;
  if (!index_path || !full_path || !chapter_path)
    goto done;
  index = fopen(index_path, "wb");
  full = fopen(full_path, "wb");
  if (index == NULL || full == NULL ||
      fprintf(index, "# %s\n\n[Full manual](llms-full.txt)\n\n## Chapters\n\n",
              inputs.project_name) < 0)
    goto done;

  char full_buf[65536];
  setvbuf(full, full_buf, _IOFBF, sizeof(full_buf));

  for (size_t i = 0; i < page_count; i++) {
    const AnchorLocation *chapter = &toc.array[page_indices[i]];
    if (write_index_entry(index, chapter) != 0 ||
        (i > 0 && fputc('\n', full) == EOF) ||
        (chapters[i].full_md.len > 0 &&
         fwrite(chapters[i].full_md.data, 1, chapters[i].full_md.len, full) !=
             chapters[i].full_md.len) ||
        write_buffer_file(chapter_path, chapters[i].chapter_md.data,
                          chapters[i].chapter_md.len) != 0 ||
        publish(chapter_path, chapter->multipage_output_directory_path,
                "index.md") != 0)
      goto done;
  }
  result = 0;

done:
  if (index != NULL && fclose(index) != 0)
    result = -1;
  if (full != NULL && fclose(full) != 0)
    result = -1;
  if (result == 0)
    result = publish(full_path, inputs.out_multi, "llms-full.txt");
  if (result == 0)
    result = publish(index_path, inputs.out_multi, "llms.txt");
  if (chapter_path != NULL)
    unlink(chapter_path);
  if (full_path != NULL)
    unlink(full_path);
  if (index_path != NULL)
    unlink(index_path);
  rmdir(staging);
  free(chapter_path);
  free(full_path);
  free(index_path);
  free(staging);
  return result;
}

static int has_relative_path(const char *url) {
  if (*url == '\0' || strchr("/?#", *url))
    return 0;
  if (!isalpha((unsigned char)*url))
    return 1;
  const char *p = url + 1;
  while (isalnum((unsigned char)*p) || (*p && strchr("+-.", *p)))
    p++;
  return *p != ':';
}

static int resolve_link(cmark_node *node, const AnchorLocation *chapter,
                        AnchorLocationArray *anchors,
                        AnchorLocationArray *chapters_by_source) {
  const char *url = cmark_node_get_url(node);
  char *new_url = NULL;
  if (*url == '#' && cmark_node_get_type(node) == CMARK_NODE_LINK) {
    AnchorLocation *target = find_anchor_location(anchors, url);
    if (target == NULL)
      return 0;
    AnchorLocation *page =
        find_anchor_location(chapters_by_source, target->file_path);
    if (page == NULL)
      return 0;
    if (cmark_node_first_child(node) == NULL) {
      cmark_node *label = cmark_node_new(CMARK_NODE_TEXT);
      cmark_node_set_literal(label, *page->title ? page->title : url + 1);
      cmark_node_append_child(node, label);
    }
    if (chapter == NULL || strcmp(page->file_path, chapter->file_path) == 0)
      return 0;
    new_url = chapter_url(chapter->multipage_base_href, page, url);
  } else if (chapter != NULL && *chapter->multipage_base_href &&
             has_relative_path(url)) {
    new_url = malloc(strlen(chapter->multipage_base_href) + strlen(url) + 1);
    if (new_url != NULL)
      sprintf(new_url, "%s%s", chapter->multipage_base_href, url);
  } else {
    return 0;
  }
  if (new_url == NULL)
    return -1;
  int ok = cmark_node_set_url(node, new_url);
  free(new_url);
  return ok ? 0 : -1;
}

static int preserve_attributes(cmark_node *node) {
  if (cmark_node_get_type(node) == CMARK_NODE_HEADING) {
    HeadingInfo *info = cmark_node_get_user_data(node);
    if (info != NULL) {
      char *attribute = malloc(sizeof(" {#}") + strlen(info->anchor));
      if (attribute == NULL)
        return -1;
      sprintf(attribute, " {#%s}", info->anchor);
      cmark_node *marker = cmark_node_new(CMARK_NODE_CUSTOM_INLINE);
      cmark_node_set_on_enter(marker, attribute);
      cmark_node_append_child(node, marker);
      free(attribute);
    }
  } else if (cmark_node_get_type(node) == CMARK_NODE_TEXT) {
    const char *literal = cmark_node_get_literal(node);
    const char *start = literal + strspn(literal, " ");
    if (strncmp(start, ":::", 3) != 0)
      return 0;
    const char *attrs = start + 3 + strspn(start + 3, " ");
    const char *end = strchr(attrs, '}');
    if (*attrs != '{' || end == NULL || !memchr(attrs, '#', end - attrs))
      return 0;
    char *prefix = strndup(literal, (size_t)(end + 1 - literal));
    if (prefix == NULL)
      return -1;
    cmark_node *marker = cmark_node_new(CMARK_NODE_CUSTOM_INLINE);
    cmark_node_set_on_enter(marker, prefix);
    free(prefix);
    cmark_node_insert_before(node, marker);
    cmark_node_set_literal(node, end + 1);
  }
  return 0;
}

static int prepare_document(cmark_node *document, const AnchorLocation *chapter,
                            AnchorLocationArray *anchors,
                            AnchorLocationArray *chapters_by_source) {
  cmark_iter *iter = cmark_iter_new(document);
  cmark_event_type event;
  int result = 0;
  while (result == 0 && (event = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
    if (event != CMARK_EVENT_ENTER)
      continue;
    cmark_node *node = cmark_iter_get_node(iter);
    cmark_node_type type = cmark_node_get_type(node);
    if (type == CMARK_NODE_LINK || type == CMARK_NODE_IMAGE)
      result = resolve_link(node, chapter, anchors, chapters_by_source);
    else if (chapter == NULL)
      result = preserve_attributes(node);
  }
  cmark_iter_free(iter);
  return result;
}

static int write_chapter(AnchorLocation *chapter, FILE *full,
                         const char *temporary, AnchorLocationArray *anchors,
                         AnchorLocationArray *chapters_by_source) {
  cmark_parser *parser = cmark_parser_new(CMARK_OPT_DEFAULT);
  cmark_node *doc = mmdoc_render_cmark_document(chapter->file_path, parser);
  int result = -1;
  if (doc == NULL)
    goto done;
  cmark_rewrite_syntax(doc);
  cmark_rewrite_spans(doc, 1);
  if (prepare_document(doc, NULL, anchors, chapters_by_source) != 0 ||
      write_markdown(full, doc) != 0 ||
      prepare_document(doc, chapter, anchors, chapters_by_source) != 0)
    goto done;

  FILE *file = fopen(temporary, "w");
  if (file != NULL) {
    result = write_markdown(file, doc);
    if (fclose(file) != 0)
      result = -1;
    if (result == 0)
      result = publish(temporary, chapter->multipage_output_directory_path,
                       "index.md");
  }
done:
  cmark_node_free(doc);
  cmark_parser_free(parser);
  return result;
}

int mmdoc_markdown(Inputs inputs, AnchorLocationArray toc,
                   const size_t *page_indices, size_t page_count,
                   AnchorLocationArray anchors) {
  char *staging = mmdoc_file_path(inputs.out_multi, ".mmdoc-XXXXXX");
  if (staging == NULL || mkdtemp(staging) == NULL) {
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

  FILE *index = NULL, *full = NULL;
  int result = -1;
  if (!index_path || !full_path || !chapter_path ||
      build_anchor_location_index(&chapters_by_source) != 0)
    goto done;
  index = fopen(index_path, "w");
  full = fopen(full_path, "w");
  if (index == NULL || full == NULL ||
      fprintf(index, "# %s\n\n[Full manual](llms-full.txt)\n\n## Chapters\n\n",
              inputs.project_name) < 0)
    goto done;

  for (size_t i = 0; i < page_count; i++) {
    AnchorLocation *chapter = &toc.array[page_indices[i]];
    if (write_index_entry(index, chapter) != 0 ||
        (i > 0 && fputc('\n', full) == EOF) ||
        write_chapter(chapter, full, chapter_path, &anchors,
                      &chapters_by_source) != 0)
      goto done;
  }
  result = 0;
done:
  if (index != NULL && fclose(index) != 0)
    result = -1;
  if (full != NULL && fclose(full) != 0)
    result = -1;
  if (result == 0)
    result = publish(full_path, inputs.out_multi, "llms-full.txt");
  if (result == 0)
    result = publish(index_path, inputs.out_multi, "llms.txt");
  if (chapter_path != NULL)
    unlink(chapter_path);
  if (full_path != NULL)
    unlink(full_path);
  if (index_path != NULL)
    unlink(index_path);
  rmdir(staging);
  free(chapter_path);
  free(full_path);
  free(index_path);
  free(staging);
  free_anchor_location_array(&chapters_by_source);
  return result;
}
