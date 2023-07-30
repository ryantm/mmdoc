/* SPDX-License-Identifier: CC0-1.0 */
#include "inputs.h"
#include "mkdir_p.h"
#include "render.h"
#include "html.h"
#include "types.h"
#include <string.h>

int mmdoc_single(Inputs inputs, AnchorLocationArray toc_anchor_locations) {
  char *out = inputs.out_single;

  if (mkdir_p(out) != 0) {
    printf("Error recursively making single directory %s", out);
    return 1;
  }

  char index_path[2048];
  FILE *index_file;
  strcpy(index_path, out);
  strcat(index_path, "/index.html");
  index_file = fopen(index_path, "w");

  fputs(
      "<!doctype html>\n"
      "<html>\n"
      "  <head>\n"
      "    <meta charset='utf-8'>\n"
      "    <meta name='viewport' content='width=device-width, "
      "initial-scale=1.0'>\n"
      "    <link rel='icon' href='favicon.svg'>\n",
      index_file);

  html_css(index_file);

      /* "    <script src='highlight.pack.js'></script>\n" */
      /* "    <script>hljs.highlightAll()</script>\n" */
      /* "    <script src='mmdoc.js'></script>\n" */
      /* "    <script>\n" */
      /* "      window.addEventListener('load', (event) => { \n" */
      /* "        let codeElems = " */
      /* "Array.from(document.querySelectorAll('code')).filter(function (elem) " */
      /* "{return !elem.parentElement.classList.contains('heade'); });\n" */
      /* "        codeElems.forEach(function (e) { e.classList.add('hljs'); });\n" */
      /* "      });\n" */
      /* "    </script>\n" */
      /* "    <title>"; */
  fputs("    <title>", index_file);
  fputs(inputs.project_name, index_file);
  fputs("</title>\n"
        "  </head>\n"
        "  <body>\n", index_file);
  fputs("  <input type='checkbox' id='sidebar-checkbox' style='display: "
        "none;'/>\n", index_file);
  fputs("    <div class='body'>\n"
        "      <div class='nav-top-container'>\n", index_file);
  fputs("    <nav class='nav-top'>\n", index_file);
  fputs("      <label for='sidebar-checkbox' class='sidebar-toggle'>≡</label>",
        index_file);
  fputs("    <button type='button' class='theme-toggle emoji'>🌘&#xFE0E;</button>",
      index_file);
  fputs("    </nav>\n"
        "    </div>\n", index_file);
  fputs("    <nav class='sidebar'>\n", index_file);
  AnchorLocation al;
  mmdoc_render_part(inputs.toc_path, index_file, RENDER_TYPE_SINGLE, &al,
                    toc_anchor_locations, "/", NULL);
  fputs("    </nav>\n", index_file);
  fputs("    <section id='main'>\n", index_file);
  fputs("      <main>\n", index_file);

  for (int i = 0; i < toc_anchor_locations.used; i++) {
    AnchorLocationArray empty_anchor_locations;
    init_anchor_location_array(&empty_anchor_locations, 0);
    mmdoc_render_part(toc_anchor_locations.array[i].file_path, index_file,
                      RENDER_TYPE_SINGLE, &toc_anchor_locations.array[i],
                      empty_anchor_locations, "/", NULL);
    free_anchor_location_array(&empty_anchor_locations);
  }

  fputs("        </main>\n"
        "      </section>\n"
        "    </div>\n", index_file);

  html_js(index_file);
  html_highlight_js(index_file);

  fputs("  </body>\n"
        "</html>\n", index_file);
  fclose(index_file);
  return 0;
}
