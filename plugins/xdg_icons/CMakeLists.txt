#
# Copyright 2023-2025 Toyota Connected North America
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

add_library(plugin_xdg_icons STATIC
        messages.cc
        xdg_icons_plugin_c_api.cc
        xdg_icons_plugin.cc
)

target_include_directories(plugin_xdg_icons PRIVATE include)

target_link_libraries(plugin_xdg_icons PUBLIC
        platform_homescreen
        plugin_common
        plugin_common_glib
)
