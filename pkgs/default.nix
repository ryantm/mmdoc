{
nixpkgs,
nixpkgs-for-manual,
system,
self,
} :

let
  pkgs = nixpkgs.legacyPackages.${system};
in rec {
  mmdoc = pkgs.callPackage ./mmdoc.nix { inherit self; };
  nixpkgs-manual = pkgs.callPackage ./nixpkgs-manual.nix { inherit mmdoc nixpkgs-for-manual; };
  devShell = pkgs.callPackage ./devShell.nix { };
}
