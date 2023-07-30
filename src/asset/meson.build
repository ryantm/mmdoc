assets = [
  'epub.css',
  'fuse.basic.min.js',
  'highlight.pack.js',
  'mmdoc.css',
  'a11y-dark.css',
  'a11y-light.css',
  'mmdoc.js',
  'mmdoc_search.js',
]

asset_headers = []
foreach asset : assets
  asset_headers += embed_c.process(asset)
endforeach
