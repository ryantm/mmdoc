{
  pkgs,
  mmdoc,
}: {
  inherit mmdoc;
  default = mmdoc;
  mmdoc-docs = pkgs.callPackage ./mmdoc-docs.nix {inherit mmdoc;};
  devShell = pkgs.callPackage ./devShell.nix {};
  fmt = pkgs.callPackage ./fmt.nix {};
}
