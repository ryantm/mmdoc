src = [
  'mkdir_p.c',
  'parse.c',
  'render.c',
  'types.c',
]

shared_lib = static_library('shared', src, dependencies : [
                              asset_dep, libcmark_gfm, libfastjson, libzip
                            ])

mmbuild = executable('mmbuild',
                     [ 'mmdoc.c' ],
                     install : true,
                     pie : true,
                     link_with : [ shared_lib ])

