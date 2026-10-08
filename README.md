# ModLoader
## Pre-requisites
### Windows 
Install [Visual Studio C++ build tools](https://visualstudio.microsoft.com/downloads/?q=build+tools) and the [Windows SDK](https://learn.microsoft.com/en-us/windows/apps/windows-sdk/downloads).

### Fedora 43/44
```
dnf install -y \
  git cmake ninja-build clang llvm llvm-devel \
  clang-devel libclang-devel lld \
  gcc gcc-c++ glibc-devel binutils \
  make pkgconf-pkg-config python3 \
  rust cargo \
  openssl-devel \
  alsa-lib-devel pulseaudio-libs-devel \
  pipewire-devel \
  libX11-devel libXext-devel libXrandr-devel \
  libXcursor-devel libXfixes-devel libXi-devel \
  libXScrnSaver-devel libXtst-devel \
  libxkbcommon-devel \
  wayland-devel wayland-protocols-devel \
  libdrm-devel mesa-libgbm-devel \
  mesa-libGL-devel mesa-libEGL-devel \
  mesa-libGLES-devel vulkan-headers \
  vulkan-loader-devel \
  systemd-devel dbus-devel libusb1-devel \
  libdecor-devel
```
### Debian 13/Ubuntu 24.04 (may require a newer Rust toolchain than provided in default repos.)
```
apt update
apt install -y \
  build-essential git cmake ninja-build \
  clang llvm lld libclang-dev llvm-dev \
  pkg-config python3 python3-dev \
  rustc cargo libssl-dev \
  libasound2-dev libpulse-dev libpipewire-0.3-dev \
  libx11-dev libxext-dev libxrandr-dev \
  libxcursor-dev libxfixes-dev libxi-dev \
  libxss-dev libxtst-dev libxkbcommon-dev \
  libwayland-dev wayland-protocols \
  libdrm-dev libgbm-dev \
  libgl1-mesa-dev libegl1-mesa-dev \
  libgles2-mesa-dev libvulkan-dev \
  libudev-dev libdbus-1-dev \
  libusb-1.0-0-dev libdecor-0-dev
```
### Arch
```
pacman -S --needed \
  base-devel git cmake ninja \
  clang llvm lld rust python pkgconf \
  openssl \
  alsa-lib libpulse pipewire \
  libx11 libxext libxrandr libxcursor \
  libxfixes libxi libxss libxtst \
  libxkbcommon wayland wayland-protocols \
  libdrm mesa vulkan-headers vulkan-icd-loader \
  systemd-libs dbus libusb libdecor
```

## SDK

To build the SDK:

```sh
git clone --recurse-submodules https://github.com/ModLoader64/ModLoader.git
cd ModLoader
git submodule update --init --recursive
mkdir build-sdk
cd build-sdk
cmake -G Ninja ../sdk
cmake --build .
```

See `build-sdk/dist`

## Client

To build the client (SDK must be built first):

```sh
mkdir build-client
cd build-client
cmake -G Ninja .. "-DMODLOADER_SDK=/path/to/sdk"
cmake --build .
```

See `build-client/dist`

