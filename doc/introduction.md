# mmdoc {#introduction}

> Minimal Markdown Documentation

Given a table of contents Markdown file, and some other Markdown files, generate:

* A single-page HTML document
* Multi-page HTML documents, with full-text search
* man pages (experimental)
* An ePub book (experimental)

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
├── epub
│   └── OEBPS
│       └── index.xhtml
├── man
│   └── man1
│       ├── mmdoc-development.1
│       ├── mmdoc-features.1
│       ├── mmdoc-introduction.1
│       ├── mmdoc-license.1
│       └── mmdoc-usage.1
├── mmdoc.epub
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
