harness_sources = files (
  'gstharness.c',
  'gsttestclock.c',
)

vkharness_static = static_library('vkharness-static', harness_sources,
    include_directories: include_directories('.'),
    c_args: ['-DGST_USE_UNSTABLE_API', '-DBUILDING_HARNESS'],
    dependencies: libvideoparsers_dependencies,
    override_options: ['werror=false'],
    install: false)

# Declare dependency
vkharness_dep = declare_dependency(
    include_directories: ('.'),
    link_with: vkharness_static)