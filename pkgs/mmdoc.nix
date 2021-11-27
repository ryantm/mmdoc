{lib,
stdenv,
cmark-gfm,
xxd,
fastJson,
libzip,
self,
ninja,
meson,
pkg-config
} :

with lib;

let
  cmark-gfm' = cmark-gfm.overrideAttrs (oldAttrs: rec {
    # remove when https://github.com/github/cmark-gfm/pull/248 merged and released
    postInstall = ''
      substituteInPlace $out/include/cmark-gfm-core-extensions.h \
        --replace '#include "config.h"' '#include <stdbool.h>'
    '';
  });
in
stdenv.mkDerivation rec {
  pname = "mmdoc";
  version = "0.8.0";

  src = self;

  nativeBuildInputs = [ ninja meson pkg-config ];

  doCheck = true;

  buildInputs = [ cmark-gfm' fastJson xxd libzip.dev ];

  meta = {
    description = "Minimal Markdown Documentation";
    homepage = src.meta.homepage;
    license = licenses.cc0;
    maintainers = with maintainers; [ ryantm ];
    platforms = platforms.unix;
  };
}
