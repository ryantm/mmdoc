{
  inputs.nixpkgs.url = "github:ryantm/nixpkgs/minman";
  inputs.flake-utils.url = "github:numtide/flake-utils";

  outputs = { self, nixpkgs, flake-utils } :
    flake-utils.lib.eachDefaultSystem (system:
    let
      pkgs = nixpkgs.legacyPackages.${system};
    in
    {
      packages = rec {
        mmdoc = pkgs.callPackage ./pkgs/mmdoc.nix {inherit self;};
        nixpkgs-manual = pkgs.callPackage ./pkgs/nixpkgs-manual.nix { inherit nixpkgs mmdoc; };
      };
      defaultPackage = self.packages.${system}.mmdoc;
      overlay = final: prev: {
        mmdoc = self.packages.${system}.mmdoc;
      };
      devShell = pkgs.mkShell {
        buildInputs = with pkgs; [
          cmark-gfm
          gnumake
          clang-tools
          gcc
          gdb
          valgrind
          xxd
          jq.dev
        ];
      };
    });
}
