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
#ifndef FLUTTER_PLUGIN_FILAMENT_VIEW_PLUGIN_H_
#define FLUTTER_PLUGIN_FILAMENT_VIEW_PLUGIN_H_

#include <memory>
#include <string>

#include <core/scene/serialization/scene_text_deserializer.h>
#include <flutter/plugin_registrar.h>
#include <flutter_homescreen.h>
#include <messages.g.h>
#include <platform_views/platform_view.h>
#include <wayland/display.h>

namespace plugin_filament_view {

class FilamentViewPlugin : public flutter::Plugin, public FilamentViewApi, public PlatformView {
  public:
    static void RegisterWithRegistrar(
      flutter::PluginRegistrar* registrar,
      int32_t id,
      std::string viewType,
      int32_t direction,
      double top,
      double left,
      double width,
      double height,
      const std::vector<uint8_t>& params,
      const std::string& assetDirectory,
      FlutterDesktopEngineRef engine,
      PlatformViewAddListener addListener,
      PlatformViewRemoveListener removeListener,
      void* platform_view_context
    );

    FilamentViewPlugin(
      int32_t id,
      std::string viewType,
      int32_t direction,
      double top,
      double left,
      double width,
      double height,
      const std::vector<uint8_t>& params,
      const std::string& assetDirectory,
      PlatformViewAddListener addListener,
      PlatformViewRemoveListener removeListener,
      void* platform_view_context
    );

    ~FilamentViewPlugin() override;

    static void setupMessageChannels(flutter::PluginRegistrar* registrar);
    static void sendReadyEvent();

    std::optional<FlutterError> ChangeMaterialParameter(
      const flutter::EncodableMap& params,
      const int64_t guid
    ) override;
    // Change material definition for the given entity.
    std::optional<FlutterError> ChangeMaterialDefinition(
      const flutter::EncodableMap& params,
      const int64_t guid
    ) override;
    // Toggle shapes visibility in the scene.
    std::optional<FlutterError> ToggleShapesInScene(bool value) override;
    // Toggle debug collider visuals in the scene.
    std::optional<FlutterError> ToggleDebugCollidableViewsInScene(bool value) override;
    // Change view quality settings.
    std::optional<FlutterError> ChangeViewQualitySettings() override;
    // Set fog options
    std::optional<FlutterError> SetFogOptions(const bool enabled) override;
    // Set the camera's orbit's anchoring/origin entity
    std::optional<FlutterError> SetCameraOrbit(
      int64_t id,
      int64_t origin_entity_id,
      const std::vector<double>& orbit_rotation
    ) override;
    // Set the camera's look-at target entity.
    std::optional<FlutterError> SetCameraTarget(
      int64_t id,
      int64_t target_entity_id,
      const std::vector<double>* target_position
    ) override;
    // Set a given camera as the active camera for a view
    std::optional<FlutterError> SetActiveCamera(const int64_t* view_id, int64_t camera_id) override;
    // Set the camera's dolly offset.
    // The dolly offset is the camera's position relative to its target.
    std::optional<FlutterError> SetCameraDolly(int64_t id, const std::vector<double>& dolly_offset)
      override;
    std::optional<FlutterError> ChangeLightTransformByGUID(
      const int64_t guid,
      double posx,
      double posy,
      double posz,
      double dirx,
      double diry,
      double dirz
    ) override;
    std::optional<FlutterError> ChangeLightColorByGUID(
      const int64_t guid,
      const std::string& color,
      int64_t intensity
    ) override;
    std::optional<FlutterError> EnqueueAnimation(const int64_t guid, int64_t animation_index)
      override;
    std::optional<FlutterError> ClearAnimationQueue(const int64_t guid) override;
    std::optional<FlutterError> PlayAnimation(const int64_t guid, int64_t animation_index) override;
    std::optional<FlutterError> ChangeAnimationSpeed(const int64_t guid, double speed) override;
    std::optional<FlutterError> PauseAnimation(const int64_t guid) override;
    std::optional<FlutterError> ResumeAnimation(const int64_t guid) override;
    std::optional<FlutterError> SetAnimationLooping(const int64_t guid, bool looping) override;
    std::optional<FlutterError> RaycastFromTap(double x, double y) override;
    std::optional<FlutterError> RequestCollisionCheckFromRay(
      const std::string& query_id,
      double origin_x,
      double origin_y,
      double origin_z,
      double direction_x,
      double direction_y,
      double direction_z,
      double length
    ) override;

    std::optional<FlutterError> SetEntityTransformScale(
      const int64_t guid,
      const std::vector<double>& scl
    ) override;
    std::optional<FlutterError> SetEntityTransformPosition(
      const int64_t guid,
      const std::vector<double>& pos
    ) override;
    std::optional<FlutterError> SetEntityTransformRotation(
      const int64_t guid,
      const std::vector<double>& rot
    ) override;

    std::optional<FlutterError> TurnOffVisualForEntity(const int64_t guid) override;
    std::optional<FlutterError> TurnOnVisualForEntity(const int64_t guid) override;
    std::optional<FlutterError> TurnOffCollisionChecksForEntity(const int64_t guid) override;
    std::optional<FlutterError> TurnOnCollisionChecksForEntity(const int64_t guid) override;

    // Disallow copy and assign.
    FilamentViewPlugin(const FilamentViewPlugin&) = delete;

    FilamentViewPlugin& operator=(const FilamentViewPlugin&) = delete;

  private:
    int32_t id_;
    void* platformViewsContext_;
    PlatformViewRemoveListener removeListener_;

    static void on_resize(double width, double height, void* data);
    static void on_set_direction(int32_t direction, void* data);
    static void on_set_offset(double left, double top, void* data);
    /// Called by the embedder when touches are passed to PlatformView in Flutter.
    static void on_touch(
      int32_t action,
      int32_t point_count,
      size_t point_data_size,
      const double* point_data,
      void* data
    );
    static void on_dispose(bool hybrid, void* data);

    static const struct platform_view_listener platform_view_listener_;
};

}  // namespace plugin_filament_view

#endif  // FLUTTER_PLUGIN_FILAMENT_VIEW_PLUGIN_H_
