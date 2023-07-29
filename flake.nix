{
  description = "Minimal Markdown Documentation";
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";
  inputs.systems.url = "github:nix-systems/default";
  outputs = {
    self,
    nixpkgs,
    systems,
  }: let
    eachSystem = nixpkgs.lib.genAttrs (import systems);
  in {
    overlays.default = final: prev: {mmdoc = self.packages.${prev.system}.mmdoc;};
    packages = eachSystem (system:
      import ./pkgs/default.nix {
        inherit self nixpkgs system;
      });
    devShells = eachSystem (system: {default = self.packages.${system}.devShell;});
    formatter = eachSystem (system: self.packages.${system}.fmt);
  };
}
