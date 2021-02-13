src = [
  'mkdir_p.c',
  'parse.c',
  'render.c',
  'types.c',
]

shared_lib = static_library('shared', src, dependencies : [
                              asset_dep, libcmark_gfm, libfastjson, libzip
                            ])

mmdoc = executable('mmdoc',
                     [ 'mmdoc.c' ],
                     install : true,
                     pie : true,
                     link_with : [ shared_lib ])