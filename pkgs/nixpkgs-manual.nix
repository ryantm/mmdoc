{
  stdenvNoCC,
  nixpkgs-for-manual,
  writeScript,
  mmdoc,
  graphviz,
  gdb,
  pkgs,
  fontconfig,
}:
stdenvNoCC.mkDerivation rec {
  name = "nixpkgs-manual";

  src = builtins.filterSource (path: type: type == "directory" || builtins.match ".*\.md" path == [] || builtins.match ".*\.dot" path == []) "${nixpkgs-for-manual}/doc";

  buildInputs = [gdb mmdoc];

  phases = ["buildPhase"];

  buildPhase = ''
    cp -r $src source
    chmod -R u+w source
    cp ${import "${nixpkgs-for-manual}/doc/doc-support/lib-functions-docs-cm.nix" {inherit pkgs;}}/*.md source/functions/library/
    FONTCONFIG_FILE=${fontconfig.out}/etc/fonts/fonts.conf \
      ${graphviz}/bin/dot -Tsvg source/contributing/staging_workflow.dot > source/staging_workflow.svg
    ${mmdoc}/bin/mmdoc nixpkgs source $out
  '';
}
