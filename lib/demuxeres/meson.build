pkgconfig = import('pkgconfig')

codecparser_sources = files (
  'codecs/gsth264decoder.c',
  'codecs/gsth264picture.c',
  'codecs/gsth265decoder.c',
  'codecs/gsth265picture.c',
)

videoparser_sources = files(
  'vkvideodecodeparser.cpp',
  'gstvkvideoparser.cpp',
)

videoparser_headers = files(
  'gstvkvideoparser.h',
)

harness_sources = files (
  'harness/gstharness.c',
  'harness/gsttestclock.c',
)

demuxeres_sources = files(
  'gstdemuxeres.c',
)

demuxeres_headers = files(
  'gstdemuxeres.h',
)

libvideoparsers_dependencies = [
    gst_dep,
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

vkharness_static = static_library('vkharness-static', harness_sources,
    include_directories: include_directories('harness'),
    c_args: ['-DGST_USE_UNSTABLE_API', '-DBUILDING_HARNESS'],
    dependencies: libvideoparsers_dependencies,
    override_options: ['werror=false'],
    install: false)

# Declare dependency
vkharness_dep = declare_dependency(
    include_directories: ('harness'),
    link_with: vkharness_static)

install_headers(videoparser_headers)
install_headers(demuxeres_headers)



vkvideoparser = shared_library(
  'gst-vkvideo-parser',
  videoparser_sources,
  include_directories: include_directories('.', 'harness', 'codecs', 'vulkan', 'vk_video', 'vkvideo_parser'),
  cpp_args: '-DGST_USE_UNSTABLE_API',
  c_args: '-DGST_USE_UNSTABLE_API',
  install: true,
  vs_module_defs: 'gstvkvideoparser.def',
  override_options: _override_options,
  dependencies: [gst_dep, gst_codecparsers_dep, gst_video_dep, vkharness_dep],
)

libvkvideoparser_dep = declare_dependency(
  dependencies: [gst_dep, gst_codecparsers_dep, gst_video_dep],
  sources: videoparser_headers,
  include_directories: include_directories('.', 'vkvideo_parser'),
  link_with: vkvideoparser,
)

pkg_name = 'gst-vkvideo-parser'
pkgconfig.generate(vkvideoparser,
  libraries : [gst_dep, gst_codecparsers_dep, gst_video_dep],
  name : pkg_name,
  description : 'Video parsers for Vulkan video',
)

demuxeres = shared_library(
  'gstdemuxeres',
  files('gstdemuxeres.c'),
  include_directories: include_directories('.'),
  c_args: ['-DGST_USE_UNSTABLE_API','-DBUILDING_DEMUXERES'],
  install: true,
#  vs_module_defs: 'gstdemuxeres.def',
  dependencies: [gst_dep, gst_codecparsers_dep, gst_video_dep, gstapp_dep],
)


libdemuxeres_dep = declare_dependency(
  dependencies: [gst_dep],
  sources: 'gstdemuxeres.h',
  include_directories: include_directories('.'),
  link_with: demuxeres,
)

pkgconfig.generate(demuxeres,
  libraries : [gst_dep],
  name : 'gstdemuxeres',
  description : 'helper to demux elementary stream',
)