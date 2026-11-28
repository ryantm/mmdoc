/* SPDX-License-Identifier: CC0-1.0 */
#include "html.h"
#include "asset.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static const char *html_escape_entity(char c, size_t *len) {
  switch (c) {
  case '&':
    *len = 5;
    return "&amp;";
  case '<':
    *len = 4;
    return "&lt;";
  case '>':
    *len = 4;
    return "&gt;";
  case '\'':
    *len = 5;
    return "&#39;";
  case '"':
    *len = 6;
    return "&quot;";
  default:
    return nullptr;
  }
}

int html_write_escaped(FILE *file, const char *text) {
  const char *run = text;
  const char *p = text;
  while (*p != '\0') {
    size_t entity_len = 0;
    const char *entity = html_escape_entity(*p, &entity_len);
    if (entity != nullptr) {
      size_t run_len = (size_t)(p - run);
      if (run_len > 0 && fwrite(run, 1, run_len, file) != run_len)
        return -1;
      if (fwrite(entity, 1, entity_len, file) != entity_len)
        return -1;
      run = p + 1;
    }
    p++;
  }
  size_t tail_len = (size_t)(p - run);
  if (tail_len > 0 && fwrite(run, 1, tail_len, file) != tail_len)
    return -1;
  return 0;
}

char *html_escape_string(const char *text) {
  size_t extra = 0;
  size_t len = 0;
  for (const char *p = text; *p != '\0'; p++) {
    size_t entity_len = 0;
    if (html_escape_entity(*p, &entity_len) != nullptr)
      extra += entity_len - 1;
    len++;
  }
  char *out = malloc(len + extra + 1);
  if (out == nullptr)
    return nullptr;
  if (extra == 0) {
    memcpy(out, text, len + 1);
    return out;
  }
  char *dst = out;
  for (const char *p = text; *p != '\0'; p++) {
    size_t entity_len = 0;
    const char *entity = html_escape_entity(*p, &entity_len);
    if (entity != nullptr) {
      memcpy(dst, entity, entity_len);
      dst += entity_len;
    } else {
      *dst++ = *p;
    }
  }
  *dst = '\0';
  return out;
}

char *mmdoc_url_encode_path(const char *path) {
  static constexpr char hex[] = "0123456789ABCDEF";
  size_t len = strlen(path);
  char *out = malloc(len * 3 + 1);
  if (out == nullptr)
    return nullptr;
  char *dst = out;
  for (const unsigned char *p = (const unsigned char *)path; *p != '\0'; p++) {
    if (isalnum(*p) || *p == '-' || *p == '.' || *p == '_' || *p == '~' ||
        *p == '/') {
      *dst++ = (char)*p;
    } else {
      *dst++ = '%';
      *dst++ = hex[*p >> 4];
      *dst++ = hex[*p & 0x0f];
    }
  }
  *dst = '\0';
  return out;
}

int html_theme_init(FILE *file) {
  return fputs("    <script>\n"
               "      (() => {\n"
               "        let theme = null\n"
               "        try { theme = localStorage.getItem('theme') } catch "
               "(error) {}\n"
               "        if (theme !== 'light' && theme !== 'dark')\n"
               "          theme = window.matchMedia('(prefers-color-scheme: "
               "dark)').matches ? 'dark' : 'light'\n"
               "        document.documentElement.classList.add(theme + "
               "'-theme')\n"
               "        const ua = navigator.userAgent || ''\n"
               "        const isWebView = /;\\s*wv\\)|Twitter|FBAN|FBAV|"
               "Instagram|LinkedInApp|BytedanceWebview|musical_ly|Line\\/|"
               "MicroMessenger/i.test(ua) || (/(iPhone|iPad|iPod)/i.test(ua) "
               "&& /AppleWebKit/i.test(ua) && !/Safari\\//i.test(ua))\n"
               "        const fromX = /^https?:\\/\\/([a-z0-9-]+\\.)?(t\\.co|"
               "x\\.com|twitter\\.com)(\\/|$)/i.test(document.referrer || '')\n"
               "        let topNav = isWebView || fromX\n"
               "        try {\n"
               "          if (topNav) sessionStorage.setItem('mmdoc-top-nav', "
               "'1')\n"
               "          else if (sessionStorage.getItem('mmdoc-top-nav') === "
               "'1') topNav = true\n"
               "        } catch (error) {}\n"
               "        if (topNav) "
               "document.documentElement.classList.add('mobile-top-nav')\n"
               "      })()\n"
               "    </script>\n",
               file) == EOF
             ? -1
             : 0;
}

int html_js(FILE *file) {
  if (fputs("<script>\n", file) == EOF)
    return -1;
  if (asset_write_to_file_mmdoc_js(file) != 0)
    return -1;
  return fputs("</script>\n", file) == EOF ? -1 : 0;
}

int html_highlight_js(FILE *file) {
  if (fputs("<script>\n", file) == EOF)
    return -1;
  if (asset_write_to_file_highlight_pack_js(file) != 0)
    return -1;
  return fputs("hljs.highlightAll()\n</script>\n", file) == EOF ? -1 : 0;
}

int html_css(FILE *file) {
  if (fputs("<style>\n", file) == EOF)
    return -1;
  if (asset_write_to_file_a11y_dark_css(file) != 0)
    return -1;
  if (asset_write_to_file_a11y_light_css(file) != 0)
    return -1;
  if (asset_write_to_file_mmdoc_css(file) != 0)
    return -1;
  return fputs("</style>\n", file) == EOF ? -1 : 0;
}
