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

#pragma once

#include <core/include/resource.h>
#include <core/systems/base/system.h>
#include <future>

namespace plugin_filament_view {

class SkyboxSystem : public System {
  public:
    SkyboxSystem() = default;

    std::future<void> Initialize();

    void setDefaultSkybox();

    std::future<Resource<std::string_view>> setSkyboxFromHdrAsset(
      const std::string& path,
      bool showSun,
      bool shouldUpdateLight,
      float intensity
    );

    std::future<Resource<std::string_view>> setSkyboxFromHdrUrl(
      const std::string& url,
      bool showSun,
      bool shouldUpdateLight,
      float intensity
    );

    std::future<Resource<std::string_view>> setSkyboxFromKTXAsset(const std::string& path);

    std::future<Resource<std::string_view>> setSkyboxFromKTXUrl(const std::string& url);

    std::future<Resource<std::string_view>> setSkyboxFromColor(const filament::math::float4& color);

    Resource<std::string_view> loadSkyboxFromHdrBuffer(
      const std::vector<uint8_t>& buffer,
      bool showSun,
      bool shouldUpdateLight,
      float intensity
    );

    Resource<std::string_view> loadSkyboxFromHdrFile(
      const std::string& assetPath,
      bool showSun,
      bool shouldUpdateLight,
      float intensity
    );

    // Disallow copy and assign.
    SkyboxSystem(const SkyboxSystem&) = delete;
    SkyboxSystem& operator=(const SkyboxSystem&) = delete;

    void onSystemInit() override;
    void update(double deltaTime) override;
    void onDestroy() override;
    void debugPrint() override;

  private:
    void setTransparentSkybox();
};
}  // namespace plugin_filament_view
