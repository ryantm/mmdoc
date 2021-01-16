{ stdenv,
nixpkgs,
mmdoc,
gdb
} :

with pkgs;

stdenv.mkDerivation rec {
  name = "nixpkgs-manual";

  src = builtins.filterSource (path: type: type == "directory" || builtins.match ".*\.md" path == []) "${nixpkgs}/doc";

  buildInputs = [gdb mmdoc];

  builder = writeScript "${name}-builder" "${mmdoc}/bin/mmdoc -s $src $out";
}
