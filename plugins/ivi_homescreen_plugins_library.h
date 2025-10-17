/*
 * Copyright 2024 Toyota Connected North America
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef IVI_HOMESCREEN_PLUGINS_LIBRARY_H
#define IVI_HOMESCREEN_PLUGINS_LIBRARY_H
#include <encodable_value.h>

#include "flutter_homescreen_plugin.h"

#include <method_result.h>
#include <memory>



void IviHomescreenPluginsRegisterPlugins(FlutterDesktopEngineRef engine);

bool IviHomescreenPluginsPlatformViewTryCreate(
    FlutterDesktopPluginRegistrarRef registrar,
    int32_t id,
    const std::string& viewType,
    int32_t direction,
    double top,
    double left,
    double width,
    double height,
    const std::vector<uint8_t>& params,
    const std::string& flutter_asset_directory,
    FlutterDesktopEngineRef engine,
    PlatformViewAddListener addListener,
    PlatformViewRemoveListener removeListener,
    void* platform_view_context
);

extern FlutterPluginLibrary IviHomescreenPluginsPluginLibrary;

#endif  // IVI_HOMESCREEN_PLUGINS_LIBRARY_H