{
stdenv,
lib,
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
cppcheck
} :

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
  ] ++ lib.optionals (!stdenv.isDarwin) [
    valgrind
  ];
}
