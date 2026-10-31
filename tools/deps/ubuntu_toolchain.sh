#!/bin/sh
# Installs the toolchain and system packages for building the SDK, OGRE and the game on Ubuntu
# 22.04 (CI's image: glibc 2.35 is the floor of everything built there): clang 21 from LLVM's
# repository (the same version as the development machines), libstdc++ 13 (jammy's 11 has no
# <format>), CMake from Kitware (jammy's is 3.22), the SDK's packages (its own CI's list) and
# OGRE's. Run as root in a fresh ubuntu:22.04.
set -eu
export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends ca-certificates curl wget gnupg git lsb-release \
  software-properties-common
wget -qO /tmp/llvm.sh https://apt.llvm.org/llvm.sh
chmod +x /tmp/llvm.sh
/tmp/llvm.sh 21
apt-get install -y clang-21 lld-21
update-alternatives --install /usr/bin/clang clang /usr/bin/clang-21 210
update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-21 210
wget -qO- https://packages.lunarg.com/lunarg-signing-key-pub.asc > /etc/apt/trusted.gpg.d/lunarg.asc
wget -qO /etc/apt/sources.list.d/lunarg-vulkan-jammy.list \
  http://packages.lunarg.com/vulkan/lunarg-vulkan-jammy.list
add-apt-repository -y ppa:ubuntu-toolchain-r/test
wget -qO- https://apt.kitware.com/keys/kitware-archive-latest.asc | gpg --dearmor \
  -o /usr/share/keyrings/kitware.gpg
echo "deb [signed-by=/usr/share/keyrings/kitware.gpg] https://apt.kitware.com/ubuntu/ jammy main" \
  > /etc/apt/sources.list.d/kitware.list
apt-get update
apt-get install -y cmake ninja-build build-essential g++-13 unzip zip autoconf python3 python3-venv \
  libgtk-3-dev libx11-xcb-dev libxss-dev vulkan-sdk \
  libwayland-dev libwayland-bin wayland-protocols libxkbcommon-dev libdecor-0-dev \
  libasound2-dev libpulse-dev libpipewire-0.3-dev \
  libx11-dev libxrandr-dev libegl-dev libgl-dev zlib1g-dev zstd file
git config --global --add safe.directory '*'
clang++ --version | head -1
