# Nixpkgs manual benchmark

Measured on 2026-09-05, Intel Core i5-6500 @ 3.20 GHz, x86_64 Linux,
GCC 15.2.0, cmark-gfm 0.29.0.gfm.13. Baseline mmdoc commit: `026067b`.
Nixpkgs [minman branch](https://github.com/ryantm/nixpkgs/tree/0b8a791c02c3b666989a5880476b1bc76e86a1be)
was pinned to `0b8a791c02c3b666989a5880476b1bc76e86a1be`.

| Measurement | Before | After | Reduction |
| --- | ---: | ---: | ---: |
| Manual generation, Nix-packaged binaries | 187.4 ms | 165.3 ms | 11.8% |
| Manual generation, Meson release binaries | 187.3 ms | 165.5 ms | 11.6% |
| Forced Nix manual rebuild, dependencies cached | 554.7 ms | 531.7 ms | 4.1% |

Generation measurements are medians of 11 runs after one warm-up per binary,
with alternating execution order and a fresh output directory for every run.
All 192 output files were hashed and identical on every run. Source preparation
uses the actual package recipe, including generated library docs and the Python
interpreter table. Dependency downloads and compilation are excluded from these
measurements. Forced rebuild measurements are medians of three runs per version,
using already evaluated derivations; they include the Nix invocation, sandbox,
source preparation, rendering, and output registration, but not flake evaluation.

Callgrind attributed 13.3% of baseline instructions to
`mmdoc_render_get_title_from_file`: every file with anchors was parsed again only
to extract its title. Anchor discovery now extracts the first anchored heading's
title from the same Markdown tree. No document cache or persistent memory was
added. Existing title-only and anchor-only entry points remain available.

The unit suite, including combined metadata coverage, passes. Valgrind reports
zero errors and no lost allocations. The two Nix manual outputs also compare
identically. Both versions report the same four unresolved input references
(treefmt library section, two treefmt functions, and allowDeprecatedx86_64Darwin).

## Reproduction

Clone the benchmark branch and pin it before preparing input:

```sh
git clone --depth 1 --branch minman https://github.com/ryantm/nixpkgs.git /tmp/mmdoc-nixpkgs-minman
# For future runs, fetch/check out the revision above if the branch has moved.
nix build --impure -f contrib/benchmark-manual.nix \
  --arg nixpkgsPath /tmp/mmdoc-nixpkgs-minman source -o /tmp/mmdoc-manual-source
```

Build the baseline before editing, then build the optimized version in a separate
directory with identical settings:

```sh
nix develop --command meson setup /tmp/mmdoc-manual-before-build --buildtype=release
nix develop --command meson compile -C /tmp/mmdoc-manual-before-build
# Apply the optimization before the next two commands.
nix develop --command meson setup /tmp/mmdoc-manual-after-build --buildtype=release
nix develop --command meson compile -C /tmp/mmdoc-manual-after-build
python3 contrib/benchmark-manual.py \
  /tmp/mmdoc-manual-before-build/src/mmdoc \
  /tmp/mmdoc-manual-after-build/src/mmdoc /tmp/mmdoc-manual-source 11
```

To build the actual Nix output with the current checkout:

```sh
nix build --impure -f contrib/benchmark-manual.nix \
  --arg nixpkgsPath /tmp/mmdoc-nixpkgs-minman manual -o /tmp/mmdoc-manual
```

Save each version's derivation path, then time
`nix build '/nix/store/…-nixpkgs-minimal-manual.drv^*' --rebuild --no-link`
to force execution rather than measure a cached output lookup. Use the packaged
`bin/mmdoc` paths with the Python script for packaged-binary generation timings.
