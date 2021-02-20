# mmdoc - Minimal Markdown Documentation

```
mmdoc project_name src out
```

takes a directory like:

```
src
├── section1.md
├── section2.md
└── toc.md
```

and ouputs:

```
out
├── man
│   └── man1
│       ├── project_name-section1.1
│       └── project_name-section2.1
├── multi
│   ├── fuse.basic.min.js
│   ├── highlight.pack.js
│   ├── minimal.css
│   ├── mono-blue.css
│   ├── search_index.js
│   ├── search.js
│   ├── section1
│   │   └── index.html
│   └── section2
│       └── index.html
├── project_name.epub
└── single
    ├── highlight.pack.js
    ├── index.html
    ├── minimal.css
    └── mono-blue.css
```

That is, given a table of contents MarkDown file, and some other MarkDown files, it generates

* A single-page HTML document
* Multi-page HTML documents
* man pages (experimental)
* An ePub book (experimental)

Here are some features:

* No options.
* Fast: written in C, using cmark a very fast MarkDown parser/renderer.
* The HTML documents have syntax highlighting via [highlight.js](https://highlightjs.org/).
* The multi-page HTML documents have full-text search via [fuse.js](https://fusejs.io/).

## Extensions beyond CommonMark

In order to be useful for Documentation, it uses several CommonMark extensions available in GitHub-flavored MarkDown and [Pandoc](https://pandoc.org/).

* Cross references

  Made with:
  ```text
  # Heading {#heading-anchor-id}
  [inline]{#inline-anchor-id}
  ```
  Referenced with:
  ```text
  [heading](#heading-anchor-id)
  [inline](#inline-anchor-id)
  ```
* GitHub tables
  ```text
  | foo | bar |
  | --- | --- |
  | baz | bim |
  ```
* Admonitions
  ```text
  ::: warning
  this is your last warning
  :::
  ```
* DD Lists
  ```text
  : term 1

  defintion 1

  : term 2

  defintion 2
  ```

## toc.md

`toc.md` is a special file for `mmdoc`. The order of the cross references in `toc.md` control the order the sections appear in the single-page HTML and ePub outputs. If a section is not referenced in `toc.md` it does not get included in any of the outputs.

## Development

For easily development, `nix` with flake support is recommended. Start a development environment with:

```
nix develop
```

### tests

Compile and run the test suite with:

```
mkdir build && cd build
meson ..
ninja test
```

### Format

```
ninja clang-format
```

### additional test: compile the nixpkgs manual

This is a temporary test that is in place while mmdoc is developed in tandem with the conversion of the nixpkgs manual to use Markdown. You can build my branch of nixpkgs-manual with:

```
nix build .#nixpkgs-manual
```

### License

The mmdoc project is licensed under the "CC0 1.0 Universal". By contributing to this project, you agree to have your work licensed under that license.

Other licenses that might be relevant to this project:

* [highlight.js](https://github.com/highlightjs/highlight.js) source code is merged into the mmdoc binary so that it can be used in the HTML outputs.

* [fuse.js](https://github.com/krisk/fuse) source code is merged into the mmdoc binary so that it can be used in the HTML outputs.

* Some styling and HTML DOM object layout are based on [mdBook](https://github.com/rust-lang/mdBook/) source code.
