{
  stdenv,
  writeScript,
  mmdoc,
  gdb,
}:
stdenv.mkDerivation rec {
  name = "mmdoc-docs";

  src = ../doc;

  buildInputs = [gdb mmdoc];

  builder = writeScript "${name}-builder" "echo $src && ${mmdoc}/bin/mmdoc mmdoc $src $out";
}
