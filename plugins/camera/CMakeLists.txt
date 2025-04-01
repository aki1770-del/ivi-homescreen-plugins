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
pkg_check_modules(PIPEWIRE REQUIRED libpipewire-0.3)
pkg_check_modules(SDL2 REQUIRED sdl2)


include(FindJPEG)

add_library(plugin_camera STATIC
        camera_plugin_c_api.cc
        camera_plugin.cc
        camera_context.cc
        messages.g.cpp
        camera_session.cc
        image.cc
        texture.cc
        texture_mjpg.cc
        texture_sink.cc
        camera_session.cc
        camera_session.cc
        CameraStream.cpp
        CameraStream.h
        CameraManager.cpp
        CameraManager.h

)

target_include_directories(plugin_camera PUBLIC
        include
        ${JPEG_INCLUDE_DIRS}
        ${PIPEWIRE_INCLUDE_DIRS}
        ${SDL2_INCLUDE_DIRS}
)

target_link_libraries(plugin_camera PUBLIC
        PkgConfig::CAMERA
        ${JPEG_LIBRARIES}
        flutter
        platform_homescreen
        plugin_common
        ${PIPEWIRE_LIBRARIES}
        ${SDL2_LIBRARIES}
)

if (JPEG_FOUND)
    target_compile_definitions(plugin_camera PUBLIC ENABLE_JPEG)
endif ()
