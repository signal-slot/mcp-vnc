# Copyright (C) 2025 Signal Slot Inc.
# SPDX-License-Identifier: LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

cmake_minimum_required(VERSION 3.16)
project(mcp-vnc-superbuild)

include(ExternalProject)

find_package(Qt6 REQUIRED COMPONENTS Core)

set(QT_TOOLCHAIN_FILE "${Qt6_DIR}/qt.toolchain.cmake")
if(NOT EXISTS "${QT_TOOLCHAIN_FILE}")
    message(FATAL_ERROR "Qt toolchain file not found at: ${QT_TOOLCHAIN_FILE}")
endif()

set(DEPS_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/qt_modules")
get_filename_component(QT_PREFIX "${Qt6_DIR}/../../.." ABSOLUTE)
get_filename_component(QT_LIB_DIR "${Qt6_DIR}/../.." ABSOLUTE)
get_filename_component(QT_LIB_DIR_NAME "${QT_LIB_DIR}" NAME)
set(DEPS_CMAKE_DIR "${DEPS_INSTALL_PREFIX}/${QT_LIB_DIR_NAME}/cmake")

ExternalProject_Add(ep_qtmcp
    SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/qtmcp"
    CMAKE_ARGS
        -DCMAKE_TOOLCHAIN_FILE=${QT_TOOLCHAIN_FILE}
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DCMAKE_INSTALL_PREFIX=${DEPS_INSTALL_PREFIX}
        -DQT_BUILD_TESTS=OFF
        -DQT_BUILD_EXAMPLES=OFF
    BUILD_ALWAYS ON
)

ExternalProject_Add(ep_qtvncclient
    SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/qtvncclient"
    CMAKE_ARGS
        -DCMAKE_TOOLCHAIN_FILE=${QT_TOOLCHAIN_FILE}
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DCMAKE_INSTALL_PREFIX=${DEPS_INSTALL_PREFIX}
        -DQT_BUILD_TESTS=OFF
        -DQT_BUILD_EXAMPLES=OFF
    BUILD_ALWAYS ON
)

ExternalProject_Add(ep_mcp-vnc
    SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src"
    BINARY_DIR "${CMAKE_BINARY_DIR}/src"
    LIST_SEPARATOR |
    CMAKE_ARGS
        -DCMAKE_TOOLCHAIN_FILE=${QT_TOOLCHAIN_FILE}
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DCMAKE_PREFIX_PATH=${DEPS_INSTALL_PREFIX}|${QT_PREFIX}
        -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}
        -DQt6McpCommon_DIR=${DEPS_CMAKE_DIR}/Qt6McpCommon
        -DQt6McpServer_DIR=${DEPS_CMAKE_DIR}/Qt6McpServer
        -DQt6VncClient_DIR=${DEPS_CMAKE_DIR}/Qt6VncClient
        -DDEPS_INCLUDE_DIR=${DEPS_INSTALL_PREFIX}/include/qt6
        -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=${CMAKE_BINARY_DIR}
    INSTALL_COMMAND ""
    DEPENDS ep_qtmcp ep_qtvncclient
    BUILD_ALWAYS ON
)
