{
  description = "Minimal Markdown Documentation";
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/26.05";
  inputs.systems.url = "github:nix-systems/default";
  inputs.drvc.url = "github:drvlang/drvc";
  inputs.drvc.inputs.nixpkgs.follows = "nixpkgs";
  outputs = {
    self,
    nixpkgs,
    systems,
    drvc,
  }: let
    eachSystem = nixpkgs.lib.genAttrs (import systems);
    project = system:
      import ./pkgs/mmdoc.nix {
        inherit drvc system;
        inherit (nixpkgs) lib;
      };
  in {
    overlays.default = final: prev: {mmdoc = self.packages.${prev.stdenv.hostPlatform.system}.mmdoc;};
    packages = eachSystem (system:
      import ./pkgs/default.nix {
        pkgs = nixpkgs.legacyPackages.${system};
        mmdoc = (project system).package;
      });
    checks = eachSystem (system: {default = (project system).check;});
    apps = eachSystem (system: {
      default = (project system).app;
      mmdoc = (project system).app;
    });
    devShells = eachSystem (system: {default = self.packages.${system}.devShell;});
    formatter = eachSystem (system: self.packages.${system}.fmt);
  };
}
