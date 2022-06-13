project('gst-vkvideo-parser', 'c', 'cpp',
  default_options : [ 'warning_level=1', 'buildtype=debug' ]
)

sources = [
  'videoparser.cpp',
  'h264dec.cpp',
  'pipeline.c',
  'videoutils.c',
  'codecs/gsth264decoder.c',
  'codecs/gsth264picture.c',
]

videoparser = shared_library(
  'gst-vkvideo-parser',
  sources,
  include_directories: ['codecs', 'vulkan', 'vk_video' ],
  cpp_args: '-DGST_USE_UNSTABLE_API',
  c_args: '-DGST_USE_UNSTABLE_API',
  dependencies: [
    dependency('gstreamer-1.0', method: 'pkg-config'),
    dependency('gstreamer-check-1.0', method: 'pkg-config'),
    dependency('gstreamer-codecparsers-1.0', method: 'pkg-config'),
    dependency('gstreamer-video-1.0', method: 'pkg-config'),
  ],
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
