{
  nixpkgs,
  system,
  self,
}: let
  pkgs = nixpkgs.legacyPackages.${system};
in rec {
  default = mmdoc;
  mmdoc = pkgs.callPackage ./mmdoc.nix {inherit self;};
  mmdoc-docs = pkgs.callPackage ./mmdoc-docs.nix {inherit mmdoc;};
  devShell = pkgs.callPackage ./devShell.nix {};
  fmt = pkgs.callPackage ./fmt.nix {};
}
