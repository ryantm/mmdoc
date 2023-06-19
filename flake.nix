{
  description = "Minimal Markdown Documentation";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";

  outputs = {
    self,
    nixpkgs,
  } @ args: let
    p = system:
      import ./pkgs/default.nix {
        inherit (args) self nixpkgs;
        inherit system;
      };
  in {
    overlays.default = final: prev: {mmdoc = self.packages.${prev.system}.mmdoc;};

    packages.aarch64-linux = p "aarch64-linux";
    devShells.aarch64-linux.default = self.packages.aarch64-linux.devShell;
    formatter.aarch64-linux = self.packages.x86_64-linux.fmt;

    packages.i686-linux = p "i686-linux";
    devShells.i686-linux.default = self.packages.i686-linux.devShell;
    formatter.i686-linux = self.packages.x86_64-linux.fmt;

    packages.x86_64-darwin = p "x86_64-darwin";
    devShells.x86_64-darwin.default = self.packages.x86_64-darwin.devShell;
    formatter.x86_64-darwin = self.packages.x86_64-linux.fmt;

    packages.x86_64-linux = p "x86_64-linux";
    devShells.x86_64-linux.default = self.packages.x86_64-linux.devShell;
    formatter.x86_64-linux = self.packages.x86_64-linux.fmt;
  };
}
