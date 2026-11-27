/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include "types.h"
#include <stdio.h>

int html_css(FILE *file);
int html_highlight_js(FILE *file);
int html_js(FILE *file);
int html_theme_init(FILE *file);
int html_write_escaped(FILE *file, const char *text);
char *html_escape_string(const char *text);
char *mmdoc_url_encode_path(const char *path);
