const corpus = [{ "url": ".\/", "title": "Introduction", "text": "IntroductionMinimal Markdown Documentation\n\nGiven a table of contents Markdown file, and some other Markdown files, generate:\n\n  - A single-page HTML document\n  - Multi-page HTML documents, with full-text search\n\n$ mmdoc project_name src_dir out_dir\n\nFor example:\n\n$ mmdoc mmdoc doc out\n\ntakes a directory like\n\ndoc\n├── development.md\n├── features.md\n├── introduction.md\n├── license.md\n├── toc.md\n└── usage.md\n\nand ouputs\n\nout\n├── multi\n│   ├── development\n│   │   └── index.html\n│   ├── features\n│   │   └── index.html\n│   ├── fuse.basic.min.js\n│   ├── highlight.pack.js\n│   ├── index.html\n│   ├── license\n│   │   └── index.html\n│   ├── minimal.css\n│   ├── mono-blue.css\n│   ├── search_index.js\n│   ├── search.js\n│   └── usage\n│       └── index.html\n└── single\n    ├── highlight.pack.js\n    ├── index.html\n    ├── minimal.css\n    └── mono-blue.css\n" }
,{ "url": "features\/", "title": "Features", "text": "Features  - No options.\n  - Fast: written in C, using cmark a very fast Markdown parser\/renderer.\n  - The HTML documents have syntax highlighting via highlight.js.\n  - The multi-page HTML documents have full-text search via fuse.js.\n" }
,{ "url": "users\/", "title": "Users of mmdoc", "text": "Users of mmdoc  - This manual!\n  - Ryan's rendering of the Nixpkgs manual\n  - Ryan's personal blog\n\nPlease contact me at ryan@ryantm.com or make an issue at https:\/\/github.com\/ryantm\/mmdoc.\n" }
,{ "url": "install\/", "title": "Install", "text": "InstallWith Nix, it is not necessary to install:\n\n$ nix run github:ryantm\/mmdoc -- <site name> <src> <out>\n\nYou could install with Nix via\n\n$ nix profile install github:ryantm\/mmdoc\n\n  - mmdoc is packaged in Nixpkgs\n\nPackaging in other repositories is welcome and will be added here as they're made known.\n" }
,{ "url": "usage\/", "title": "Usage", "text": "Usagetoc.md\n\ntoc.md is a special file for mmdoc. The order of the cross references in toc.md control the order the sections appear in\nthe single-page HTML and ePub outputs. If a section is not referenced in toc.md it does not get included in any of the\noutputs.\n\nExtensions beyond CommonMark\n\nIn order to be useful for Documentation, it uses several CommonMark extensions available in GitHub-flavored Markdown and\nPandoc.\n\n  - Cross references\n    \n    Made with:\n    \n    # Heading {#heading-anchor-id}\n    [inline]{#inline-anchor-id}\n\n    Referenced with:\n    \n    [heading](#heading-anchor-id)\n    [inline](#inline-anchor-id)\n\n  - GitHub tables\n    \n    | foo | bar |\n    | --- | --- |\n    | baz | bim |\n\n  - Admonitions\n    \n    ::: warning\n    this is your last warning\n    :::\n\n        this is your last warning \n    \n    ::: {.example #anchor}\n    this is your last warning\n    :::\n\n        this is your last warning \n\n  - DD Lists\n    \n    term 1\n    \n    : defintion 1\n    \n    term 2\n    \n    : defintion 2\n" }
,{ "url": "contributing\/", "title": "Contributing", "text": "ContributingFor easy development, nix with flakes support is recommended.\n\nTestCompile and run the test suite with:\n\n$ nix build\n\nor\n\n$ mkdir build && cd build\n$ meson ..\n$ ninja test\n\nFormat\n\n$ nix fmt\n\nDocumentation\n\nBuild the mmdoc docs to .\/result with:\n\n$ nix develop -c doc-build\n\nContinuosly build the mmdoc docs and serve them on port 8000 with:\n\n$ nix develop -c doc-watch\n\nNixpkgs manual\n\nThis is a temporary test that is in place while mmdoc is developed in tandem with the conversion of the nixpkgs manual\nto use Markdown. You can build my branch of nixpkgs-manual with:\n\nBuild the nixpkgs manual at .\/result with:\n\n$ nix develop -c np-build\n\nContinuosly build the nixpkgs manual and serve them on port 8000 with:\n\n$ nix develop -c np-watch\n\nMaking a new release\n\n1.  run .\/contrib\/release {major.minor.patch}. For example, .\/contrib\/release 0.16.0.\n2.  edit commit message\n3.  push commit and tags to GitHub\n" }
,{ "url": "license\/", "title": "License", "text": "LicenseThe mmdoc project is licensed under the \"CC0 1.0 Universal\". By contributing to this project, you agree to have\nyour work licensed under that license.\n\nOther licenses that might be relevant to this project:\n\n  - highlight.js source code is merged into the mmdoc binary so that it can be used in the HTML outputs.\n\n  - fuse.js source code is merged into the mmdoc binary so that it can be used in the HTML outputs.\n\n  - Some styling and HTML DOM object layout are based on mdBook source code.\n" }
]