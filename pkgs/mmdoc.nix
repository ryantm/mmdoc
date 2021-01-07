{stdenv,
fetchFromGitHub,
makeWrapper,

cmark-gfm,
coreutils,
findutils,
gnugrep,
gnused,
jq,
self
} :

let

  runtimePath = stdenv.lib.makeBinPath [
    cmark-gfm
    coreutils
    findutils
    gnugrep
    gnused
    jq
  ];

in

stdenv.mkDerivation rec {
  pname = "mmdoc";
  version = "0.0.4";

  src = self;

  nativeBuildInputs = [ makeWrapper ];

  postFixup = ''
    wrapProgram $out/bin/mmdoc --argv0 mmdoc --prefix PATH ":" "${runtimePath}"
  '';

  meta = with stdenv.lib; {
    description = "Minimal Markdown Documentation";
    homepage = src.meta.homepage;
    license = licenses.cc0;
    maintainers = with maintainers; [ ryantm ];
    platforms = platforms.unix;
  };

}
