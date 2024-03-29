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

cc = meson.get_compiler('c')
cxx = meson.get_compiler('cpp')

if cxx.has_link_argument('-Wl,-z,nodelete')
  add_project_link_arguments('-Wl,-z,nodelete', language: 'cpp')
endif
plugins_install_dir = join_paths(get_option('libdir'), 'gstreamer-1.0')

gstreamer_deps = []
glib_deps = []

gst_fallback = []
if get_option('vkparser_standalone').enabled()
  gst_fallback = ['gstreamer-1.0']
endif

gstreamer_full_dep = dependency('gstreamer-full-1.0', fallback: gst_fallback, required :false)

if gstreamer_full_dep.found()
  gstreamer_deps += gstreamer_full_dep
  glib_deps = gstreamer_full_dep
else
  gst_dep = dependency('gstreamer-1.0', version: gst_req)
  gst_video_dep = dependency('gstreamer-video-1.0', version: gst_req)
  gst_audio_dep = dependency('gstreamer-audio-1.0', version: gst_req)
  gst_codecparsers_dep = dependency('gstreamer-codecparsers-1.0', version: gst_req )
  gstapp_dep = dependency('gstreamer-app-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'app_dep'])
  glib_dep = dependency('glib-2.0')
  gmodule_dep = dependency('gmodule-2.0')
  gstreamer_deps += [gst_dep, gst_video_dep, gst_audio_dep, gst_codecparsers_dep, gstapp_dep]
  glib_deps += [glib_dep, gmodule_dep]
endif

subdir('include')
subdir('lib')
subdir('test')
