pkgconfig = import('pkgconfig')

videoparser_sources = files(
  'vkvideodecodeparser.cpp',
  'gstvkvideoparser.cpp',
)

videoparser_headers = files(
  'gstvkvideoparser.h',
)

install_headers(videoparser_headers)


vkvideoparser_dependencies = [
  gstreamer_deps,
  vulkan_include_dep,
  vkcodecparser_dep,
  vkharness_dep,
]

vkvideoparser_args = [
  '-DGST_USE_UNSTABLE_API'
]

if get_option('vkparser_standalone').disabled()
  vkvideoparser_args += ['-DVKPARSER_EXTERNAL_PLUGIN']
else
  vkvideoparser_dependencies += gstvkparser_dep
endif


vkvideoparser = shared_library(
  'gstvkvideoparser',
  videoparser_sources,
  include_directories: include_directories('.'),
  cpp_args: vkvideoparser_args,
  c_args: vkvideoparser_args,
  install: true,
  vs_module_defs: 'gstvkvideoparser.def',
  override_options: _override_options,
  dependencies: vkvideoparser_dependencies,
)

libvkvideoparser_dep = declare_dependency(
  sources: videoparser_headers,
  include_directories: include_directories('.'),
  link_with: vkvideoparser,
)

pkg_name = 'gstvkvideoparser'
pkgconfig.generate(vkvideoparser,
  libraries : gstreamer_deps,
  name : pkg_name,
  description : 'Video parsers for Vulkan video',
)
