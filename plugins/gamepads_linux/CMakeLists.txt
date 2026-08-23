#
# Copyright 2026 Toyota Connected North America
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

# SDL3 for the gamepad database and hot-plug. Only the gamepad subsystem is
# initialised -- no video, no audio, and SDL's event queue is left undrained, so
# nothing here competes with the shell's own input path.
pkg_check_modules(SDL3 IMPORTED_TARGET REQUIRED sdl3)

add_library(plugin_gamepads_linux STATIC
        gamepads_linux_plugin_c_api.cc
        gamepads_linux_plugin.cc
)

target_include_directories(plugin_gamepads_linux PRIVATE include)


target_link_libraries(plugin_gamepads_linux PUBLIC
        flutter
        platform_homescreen
        PkgConfig::SDL3
)
