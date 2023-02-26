{
  description = "Minimal Markdown Documentation";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";

  outputs = {
    self,
    nixpkgs,
  } @ args: {
    overlays.default = final: prev: {mmdoc = self.packages.mmdoc;};

    packages.aarch64-linux = import ./pkgs/default.nix (args // {system = "aarch64-linux";});
    devShells.aarch64-linux.default = self.packages.aarch64-linux.devShell;

    packages.i686-linux = import ./pkgs/default.nix (args // {system = "i686-linux";});
    devShells.i686-linux.default = self.packages.i686-linux.devShell;

    packages.x86_64-darwin = import ./pkgs/default.nix (args // {system = "x86_64-darwin";});
    devShells.x86_64-darwin.default = self.packages.x86_64-darwin.devShell;

    packages.x86_64-linux = import ./pkgs/default.nix (args // {system = "x86_64-linux";});
    devShells.x86_64-linux.default = self.packages.x86_64-linux.devShell;
  };
}
