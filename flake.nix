{
  inputs.nixpkgs.url = "nixpkgs";
  inputs.flake-utils.url = "github:numtide/flake-utils";

  outputs = { self, nixpkgs, flake-utils } :
    flake-utils.lib.eachDefaultSystem (system:
    let
      pkgs = nixpkgs.legacyPackages.${system};
    in
    {
      packages.mmdoc = pkgs.callPackage ./pkgs/mmdoc.nix {inherit self;};
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
        ];
      };
    });
}
