/* SPDX-License-Identifier: CC0-1.0 */
#pragma once

extern unsigned char ___src_asset_epub_css[];
extern unsigned int ___src_asset_epub_css_len;

int asset_write_to_dir_highlight_pack_js(char *dir);
int asset_write_to_dir_fuse_basic_min_js(char *dir);
int asset_write_to_dir_a11y_light_css(char *dir);
int asset_write_to_dir_a11y_dark_css(char *dir);
int asset_write_to_dir_epub_css(char *dir);
int asset_write_to_dir_minimal_css(char *dir);
int asset_write_to_dir_search_js(char *dir);
