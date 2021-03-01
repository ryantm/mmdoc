{
  inputs.nixpkgs.url = "github:ryantm/nixpkgs/minman";
  inputs.flake-utils.url = "github:numtide/flake-utils";

  outputs = { self, nixpkgs, flake-utils } : {
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
        nixpkgs-manual = pkgs.callPackage ./pkgs/nixpkgs-manual.nix { inherit nixpkgs mmdoc; };
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
