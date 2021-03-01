# Development {#development}

For easily development, `nix` with flake support is recommended. Start a development environment with:

```
nix develop
```

## Test {#development-tests}

Compile and run the test suite with:

```
mkdir build && cd build
meson ..
ninja test
```

## Format

```
ninja clang-format
```

## additional test: compile the nixpkgs manual

This is a temporary test that is in place while mmdoc is developed in tandem with the conversion of the nixpkgs manual to use Markdown. You can build my branch of nixpkgs-manual with:

```
nix build .#nixpkgs-manual
```
