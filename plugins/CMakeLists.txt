
#
# Copyright 2020-2024 Toyota Connected North America
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

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

include_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${PROJECT_BINARY_DIR}/config
)

find_package(PkgConfig REQUIRED)

macro(ENABLE_PLUGIN plugin)
    string(TOUPPER ${plugin} ucase_plugin)
    set(ENABLE_PLUGIN_${ucase_plugin} ON)
    message(STATUS "Plugin: ${plugin}")

    add_subdirectory(${plugin})

    target_compile_definitions(plugin_${plugin} PUBLIC ENABLE_PLUGIN_${ucase_plugin})
    add_sanitizers(plugin_${plugin})

    if (IPO_SUPPORT_RESULT)
        set_property(TARGET plugin_${plugin} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif ()

    target_link_libraries(${_target} PUBLIC plugin_${plugin})
    target_compile_definitions(${_target} PUBLIC ENABLE_PLUGIN_${ucase_plugin})

    #print_target_properties(plugin_${plugin})

endmacro(ENABLE_PLUGIN)

macro(PLUGIN_OPTION plugin_name default)
    # plugin_name"Camera Pipewire" -> underscore_fname"Camera_Pipewire"
    string(REPLACE " " "_" underscore_fname ${plugin_name})
    # plugin_name"Camera Pipewire" -> camelcase_name"CameraPipewire"
    string(REPLACE " " "" camelcase_name ${plugin_name})
    # underscore_fname"Camera_Pipewire" -> plugin"camera_pipewire"
    string(TOLOWER ${underscore_fname} plugin)
    # plugin"camera_pipewire" -> ucase_plugin"CAMERA_PIPEWIRE"
    string(TOUPPER ${plugin} ucase_plugin)

    if (ENABLE_PLUGINS)
        option(BUILD_PLUGIN_${ucase_plugin} ${plugin_name} ${default})
    else ()
        set(BUILD_PLUGIN_${ucase_plugin} OFF)
    endif ()

    set(ENABLE_PLUGIN_${ucase_plugin} OFF)
    if (BUILD_PLUGIN_${ucase_plugin})
        ENABLE_PLUGIN(${plugin})
        string(APPEND PLUGINS_LIST "${plugin_name};")

        if(NOT "${plugin}" STREQUAL "common")
            # After including a plugin library (eg. ivi-homescreen-plugins)
            # the `PLUGINS_LIST` variable will contain a semicolon-separated list of
            # camel-case (with spaces) plugin names, e.g. "Url Launcher;Video Player Linux"
            #
            # This populates the PLUGIN_HEADERS and PLUGIN_DATA_ARRAY variables
            set (plugin_header "${plugin}/${plugin}_plugin_c_api.h")
            set (plugin_header_check_path "${CMAKE_CURRENT_SOURCE_DIR}/${plugin}/include/${plugin_header}")
            # Make sure the header file exists
            if (NOT EXISTS "${plugin_header_check_path}")
                message(FATAL_ERROR "Plugin header file not found: ${plugin_header_check_path}")
            endif ()

            string (APPEND PLUGIN_DATA_ARRAY "    ${camelcase_name}Plugin,\n")
        else ()
            set (plugin_header "${plugin}/${plugin}.h")
        endif ()

        string (APPEND PLUGIN_HEADERS "#include \"${plugin_header}\"\n")
    endif ()

endmacro(PLUGIN_OPTION)

#
#
#

set(_library_name ivi_homescreen_plugins)

# target used at top level
set(_target plugin_library_${_library_name})
add_library(${_target} ${_library_name}_library.cc)
target_include_directories(${_target} PUBLIC . ${CMAKE_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(${_target} PUBLIC rapidjson spdlog flutter)

PLUGIN_LIBRARY("Ivi Homescreen Plugins" ON)

# Plugins
PLUGIN_OPTION("Common" ON)
PLUGIN_OPTION("Audioplayers Linux" OFF)
PLUGIN_OPTION("Camera" OFF)
PLUGIN_OPTION("Camera Pipewire" OFF)
PLUGIN_OPTION("Cloud Firestore" OFF)
PLUGIN_OPTION("Desktop Window Linux" OFF)
PLUGIN_OPTION("File Selector" OFF)
PLUGIN_OPTION("Firebase Auth" OFF)
PLUGIN_OPTION("Firebase Storage" OFF)
PLUGIN_OPTION("Go Router" ON)
PLUGIN_OPTION("Google Sign In" OFF)
PLUGIN_OPTION("Integration Test" OFF)
PLUGIN_OPTION("PDF" OFF)
PLUGIN_OPTION("Secure Storage" OFF)
PLUGIN_OPTION("Url Launcher" OFF)
PLUGIN_OPTION("Video Player Linux" OFF)

if (BUILD_PLUGIN_CLOUD_FIRESTORE OR BUILD_PLUGIN_FIREBASE_AUTH OR BUILD_PLUGIN_FIREBASE_STORAGE)
    PLUGIN_OPTION("Firebase Core" ON)
    include(firebase)
endif ()

PLUGIN_OPTION("Rive Text" OFF)
PLUGIN_OPTION("Flatpak" OFF)
PLUGIN_OPTION("Webrtc" OFF)

# Platform View Plugins
PLUGIN_OPTION("Layer Playground View" OFF)
PLUGIN_OPTION("Nav Render View" OFF)
PLUGIN_OPTION("Webview Flutter View" OFF)

#
#
#

message (STATUS "=== Plugin library: ${_library_name} ===")
string (REPLACE ";" ",\n" readable_plugin_list "${PLUGINS_LIST}")
string (REPLACE ",\n$" ",\n" readable_plugin_list "${PLUGINS_LIST}")
message(STATUS "Enabled plugins: ${readable_plugin_list}")

# Setup *_library.cc.in
# make sure PLUGIN_DATA_ARRAY doesn't end with a comma
string (REGEX REPLACE ",\n$" "\n" PLUGIN_DATA_ARRAY "${PLUGIN_DATA_ARRAY}")

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/${_library_name}_library.cc.in
    ${CMAKE_CURRENT_BINARY_DIR}/${_library_name}_library.cc
    @ONLY
)

target_sources(${_target} PUBLIC
        ${CMAKE_CURRENT_BINARY_DIR}/${_library_name}_library.cc
)


target_link_libraries(plugins INTERFACE ${_target})
