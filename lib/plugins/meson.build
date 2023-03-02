pkgconfig = import('pkgconfig')

vkparser_sources = files (
  'gstvkh264dec.cpp',
  'gstvkh265dec.cpp',
  'gstvkelements.c',
  'videoutils.c',
  'plugin.c',
)

libvideoparsers_dependencies = [
    gstreamer_deps,
    vkcodecparser_dep,
    vulkan_include_dep
]

gstvkparser_target_type = 'shared_library'
if get_option('vkparser_standalone').enabled()
  gstvkparser_target_type = 'static_library'
endif

gstvkparser = build_target('gstvkparser',
	  vkparser_sources,
    target_type: gstvkparser_target_type,
	  c_args : plugin_c_args + ['-DGST_USE_UNSTABLE_API'],
    cpp_args : ['-DGST_USE_UNSTABLE_API'],
	  include_directories: include_directories('../..'),
	  dependencies : libvideoparsers_dependencies,
	  install : true,
	  install_dir : plugins_install_dir,
    override_options: _override_options,
)

gstvkparser_dep = declare_dependency(
  dependencies: libvideoparsers_dependencies,
  include_directories: include_directories('.'),
  link_with: gstvkparser,
)
