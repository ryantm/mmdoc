# mmdoc {#introduction}

> Minimal Markdown Documentation

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

That is, given a table of contents Markdown file, and some other Markdown files, it generates

* A single-page HTML document
* Multi-page HTML documents
* man pages (experimental)
* An ePub book (experimental)
