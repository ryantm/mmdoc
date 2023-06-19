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
}: let
  doc-build = writeScriptBin "doc-build" ''
    nix build .#mmdoc-docs
  '';

  doc-watch = writeScriptBin "doc-watch" ''
    killbg() {
      for p in "''${pids[@]}" ; do
        kill "$p";
      done
    }
    trap killbg EXIT
    pids=()
    ${python3}/bin/python -m http.server --directory ./result &
    pids+=($!)
    find doc src test | ${entr}/bin/entr -cd ${doc-build}/bin/doc-build
  '';

  np-build = writeScriptBin "np-build" ''
    nix build .#nixpkgs-manual
  '';

  np-watch = writeScriptBin "np-watch" ''
    killbg() {
      for p in "''${pids[@]}" ; do
        kill "$p";
      done
    }
    trap killbg EXIT
    pids=()
    ${python3}/bin/python -m http.server --directory ./result &
    pids+=($!)
    find doc src test | ${entr}/bin/entr -cd ${np-build}/bin/np-build
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

        np-build
        np-watch
      ]
      ++ lib.optionals (!stdenv.isDarwin) [
        valgrind
      ];
  }
