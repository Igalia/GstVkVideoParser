project('gst-vkvideo-parser', 'c', 'cpp',
  default_options : [ 'buildtype=debug', 'cpp_std=c++17' ],
  version: '1.21.0'
)


gst_version = meson.project_version()
api_version = '1.0'

gst_req = '>= 1.20'

plugin_c_args = ['-DHAVE_CONFIG_H']

cdata = configuration_data()
cdata.set_quoted('PACKAGE_VERSION', gst_version)
cdata.set_quoted('PACKAGE', 'gst-gstvkparser-plugin')
cdata.set_quoted('GST_LICENSE', 'LGPL')
cdata.set_quoted('GST_API_VERSION', api_version)
cdata.set_quoted('GST_PACKAGE_NAME', 'GStreamer VK Video Parser')
cdata.set_quoted('GST_PACKAGE_ORIGIN', 'https://gstreamer.freedesktop.org')
configure_file(output : 'config.h', configuration : cdata)

_override_options = []
build_system = build_machine.system()
if build_system == 'windows'
  _override_options = ['cpp_std=vc++latest']
endif

cxx = meson.get_compiler('cpp')

if cxx.has_link_argument('-Wl,-z,nodelete')
  add_project_link_arguments('-Wl,-z,nodelete', language: 'cpp')
endif
plugins_install_dir = join_paths(get_option('libdir'), 'gstreamer-1.0')

gst_dep = dependency('gstreamer-1.0', version: gst_req)
gst_check_dep = dependency('gstreamer-check-1.0', version: gst_req)
gst_video_dep = dependency('gstreamer-video-1.0', version: gst_req)
gst_codecparsers_dep = dependency('gstreamer-codecparsers-1.0', version: gst_req )

subdir('lib')
subdir('plugins')
subdir('test')
