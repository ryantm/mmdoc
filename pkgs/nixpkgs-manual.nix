{ stdenv,
nixpkgs-for-manual,
writeScript,
mmdoc,
gdb,
} :

stdenv.mkDerivation rec {
  name = "nixpkgs-manual";

  src = "${nixpkgs-for-manual}/doc";

  buildInputs = [gdb mmdoc];

  builder = writeScript "${name}-builder" "echo $src && ${mmdoc}/bin/mmdoc nixpkgs $src $out";
}
