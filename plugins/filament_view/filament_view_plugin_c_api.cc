/*
 * Copyright 2020-2024 Toyota Connected North America
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

#include <filament_view_plugin.h>
#include <include/filament_view/filament_view_plugin_c_api.h>

#include <core/systems/derived/view_target_system.h>
#include <core/systems/ecs.h>

#include <asio/post.hpp>
#include <flutter/plugin_registrar.h>

#include <future>

void FilamentViewPluginCApiRegisterWithRegistrar(
  FlutterDesktopPluginRegistrarRef registrar,
  const int32_t id,
  const std::string& viewType,
  const int32_t direction,
  const double top,
  const double left,
  const double width,
  const double height,
  const std::vector<uint8_t>& params,
  const std::string& assetDirectory,
  FlutterDesktopEngineRef engine,
  const PlatformViewAddListener addListener,
  const PlatformViewRemoveListener removeListener,
  void* platform_view_context
) {
  plugin_filament_view::FilamentViewPlugin::RegisterWithRegistrar(
    flutter::PluginRegistrarManager::GetInstance()->GetRegistrar<flutter::PluginRegistrar>(registrar
    ),
    id, viewType, direction, top, left, width, height, params, assetDirectory, engine, addListener,
    removeListener, platform_view_context
  );

  // Uncomment if you want two views.
  /*plugin_filament_view::FilamentViewPlugin::RegisterWithRegistrar(
      flutter::PluginRegistrarManager::GetInstance()
          ->GetRegistrar<flutter::PluginRegistrar>(registrar),
      id, std::move(viewType), direction, top, 1200, width, height, params,
      assetDirectory, engine, addListener, removeListener,
      platform_view_context);

  plugin_filament_view::FilamentViewPlugin::RegisterWithRegistrar(
      flutter::PluginRegistrarManager::GetInstance()
          ->GetRegistrar<flutter::PluginRegistrar>(registrar),
      id, std::move(viewType), direction, top, 800, width, height, params,
      assetDirectory, engine, addListener, removeListener,
      platform_view_context);*/

  // after we're done doing setup, kick off the run loops
  if (const auto ecs = plugin_filament_view::ECSManager::GetInstance();
      ecs->getRunState() == plugin_filament_view::ECSManager::RunState::Initialized) {
    ecs->debugPrint();
    // ecs->StartMainLoop();

    /*
     *  Indirectly kicks off the rendering loop by processing ViewTarget init requests
     *  This has to be done on the rendering thread, so we post a task
     */
    // TODO: take care of this as part of https://github.com/toyota-connected/fluorite/issues/10
    auto viewTargetSystem = ecs->getSystem<plugin_filament_view::ViewTargetSystem>(
      "FilamentViewPluginCApiRegisterWithRegistrar"
    );
    std::promise<void> startupPromise;
    auto startupFuture = startupPromise.get_future();
    post(*ecs->getStrand(), [&viewTargetSystem, &startupPromise] {
      spdlog::debug("== Starting ViewTargetSystem frame rendering loops ==");

      viewTargetSystem->ProcessMessages();
      startupPromise.set_value();
    });

    startupFuture.wait();
  }
}
