# Local Linux x86_64 cross-build environment for the SDL3 shell. Runs natively
# on the arm64 Docker host and cross-compiles with Debian's x86_64 GCC. SDL
# loads X11/Wayland/audio libraries at run time; only their headers are needed.
# squashfs-tools packs the AppImage file system (tools/desktop/appimage.py).
FROM debian@sha256:a29215f6a35e51e22adffa17f89e9d2ef06214e64a2bad10d765c46aea49f11f
RUN dpkg --add-architecture amd64 \
 && apt-get update \
 && apt-get install -y --no-install-recommends \
      cmake ninja-build python3 file binutils-x86-64-linux-gnu g++-x86-64-linux-gnu \
      pkg-config squashfs-tools libc6-dev:amd64 \
      libx11-dev:amd64 libxext-dev:amd64 libxrandr-dev:amd64 libxcursor-dev:amd64 libxfixes-dev:amd64 \
      libxi-dev:amd64 libxss-dev:amd64 libxtst-dev:amd64 libxkbcommon-dev:amd64 \
      libwayland-dev:amd64 wayland-protocols libdecor-0-dev:amd64 libegl-dev:amd64 libgl-dev:amd64 \
      libgles-dev:amd64 libdrm-dev:amd64 libgbm-dev:amd64 libasound2-dev:amd64 libpulse-dev:amd64 \
      libpipewire-0.3-dev:amd64 libdbus-1-dev:amd64 libudev-dev:amd64 \
 && rm -rf /var/lib/apt/lists/*
ENV PKG_CONFIG_LIBDIR=/usr/lib/x86_64-linux-gnu/pkgconfig:/usr/share/pkgconfig
