# Video parsing library

The purpose of this library is to propose a replacement to the closed and
proprietary NVIDIA's library used in [Vulkan Video
Samples](https://github.com/nvpro-samples/vk_video_samples) and [Conformance
Test Suite](https://github.com/KhronosGroup/VK-GL-CTS), with one using
[GStreamer](https://gstreamer.freedesktop.org/). It aims to be API compatible.

Currently supports H.264 anh H.265 streams.

## Setup

### Ubuntu/Debian/etc

```sh
apt install python3-pip ninja-build pkg-config
pip3 install --user meson
apt install libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libgstreamer-plugins-bad1.0-dev gstreamer1.0-plugins-bad
```

### Windows

```sh
choco install --yes pkgconfiglite
choco install --yes gstreamer gstreamer-devel
```

## Build

### Windows

   1. Open a Developer Command Prompt for VS201x
   2. Go to root of the cloned git repository
   3. set PKG_CONFIG_PATH: C:\gstreamer\1.0\msvc_x86_64\lib\pkgconfig

### Common

```sh
    $ meson builddir
    $ ninja -C builddir
    $ meson builddir test
```