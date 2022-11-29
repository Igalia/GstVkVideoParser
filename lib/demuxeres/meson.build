demuxeres_sources = files(
  'gstdemuxeres.c',
)

demuxeres_headers = files(
  'gstdemuxeres.h',
)

install_headers(demuxeres_headers)

demuxer_es_deps = [gst_dep, gst_video_dep, gst_audio_dep, gstapp_dep]

demuxeres = shared_library(
  'gstdemuxeres',
  files('gstdemuxeres.c'),
  include_directories: include_directories('.'),
  c_args: ['-DGST_USE_UNSTABLE_API','-DBUILDING_DEMUXERES'],
  install: true,
#  vs_module_defs: 'gstdemuxeres.def',
  dependencies: demuxer_es_deps,
)

libdemuxeres_dep = declare_dependency(
  dependencies: demuxer_es_deps,
  sources: 'gstdemuxeres.h',
  include_directories: include_directories('.'),
  link_with: demuxeres,
)

pkgconfig.generate(demuxeres,
  libraries : demuxer_es_deps,
  name : 'gstdemuxeres',
  description : 'helper to demux elementary stream',
)