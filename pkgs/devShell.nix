{
  stdenv,
  lib,
  writeScriptBin,
  mkShell,
  meson,
  ninja,
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
    buildInputs = [];

    nativeBuildInputs =
      [
        meson
        ninja
        clang-tools
        gdb
        cppcheck

        doc-build
      ]
      ++ lib.optionals (!stdenv.isDarwin) [
        doc-watch
        valgrind
      ];
  }
