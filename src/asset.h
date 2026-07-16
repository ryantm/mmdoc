/* SPDX-License-Identifier: CC0-1.0 */
#pragma once

#include <stdio.h>

int asset_write_to_dir_fuse_basic_min_js(char *dir);
int asset_write_to_dir_highlight_pack_js(char *dir);
int asset_write_to_dir_a11y_dark_css(char *dir);
int asset_write_to_dir_a11y_light_css(char *dir);
int asset_write_to_dir_mmdoc_css(char *dir);
int asset_write_to_dir_mmdoc_js(char *dir);
int asset_write_to_dir_mmdoc_search_js(char *dir);

int asset_write_to_file_a11y_dark_css(FILE *file);
int asset_write_to_file_a11y_light_css(FILE *file);
int asset_write_to_file_highlight_pack_js(FILE *file);
int asset_write_to_file_mmdoc_css(FILE *file);
int asset_write_to_file_mmdoc_js(FILE *file);
