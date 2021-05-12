{
stdenv,
lib,
writeScriptBin,
mkShell,
cmark-gfm,
fastJson,
libzip,
pkg-config,
meson,
ninja,
xxd,
clang-tools,
gdb,
valgrind,
cppcheck,

entr,
python3,
nix,
} :

let

  fmt = writeScriptBin "fmt" ''
    ${ninja}/bin/ninja -C build clang-format
  '';

  doc-build = writeScriptBin "doc-build" ''
     nix build && ./result/bin/mmdoc mmdoc doc out
  '';

  doc-watch = writeScriptBin "doc-watch" ''
    killbg() {
      for p in "''${pids[@]}" ; do
        kill "$p";
      done
    }
    trap killbg EXIT
    pids=()
    ${python3}/bin/python -m http.server --directory ./out &
    pids+=($!)
    find ./doc/**.md | ${entr}/bin/entr ${doc-build}/bin/doc-build
  '';

in

mkShell {

  buildInputs = [
    cmark-gfm
    fastJson
    libzip.dev
  ];

  nativeBuildInputs = [
    pkg-config
    meson
    ninja
    xxd
    clang-tools
    gdb
    cppcheck

    doc-build
    doc-watch
    fmt
  ] ++ lib.optionals (!stdenv.isDarwin) [
    valgrind
  ];
}
