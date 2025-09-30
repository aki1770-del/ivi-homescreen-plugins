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
#include <core/scene/indirect_light/indirect_light.h>
#include <core/scene/view_target.h>
#include <core/systems/base/system.h>
#include <core/utils/ibl_profiler.h>

namespace plugin_filament_view {

class IndirectLight;
class DefaultIndirectLight;
class KtxIndirectLight;
class HdrIndirectLight;

class IndirectLightSystem : public System {
  public:
    IndirectLightSystem() = default;

    void setDefaultIndirectLight();

    std::future<Resource<std::string_view>> setIndirectLightFromKtxAsset(
      const std::string& path,
      double intensity
    );

    std::future<Resource<std::string_view>> setIndirectLightFromKtxUrl(
      const std::string& url,
      double intensity
    );

    std::future<Resource<std::string_view>> setIndirectLightFromHdrAsset(
      const std::string& path,
      double intensity
    );

    std::future<Resource<std::string_view>> setIndirectLightFromHdrUrl(
      const std::string& url,
      double intensity
    );

    Resource<std::string_view> loadIndirectLightHdrFromFile(
      const std::string& asset_path,
      double intensity
    );

    std::future<Resource<std::string_view>> setIndirectLight(DefaultIndirectLight* indirectLight);

    // Disallow copy and assign.
    IndirectLightSystem(const IndirectLightSystem&) = delete;
    IndirectLightSystem& operator=(const IndirectLightSystem&) = delete;

    ~IndirectLightSystem() override;

    void onSystemInit() override;
    void update(double deltaTime) override;
    void onDestroy() override;
    void debugPrint() override;

  private:
    std::unique_ptr<DefaultIndirectLight> indirect_light_;
};
}  // namespace plugin_filament_view
