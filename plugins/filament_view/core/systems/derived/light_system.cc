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

#include "light_system.h"

#include <asio/post.hpp>
#include <core/components/derived/light.h>
#include <core/entity/base/entityobject.h>
#include <core/include/color.h>
#include <core/systems/derived/filament_system.h>
#include <core/systems/ecs.h>
#include <core/utils/asserts.h>
#include <core/utils/uuidGenerator.h>
#include <filament/Color.h>
#include <filament/LightManager.h>
#include <filament/Scene.h>
#include <plugins/common/common.h>
#include <utils/EntityManager.h>

namespace plugin_filament_view {
using filament::math::float3;
using filament::math::mat3f;
using filament::math::mat4f;

////////////////////////////////////////////////////////////////////////////////////
void LightSystem::CreateDefaultLight() {
  SPDLOG_DEBUG("{}", __FUNCTION__);

  m_poDefaultLight = std::make_shared<EntityObject>("DefaultLight", generateUUID());
  const auto oLightComp = std::make_shared<Light>();
  ecs->addComponent(m_poDefaultLight->getGuid(), oLightComp);

  oLightComp->setIntensity(200);
  oLightComp->setDirection({0, -1, 0});
  oLightComp->setPosition({0, 5, 0});
  oLightComp->setCastLight(true);
  // if you're in an closed space (IE Garage), it will self shadow cast
  oLightComp->setCastShadows(false);

  BuildLightAndAddToScene(*oLightComp);

  ecs->addEntity(m_poDefaultLight);
}

////////////////////////////////////////////////////////////////////////////////////
void LightSystem::BuildLightAndAddToScene(Light& light) {
  BuildLight(light);
  AddLightToScene(light);
}

////////////////////////////////////////////////////////////////////////////////////
void LightSystem::BuildLight(Light& light) {
  const auto filamentSystem = ecs->getSystem<FilamentSystem>("BuildLight");
  const auto engine = filamentSystem->getFilamentEngine();

  if (!light.m_poFilamentEntityLight) {
    light.m_poFilamentEntityLight = engine->getEntityManager().create();
  } else {
    RemoveLightFromScene(light);
  }

  auto builder = filament::LightManager::Builder(light.getLightType());

  if (light.getColorTemperature() > 0) {
    auto cct = filament::Color::cct(light.getColorTemperature());
    auto red = cct.r;
    auto green = cct.g;
    auto blue = cct.b;
    builder.color({red * 255, green * 255, blue * 255});
  } else {
    builder.color({light.color[0], light.color[1], light.color[2]});
  }

  // Note while not all of these vars are used in every scenario
  // we're expecting filament to throw away the values that are
  // not needed.
  builder.intensity(light.getIntensity());
  builder.position(light.getPosition());
  builder.direction(light.getDirection());
  builder.castLight(light.getCastLight());
  builder.castShadows(light.getCastShadows());
  builder.falloff(light.getFalloffRadius());

  builder.spotLightCone(light.getSpotLightConeInner(), light.getSpotLightConeOuter());

  builder.sunAngularRadius(light.getSunAngularRadius());
  builder.sunHaloSize(light.getSunHaloSize());
  builder.sunHaloFalloff(light.getSunHaloFalloff());

  builder.build(*engine, light.m_poFilamentEntityLight);
}

////////////////////////////////////////////////////////////////////////////////////
void LightSystem::RemoveLightFromScene(const Light& light) {
  const auto filamentSystem = ecs->getSystem<FilamentSystem>("lightManager::RemoveLightFromScene");

  const auto scene = filamentSystem->getFilamentScene();

  scene->remove(light.m_poFilamentEntityLight);
}

////////////////////////////////////////////////////////////////////////////////////
void LightSystem::AddLightToScene(const Light& light) {
  const auto filamentSystem = ecs->getSystem<FilamentSystem>("lightManager::AddLightToScene");

  const auto scene = filamentSystem->getFilamentScene();

  scene->addEntity(light.m_poFilamentEntityLight);
}

////////////////////////////////////////////////////////////////////////////////////
void LightSystem::onSystemInit() {
  registerMessageHandler(ECSMessageType::ChangeSceneLightProperties, [this](const ECSMessage& msg) {
    SPDLOG_TRACE("ChangeSceneLightProperties");

    const auto guid = msg.getData<EntityGUID>(ECSMessageType::ChangeSceneLightProperties);

    const auto colorValue = msg.getData<std::vector<double>>(ECSMessageType::floatVec4);

    const auto intensityValue = msg.getData<float>(
      ECSMessageType::ChangeSceneLightPropertiesIntensity
    );

    // find the entity in our list:
    const auto theLight = ecs->getComponent<Light>(guid);
    runtime_assert(theLight != nullptr, fmt::format("Entity({}): Light not found", guid));

    theLight->setIntensity(intensityValue);
    theLight->color = float3(
      static_cast<float>(colorValue[0]), static_cast<float>(colorValue[1]),
      static_cast<float>(colorValue[2])
    );
    // Ignoring alpha channel for now: colorValue[3]

    RemoveLightFromScene(*theLight);
    BuildLightAndAddToScene(*theLight);

    SPDLOG_TRACE("ChangeSceneLightProperties Complete");
  });

  registerMessageHandler(ECSMessageType::ChangeSceneLightTransform, [this](const ECSMessage& msg) {
    SPDLOG_TRACE("ChangeSceneLightTransform");

    const auto guid = msg.getData<EntityGUID>(ECSMessageType::ChangeSceneLightTransform);

    const auto position = msg.getData<float3>(ECSMessageType::Position);

    const auto rotation = msg.getData<float3>(ECSMessageType::Direction);

    // find the entity in our list:
    const auto theLight = ecs->getComponent<Light>(guid);
    runtime_assert(theLight != nullptr, fmt::format("Entity({}): Light not found", guid));
    theLight->setPosition(position);
    theLight->setDirection(rotation);

    RemoveLightFromScene(*theLight);
    BuildLightAndAddToScene(*theLight);

    SPDLOG_TRACE("ChangeSceneLightTransform Complete");
  });
}

////////////////////////////////////////////////////////////////////////////////////
void LightSystem::update(double /*deltaTime*/) {}

////////////////////////////////////////////////////////////////////////////////////
void LightSystem::onDestroy() {
  if (m_poDefaultLight != nullptr) {
    const auto component = ecs->getComponent<Light>(m_poDefaultLight->getGuid());
    RemoveLightFromScene(*component);

    m_poDefaultLight.reset();
  }
}

////////////////////////////////////////////////////////////////////////////////////
void LightSystem::debugPrint() {
  spdlog::debug("{}", __FUNCTION__);

  // TODO Update print out list of lights
}

}  // namespace plugin_filament_view
