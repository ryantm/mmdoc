# Install {#install}

To run the flake from source, enable the [Nix features used by drvc](../contributing/index.md#development-tests):

``` ShellSession
$ nix run github:ryantm/mmdoc -- <site name> <src> <out>
```

You could install with Nix via

``` ShellSession
$ nix profile install github:ryantm/mmdoc
```

- [mmdoc is packaged in Nixpkgs](https://search.nixos.org/packages?channel=23.05&show=mmdoc&from=0&size=50&sort=relevance&type=packages&query=mmdoc)

Packaging in other repositories is welcome and will be added here as they're made known.
