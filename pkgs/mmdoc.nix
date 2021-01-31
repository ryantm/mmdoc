{stdenv,
cmark-gfm,
xxd,
fastJson,
self,
} :

stdenv.mkDerivation rec {
  pname = "mmdoc";
  version = "0.0.9";

  src = self;

  buildInputs = [ cmark-gfm fastJson xxd ];

  meta = with stdenv.lib; {
    description = "Minimal Markdown Documentation";
    homepage = src.meta.homepage;
    license = licenses.cc0;
    maintainers = with maintainers; [ ryantm ];
    platforms = platforms.unix;
  };

}
