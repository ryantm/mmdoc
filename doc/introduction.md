# mmdoc {#introduction}

> Minimal Markdown Documentation

```
mmdoc project_name src out
```

takes a directory like:

```
doc
├── development.md
├── features.md
├── introduction.md
├── license.md
├── toc.md
└── usage.md
```

and ouputs:

```
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

That is, given a table of contents Markdown file, and some other Markdown files, it generates

* A single-page HTML document
* Multi-page HTML documents
* man pages (experimental)
* An ePub book (experimental)
