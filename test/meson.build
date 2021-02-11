exe = executable('unittest', ['test.c'],
                 link_with : [ shared_lib ])

test_conf = configuration_data()
test_conf.set_quoted('TEST_EXAMPLE_DIR', meson.current_source_dir() + '/example/')

configure_file(input : 'testconfig.h.in',
  output : 'testconfig.h',
  configuration : test_conf)

test('unittest', exe)
