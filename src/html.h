/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include "types.h"
#include <stdio.h>

[[nodiscard]] int html_css(FILE *file);
[[nodiscard]] int html_highlight_js(FILE *file);
[[nodiscard]] int html_js(FILE *file);
[[nodiscard]] int html_theme_init(FILE *file);
[[nodiscard]] int html_write_escaped(FILE *file, const char *text);
[[nodiscard]] char *html_escape_string(const char *text);
[[nodiscard]] char *mmdoc_url_encode_path(const char *path);
