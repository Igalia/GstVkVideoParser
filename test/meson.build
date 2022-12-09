sources = files('test.cpp', 'dump.cpp')
sources_dlopen = files('testdlopen.cpp', 'dump.cpp')

gsttest = executable(
  'testapp', sources,
  dependencies: [libdemuxeres_dep, libvkvideoparser_dep, glib_dep, vulkan_include_dep],
  override_options: _override_options,
)

gstdlopentest = executable(
  'testdlopenapp', sources_dlopen,
  dependencies: [glib_dep, gmodule_dep, vulkan_include_dep, libvkvideoparser_dep.partial_dependency(includes: true), libdemuxeres_dep],
  include_directories: include_directories('../lib'),
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
  include_directories: include_directories('../lib'),
  dependencies: [ cpp.find_library('nvidia-vkvideo-parser', dirs: external_libs_dir), glib_dep, vulkan_include_dep, libvkvideoparser_dep.partial_dependency(includes: true), libdemuxeres_dep ]
)

demuxerestest = executable(
  'testdemuxeres',
  files('testdemuxeres.cpp'),
  override_options: _override_options,
  dependencies: [libdemuxeres_dep, glib_dep]
)

h264sample = files(join_paths(source_root, 'samples', 'Sample_10.avc'))
h265sample = files(join_paths(source_root, 'samples', 'Sample_10.hevc'))

test('test', gsttest, args: [h264sample], suite: ['h264', 'gst'])
test('test', nvtest, args: [ h264sample], suite: ['h264', 'nv'])
test('test', demuxerestest, args: [ h264sample], suite: ['h264', 'demuxeres'])

test('test', gsttest, args: [h265sample], suite: ['h265', 'gst'])
test('test', nvtest, args: [h265sample], suite: ['h265', 'nv'])
test('test', demuxerestest, args: [ h265sample], suite: ['h265', 'demuxeres'])
