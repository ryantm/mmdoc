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
│   ├── a11y-dark.css
│   ├── a11y-light.css
│   ├── development
│   │   └── index.html
│   ├── features
│   │   └── index.html
│   ├── fuse.basic.min.js
│   ├── highlight.pack.js
│   ├── index.html
│   ├── license
│   │   └── index.html
│   ├── mmdoc.css
│   ├── mmdoc.js
│   ├── mmdoc_search.js
│   ├── search_index.js
│   └── usage
│       └── index.html
└── single
    └── index.html
```

The multi-page output uses shared, cacheable assets. The single-page output
embeds its styling and scripts in `index.html` so it remains portable as one
file.
