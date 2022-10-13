pkgconfig = import('pkgconfig')

sources = [
  'videoparser.cpp',
  'h264dec.cpp',
  'h265dec.cpp',
  'pipeline.cpp',
  'videoutils.c',
  'codecs/gsth264decoder.c',
  'codecs/gsth264picture.c',
  'codecs/gsth265decoder.c',
  'codecs/gsth265picture.c',
]

videoparser_headers = [
  'videoparser.h',
]
gst_req = '>= 1.20'
gst_dep = dependency('gstreamer-1.0', version: gst_req)
gst_check_dep = dependency('gstreamer-check-1.0')
gst_codecparsers_dep = dependency('gstreamer-codecparsers-1.0', version: gst_req )
gst_video_dep = dependency('gstreamer-video-1.0')

libvideoparsers_dependencies = [
    gst_dep,
    gst_check_dep,
    gst_codecparsers_dep,
    gst_video_dep,
]
install_headers(videoparser_headers)

videoparser = shared_library(
  'gst-vkvideo-parser',
  sources,
  include_directories: ['codecs', 'vulkan', 'vk_video', 'vkvideo_parser' ],
  cpp_args: '-DGST_USE_UNSTABLE_API',
  c_args: '-DGST_USE_UNSTABLE_API',
  install: true,
  dependencies: libvideoparsers_dependencies,
)

libvideoparser_dep = declare_dependency(
  dependencies: libvideoparsers_dependencies,
  sources: videoparser_headers,
  include_directories: include_directories('.', 'vkvideo_parser'),
  link_with: videoparser,
)

pkg_name = 'gst-vkvideo-parser'
pkgconfig.generate(videoparser,
  libraries : [gst_dep, gst_codecparsers_dep, gst_video_dep],
  name : pkg_name,
  description : 'Video parsers for Vulkan video',
)


