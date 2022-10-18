
glib_dep = dependency('glib-2.0')
executable(
  'testapp', [ 'test.cpp', 'dump.cpp' ],
  dependencies: [libvideoparser_dep, glib_dep],
  override_options: _override_options,
)

cpp = meson.get_compiler('cpp')

if build_system == 'windows'
  external_libs_dir = join_paths(meson.source_root(), 'external_libs', 'wddm2_amd64_release')
else
  external_libs_dir = join_paths(meson.source_root(), 'external_libs', 'linux_amd64_release')
endif

executable(
  'nvtestapp', [ 'test.cpp', 'dump.cpp' ],
  override_options: _override_options,
  include_directories: include_directories('../lib', '../lib/vkvideo_parser'),
  dependencies: [ cpp.find_library('nvidia-vkvideo-parser', dirs: external_libs_dir), glib_dep ]
)
