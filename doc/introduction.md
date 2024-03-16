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

and ouputs

```text
out
├── multi
│   ├── development
│   │   └── index.html
│   ├── features
│   │   └── index.html
│   ├── fuse.basic.min.js
│   ├── highlight.pack.js
│   ├── index.html
│   ├── license
│   │   └── index.html
│   ├── minimal.css
│   ├── mono-blue.css
│   ├── search_index.js
│   ├── search.js
│   └── usage
│       └── index.html
└── single
    ├── highlight.pack.js
    ├── index.html
    ├── minimal.css
    └── mono-blue.css
```
