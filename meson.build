project('gst-vkvideo-parser', 'cpp',
  default_options : [ 'warning_level=1', 'buildtype=debug' ]
)

sources = [
  'videoparser.cpp',
  'pipeline.cpp'
]

videoparser = shared_library(
  'gst-vkvideo-parser',
  sources,
  dependencies: [
    dependency('gstreamer-1.0', method: 'pkg-config'),
    dependency('gstreamer-app-1.0', method: 'pkg-config'),
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
