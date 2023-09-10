{
  nixpkgs,
  system,
  self,
}: let
  pkgs = nixpkgs.legacyPackages.${system};
  nixpkgs-for-manual = pkgs.fetchFromGitHub {
    owner = "ryantm";
    repo = "nixpkgs";
    rev = "99bc88dcdbdd8f375fd3cd7395eafe55d9857283";
    sha256 = "08q5y5j2qpxk5gpjyhbz10ghyzicbdqibfpfnxgxq8fz649r1i7w";
  };
in rec {
  default = mmdoc;
  mmdoc = pkgs.callPackage ./mmdoc.nix {inherit self;};
  mmdoc-docs = pkgs.callPackage ./mmdoc-docs.nix {inherit mmdoc;};
  devShell = pkgs.callPackage ./devShell.nix {};
  fmt = pkgs.callPackage ./fmt.nix {};
  nixpkgs-manual-mmdoc = pkgs.callPackage "${nixpkgs}/pkgs/tools/nix/nixpkgs-manual-mmdoc" {

    inherit mmdoc;
  };
}
