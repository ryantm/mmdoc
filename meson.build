project('mmdoc', 'c',
  version : '0.19.0',
  license : 'CC0-1.0',
)

possible_cc_flags = [
  '-ffast-math',
  '-fno-common',
  '-fdiagnostics-show-option',
  '-fno-strict-aliasing',
  '-fvisibility=hidden',
  '-fcf-protection',
  '--param=ssp-buffer-size=4'
]

possible_link_flags = [
  '-Wl,-z,relro',
  '-Wl,-z,now',
  '-Wl,--gc-sections',
  '-fstack-protector'
]

cc = meson.get_compiler('c')
add_project_arguments(cc.get_supported_arguments(possible_cc_flags), language : 'c')
add_project_link_arguments(cc.get_supported_link_arguments(possible_link_flags), language : 'c')

libcmark_gfm = dependency('libcmark-gfm', required: true)
libfastjson = dependency('libfastjson', required: true)
libzip = dependency('libzip', required: true)

xxd_prog = find_program('xxd')


embed_c = generator(xxd_prog,
   output : '@PLAINNAME@.h',
   arguments : [ '-i', '@INPUT@', '@OUTPUT@'])

subdir('src/asset')
subdir('src')
subdir('test')
