project('gst-vkvideo-parser', 'c', 'cpp',
  default_options : [ 'warning_level=1', 'buildtype=debug'],
  version: '0.1.0'
)
build_system = build_machine.system()

cpp = meson.get_compiler('cpp')
if cpp.get_id() == 'msvc'
  add_project_arguments('/std:c++latest', language : 'cpp')
else
  add_project_arguments('-std=c++17', language : 'cpp')
endif 


subdir('lib')
subdir('test')
