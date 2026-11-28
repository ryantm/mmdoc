# Introduction {#introduction}

> Minimal Markdown Documentation

Given a table of contents Markdown file, and some other Markdown files, generate:

- A single-page HTML document
- Multi-page HTML documents, with full-text search
- Markdown chapters, an index for agents, and a full manual in one text file

``` ShellSession
$ mmdoc project_name src_dir out_dir
```

For example:

``` ShellSession
$ mmdoc mmdoc doc out
```

takes a directory like

``` text
doc
├── development.md
├── features.md
├── introduction.md
├── license.md
├── toc.md
└── usage.md
```

and outputs

``` text
out
├── multi
│   ├── a11y-dark.<hash>.css
│   ├── a11y-light.<hash>.css
│   ├── development
│   │   ├── index.html
│   │   └── index.md
│   ├── features
│   │   ├── index.html
│   │   └── index.md
│   ├── fuse.basic.min.<hash>.js
│   ├── highlight.pack.<hash>.js
│   ├── index.html
│   ├── index.md
│   ├── license
│   │   ├── index.html
│   │   └── index.md
│   ├── llms-full.txt
│   ├── llms.txt
│   ├── mmdoc.<hash>.css
│   ├── mmdoc.<hash>.js
│   ├── mmdoc_search.<hash>.js
│   ├── search_index.<hash>.js
│   └── usage
│       ├── index.html
│       └── index.md
└── single
    └── index.html
```

The multi-page output uses shared assets with content hashes in their names so
browsers can cache them without serving stale content. The single-page output
embeds its styling and scripts in `index.html` so it remains portable as one
file.
