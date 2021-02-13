assets = [
  'epub.css',
  'fuse.basic.min.js',
  'highlight.pack.js',
  'minimal.css',
  'mono-blue.css',
  'search.js',
]

asset_headers = []
foreach asset : assets
  asset_headers += embed_c.process(asset)
endforeach

asset_lib = static_library('assets', asset_headers)

asset_dep = declare_dependency(
   link_with: asset_lib,
   include_directories: asset_lib.private_dir_include()
)
