src = [
  'anchors.c',
  'asset.c',
  'html.c',
  'epub.c',
  'files.c',
  'inputs.c',
  'man.c',
  'mkdir_p.c',
  'multi.c',
  'parse.c',
  'render.c',
  'refs.c',
  'single.c',
  'types.c',
]

src += asset_headers

shared_lib = static_library(
           'shared',
           src,
           dependencies : [
             libcmark_gfm, libfastjson, libzip
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