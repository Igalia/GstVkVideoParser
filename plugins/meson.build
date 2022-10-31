pkgconfig = import('pkgconfig')

vkparser_sources = files (
  'gstvkh264dec.cpp',
  'gstvkh265dec.cpp',
  'gstvkelements.c',
  'videoutils.c',
  'plugin.c',
)

libvideoparsers_dependencies = [
    gst_dep,
    gst_video_dep,
    gst_codecparsers_dep,
    vkcodecparser_dep,
    vulkan_include_dep
]

install_headers(videoparser_headers)

gstvkparser = library('gstvkparser',
	  vkparser_sources,
	  c_args : plugin_c_args + ['-DGST_USE_UNSTABLE_API'],
    cpp_args : ['-DGST_USE_UNSTABLE_API'],
	  include_directories: include_directories('..'),
	  dependencies : libvideoparsers_dependencies,
	  install : true,
	  install_dir : plugins_install_dir,
    override_options: _override_options,
)

