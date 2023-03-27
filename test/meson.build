sources = files('test.cpp', 'dump.cpp')

gsttest = executable(
  'testapp', sources,
  dependencies: [libdemuxeres_dep, libvkvideoparser_dep, glib_dep, vulkan_include_dep],
  override_options: _override_options,
)

source_root = meson.source_root()

demuxerestest = executable(
  'testdemuxeres',
  files('testdemuxeres.cpp'),
  override_options: _override_options,
  dependencies: [libdemuxeres_dep, glib_dep]
)

h264sample = files(join_paths(source_root, 'samples', 'Sample_10.avc'))
h265sample = files(join_paths(source_root, 'samples', 'Sample_10.hevc'))

test('test', gsttest, args: ['-q',h264sample], suite: ['h264', 'gst'])
test('test', demuxerestest, args: [ h264sample], suite: ['h264', 'demuxeres'])

test('test', gsttest, args: ['-q',h265sample], suite: ['h265', 'gst'])
test('test', demuxerestest, args: [ h265sample], suite: ['h265', 'demuxeres'])
