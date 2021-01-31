{ stdenv,
nixpkgs,
writeScript,
mmdoc,
gdb,
} :

stdenv.mkDerivation rec {
  name = "nixpkgs-manual";

  src = "${nixpkgs}/doc";

  buildInputs = [gdb mmdoc];

  builder = writeScript "${name}-builder" "echo $src && ${mmdoc}/bin/mmdoc nixpkgs $src $out";
}
