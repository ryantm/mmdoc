{stdenv,
cmark-gfm,
xxd,
fastJson,
libzip,
self,
} :

stdenv.mkDerivation rec {
  pname = "mmdoc";
  version = "0.1.3";

  src = self;

  buildInputs = [ cmark-gfm fastJson xxd libzip.dev ];

  meta = with stdenv.lib; {
    description = "Minimal Markdown Documentation";
    homepage = src.meta.homepage;
    license = licenses.cc0;
    maintainers = with maintainers; [ ryantm ];
    platforms = platforms.unix;
  };

}
