{
  drvc,
  lib,
  system,
}: let
  # drvlang.json lists the C sources, embedded assets, test fixtures and system libraries.
  project = {
    inherit system;
    root = ../.;
  };
  version = "0.30.8";
in {
  package =
    drvc.lib.mkPackage project
    // {
      inherit version;
      name = "mmdoc-${version}";
      pname = "mmdoc";
      meta = {
        mainProgram = "mmdoc";
        description = "Minimal Markdown Documentation";
        homepage = "https://github.com/ryantm/mmdoc";
        license = lib.licenses.cc0;
        maintainers = [lib.maintainers.ryantm];
        platforms = builtins.attrNames drvc.packages;
      };
    };
  check = drvc.lib.mkTest project;
  app = drvc.lib.mkApp project;
}
