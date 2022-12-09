codecparser_sources = files (
  'gsth264decoder.c',
  'gsth264picture.c',
  'gsth265decoder.c',
  'gsth265picture.c',
)

vkcodecparser_static = static_library('vkcodecparser-static', codecparser_sources,
    c_args: ['-DGST_USE_UNSTABLE_API'],
    dependencies: libvideoparsers_dependencies,
    override_options: ['werror=false'],
    install: false)

# Declare dependency
vkcodecparser_dep = declare_dependency(
    include_directories: ('.'),
    link_with: vkcodecparser_static)