# Introduction {#introduction}

> Minimal Markdown Documentation

Given a table of contents Markdown file, and some other Markdown files, generate:

* A single-page HTML document
* Multi-page HTML documents, with full-text search

```ShellSession
$ mmdoc project_name src_dir out_dir
```

For example:

```ShellSession
$ mmdoc mmdoc doc out
```

takes a directory like

```text
doc
├── development.md
├── features.md
├── introduction.md
├── license.md
├── toc.md
└── usage.md
```

and outputs

```text
out
├── multi
│   ├── a11y-dark.<hash>.css
│   ├── a11y-light.<hash>.css
│   ├── development
│   │   └── index.html
│   ├── features
│   │   └── index.html
│   ├── fuse.basic.min.<hash>.js
│   ├── highlight.pack.<hash>.js
│   ├── index.html
│   ├── license
│   │   └── index.html
│   ├── mmdoc.<hash>.css
│   ├── mmdoc.<hash>.js
│   ├── mmdoc_search.<hash>.js
│   ├── search_index.<hash>.js
│   └── usage
│       └── index.html
└── single
    └── index.html
```

The multi-page output uses shared assets with content hashes in their names so
browsers can cache them without serving stale content. The single-page output
embeds its styling and scripts in `index.html` so it remains portable as one
file.
