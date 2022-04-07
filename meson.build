project('gst-vkvideo-parser', 'c', 'cpp',
  default_options : [ 'warning_level=1', 'buildtype=debug' ]
)

cpp_sources = [
  'videoparser.cpp',
]

c_sources = [
  'pipeline.c',
  'h264dec.c',
  'codecs/gsth264decoder.c',
  'codecs/gsth264picture.c',
]

gst_dep = dependency('gstreamer-1.0', method: 'pkg-config')
gst_codecparsers_dep = dependency('gstreamer-codecparsers-1.0', method: 'pkg-config')

gstvideoparser = static_library(
  'gstvideoparser', c_sources,
  include_directories: 'codecs',
  c_args: '-DGST_USE_UNSTABLE_API',
  dependencies: [
    gst_dep, gst_codecparsers_dep,
    dependency('gstreamer-app-1.0', method: 'pkg-config'),
    dependency('gstreamer-video-1.0', method: 'pkg-config'),
  ],
)

videoparser = shared_library(
  'gst-vkvideo-parser',
  cpp_sources,
  cpp_args: '-DGST_USE_UNSTABLE_API',
  link_with: gstvideoparser,
  dependencies: [ gst_dep, gst_codecparsers_dep, ],
)

executable(
  'testapp', [ 'test.cpp' ],
  link_with: videoparser,
)

cpp = meson.get_compiler('cpp')
executable(
  'nvtestapp', [ 'test.cpp' ],
  dependencies: [ cpp.find_library('nvidia-vkvideo-parser', dirs: meson.source_root()) ]
)
