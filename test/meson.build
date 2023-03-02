source_root = meson.source_root()

sources = files('test.cpp', 'dump.cpp')
sources_dlopen = files('testdlopen.cpp', 'dump.cpp')

h264sample = files(join_paths(source_root, 'samples', 'Sample_10.avc'))
h265sample = files(join_paths(source_root, 'samples', 'Sample_10.hevc'))


if build_system == 'windows'
  external_libs_dir = join_paths(source_root, 'external_libs', 'wddm2_amd64_release')
else
  external_libs_dir = join_paths(source_root, 'external_libs', 'linux_amd64_release')
endif


demuxerestest = executable(
  'testdemuxeres',
  files('testdemuxeres.cpp'),
  override_options: _override_options,
  dependencies: [libdemuxeres_dep]
)


if get_option('vkparser_standalone').disabled()

  gsttest = executable(
    'testapp', sources,
    dependencies: [glib_deps, libvkvideoparser_dep, libdemuxeres_dep, vulkan_include_dep],
    override_options: _override_options,
  )

  gstdlopentest = executable(
    'testdlopenapp', sources_dlopen,
    dependencies: [glib_deps, vulkan_include_dep, libvkvideoparser_dep.partial_dependency(includes: true), libdemuxeres_dep],
    include_directories: include_directories('../lib'),
    override_options: _override_options,
  )

  nvtest = executable(
    'nvtestapp', sources,
    override_options: _override_options,
    include_directories: include_directories('../lib'),
    dependencies: [ cxx.find_library('nvidia-vkvideo-parser', dirs: external_libs_dir), vulkan_include_dep, libvkvideoparser_dep.partial_dependency(includes: true), libdemuxeres_dep ]
  )

  test('test', gsttest, args: ['-q',h264sample], suite: ['h264', 'gst'])
  test('test', nvtest, args: ['-q',h264sample], suite: ['h264', 'nv'])


  test('test', gsttest, args: ['-q',h265sample], suite: ['h265', 'gst'])
  test('test', nvtest, args: ['-q',h265sample], suite: ['h265', 'nv'])

  test('test', demuxerestest, args: [ h264sample], suite: ['h264', 'demuxeres'])
  test('test', demuxerestest, args: [ h265sample], suite: ['h265', 'demuxeres'])
endif


gsttestes = executable(
  'testesapp', files('testes.cpp', 'dump.cpp'),
  dependencies: [glib_deps, libvkvideoparser_dep, vulkan_include_dep],
  override_options: _override_options,
)
test('test', gsttestes, args: ['-c', 'h264',h264sample], suite: ['h264', 'gstes'])
test('test', gsttestes, args: ['-c', 'h265', h265sample], suite: ['h265', 'gstes'])

