# Usage {#usage}

## toc.md

`toc.md` is a special file for `mmdoc`. The order of the cross references in `toc.md` control the order the sections appear in the single-page HTML and ePub outputs. If a section is not referenced in `toc.md` it does not get included in any of the outputs.

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
