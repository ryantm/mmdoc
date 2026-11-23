# Usage {#usage}

## toc.md

The cross-references in `toc.md` select the source files to publish and set their reading order.

## Reading the manual as Markdown {#agent-markdown}

The usual `mmdoc PROJECT SRC OUT` command writes `llms.txt` at the root of
`OUT/multi/`. An agent can read this index and follow a chapter link, such as
`usage/index.md`, to fetch just the relevant text. For the whole manual in one
request, fetch `llms-full.txt` from the same directory.

Both forms keep code examples, tables, and mmdoc's explicit `{#id}` anchors.
Cross-chapter links in individual chapters point to the corresponding Markdown
file. In the full manual they point to an anchor within that file.

When publishing from a branch on GitHub Pages, add an empty `.nojekyll` file
at the published root so Pages serves the Markdown chapters at their original URLs.

## Extensions beyond CommonMark

In order to be useful for Documentation, it uses several CommonMark extensions available in GitHub-flavored Markdown and [Pandoc](https://pandoc.org/).

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

  ::: warning
  this is your last warning
  :::

  ```text
  ::: {.example #anchor}
  this is your last warning
  :::
  ```
  ::: {.example #anchor}
  this is your last warning
  :::

* DD Lists
  ```text
  term 1

  : defintion 1

  term 2

  : defintion 2
  ```

## Generated interface

The table of contents, search, and theme controls are available from the top
navigation bar. Press `/` or <kbd>Control</kbd>/<kbd>Command</kbd>+<kbd>K</kbd>
to open search, then use the up and down arrow keys to cycle through results.
Outside interactive controls, the left and right arrow keys move between pages.
A skip link appears when focused so keyboard users can move directly to the
main content.
