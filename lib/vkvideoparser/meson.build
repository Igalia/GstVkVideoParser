pkgconfig = import('pkgconfig')



videoparser_sources = files(
  'vkvideodecodeparser.cpp',
  'gstvkvideoparser.cpp',
)

videoparser_headers = files(
  'gstvkvideoparser.h',
)

install_headers(videoparser_headers)

vkvideoparser = shared_library(
  'gstvkvideoparser',
  videoparser_sources,
  include_directories: include_directories('.'),
  cpp_args: '-DGST_USE_UNSTABLE_API',
  c_args: '-DGST_USE_UNSTABLE_API',
  install: true,
  vs_module_defs: 'gstvkvideoparser.def',
  override_options: _override_options,
  dependencies: [gst_dep, gst_video_dep, vulkan_include_dep, vkcodecparser_dep, vkharness_dep],
)

libvkvideoparser_dep = declare_dependency(
  dependencies: [gst_dep, gst_codecparsers_dep, gst_video_dep],
  sources: videoparser_headers,
  include_directories: include_directories('.'),
  link_with: vkvideoparser,
)

pkg_name = 'gstvkvideoparser'
pkgconfig.generate(vkvideoparser,
  libraries : [gst_dep, gst_codecparsers_dep, gst_video_dep],
  name : pkg_name,
  description : 'Video parsers for Vulkan video',
)
