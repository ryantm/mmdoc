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
  inotify-tools,
  simple-http-server,
  nix,
}: let
  doc-build = writeScriptBin "doc-build" ''
    nix build .#mmdoc-docs -L
  '';

  doc-watch = writeScriptBin "doc-watch" ''
    killbg() {
      for p in "''${pids[@]}" ; do
        kill "$p";
      done
    }
    trap killbg EXIT
    pids=()
    ${simple-http-server}/bin/simple-http-server --compress=js,css,html,svg --index --nocache ./result &
    pids+=($!)
    trap exit SIGINT

    ${doc-build}/bin/doc-build

    while ${inotify-tools}/bin/inotifywait --event modify --event create --recursive doc src
    do
      ${doc-build}/bin/doc-build
      killbg
      ${simple-http-server}/bin/simple-http-server --compress=js,css,html,svg --index --nocache ./result &
      pids+=($!)
    done
  '';

in
  mkShell {
    name = "mmdoc";
    buildInputs = [
      cmark-gfm
      fastJson
      libzip.dev
    ];

    nativeBuildInputs =
      [
        pkg-config
        meson
        ninja
        xxd
        clang-tools
        gdb
        cppcheck

        doc-build
        doc-watch
      ]
      ++ lib.optionals (!stdenv.isDarwin) [
        valgrind
      ];
  }
