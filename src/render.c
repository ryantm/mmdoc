/* SPDX-License-Identifier: CC0-1.0 */
#include "mkdir_p.h"
#include "render.h"
#include "fuse.basic.min.js.h"
#include "highlight.pack.js.h"
#include "minimal.css.h"
#include "epub.css.h"
#include "mono-blue.css.h"
#include "search.js.h"
#include "parse.h"
#include "types.h"
#include <cmark-gfm-core-extensions.h>
#include <cmark-gfm-extension_api.h>
#include <cmark-gfm.h>
#include <errno.h>
#include <libfastjson/json_object.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <zip.h>


int replace_bracket_with_span(cmark_node *node) {
  const char *lit = cmark_node_get_literal(node);
  char *id = malloc(strlen(lit) + 1);
  int pos = parse_heading_bracketed_span_id(lit, id);
  if (-1 == pos) {
    free(id);
    return 0;
  }
  char *new_lit = malloc(strlen(lit) + 1);
  int i;
  for (i = 0; i < pos; i++) {
    new_lit[i] = lit[i];
  }
  new_lit[i] = '\0';
  char *first_span = malloc(12 + strlen(lit) + 1);
  strcpy(first_span, "<span id='");
  strcat(first_span, id);
  strcat(first_span, "'>");
  cmark_node *new_node = cmark_node_new(CMARK_NODE_HTML_INLINE);
  cmark_node_set_literal(new_node, first_span);
  cmark_node_insert_before(node, new_node);
  new_node = cmark_node_new(CMARK_NODE_HTML_INLINE);
  cmark_node_set_literal(new_node, "</span>");
  cmark_node_insert_after(node, new_node);
  new_node = cmark_node_new(CMARK_NODE_TEXT);
  cmark_node_set_literal(new_node, new_lit);
  cmark_node_insert_after(node, new_node);
  cmark_node_replace(node, new_node);
  cmark_node_free(node);
  free(id);
  free(new_lit);
  free(first_span);
  return 1;
}

int replace_link_bracket_with_span(cmark_node *node) {
  const char *lit = cmark_node_get_literal(node);
  char *id = malloc(strlen(lit) + 1);
  char *span_text = malloc(strlen(lit) + 1);
  int pos = parse_link_bracketed_span_id(lit, span_text, id);
  if (-1 == pos) {
    free(id);
    free(span_text);
    return 0;
  }
  char *new_l_lit = malloc(strlen(lit) + 1);
  int i;
  for (i = 0; i < pos; i++) {
    new_l_lit[i] = lit[i];
  }
  new_l_lit[i] = '\0';
  cmark_node *new_node = cmark_node_new(CMARK_NODE_TEXT);
  cmark_node_set_literal(new_node, new_l_lit);
  cmark_node_insert_before(node, new_node);

  char *first_span = malloc(12 + strlen(lit) + 1);
  strcpy(first_span, "<span id='");
  strcat(first_span, id);
  strcat(first_span, "'>");
  new_node = cmark_node_new(CMARK_NODE_HTML_INLINE);
  cmark_node_set_literal(new_node, first_span);
  cmark_node_insert_before(node, new_node);

  new_node = cmark_node_new(CMARK_NODE_TEXT);
  cmark_node_set_literal(new_node, span_text);
  cmark_node_insert_before(node, new_node);

  new_node = cmark_node_new(CMARK_NODE_HTML_INLINE);
  cmark_node_set_literal(new_node, "</span>");
  cmark_node_insert_before(node, new_node);

  char *new_r_lit = malloc(strlen(lit) + 1);
  int end_of_span = pos + 1 + strlen(span_text) + 3 + strlen(id) + 2;
  for (i = end_of_span; lit[i] != '\0'; i++) {
    new_r_lit[i - end_of_span] = lit[i];
  }
  new_r_lit[i - end_of_span] = '\0';
  new_node = cmark_node_new(CMARK_NODE_TEXT);
  cmark_node_set_literal(new_node, new_r_lit);
  cmark_node_insert_before(node, new_node);

  cmark_node_unlink(node);
  cmark_node_free(node);
  free(id);
  free(span_text);
  free(first_span);
  free(new_r_lit);
  free(new_l_lit);
  return 1;
}

int replace_admonition_start(cmark_node *node) {
  const char *lit = cmark_node_get_literal(node);
  char *admonition_type = malloc(strlen(lit) + 1);
  int pos = parse_admonition_start(lit, admonition_type);
  if (-1 == pos) {
    free(admonition_type);
    return 0;
  }
  if (strcmp(admonition_type, "attention") != 0 &&
      strcmp(admonition_type, "caution") != 0 &&
      strcmp(admonition_type, "danger") != 0 &&
      strcmp(admonition_type, "error") != 0 &&
      strcmp(admonition_type, "hint") != 0 &&
      strcmp(admonition_type, "important") != 0 &&
      strcmp(admonition_type, "danger") != 0 &&
      strcmp(admonition_type, "note") != 0 &&
      strcmp(admonition_type, "tip") != 0 &&
      strcmp(admonition_type, "warning") != 0) {
    printf("admonition type '%s' doesn't match\n", admonition_type);
    free(admonition_type);
    return 0;
  }

  cmark_node *parent = cmark_node_parent(node);
  cmark_node *new_node = cmark_node_new(CMARK_NODE_HTML_BLOCK);
  if (strcmp(admonition_type, "attention") == 0)
    cmark_node_set_literal(
        new_node, "<div class='attention'><h3 class='title'>Attention</h3>");
  if (strcmp(admonition_type, "caution") == 0)
    cmark_node_set_literal(
        new_node, "<div class='caution'><h3 class='title'>Caution</h3>");
  if (strcmp(admonition_type, "danger") == 0)
    cmark_node_set_literal(new_node,
                           "<div class='danger'><h3 class='title'>Danger</h3>");
  if (strcmp(admonition_type, "error") == 0)
    cmark_node_set_literal(new_node,
                           "<div class='error'><h3 class='title'>Error</h3>");
  if (strcmp(admonition_type, "hint") == 0)
    cmark_node_set_literal(new_node,
                           "<div class='hint'><h3 class='title'>Hint</h3>");
  if (strcmp(admonition_type, "important") == 0)
    cmark_node_set_literal(
        new_node, "<div class='important'><h3 class='title'>Important</h3>");
  if (strcmp(admonition_type, "danger") == 0)
    cmark_node_set_literal(new_node,
                           "<div class='danger'><h3 class='title'>Danger</h3>");
  if (strcmp(admonition_type, "note") == 0)
    cmark_node_set_literal(new_node,
                           "<div class='note'><h3 class='title'>Note</h3>");
  if (strcmp(admonition_type, "tip") == 0)
    cmark_node_set_literal(new_node,
                           "<div class='tip'><h3 class='title'>Tip</h3>");
  if (strcmp(admonition_type, "warning") == 0)
    cmark_node_set_literal(
        new_node, "<div class='warning'><h3 class='title'>Warning</h3>");
  cmark_node_insert_before(parent, new_node);

  free(admonition_type);
  cmark_node_unlink(node);
  cmark_node_free(node);
  return 1;
}

int replace_admonition_end(cmark_node *node) {
  const char *lit = cmark_node_get_literal(node);
  int pos = parse_admonition_end(lit);
  if (-1 == pos)
    return 0;

  cmark_node *parent = cmark_node_parent(node);
  cmark_node *new_node = cmark_node_new(CMARK_NODE_HTML_BLOCK);
  cmark_node_set_literal(new_node, "</div>");
  cmark_node_insert_after(parent, new_node);
  cmark_node_unlink(node);
  cmark_node_free(node);
  return 1;
}

int replace_dd(cmark_node *node) {
  const char *lit = cmark_node_get_literal(node);
  int pos = parse_dd(lit);
  if (-1 == pos)
    return 0;

  cmark_node *parent = cmark_node_parent(node);
  parent = cmark_node_previous(parent);
  if (parent == NULL)
    return 0;

  cmark_node *new_node = NULL;
  cmark_node *previous = NULL;

  new_node = cmark_node_new(CMARK_NODE_HTML_BLOCK);
  cmark_node_set_literal(new_node, "<dl>");
  cmark_node_insert_before(parent, new_node);

  for(;;) {
    if (parent == NULL)
      break;

    cmark_node *child = cmark_node_first_child(parent);
    lit = cmark_node_get_literal(child);
    pos = parse_dd(lit);
    if (pos != -1) {
      new_node = cmark_node_new(CMARK_NODE_HTML_BLOCK);
      cmark_node_set_literal(new_node, "<dd>");
      cmark_node_insert_before(parent, new_node);

      new_node = cmark_node_new(CMARK_NODE_HTML_BLOCK);
      cmark_node_set_literal(new_node, "</dd>");
      cmark_node_insert_after(parent, new_node);

      cmark_node_set_literal(child, lit + pos);

      previous = parent;
      parent = cmark_node_next(parent);
      previous = parent;
      parent = cmark_node_next(parent);
      continue;
    }
    previous = parent;
    parent = cmark_node_next(parent);
    if (parent == NULL)
      break;

    child = cmark_node_first_child(parent);
    lit = cmark_node_get_literal(child);
    pos = parse_dd(lit);
    if (pos != -1) {
      new_node = cmark_node_new(CMARK_NODE_HTML_BLOCK);
      cmark_node_set_literal(new_node, "<dt>");
      cmark_node_insert_before(previous, new_node);

      new_node = cmark_node_new(CMARK_NODE_HTML_BLOCK);
      cmark_node_set_literal(new_node, "</dt>");
      cmark_node_insert_after(previous, new_node);

      new_node = cmark_node_new(CMARK_NODE_HTML_BLOCK);
      cmark_node_set_literal(new_node, "<dd>");
      cmark_node_insert_before(parent, new_node);

      new_node = cmark_node_new(CMARK_NODE_HTML_BLOCK);
      cmark_node_set_literal(new_node, "</dd>");
      cmark_node_insert_after(parent, new_node);

      cmark_node_set_literal(child, lit + pos);

      previous = parent;
      parent = cmark_node_next(parent);
      previous = parent;
      parent = cmark_node_next(parent);
      continue;
    }
    break;
  }

  new_node = cmark_node_new(CMARK_NODE_HTML_BLOCK);
  cmark_node_set_literal(new_node, "</dl>");
  cmark_node_insert_before(previous, new_node);

  return 1;
}

void replace_link(cmark_node *node, char *input_file_path,
                  AnchorLocationArray anchor_locations) {
  const char *url = cmark_node_get_url(node);
  if (strlen(url) < 2)
    return;
  if (url[0] != '#')
    return;
  const char *anchor = url;

  AnchorLocation anchor_location;
  int found = 0;
  for (int i = 0; i < anchor_locations.used; i++) {
    if (strcmp(anchor, anchor_locations.array[i].anchor) == 0) {
      anchor_location = anchor_locations.array[i];
      found = 1;
      break;
    }
  }
  if (!found) {
    printf("Anchor \"%s\" referenced in %s not found.\n", anchor,
           input_file_path);
    return;
  }
  char *new_url =
      malloc(strlen(anchor_location.multipage_url) + strlen(url) + 1);
  strcpy(new_url, anchor_location.multipage_url);
  strcat(new_url, url);
  cmark_node_set_url(node, new_url);
  free(new_url);
}

void insert_search_index(FILE *search_index_path, const char *text,
                         const char *multipage_url) {
  fjson_object *obj = fjson_object_new_object();
  fjson_object *obj_url = fjson_object_new_string(multipage_url);
  fjson_object *obj_text = fjson_object_new_string(text);
  fjson_object_object_add(obj, "url", obj_url);
  fjson_object_object_add(obj, "text", obj_text);
  fputs(fjson_object_to_json_string(obj), search_index_path);
  fputs("\n,", search_index_path);
}

void cmark_rewrite(cmark_node *document, cmark_mem *mem, char *input_file_path,
                   render_type render_type,
                   AnchorLocationArray anchor_locations) {
  cmark_iter *iter = cmark_iter_new(document);

  cmark_event_type event;
  cmark_node *node;
  cmark_node_type type;

  while (event = cmark_iter_next(iter)) {
    switch (event) {
    case CMARK_EVENT_ENTER:
      node = cmark_iter_get_node(iter);
      type = cmark_node_get_type(node);
      if (type == CMARK_NODE_LINK && render_type == RENDER_TYPE_MULTIPAGE) {
        replace_link(node, input_file_path, anchor_locations);
        continue;
      }
      if (type != CMARK_NODE_TEXT)
        continue;
      if (replace_link_bracket_with_span(node))
        continue;
      if (replace_bracket_with_span(node))
        continue;
      if (replace_admonition_start(node))
        continue;
      if (replace_admonition_end(node))
        continue;
      if (replace_dd(node))
        continue;
    }
    if (CMARK_EVENT_DONE == event)
      break;
  }
  cmark_iter_free(iter);
}

void render_debug_cmark_node(cmark_node *document) {
  cmark_iter *iter = cmark_iter_new(document);

  cmark_event_type event;
  cmark_node *node;
  int indent = 0;
  while (event = cmark_iter_next(iter)) {
    switch (event) {
    case CMARK_EVENT_ENTER:
      node = cmark_iter_get_node(iter);
      for (int i = 0; i < indent; i++) {
        printf("  ");
      }
      printf("ENTER: %s LITERAL: %s\n", cmark_node_get_type_string(node),
             cmark_node_get_literal(node));

      switch (cmark_node_get_type(node)) {
      case CMARK_NODE_HTML_BLOCK:
      case CMARK_NODE_THEMATIC_BREAK:
      case CMARK_NODE_CODE_BLOCK:
      case CMARK_NODE_TEXT:
      case CMARK_NODE_SOFTBREAK:
      case CMARK_NODE_LINEBREAK:
      case CMARK_NODE_CODE:
      case CMARK_NODE_HTML_INLINE:
        break;
      default:
        indent++;
      }
      break;
    case CMARK_EVENT_EXIT:
      indent--;
      node = cmark_iter_get_node(iter);
      for (int i = 0; i < indent; i++) {
        printf("  ");
      }
      printf("EXIT: %s\n", cmark_node_get_type_string(node));
      break;
    }
    if (event == CMARK_EVENT_DONE) {
      printf("DONE\n\n");
      break;
    }
  }
  cmark_iter_free(iter);
}

void mmdoc_render_part(char *file_path, FILE *output_file,
                       render_type render_type,
                       AnchorLocationArray anchor_locations,
                       char *multipage_url, FILE *search_index_path) {
  char buffer[4096];
  size_t bytes;

  int options = CMARK_OPT_DEFAULT | CMARK_OPT_UNSAFE;

  cmark_gfm_core_extensions_ensure_registered();
  cmark_mem *mem = cmark_get_default_mem_allocator();

  cmark_syntax_extension *table_extension =
      cmark_find_syntax_extension("table");

  FILE *file = fopen(file_path, "rb");

  cmark_parser *parser = cmark_parser_new_with_mem(options, mem);
  cmark_parser_attach_syntax_extension(parser, table_extension);
  while ((bytes = fread(buffer, 1, sizeof(buffer), file)) > 0) {
    cmark_parser_feed(parser, buffer, bytes);
    if (bytes < sizeof(buffer)) {
      break;
    }
  }
  fclose(file);
  cmark_node *document = cmark_parser_finish(parser);

  /* printf("BEFORE\n"); */
  /* render_debug_cmark_node(document); */

  cmark_rewrite(document, mem, file_path, render_type, anchor_locations);

  /* printf("AFTER\n"); */
  /* render_debug_cmark_node(document); */

  if (render_type != RENDER_TYPE_MAN) {
    char *result = cmark_render_html_with_mem(
                                              document, options, cmark_parser_get_syntax_extensions(parser), mem);

    if (search_index_path != NULL) {
      char *plaintext_result =
        cmark_render_plaintext_with_mem(document, options, 120, mem);
      insert_search_index(search_index_path, plaintext_result, multipage_url);
      mem->free(plaintext_result);
    }
    fputs(result, output_file);
    mem->free(result);
  }
  else {
    char *result = cmark_render_man_with_mem(document, options, 240, mem);
    fputs(result, output_file);
    mem->free(result);
  }
  cmark_node_free(document);
  cmark_parser_free(parser);
}

int mmdoc_render_single(char *out, char *toc_path, Array toc_refs,
                        AnchorLocationArray anchor_locations) {
  char asset_path[2048];
  FILE *asset_file;
  strcpy(asset_path, out);
  strcat(asset_path, "/highlight.pack.js");
  asset_file = fopen(asset_path, "w");
  for (int i = 0; i < ___src_asset_highlight_pack_js_len; i++) {
    fputc(___src_asset_highlight_pack_js[i], asset_file);
  }
  fclose(asset_file);

  strcpy(asset_path, out);
  strcat(asset_path, "/style.css");
  asset_file = fopen(asset_path, "w");
  for (int i = 0; i < ___src_asset_minimal_css_len; i++) {
    fputc(___src_asset_minimal_css[i], asset_file);
  }
  fclose(asset_file);

  strcpy(asset_path, out);
  strcat(asset_path, "/mono-blue.css");
  asset_file = fopen(asset_path, "w");
  for (int i = 0; i < ___src_asset_mono_blue_css_len; i++) {
    fputc(___src_asset_mono_blue_css[i], asset_file);
  }
  fclose(asset_file);

  char index_path[2048];
  FILE *index_file;
  strcpy(index_path, out);
  strcat(index_path, "/index.html");
  index_file = fopen(index_path, "w");

  char *html_head =
      "<!doctype html>\n"
      "<html>\n"
      "  <head>\n"
      "    <base href='/'>\n"
      "    <meta charset='utf-8'>\n"
      "    <link href='style.css' rel='stylesheet' type='text/css'>\n"
      "    <link rel='stylesheet' href='mono-blue.css'>\n"
      "    <script src='highlight.pack.js'></script>\n"
      "    <script>hljs.initHighlightingOnLoad();</script>\n"
      "  </head>\n"
      "  <body>\n"
      "    <nav>\n";
  fputs(html_head, index_file);
  mmdoc_render_part(toc_path, index_file, RENDER_TYPE_SINGLE, anchor_locations,
                    NULL, NULL);
  fputs("    </nav>\n", index_file);
  fputs("    <section>\n", index_file);

  for (int i = 0; i < toc_refs.used; i++) {
    char *file_path;
    int found = 0;
    for (int j = 0; j < anchor_locations.used; j++) {
      if (0 == strcmp(toc_refs.array[i], anchor_locations.array[j].anchor)) {
        file_path = anchor_locations.array[j].file_path;
        found = 1;
        break;
      }
    }
    if (!found) {
      printf("Anchor \"%s\" referenced in toc.md not found.\n",
             toc_refs.array[i]);
      return 1;
    }
    AnchorLocationArray empty_anchor_locations;
    init_anchor_location_array(&empty_anchor_locations, 0);
    mmdoc_render_part(file_path, index_file, RENDER_TYPE_SINGLE,
                      empty_anchor_locations, NULL, NULL);
    free_anchor_location_array(&empty_anchor_locations);
  }
  char *html_foot = "    </section>\n"
                    "  </body>\n"
                    "</html>\n";
  fputs(html_foot, index_file);
  fclose(index_file);
  return 0;
}

int mmdoc_render_epub(char *out, char *out_epub_file, char *toc_path, Array toc_refs,
                      AnchorLocationArray anchor_locations, char *project_name) {

  const char *oebps = "OEBPS";
  char *oebps_dir_path = malloc(strlen(out) + 1 + strlen(oebps) + 1);
  sprintf(oebps_dir_path, "%s/%s", out, oebps);
  if (mkdir_p(oebps_dir_path) != 0) {
    printf("Error recursively making directory %s", oebps_dir_path);
    return 1;
  }

  const char *index = "index.xhtml";
  char *index_path = malloc(strlen(oebps_dir_path) + 1 + strlen(index) + 1);
  sprintf(index_path, "%s/%s", oebps_dir_path, index);
  FILE *index_file;
  index_file = fopen(index_path, "w");

  char *xhtml_head =
    "<?xml version='1.0' encoding='UTF-8' ?>\n"
    "<!DOCTYPE html PUBLIC '-//W3C//DTD XHTML 1.1//EN' 'http://www.w3.org/TR/xhtml11/DTD/xhtml11.dtd'>\n"
    "<html xmlns='http://www.w3.org/1999/xhtml' xml:lang='en'>\n"
    "  <head>\n"
    "    <meta http-equiv='Content-Type' content='application/xhtml+xml; charset=utf-8' />\n"
    "    <link href='style.css' rel='stylesheet' type='text/css'>\n"
    "  </head>\n"
    "  <body>\n";
  fputs(xhtml_head, index_file);
  mmdoc_render_part(toc_path, index_file, RENDER_TYPE_SINGLE, anchor_locations,
                    NULL, NULL);
  for (int i = 0; i < toc_refs.used; i++) {
    char *file_path;
    int found = 0;
    for (int j = 0; j < anchor_locations.used; j++) {
      if (0 == strcmp(toc_refs.array[i], anchor_locations.array[j].anchor)) {
        file_path = anchor_locations.array[j].file_path;
        found = 1;
        break;
      }
    }
    if (!found) {
      printf("Anchor \"%s\" referenced in toc.md not found.\n",
             toc_refs.array[i]);
      return 1;
    }
    AnchorLocationArray empty_anchor_locations;
    init_anchor_location_array(&empty_anchor_locations, 0);
    mmdoc_render_part(file_path, index_file, RENDER_TYPE_SINGLE,
                      empty_anchor_locations, NULL, NULL);
    free_anchor_location_array(&empty_anchor_locations);
  }
  char *html_foot = "  </body>\n"
                    "</html>\n";
  fputs(html_foot, index_file);
  fclose(index_file);

  int *errorp;
  zip_t *zip = zip_open(out_epub_file, ZIP_CREATE | ZIP_TRUNCATE, errorp);
  if (errorp != 0) {
    printf("Error making zip file at %s\n", out_epub_file);
    return 1;
  }
  const char *mimetype = "application/epub+zip";
  zip_source_t *source_mimetype = zip_source_buffer(zip, mimetype, strlen(mimetype), 0);
  zip_file_add(zip, "mimetype", source_mimetype, ZIP_FL_OVERWRITE | ZIP_FL_ENC_UTF_8);

  char *container =
    "<?xml version='1.0' encoding='UTF-8' ?>\n"
    "<container version='1.0' xmlns='urn:oasis:names:tc:opendocument:xmlns:container'>\n"
    "  <rootfiles>\n"
    "    <rootfile full-path='OEBPS/content.opf' media-type='application/oebps-package+xml'/>\n"
    "  </rootfiles>\n"
    "</container>";
  zip_source_t *source_container = zip_source_buffer(zip, container, strlen(container), 0);
  zip_file_add(zip, "META-INF/container.xml", source_container, ZIP_FL_OVERWRITE | ZIP_FL_ENC_UTF_8);

  char *content_before_title =
    "<?xml version='1.0'?>\n"
    "<package version='2.0' xmlns='http://www.idpf.org/2007/opf' unique-identifier='BookId'>\n"
    "  <metadata xmlns:dc='http://purl.org/dc/elements/1.1/' xmlns:opf='http://www.idpf.org/2007/opf'>\n"
    "    <dc:title>";
  char *content_after_title = "</dc:title>\n"
    "    <dc:language>en</dc:language>\n"
    "  </metadata>\n"
    "  <manifest>\n"
    "    <item id='index' href='index.xhtml' media-type='application/xhtml+xml'/>\n"
    "    <item id='stylesheet' href='style.css' media-type='text/css'/>\n"
    "  </manifest>\n"
    "  <spine toc='ncx'>\n"
    "    <itemref idref='index' />\n"
    "  </spine>\n"
    "</package>\n";
  char *content = malloc(strlen(content_before_title) + strlen(project_name) + strlen(content_after_title) + 1);
  strcpy(content, content_before_title);
  strcat(content, project_name);
  strcat(content, content_after_title);
  zip_source_t *source_content = zip_source_buffer(zip, content, strlen(content), 0);
  zip_file_add(zip, "OEBPS/content.opf", source_content, ZIP_FL_OVERWRITE | ZIP_FL_ENC_UTF_8);

  zip_source_t *source_index = zip_source_file(zip, index_path, 0, 0);
  zip_file_add(zip, "OEBPS/index.xhtml", source_index, ZIP_FL_ENC_UTF_8);

  zip_source_t *source_css = zip_source_buffer(zip, ___src_asset_epub_css, ___src_asset_epub_css_len, 0);
  zip_file_add(zip, "OEBPS/style.css", source_css, ZIP_FL_ENC_UTF_8);

  zip_close(zip);
  return 0;
}

int mmdoc_render_multi_page(char *page_path, char *toc_path, char *input_path,
                            AnchorLocationArray anchor_locations,
                            char *multipage_url, FILE *search_index_file) {
  FILE *page_file;
  page_file = fopen(page_path, "w");
  char *html_head =
      "<!doctype html>\n"
      "<html>\n"
      "  <head>\n"
      "    <base href='/'>\n"
      "    <meta charset='utf-8'>\n"
      "    <link href='style.css' rel='stylesheet' type='text/css'>\n"
      "    <link rel='stylesheet' href='mono-blue.css'>\n"
      "    <script src='highlight.pack.js'></script>\n"
      "    <script>hljs.initHighlightingOnLoad();</script>\n"
      "    <script src='fuse.basic.min.js'></script>\n"
      "    <script src='search_index.js'></script>\n"
      "    <script src='search.js'></script>\n"
      "  </head>\n"
      "  <body>\n"
      "    <nav>\n"
      "      <input type='search' id='search' placeholder='Search'>\n"
      "      <div id='search-results'></div>\n";
  fputs(html_head, page_file);
  mmdoc_render_part(toc_path, page_file, RENDER_TYPE_MULTIPAGE,
                    anchor_locations, NULL, NULL);
  fputs("    </nav>\n", page_file);
  fputs("    <section>\n", page_file);
  mmdoc_render_part(input_path, page_file, RENDER_TYPE_MULTIPAGE,
                    anchor_locations, multipage_url, search_index_file);
  char *html_foot = "    </section>\n"
                    "  </body>\n"
                    "</html>\n";
  fputs(html_foot, page_file);
  fclose(page_file);
}

int mmdoc_render_multi(char *out, char *src, char *toc_path, Array toc_refs,
                       AnchorLocationArray anchor_locations) {
  char asset_path[2048];
  FILE *asset_file;
  strcpy(asset_path, out);
  strcat(asset_path, "/search.js");
  asset_file = fopen(asset_path, "w");
  for (int i = 0; i < ___src_asset_search_js_len; i++) {
    fputc(___src_asset_search_js[i], asset_file);
  }
  fclose(asset_file);

  strcpy(asset_path, out);
  strcat(asset_path, "/fuse.basic.min.js");
  asset_file = fopen(asset_path, "w");
  for (int i = 0; i < ___src_asset_fuse_basic_min_js_len; i++) {
    fputc(___src_asset_fuse_basic_min_js[i], asset_file);
  }
  fclose(asset_file);

  strcpy(asset_path, out);
  strcat(asset_path, "/highlight.pack.js");
  asset_file = fopen(asset_path, "w");
  for (int i = 0; i < ___src_asset_highlight_pack_js_len; i++) {
    fputc(___src_asset_highlight_pack_js[i], asset_file);
  }
  fclose(asset_file);

  strcpy(asset_path, out);
  strcat(asset_path, "/style.css");
  asset_file = fopen(asset_path, "w");
  for (int i = 0; i < ___src_asset_minimal_css_len; i++) {
    fputc(___src_asset_minimal_css[i], asset_file);
  }
  fclose(asset_file);

  strcpy(asset_path, out);
  strcat(asset_path, "/mono-blue.css");
  asset_file = fopen(asset_path, "w");
  for (int i = 0; i < ___src_asset_mono_blue_css_len; i++) {
    fputc(___src_asset_mono_blue_css[i], asset_file);
  }
  fclose(asset_file);

  char search_index_path[2048];
  strcpy(search_index_path, out);
  strcat(search_index_path, "/search_index.js");
  FILE *search_index_file = fopen(search_index_path, "w");
  fputs("const corpus = [", search_index_file);
  for (int i = 0; i < toc_refs.used; i++) {
    AnchorLocation anchor_location;
    int found = 0;
    for (int j = 0; j < anchor_locations.used; j++) {
      if (0 == strcmp(toc_refs.array[i], anchor_locations.array[j].anchor)) {
        anchor_location = anchor_locations.array[j];
        found = 1;
        break;
      }
    }
    if (!found) {
      printf("Anchor \"%s\" referenced in toc.md not found.\n",
             toc_refs.array[i]);
      return 1;
    }
    mmdoc_render_multi_page(anchor_location.multipage_output_file_path,
                            toc_path, anchor_location.file_path,
                            anchor_locations, anchor_location.multipage_url,
                            search_index_file);
  }
  fseek(search_index_file, -1, SEEK_CUR);
  fputs("]", search_index_file);
  fclose(search_index_file);
  return 0;
}

int mmdoc_render_man(char *out, char *src, char *toc_path, Array toc_refs,
                     AnchorLocationArray anchor_locations) {
  for (int i = 0; i < toc_refs.used; i++) {
    AnchorLocation anchor_location;
    int found = 0;
    for (int j = 0; j < anchor_locations.used; j++) {
      if (0 == strcmp(toc_refs.array[i], anchor_locations.array[j].anchor)) {
        anchor_location = anchor_locations.array[j];
        found = 1;
        break;
      }
    }
    if (!found) {
      printf("Anchor \"%s\" referenced in toc.md not found.\n",
             toc_refs.array[i]);
      return 1;
    }
    FILE *output_file;
    output_file = fopen(anchor_location.man_output_file_path, "w");
    fputs(anchor_location.man_header, output_file);
    mmdoc_render_part(anchor_location.file_path, output_file, RENDER_TYPE_MAN, anchor_locations, NULL, NULL);
    fclose(output_file);
  }

  return 0;
}
