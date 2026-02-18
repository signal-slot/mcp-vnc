# Copyright (C) 2025 Signal Slot Inc.
# SPDX-License-Identifier: LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

# Stage 1: Build
FROM debian:trixie AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    cmake \
    dpkg-dev \
    g++ \
    git \
    ninja-build \
    qt6-base-dev \
    qt6-base-private-dev \
    qt6-multimedia-dev \
    libgl-dev \
    zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

# Align submodule Qt version requirements with system Qt version
RUN QT_VER=$(qmake6 -query QT_VERSION) && \
    sed -i "s/QT_REPO_MODULE_VERSION \"[^\"]*\"/QT_REPO_MODULE_VERSION \"${QT_VER}\"/" \
    3rdparty/qtmcp/.cmake.conf 3rdparty/qtvncclient/.cmake.conf

# Fix path calculations for Debian multiarch layout.
# On Debian, Qt modules install to lib/<triplet>/ and include/<triplet>/
# but the superbuild CMakeLists.txt expects <triplet>/ directly.
RUN sed -i \
    -e 's|"${DEPS_INSTALL_PREFIX}/${QT_LIB_DIR_NAME}/cmake"|"${DEPS_INSTALL_PREFIX}/lib/${QT_LIB_DIR_NAME}/cmake"|' \
    -e 's|${DEPS_INSTALL_PREFIX}/include/qt6|${DEPS_INSTALL_PREFIX}/include/${QT_LIB_DIR_NAME}/qt6|' \
    CMakeLists.txt

RUN cmake -B build -DCMAKE_BUILD_TYPE=Release -G Ninja \
    && cmake --build build

# Collect build artifacts into a flat staging area
RUN ARCH=$(dpkg-architecture -qDEB_HOST_MULTIARCH) && \
    mkdir -p /staging/lib /staging/plugins && \
    cp -a build/qt_modules/lib/${ARCH}/libQt6*.so* /staging/lib/ && \
    cp -a build/qt_modules/lib/${ARCH}/qt6/plugins/* /staging/plugins/

# Stage 2: Runtime
FROM debian:trixie-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
    libqt6core6 \
    libqt6network6 \
    libqt6gui6 \
    libqt6widgets6 \
    libqt6multimedia6 \
    libgl1 \
    zlib1g \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /src/build/mcp-vnc/mcp-vnc /usr/bin/mcp-vnc
COPY --from=builder /staging/lib/ /usr/local/lib/
COPY --from=builder /staging/plugins/ /usr/local/lib/qt6/plugins/
RUN ldconfig

ENV QT_PLUGIN_PATH=/usr/local/lib/qt6/plugins
ENV QT_QPA_PLATFORM=offscreen

ENTRYPOINT ["/usr/bin/mcp-vnc"]
