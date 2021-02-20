/* SPDX-License-Identifier: CC0-1.0 */
#include "asset.h"
#include "mkdir_p.h"
#include "render.h"
#include <stdlib.h>
#include <string.h>
#include <zip.h>

int mmdoc_epub(char *out, char *out_epub_file, char *toc_path,
               AnchorLocationArray toc_anchor_locations, char *project_name) {

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
      "<!DOCTYPE html PUBLIC '-//W3C//DTD XHTML 1.1//EN' "
      "'http://www.w3.org/TR/xhtml11/DTD/xhtml11.dtd'>\n"
      "<html xmlns='http://www.w3.org/1999/xhtml' xml:lang='en'>\n"
      "  <head>\n"
      "    <meta http-equiv='Content-Type' content='application/xhtml+xml; "
      "charset=utf-8' />\n"
      "    <link href='epub.css' rel='stylesheet' type='text/css'>\n"
      "  </head>\n"
      "  <body>\n";
  fputs(xhtml_head, index_file);
  AnchorLocation al;
  mmdoc_render_part(toc_path, index_file, RENDER_TYPE_SINGLE, &al,
                    toc_anchor_locations, NULL, NULL);

  for (int i = 0; i < toc_anchor_locations.used; i++) {
    char *file_path = toc_anchor_locations.array[i].file_path;
    AnchorLocationArray empty_anchor_locations;
    init_anchor_location_array(&empty_anchor_locations, 0);
    mmdoc_render_part(file_path, index_file, RENDER_TYPE_SINGLE, &al,
                      empty_anchor_locations, NULL, NULL);
    free_anchor_location_array(&empty_anchor_locations);
  }
  char *html_foot = "  </body>\n"
                    "</html>\n";
  fputs(html_foot, index_file);
  fclose(index_file);

  int *errorp = 0;
  zip_t *zip = zip_open(out_epub_file, ZIP_CREATE | ZIP_TRUNCATE, errorp);
  if (errorp != 0) {
    printf("Error making zip file at %s\n", out_epub_file);
    return 1;
  }
  const char *mimetype = "application/epub+zip";
  zip_source_t *source_mimetype =
      zip_source_buffer(zip, mimetype, strlen(mimetype), 0);
  zip_file_add(zip, "mimetype", source_mimetype,
               ZIP_FL_OVERWRITE | ZIP_FL_ENC_UTF_8);

  char *container = "<?xml version='1.0' encoding='UTF-8' ?>\n"
                    "<container version='1.0' "
                    "xmlns='urn:oasis:names:tc:opendocument:xmlns:container'>\n"
                    "  <rootfiles>\n"
                    "    <rootfile full-path='OEBPS/content.opf' "
                    "media-type='application/oebps-package+xml'/>\n"
                    "  </rootfiles>\n"
                    "</container>";
  zip_source_t *source_container =
      zip_source_buffer(zip, container, strlen(container), 0);
  zip_file_add(zip, "META-INF/container.xml", source_container,
               ZIP_FL_OVERWRITE | ZIP_FL_ENC_UTF_8);

  char *content_before_title =
      "<?xml version='1.0'?>\n"
      "<package version='2.0' xmlns='http://www.idpf.org/2007/opf' "
      "unique-identifier='BookId'>\n"
      "  <metadata xmlns:dc='http://purl.org/dc/elements/1.1/' "
      "xmlns:opf='http://www.idpf.org/2007/opf'>\n"
      "    <dc:title>";
  char *content_after_title =
      "</dc:title>\n"
      "    <dc:language>en</dc:language>\n"
      "  </metadata>\n"
      "  <manifest>\n"
      "    <item id='index' href='index.xhtml' "
      "media-type='application/xhtml+xml'/>\n"
      "    <item id='stylesheet' href='epub.css' media-type='text/css'/>\n"
      "  </manifest>\n"
      "  <spine toc='ncx'>\n"
      "    <itemref idref='index' />\n"
      "  </spine>\n"
      "</package>\n";
  char *content = malloc(strlen(content_before_title) + strlen(project_name) +
                         strlen(content_after_title) + 1);
  strcpy(content, content_before_title);
  strcat(content, project_name);
  strcat(content, content_after_title);
  zip_source_t *source_content =
      zip_source_buffer(zip, content, strlen(content), 0);
  zip_file_add(zip, "OEBPS/content.opf", source_content,
               ZIP_FL_OVERWRITE | ZIP_FL_ENC_UTF_8);

  zip_source_t *source_index = zip_source_file(zip, index_path, 0, 0);
  zip_file_add(zip, "OEBPS/index.xhtml", source_index, ZIP_FL_ENC_UTF_8);

  zip_source_t *source_css = zip_source_buffer(zip, ___src_asset_epub_css,
                                               ___src_asset_epub_css_len, 0);
  zip_file_add(zip, "OEBPS/epub.css", source_css, ZIP_FL_ENC_UTF_8);

  zip_close(zip);
  return 0;
}
