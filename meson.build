project('mmdoc', 'c',
  version : '0.30.7',
  license : 'CC0-1.0',
  default_options : ['c_std=gnu23'],
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

threads = dependency('threads', required: true)

subdir('src')
subdir('test')
