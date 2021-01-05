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

  installPhase = ''
    runHook preInstall

    mkdir -p "$out/bin"
    mkdir -p "$out/lib/mmdoc"
    install "$src/bin/mmdoc" "$out/bin"
    install "$src/lib/mmdoc/fuse.basic.min.js" "$out/lib/mmdoc"
    install "$src/lib/mmdoc/highlight.pack.js" "$out/lib/mmdoc"
    install "$src/lib/mmdoc/minimal.css" "$out/lib/mmdoc"
    install "$src/lib/mmdoc/mono-blue.css" "$out/lib/mmdoc"
    install "$src/lib/mmdoc/search.js" "$out/lib/mmdoc"
    patchShebangs $out/bin

    runHook postInstall
  '';

  fixupPhase = ''
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
