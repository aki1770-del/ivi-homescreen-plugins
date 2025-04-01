#
# Copyright 2023-2024 Toyota Connected North America
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
find_package(PkgConfig REQUIRED)

pkg_check_modules(CAMERA IMPORTED_TARGET REQUIRED libcamera)
pkg_check_modules(PIPEWIRE IMPORTED_TARGET REQUIRED libpipewire-0.3)


include(FindJPEG)

add_library(plugin_camera STATIC
        camera_plugin_c_api.cc
        camera_plugin.cc
        camera_context.cc
        messages.g.cpp
        CameraStream.cpp
        CameraStream.h
        CameraManager.cpp
        CameraManager.h

)

target_include_directories(plugin_camera PUBLIC
        include
        ${JPEG_INCLUDE_DIRS}
)

target_link_libraries(plugin_camera PUBLIC
        PkgConfig::CAMERA
        PkgConfig::PIPEWIRE
        ${JPEG_LIBRARIES}
        flutter
        platform_homescreen
        plugin_common
)

if (JPEG_FOUND)
    target_compile_definitions(plugin_camera PUBLIC ENABLE_JPEG)
endif ()
