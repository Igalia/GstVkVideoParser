glib_dep = dependency('glib-2.0')
gmodule_dep = dependency('gmodule-2.0')

sources = files('test.cpp', 'dump.cpp')
sources_dlopen = files('testdlopen.cpp', 'dump.cpp')

gsttest = executable(
  'testapp', sources,
  dependencies: [libvkvideoparser_dep, glib_dep],
  override_options: _override_options,
)

gstdlopentest = executable(
  'testdlopenapp', sources_dlopen,
  dependencies: [glib_dep, gmodule_dep],
  include_directories: include_directories('../lib', '../lib/vkvideo_parser'),
  override_options: _override_options,
)

cpp = meson.get_compiler('cpp')
source_root = meson.source_root()

if build_system == 'windows'
  external_libs_dir = join_paths(source_root, 'external_libs', 'wddm2_amd64_release')
else
  external_libs_dir = join_paths(source_root, 'external_libs', 'linux_amd64_release')
endif

nvtest = executable(
  'nvtestapp', sources,
  override_options: _override_options,
  include_directories: include_directories('../lib', '../lib/vkvideo_parser'),
  dependencies: [ cpp.find_library('nvidia-vkvideo-parser', dirs: external_libs_dir), glib_dep ]
)

h264sample = files(join_paths(source_root, 'samples', 'Sample_10.avc'))
h265sample = files(join_paths(source_root, 'samples', 'Sample_10.hevc'))

test('test', gsttest, args: ['-c', 'h264', h264sample], suite: ['h264', 'gst'])
test('test', nvtest, args: ['-c', 'h264', h264sample], suite: ['h264', 'nv'])

test('test', gsttest, args: ['-c', 'h265', h265sample], suite: ['h265', 'gst'])
test('test', nvtest, args: ['-c', 'h265', h265sample], suite: ['h265', 'nv'])
