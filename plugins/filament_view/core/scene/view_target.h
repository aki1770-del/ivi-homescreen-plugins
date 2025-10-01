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

#include <asio/io_context_strand.hpp>
#include <core/components/derived/camera.h>
#include <core/components/derived/transform.h>
#include <core/scene/camera/touch_pair.h>
#include <core/scene/geometry/ray.h>

#include <cstdint>
#include <event_channel.h>
#include <filament/Engine.h>
#include <flutter_desktop_plugin_registrar.h>
#include <gltfio/AssetLoader.h>
#include <viewer/Settings.h>

namespace plugin_filament_view {

class Camera;

/// TODO: add missing docs on everything here
class ViewTarget {
  public:
    static constexpr size_t kNullViewId = -1;
    const size_t id;

    std::shared_ptr<std::promise<void>> framePromise;

    ViewTarget(size_t id, int32_t top, int32_t left, FlutterDesktopEngineState* state);

    ~ViewTarget();

    enum ePredefinedQualitySettings { Lowest, Low, Medium, High, Ultra };

    // Disallow copy and assign.
    ViewTarget(const ViewTarget&) = delete;
    ViewTarget& operator=(const ViewTarget&) = delete;

    void setInitialized() {
      spdlog::debug(
        "ViewTarget::setInitialized (previously {})", initialized_ ? "already true" : "false"
      );
      if (initialized_) return;

      initialized_ = true;
      spdlog::debug("ViewTarget::setInitialized calling OnFrame for the first time!");
      OnFrame(this, nullptr, 0);
    }

    /*
     *  Filament stuff
     */
    void InitializeFilamentInternals(uint32_t width, uint32_t height);
    [[nodiscard]] ::filament::View* getFilamentView() const { return fview_; }
    filament::gltfio::FilamentAsset* getAsset() { return asset_; }
    void setAnimator(filament::gltfio::Animator* animator) { fanimator_ = animator; }

    /*
     *  Viewport
     */
    /// Sets the native window offset for this view target.
    void setOffset(double left, double top);
    /// Sets the native window size for this view target.
    void resize(double width, double height);
    [[nodiscard]] float calculateAspectRatio() const;
    /// Converts a [TouchPair] to a [Ray] to run a raycast with.
    Ray touchToRay(TouchPair touch) const;

    /*
     *  Camera
     */
    /// Called by [ViewTargetSystem] on every frame
    void updateCameraSettings(
      Camera& cameraData,
      Transform& transform,
      Transform* orbitOriginTransform = nullptr,
      const filament::math::float3* targetPosition = nullptr
    );

    /// Called by Flutter when a touch event occurs.
    /// TODO: return touch result directly here, don't do callbacks
    void onTouch(
      int32_t action,
      int32_t point_count,
      size_t point_data_size,
      const double* point_data
    ) const;

    void onTouch(const double x, const double y) const;

    void onTouch(const TouchPair touch) const;

    /*
     *  Rendering
     */
    /// Sets the quality setting preset for the view target.
    void ChangeQualitySettings(ePredefinedQualitySettings qualitySettings);
    /// Sets the fog options for the view target.
    void setFogOptions(const filament::View::FogOptions& fogOptions);
    /// Returns the current render settings for the view target.
    filament::viewer::Settings& getSettings() { return settings_; }

  private:
    /*
     *  Camera
     */
    static constexpr float kDefaultNearPlane = 0.05f;   // 5 cm
    static constexpr float kDefaultFarPlane = 1000.0f;  // 1 km
    static constexpr float kDefaultAperture = 16.0f;
    static constexpr float kDefaultShutterSpeed = 1.0f / 125;
    static constexpr float kDefaultSensitivity = 100.0f;
    static constexpr float kDefaultFocalLength = 28.0f;

    filament::Camera* camera_ = nullptr;
    utils::Entity cameraEntity_;
    filament::Engine* _engine = nullptr;

    /// Initializes the camera
    void _initCamera();

    void _setExposure(const Exposure& exposure);
    void _setProjection(const Projection& projection);
    void _setLens(const LensProjection& lensProjection);
    /// Sets the eyes view matrices for stereoscopic rendering (if applicable).
    void _setEyes(const double ipd);

    /*
     *  Wayland
     */
    void setupWaylandSubsurface();

    FlutterDesktopEngineState* state_;
    filament::viewer::Settings settings_;
    filament::gltfio::FilamentAsset* asset_{};
    int32_t left_;
    int32_t top_;

    bool initialized_{};

    wl_display* display_{};
    wl_surface* surface_{};
    wl_surface* parent_surface_{};
    wl_callback* callback_;
    wl_subsurface* subsurface_{};

    struct _native_window {
        struct wl_display* display;
        struct wl_surface* surface;
        uint32_t width;
        uint32_t height;
    } native_window_{};

    ::filament::SwapChain* fswapChain_{};
    std::mutex frameLock_;
    ::filament::View* fview_{};

    // todo to be moved?
    ::filament::gltfio::Animator* fanimator_;

    static void SendFrameViewCallback(
      const std::string& methodName,
      std::initializer_list<std::pair<const char*, flutter::EncodableValue>> args
    );

    static void OnFrame(void* data, wl_callback* callback, uint32_t time);

    static const wl_callback_listener frame_listener;
    int _prev_fps_counter = 0;
    int _fps_counter = 0;
    uint32_t _last_fps_reset_time = 0;

    void DrawFrame(uint32_t time);

    void setupView(uint32_t width, uint32_t height);

    uint32_t m_LastTime = 0;
};

}  // namespace plugin_filament_view
