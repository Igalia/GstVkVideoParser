executable(
  'testapp', [ 'test.cpp', 'dump.cpp' ],
  dependencies: libvideoparser_dep,
)

lib_include = include_directories('../lib')

cpp = meson.get_compiler('cpp')
executable(
  'nvtestapp', [ 'test.cpp', 'dump.cpp' ],
  include_directories: lib_include,
  dependencies: [ cpp.find_library('nvidia-vkvideo-parser', dirs: meson.source_root()) ]
)
