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

#include "view_target_system.h"

#include <filament/utils/EntityManager.h>

#include <core/components/derived/transform.h>
#include <core/include/literals.h>
#include <core/scene/view_target.h>
#include <core/systems/derived/filament_system.h>
#include <core/systems/ecs.h>
#include <core/utils/asserts.h>

#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

#include <plugins/common/common.h>

namespace plugin_filament_view {

// Filament system, engine, entity manager
smarter_shared_ptr<FilamentSystem> _filamentSystem = nullptr;
smarter_raw_ptr<filament::Engine> _engine = nullptr;
smarter_raw_ptr<utils::EntityManager> _em = nullptr;

////////////////////////////////////////////////////////////////////////////////////
void ViewTargetSystem::onSystemInit() {
  // Get filament engine
  _filamentSystem = ecs->getSystem<FilamentSystem>("ViewTargetSystem::onSystemInit");
  _engine = _filamentSystem->getFilamentEngine();
  _em = _engine->getEntityManager();

  /*
   *  Message handlers
   */
  registerMessageHandler(ECSMessageType::ViewTargetCreateRequest, [this](const ECSMessage& msg) {
    spdlog::trace("ViewTargetCreateRequest");

    const auto state = msg.getData<FlutterDesktopEngineState*>(
      ECSMessageType::ViewTargetCreateRequest
    );
    const auto top = msg.getData<int>(ECSMessageType::ViewTargetCreateRequestTop);
    const auto left = msg.getData<int>(ECSMessageType::ViewTargetCreateRequestLeft);
    const auto width = msg.getData<uint32_t>(ECSMessageType::ViewTargetCreateRequestWidth);
    const auto heigth = msg.getData<uint32_t>(ECSMessageType::ViewTargetCreateRequestHeight);

    const auto nWhich = nSetupViewTargetFromDesktopState(top, left, state);
    getViewTarget(nWhich)->InitializeFilamentInternals(width, heigth);

    // setCameraFromSerializedData();

    spdlog::trace("ViewTargetCreateRequest Complete");
  });

  registerMessageHandler(
    ECSMessageType::ViewTargetStartRenderingLoops,
    [this](const ECSMessage& /*msg*/) {
      spdlog::trace("ViewTargetStartRenderingLoops");
      KickOffFrameRenderingLoops();
      spdlog::trace("ViewTargetStartRenderingLoops Complete");
    }
  );

  registerMessageHandler(ECSMessageType::ChangeViewQualitySettings, [this](const ECSMessage& msg) {
    spdlog::trace("ChangeViewQualitySettings");

    // Not Currently Implemented -- currently will change all view targes.
    // ChangeViewQualitySettingsWhichView
    auto settingsId = msg.getData<int>(ECSMessageType::ChangeViewQualitySettings);

    spdlog::debug("ChangeViewQualitySettings: {}", settingsId);
    for (const auto& viewTarget : _viewTargets) {
      viewTarget->ChangeQualitySettings(
        static_cast<ViewTarget::ePredefinedQualitySettings>(settingsId)
      );
    }

    spdlog::trace("ChangeViewQualitySettings Complete");

    // setCameraFromSerializedData();
  });

  // set fog options
  registerMessageHandler(ECSMessageType::SetFogOptions, [this](const ECSMessage& msg) {
    spdlog::trace("SetFogOptions");

    const bool enabled = msg.getData<bool>(ECSMessageType::SetFogOptions);

    // default values
    /// TODO: make constexpr?
    const filament::View::FogOptions enabledFogOptions = {
      .distance = 20.0f,
      // .cutOffDistance = 0.0f,
      .maximumOpacity = 1.0f,
      .height = 0.0f,
      .heightFalloff = 1.0f,
      .color = filament::math::float3(1.0f, 1.0f, 1.0f),
      .density = 1.5f,
      .inScatteringStart = 0.0f,
      .inScatteringSize = -1.0f,
      .enabled = true,
    };

    /// TODO: make constexpr?
    const filament::View::FogOptions disabledFogOptions = {
      .enabled = false,
    };

    // Set the fog options
    for (const auto& viewTarget : _viewTargets) {
      viewTarget->setFogOptions(enabled ? enabledFogOptions : disabledFogOptions);
    }

    spdlog::trace("SetFogOptions Complete");
  });

  registerMessageHandler(ECSMessageType::ResizeWindow, [this](const ECSMessage& msg) {
    spdlog::trace("ResizeWindow");
    const auto nWhich = msg.getData<size_t>(ECSMessageType::ResizeWindow);
    const auto fWidth = msg.getData<double>(ECSMessageType::ResizeWindowWidth);
    const auto fHeight = msg.getData<double>(ECSMessageType::ResizeWindowHeight);

    getViewTarget(nWhich)->resize(fWidth, fHeight);

    spdlog::trace("ResizeWindow Complete");

    // setCameraFromSerializedData();
  });

  registerMessageHandler(ECSMessageType::MoveWindow, [this](const ECSMessage& msg) {
    spdlog::trace("MoveWindow");
    const auto nWhich = msg.getData<size_t>(ECSMessageType::ResizeWindow);
    const auto fLeft = msg.getData<double>(ECSMessageType::MoveWindowLeft);
    const auto fTop = msg.getData<double>(ECSMessageType::MoveWindowTop);

    getViewTarget(nWhich)->setOffset(fLeft, fTop);

    spdlog::trace("MoveWindow Complete");

    // setCameraFromSerializedData();
  });
}

void ViewTargetSystem::initializeEntity(EntityGUID entityGuid) {
  // Get entity and its Camera, Transform components
  auto entity = ecs->getEntity(entityGuid);
  auto camera = ecs->getComponent<Camera>(entityGuid);
  auto transform = ecs->getComponent<Transform>(entityGuid);

  // Check requirements:
  // - Entity must not have a _fEntity already set
  // - Must have a Camera component
  // - Must have a Transform component
  runtime_assert(
    entity != nullptr && !entity->_fEntity && camera != nullptr && transform != nullptr,
    (fmt::format(
      "[{}] Entity({}) does not match initialization requirements",  //
      __FUNCTION__, entityGuid
    ))
  );

  // Set up entity + camera in Filament
  entity->_fEntity = _em->create();
  // Our [Camera] component does not wrap a Filament camera! [ViewTarget] takes care of that
  _engine->getTransformManager().create(entity->_fEntity);
  transform->_fInstance = _engine->getTransformManager().getInstance(entity->_fEntity);
}

////////////////////////////////////////////////////////////////////////////////////
void ViewTargetSystem::update(double /*deltaTime*/) {
  // Get all cameras
  const auto& cameras = ecs->getComponentsOfType<Camera>();

  // Update each camera's view target
  //  The process is camera-driven, as not every view target might have a Camera entity associated
  //  with it
  //  - which is fine! We might want some view targets to have a static camera,
  //  also this way we're updating the camera in a 'push' manner rather than polling it

  // Keep track of whether a view target has already been set on this frame
  std::map<size_t, EntityGUID> viewTargetSetBy;

  for (const auto& camera : cameras) {
    const EntityGUID cameraId = camera->getOwner()->getGuid();
    const size_t viewId = camera->getViewId();
    if (viewId == ViewTarget::kNullViewId)
      continue;  // camera has no view target associated with it, skip it

    auto* viewTarget = getViewTarget(viewId);
    if (viewTarget == nullptr) {
      spdlog::error("ViewTarget at index {} is null", viewId);
      continue;
    }

    // Check if this view target has already been set for this frame
    if (viewTargetSetBy[viewId]) {
      spdlog::warn(
        "View target {} has already been set for this frame by camera({}) - another camera({}) is "
        "setting it again, skipping",
        viewId, viewTargetSetBy[viewId], cameraId
      );
      continue;
    }

    // Update the camera settings for the view target
    const auto transform = ecs->getComponent<Transform>(cameraId);
    const auto orbitOriginTransform = ecs->getComponent<Transform>(camera->orbitOriginEntity);
    const filament::math::float3* targetPosition = nullptr;

    spdlog::trace("Checking camera({}) enableTarget", cameraId);
    if (camera->enableTarget) {
      // If the camera has a target entity, get its transform
      if (camera->targetEntity != kNullGuid) {
        spdlog::trace("has target entity: {}", camera->targetEntity);
        auto* targetTransform = ecs->getComponent<Transform>(camera->targetEntity).get();

        if (targetTransform == nullptr) {
          spdlog::warn(
            "Camera({}) target entity({}) has no transform, skipping", cameraId,
            camera->targetEntity
          );
        } else {
          // If the target entity has a transform, use its global position
          spdlog::trace("Using target entity's global position");
          // Set the target point to the target entity's global position
          camera->targetPoint = targetTransform->getGlobalPosition();
        }
      } else {
        // If no target entity, use the target position directly
        spdlog::trace("Using target position directly");
      }

      targetPosition = &camera->targetPoint;
    } else {
      spdlog::trace("camera enableTarget=false");
    }

    spdlog::trace("Updating camera...");
    viewTarget->updateCameraSettings(
      *camera, *transform, orbitOriginTransform.get(), targetPosition
    );
    spdlog::trace("Updated camera settings for view target {} by camera {}", viewId, cameraId);

    // Set the flag
    viewTargetSetBy[viewId] = cameraId;
  }
}

void ViewTargetSystem::setViewCamera(size_t viewId, EntityGUID cameraId) {
  // Get all the cameras
  const auto& cameras = ecs->getComponentsOfType<Camera>();
  for (const auto& camera : cameras) {
    // Found the camera, set it as the main camera for the view target
    if (camera->getOwner()->getGuid() == cameraId) {
      camera->setViewId(viewId);

      spdlog::debug("Setting camera {} as main camera for view target {}", cameraId, viewId);
    }
    // If found another camera with the same viewId, unset it
    else if (camera->getViewId() == viewId) {
      camera->setViewId(ViewTarget::kNullViewId);
      spdlog::trace(
        "Unsetting camera {} from view target {} - not main camera", camera->getOwner()->getGuid(),
        viewId
      );
    }
  }
}

////////////////////////////////////////////////////////////////////////////////////
void ViewTargetSystem::onDestroy() {}

////////////////////////////////////////////////////////////////////////////////////
void ViewTargetSystem::debugPrint() {}

ViewTarget* ViewTargetSystem::getViewTarget(size_t index) const {
  if (index >= _viewTargets.size()) {
    spdlog::error("Invalid view target index: {}", index);
    return nullptr;
  }
  return _viewTargets[index].get();
}

////////////////////////////////////////////////////////////////////////////////////
void ViewTargetSystem::KickOffFrameRenderingLoops() const {
  for (const auto& viewTarget : _viewTargets) {
    viewTarget->setInitialized();
  }
}

////////////////////////////////////////////////////////////////////////////////////
size_t ViewTargetSystem::nSetupViewTargetFromDesktopState(
  int32_t top,
  int32_t left,
  FlutterDesktopEngineState* state
) {
  size_t id = _viewTargets.size();
  _viewTargets.emplace_back(std::make_unique<ViewTarget>(id, top, left, state));
  return id;
}

}  // namespace plugin_filament_view
