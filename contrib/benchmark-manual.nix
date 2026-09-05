# nix build --impure -f contrib/benchmark-manual.nix \
#   --arg nixpkgsPath /tmp/mmdoc-nixpkgs-minman source -o /tmp/mmdoc-manual-source
{
  nixpkgsPath,
  mmdocFlake ? "git+file://${toString ../.}",
  system ? builtins.currentSystem,
}:
let
  current = builtins.getFlake mmdocFlake;
  pkgs = import nixpkgsPath { inherit system; };
  mmdoc = current.packages.${system}.mmdoc;
  manual = pkgs.nixpkgs-manual-mmdoc.override { inherit mmdoc; };
in {
  inherit manual;
  source = manual.overrideAttrs (old: {
    name = "mmdoc-benchmark-source";
    buildCommand = builtins.replaceStrings
      [ "${mmdoc}/bin/mmdoc nixpkgs doc $out" ]
      [ "cp -r doc $out" ] old.buildCommand;
  });
}
