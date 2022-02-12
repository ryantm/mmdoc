{
  cmark-gfm
}:

cmark-gfm.overrideAttrs (oldAttrs: rec {
  # remove when https://github.com/github/cmark-gfm/pull/248 merged and released
  postInstall = ''
    substituteInPlace $out/include/cmark-gfm-core-extensions.h \
    --replace '#include "config.h"' '#include <stdbool.h>'
  '';
})
