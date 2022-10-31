pkgconfig = import('pkgconfig')

codecparser_sources = files (
  'codecs/gsth264decoder.c',
  'codecs/gsth264picture.c',
  'codecs/gsth265decoder.c',
  'codecs/gsth265picture.c',
)

sources = files(
  'vkvideodecodeparser.cpp',
  'gstvkvideoparser.cpp',
)

videoparser_headers = files(
  'gstvkvideoparser.h',
)


libvideoparsers_dependencies = [
    gst_dep,
    gst_check_dep,
    gst_codecparsers_dep,
    gst_video_dep,
]

vkcodecparser_static = static_library('vkcodecparser-static', codecparser_sources,
    c_args: ['-DGST_USE_UNSTABLE_API'],
    dependencies: libvideoparsers_dependencies,
    override_options: ['werror=false'],
    install: false)

# Declare dependency
vkcodecparser_dep = declare_dependency(
    include_directories: ('codecs'),
    link_with: vkcodecparser_static)

vulkan_include_dep = declare_dependency(
    include_directories: ['.', 'vk_video','vkvideo_parser', 'vulkan'],
    )

install_headers(videoparser_headers)



videoparser = shared_library(
  'gst-vkvideo-parser',
  sources,
  include_directories: include_directories('.', 'codecs', 'vulkan', 'vk_video', 'vkvideo_parser'),
  cpp_args: '-DGST_USE_UNSTABLE_API',
  c_args: '-DGST_USE_UNSTABLE_API',
  install: true,
  vs_module_defs: 'gstvkvideoparser.def',
  override_options: _override_options,
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
