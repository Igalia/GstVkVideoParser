
#!/usr/bin/env python3

from os import environ, path
import sys
from subprocess import call

BUILDDIR='buildfull'
if len(sys.argv) == 2:
    BUILDDIR = sys.argv[1]

gst_options = [
'--default-library=static',
'--force-fallback-for=gstreamer-1.0,glib,libffi,pcre2',
'-Dauto_features=disabled',
'-Dglib:tests=false',
'-Djson-glib:tests=false',
'-Dpcre2:test=false',
'-Dvkparser_standalone=enabled',
'-Dgstreamer-1.0:libav=disabled',
'-Dgstreamer-1.0:ugly=disabled',
'-Dgstreamer-1.0:ges=disabled',
'-Dgstreamer-1.0:devtools=disabled',
'-Dgstreamer-1.0:default_library=static',
'-Dgstreamer-1.0:rtsp_server=disabled',
'-Dgstreamer-1.0:gst-full-target-type=static_library',
'-Dgstreamer-1.0:gst-full-libraries=gstreamer-video-1.0, gstreamer-audio-1.0, gstreamer-app-1.0, gstreamer-codecparsers-1.0',
'-Dgstreamer-1.0:tools=disabled',
'-Dgst-plugins-base:playback=enabled',
'-Dgst-plugins-base:app=enabled',
'-Dgst-plugins-bad:videoparsers=enabled',
'-Dgst-plugins-base:typefind=enabled'
]

is_windows = sys.platform.startswith('win')
if is_windows:
    gst_options += ['--vsenv']

cmdline = ['meson', BUILDDIR] + gst_options
call(cmdline)

