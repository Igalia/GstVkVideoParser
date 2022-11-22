demuxeres_sources = files(
  'gstdemuxeres.c',
)

demuxeres_headers = files(
  'gstdemuxeres.h',
)

install_headers(demuxeres_headers)

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