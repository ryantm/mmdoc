{
  inputs.nixpkgs.url = "nixpkgs";
  inputs.flake-utils.url = "github:numtide/flake-utils";

  outputs = { self, nixpkgs, flake-utils } :
    flake-utils.lib.eachDefaultSystem (system: {
      packages.mmdoc = nixpkgs.legacyPackages.${system}.callPackage ./pkgs/mmdoc.nix {inherit self;};
      defaultPackage = self.packages.${system}.mmdoc;
      overlay = final: prev: {
        mmdoc = self.pcakges.${system}.mmdoc;
      };
    });
}
