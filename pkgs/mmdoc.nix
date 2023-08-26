{
  lib,
  stdenv,
  cmark-gfm,
  xxd,
  fastJson,
  libzip,
  self,
  ninja,
  meson,
  pkg-config,
}:
with lib;
  stdenv.mkDerivation rec {
    pname = "mmdoc";
    version = "0.18.0";

    src = self;

    nativeBuildInputs = [ninja meson pkg-config xxd];

    buildInputs = [cmark-gfm fastJson libzip];

    doCheck = stdenv.isx86_64;

    meta = {
      description = "Minimal Markdown Documentation";
      homepage = "https://github.com/ryantm/mmdoc";
      license = licenses.cc0;
      maintainers = with maintainers; [ryantm];
      platforms = platforms.unix;
    };
  }
