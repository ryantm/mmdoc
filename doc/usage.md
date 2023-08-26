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
