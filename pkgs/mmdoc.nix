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

stdenv.mkDerivation rec {
  pname = "mmdoc";
  version = "0.7.0";

  src = self;

  nativeBuildInputs = [ ninja meson pkg-config ];

  doCheck = true;

  buildInputs = [ cmark-gfm fastJson xxd libzip.dev ];

  meta = {
    description = "Minimal Markdown Documentation";
    homepage = src.meta.homepage;
    license = licenses.cc0;
    maintainers = with maintainers; [ ryantm ];
    platforms = platforms.unix;
  };
}
