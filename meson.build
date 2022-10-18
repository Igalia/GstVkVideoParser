project('gst-vkvideo-parser', 'c', 'cpp',
  default_options : [ 'buildtype=debug', 'cpp_std=c++17' ],
  version: '0.1.0'
)

_override_options = []
build_system = build_machine.system()
if build_system == 'windows'
  _override_options = ['cpp_std=vc++latest']
endif

subdir('lib')
subdir('test')
