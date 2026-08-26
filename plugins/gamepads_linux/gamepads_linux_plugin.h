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

#ifndef FLUTTER_PLUGIN_GAMEPADS_LINUX_PLUGIN_H_
#define FLUTTER_PLUGIN_GAMEPADS_LINUX_PLUGIN_H_

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <flutter/method_channel.h>
#include <flutter/plugin_registrar.h>

#include <SDL3/SDL_gamepad.h>

namespace plugin_gamepads_linux {

// Gamepad input over the `xyz.luan/gamepads` channel, backed by SDL3.
//
// ## Why the shell needs this at all
//
// It reads input through libinput, and libinput deliberately ignores joysticks
// and gamepads -- it serves pointers, keyboards, touch and tablets. Under the
// Wayland backends input arrives from the compositor, and Wayland has no
// gamepad protocol either. So a pad cannot come through either path the shell
// already has.
//
// ## Why SDL3 rather than the kernel's joystick API
//
// `gamepads_linux` on pub reads /dev/input/js* and reports the raw driver index
// as the key. That index is per-device: button 0 on one pad is not button 0 on
// another, and an app is left to carry its own mapping table per controller.
//
// SDL3 already carries that table, and -- the deciding fact for a Godot port --
// its enum *is* Godot's. SDL_GAMEPAD_BUTTON_SOUTH is 0 and JoyButton A is 0,
// DPAD_UP/DOWN/LEFT/RIGHT are 11/12/13/14 in both, LEFTX/LEFTY/RIGHTX/RIGHTY
// are 0/1/2/3 and the triggers 4/5. Godot uses SDL's controller database, so a
// project's `button_index` and `axis` numbers mean SDL's, and DOGWALK's own
// bindings land on them one for one. Emitting SDL's numbering hands a port
// exactly the numbers its InputMap was written against; emitting the kernel's
// hands it numbers it would have to translate.
//
// SDL3 also brings hot-plug, which the js path would need a rescan loop for,
// and rumble -- which this game calls (`Input.start_joy_vibration`).
//
// ## The wire
//
// `gamepads`' contract documents `key` as "a platform-dependant identifier", so
// this reports SDL's numbering as a decimal string: "0" for SOUTH, "11" for
// DPAD_UP. That is deliberately *not* what gamepads_linux reports for the same
// pad. The divergence is the point -- see above -- and it is stated here rather
// than discovered.
//
// Axis values are Sint16 handed across unscaled, matching gamepads_linux's
// choice to let the app divide.
class GamepadsLinuxPlugin final : public flutter::Plugin {
 public:
  static void RegisterWithRegistrar(flutter::PluginRegistrar* registrar);

  explicit GamepadsLinuxPlugin(
      std::unique_ptr<flutter::MethodChannel<flutter::EncodableValue>> channel);

  ~GamepadsLinuxPlugin() override;

  GamepadsLinuxPlugin(const GamepadsLinuxPlugin&) = delete;
  GamepadsLinuxPlugin& operator=(const GamepadsLinuxPlugin&) = delete;

 private:
  /// One open pad, and the state last reported for it.
  ///
  /// The previous state is what turns SDL's level-triggered view into the
  /// edge-triggered events the channel carries: nothing is sent for an axis
  /// resting where it was.
  struct Pad {
    SDL_Gamepad* handle{nullptr};
    std::string id;
    std::string name;
    bool buttons[SDL_GAMEPAD_BUTTON_COUNT]{};
    Sint16 axes[SDL_GAMEPAD_AXIS_COUNT]{};
  };

  void HandleMethodCall(
      const flutter::MethodCall<flutter::EncodableValue>& call,
      std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);

  /// Opens every pad SDL currently reports, and closes any that went away.
  void Rescan();

  /// Polls SDL and emits an event per changed button or axis.
  void Pump();

  void Emit(const Pad& pad,
            const char* type,
            int index,
            double value) const;

  std::unique_ptr<flutter::MethodChannel<flutter::EncodableValue>> channel_;

  /// Guards [pads_], which the reader thread writes and `listGamepads` reads.
  mutable std::mutex pads_mutex_;
  std::map<SDL_JoystickID, Pad> pads_;

  std::thread reader_;
  std::atomic_bool running_{false};
  bool sdl_ready_{false};
};

}  // namespace plugin_gamepads_linux

#endif  // FLUTTER_PLUGIN_GAMEPADS_LINUX_PLUGIN_H_
