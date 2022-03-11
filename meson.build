project('gst-vkvideo-parser', 'cpp',
  default_options : [ 'warning_level=1', 'buildtype=debug' ]
)

sources = [
  'videoparser.cpp'
]

videoparser = shared_library(
  'gst-vkvideo-parser',
  sources,
  dependencies: [
    dependency('gstreamer-1.0', method: 'pkg-config'),
  ],
)

executable(
  'testapp', [ 'test.cpp' ],
  link_with: videoparser,
)
