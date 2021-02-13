src = [
  'mkdir_p.c',
  'parse.c',
  'render.c',
  'types.c',
]

shared_lib = static_library('shared', src, dependencies : [
                              asset_dep, libcmark_gfm, libfastjson, libzip
                            ])

mmdoc_conf = configuration_data()
mmdoc_conf.set_quoted('MMDOC_VERSION', meson.project_version())

configure_file(input : 'mmdocconfig.h.in',
  output : 'mmdocconfig.h',
  configuration : mmdoc_conf)


mmdoc = executable('mmdoc',
                     [ 'mmdoc.c' ],
                     install : true,
                     pie : true,
                     link_with : [ shared_lib ])