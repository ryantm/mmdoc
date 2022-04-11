{
  description = "Minimal Markdown Documentation";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";
  inputs.nixpkgs-for-manual.url = "github:ryantm/nixpkgs/minman";

  outputs = {
    self,
    nixpkgs,
    nixpkgs-for-manual,
  } @ args: {
    overlay = final: prev: {mmdoc = self.packages.mmdoc;};

    packages."aarch64-linux" = import ./pkgs/default.nix (args // {system = "aarch64-linux";});
    defaultPackage."aarch64-linux" = self.packages."aarch64-linux".mmdoc;
    devShell."aarch64-linux" = self.packages."aarch64-linux".devShell;

    packages."i686-linux" = import ./pkgs/default.nix (args // {system = "i686-linux";});
    defaultPackage."i686-linux" = self.packages."i686-linux".mmdoc;
    devShell."i686-linux" = self.packages."i686-linux".devShell;

    packages."x86_64-darwin" = import ./pkgs/default.nix (args // {system = "x86_64-darwin";});
    defaultPackage."x86_64-darwin" = self.packages."x86_64-darwin".mmdoc;
    devShell."x86_64-darwin" = self.packages."x86_64-darwin".devShell;

    packages."x86_64-linux" = import ./pkgs/default.nix (args // {system = "x86_64-linux";});
    defaultPackage."x86_64-linux" = self.packages."x86_64-linux".mmdoc;
    devShell."x86_64-linux" = self.packages."x86_64-linux".devShell;
  };
}
