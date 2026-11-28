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
  versionLine =
    lib.findFirst
    (l: builtins.match ".*version[[:space:]]*:[[:space:]]*'([^']+)'.*" l != null)
    (throw "version not found in meson.build")
    (lib.splitString "\n" (builtins.readFile ../meson.build));
  version = builtins.head (builtins.match ".*version[[:space:]]*:[[:space:]]*'([^']+)'.*" versionLine);
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
