/*
 * Copyright 2026 Toyota Connected North America
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

#include "gamepads_linux_plugin.h"

#include <chrono>
#include <set>
#include <string>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_joystick.h>

#include <flutter/standard_method_codec.h>

#include "plugins/common/logging.h"

namespace plugin_gamepads_linux {

namespace {

constexpr char kChannelName[] = "xyz.luan/gamepads";
constexpr char kMethodListGamepads[] = "listGamepads";
constexpr char kMethodOnEvent[] = "onGamepadEvent";

/// How often the pads are read, in milliseconds.
///
/// Faster than a 60 Hz frame so a button pressed and released between two
/// frames is still seen, and slow enough that an idle pad costs nothing worth
/// measuring.
constexpr int kPollMs = 8;

int64_t NowMillis() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(steady_clock::now().time_since_epoch())
      .count();
}

}  // namespace

// static
void GamepadsLinuxPlugin::RegisterWithRegistrar(
    flutter::PluginRegistrar* registrar) {
  auto channel =
      std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
          registrar->messenger(), kChannelName,
          &flutter::StandardMethodCodec::GetInstance());
  registrar->AddPlugin(
      std::make_unique<GamepadsLinuxPlugin>(std::move(channel)));
}

GamepadsLinuxPlugin::GamepadsLinuxPlugin(
    std::unique_ptr<flutter::MethodChannel<flutter::EncodableValue>> channel)
    : channel_(std::move(channel)) {
  channel_->SetMethodCallHandler([this](const auto& call, auto result) {
    HandleMethodCall(call, std::move(result));
  });

  // Gamepad only: no video, no audio, no event queue of SDL's that could
  // contend with the shell's own. State is polled instead, which is why
  // SDL_UpdateGamepads is called by hand below.
  if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
    ihs::log::warn("[gamepads] SDL gamepad init failed: {}", SDL_GetError());
    return;
  }
  sdl_ready_ = true;

  Rescan();
  running_.store(true, std::memory_order_relaxed);
  reader_ = std::thread(&GamepadsLinuxPlugin::Pump, this);
}

GamepadsLinuxPlugin::~GamepadsLinuxPlugin() {
  running_.store(false, std::memory_order_relaxed);
  if (reader_.joinable()) {
    reader_.join();
  }
  {
    const std::lock_guard<std::mutex> lock(pads_mutex_);
    for (auto& [id, pad] : pads_) {
      if (pad.handle != nullptr) {
        SDL_CloseGamepad(pad.handle);
      }
    }
    pads_.clear();
  }
  if (sdl_ready_) {
    SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
  }
}

void GamepadsLinuxPlugin::Rescan() {
  int count = 0;
  SDL_JoystickID* ids = SDL_GetGamepads(&count);
  std::set<SDL_JoystickID> present;
  if (ids != nullptr) {
    for (int i = 0; i < count; i++) {
      present.insert(ids[i]);
    }
  }

  const std::lock_guard<std::mutex> lock(pads_mutex_);

  for (const SDL_JoystickID id : present) {
    if (pads_.count(id) != 0) {
      continue;
    }
    SDL_Gamepad* handle = SDL_OpenGamepad(id);
    if (handle == nullptr) {
      ihs::log::warn("[gamepads] could not open {}: {}", id, SDL_GetError());
      continue;
    }
    Pad pad;
    pad.handle = handle;
    pad.id = std::to_string(id);
    const char* name = SDL_GetGamepadName(handle);
    pad.name = name != nullptr ? name : pad.id;
    // Seeded from the pad's current state rather than from zero, so a stick
    // already held at connect does not read as a fresh push.
    for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; b++) {
      pad.buttons[b] =
          SDL_GetGamepadButton(handle, static_cast<SDL_GamepadButton>(b));
    }
    for (int a = 0; a < SDL_GAMEPAD_AXIS_COUNT; a++) {
      pad.axes[a] = SDL_GetGamepadAxis(handle, static_cast<SDL_GamepadAxis>(a));
    }
    ihs::log::info("[gamepads] {} connected: {}", pad.id, pad.name);
    pads_.emplace(id, std::move(pad));
  }

  for (auto it = pads_.begin(); it != pads_.end();) {
    if (present.count(it->first) != 0) {
      ++it;
      continue;
    }
    ihs::log::info("[gamepads] {} disconnected", it->second.id);
    if (it->second.handle != nullptr) {
      SDL_CloseGamepad(it->second.handle);
    }
    it = pads_.erase(it);
  }

  SDL_free(ids);
}

void GamepadsLinuxPlugin::Emit(const Pad& pad,
                               const char* type,
                               const int index,
                               const double value) const {
  channel_->InvokeMethod(
      kMethodOnEvent,
      std::make_unique<flutter::EncodableValue>(flutter::EncodableMap{
          {flutter::EncodableValue("gamepadId"),
           flutter::EncodableValue(pad.id)},
          {flutter::EncodableValue("time"),
           flutter::EncodableValue(NowMillis())},
          {flutter::EncodableValue("type"),
           flutter::EncodableValue(std::string(type))},
          {flutter::EncodableValue("key"),
           flutter::EncodableValue(std::to_string(index))},
          {flutter::EncodableValue("value"), flutter::EncodableValue(value)},
      }));
}

void GamepadsLinuxPlugin::Pump() {
  int64_t last_rescan = 0;
  while (running_.load(std::memory_order_relaxed)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs));

    // Hot-plug, checked about once a second. SDL reports arrivals through its
    // event queue, which this deliberately does not drain -- so the pad list is
    // re-read instead, which costs one enumeration a second and keeps the
    // shell's event loop entirely to itself.
    const int64_t now = NowMillis();
    if (now - last_rescan > 1000) {
      last_rescan = now;
      Rescan();
    }

    SDL_UpdateGamepads();

    const std::lock_guard<std::mutex> lock(pads_mutex_);
    for (auto& [id, pad] : pads_) {
      if (pad.handle == nullptr) {
        continue;
      }
      for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; b++) {
        const bool down =
            SDL_GetGamepadButton(pad.handle, static_cast<SDL_GamepadButton>(b));
        if (down != pad.buttons[b]) {
          pad.buttons[b] = down;
          Emit(pad, "button", b, down ? 1.0 : 0.0);
        }
      }
      for (int a = 0; a < SDL_GAMEPAD_AXIS_COUNT; a++) {
        const Sint16 value =
            SDL_GetGamepadAxis(pad.handle, static_cast<SDL_GamepadAxis>(a));
        if (value != pad.axes[a]) {
          pad.axes[a] = value;
          Emit(pad, "analog", a, static_cast<double>(value));
        }
      }
    }
  }
}

void GamepadsLinuxPlugin::HandleMethodCall(
    const flutter::MethodCall<flutter::EncodableValue>& call,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
  if (call.method_name() == kMethodListGamepads) {
    flutter::EncodableList list;
    {
      const std::lock_guard<std::mutex> lock(pads_mutex_);
      list.reserve(pads_.size());
      for (const auto& [id, pad] : pads_) {
        list.emplace_back(flutter::EncodableMap{
            {flutter::EncodableValue("id"), flutter::EncodableValue(pad.id)},
            {flutter::EncodableValue("name"),
             flutter::EncodableValue(pad.name)},
        });
      }
    }
    result->Success(flutter::EncodableValue(list));
    return;
  }
  result->NotImplemented();
}

}  // namespace plugin_gamepads_linux
