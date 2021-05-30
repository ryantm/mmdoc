# Contributing {#contributing}

For easy development, `nix` with [flakes support](https://nixos.wiki/wiki/Flakes) is recommended.

## Test {#development-tests}

Compile and run the test suite with:

```ShellSession
$ nix build
```

or

```ShellSession
$ mkdir build && cd build
$ meson ..
$ ninja test
```

## Format

```ShellSession
$ nix develop -c ninja -C build clang-format
```

## Documentation

Build the mmdoc docs to `./result` with:

```ShellSession
$ nix develop -c doc-build
```

Continuosly build the mmdoc docs and serve them on port 8000 with:

```ShellSession
$ nix develop -c doc-watch
```

## Nixpkgs manual

This is a temporary test that is in place while mmdoc is developed in tandem with the conversion of the nixpkgs manual to use Markdown. You can build my branch of nixpkgs-manual with:

Build the nixpkgs manual at `./result` with:

```ShellSession
$ nix develop -c np-build
```

Continuosly build the nixpkgs manual and serve them on port 8000 with:

```ShellSession
$ nix develop -c np-watch
```
