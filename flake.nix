{
  inputs.nixpkgs.url = "github:nixos/nixpkgs";
  inputs.flake-utils.url = "github:numtide/flake-utils";
  inputs.nixpkgs-manual.url = "github:ryantm/nixpkgs/minman";


  outputs = { self, nixpkgs, nixpkgs-manual, flake-utils } : {
    overlay = final: prev: {
      mmdoc = self.packages.mmdoc;
    };
  } // flake-utils.lib.eachDefaultSystem (system:
    let
      pkgs = nixpkgs.legacyPackages.${system};
    in
    {
      packages = rec {
        mmdoc = pkgs.callPackage ./pkgs/mmdoc.nix {inherit self;};
        nixpkgs-manual = pkgs.callPackage ./pkgs/nixpkgs-manual.nix { inherit mmdoc; nixpkgs = nixpkgs-manual; };
      };
      defaultPackage = self.packages.${system}.mmdoc;
      devShell = pkgs.mkShell {
        buildInputs = with pkgs; [
          cmark-gfm
          fastJson
          libzip.dev
        ];
        nativeBuildInputs = with pkgs; [
          pkgconfig
          meson
          ninja
          xxd
          clang-tools
          gdb
          valgrind
          cppcheck
        ];
      };
    });
}
